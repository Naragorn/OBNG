# The render's CPU work on several threads (spec, 2026-11-14)

The second large piece (the first: docs/subsystems/actor-frame-parallel.md). What the render thread does for a
frame before and around its Direct3D calls - the cull walk, the accumulation into pass lists, the sort, the
shader constants, the skinning's bone matrices - read whole and prepared on worker threads, with the device calls
kept on the render thread.

## Why this piece (measured)

| where | draws a pass | the scene pass | of a frame |
|---|---:|---:|---:|
| Nadi's Windows session 2026-11-14, the horde in Anvil | 2 500-4 000 (1 000-1 800 skinned) | 11.5-13.5 ms | 34 ms |
| Linux, the gate horde without sound | 1 900-2 700 | 14.3 ms | 54 ms |
| Nadi's Windows session, the full-rate frames | about 970 | 2.9-3.8 ms | 11.1 ms |

- With a quarter of the pixels the scene pass is no shorter (14.3 -> 14.7 ms): it is the CPU's work, not the GPU's.
- The cull walk and finish ("the driver") alone: 4.5 ms a pass at 3 395 objects visited, 8-13 ms in the horde.
- `0x0040D4D0`, the render's frame function: 12.5 ms a call in the gate horde ([Timed]).

## What is already built on this path

The single-pass stereo draw (one cull and one accumulation for both eyes), the constant maps' arrange
(`ConstantArrange`), draw instancing for equal records (`DrawInstancing=2`), the occlusion culler with its node
cull and quick decision, the volatile pack skip, the actor shadows at rest. All on the render thread.

## What Direct3D 9 allows

The device takes calls from one thread (DXVK's command stream is filled by that thread and executed by its own
`dxvk-cs` thread, 10 % of a core in the horde). So the draws stay where they are. What can move: everything that
decides and prepares a draw without the device.

## The plan

1. **The pass's breakdown by step** in the horde and at the Market: the cull walk (0x0070DFB0 / the driver), the
   accumulator's finish 0x007AE070 with its sort, DrawRenderPass 0x007A9820 per record (light constants, the
   shader's setup), the skinned draw's bone matrices, the device calls themselves. `[Timed]` and the sampler by
   function; the slices eye-pass, pass-drivers, pass-lists, draw-setup, draw-state and stereo-draw have most
   functions mapped already (re/functions/).
2. **The read-and-write map of the walk and the accumulation** (what the cull walk writes into the scene graph -
   the visible sets, the fade nodes' state - and into the accumulator), as for the actors' frame.
3. **Candidates, by what the breakdown says is large**:
   - the cull walk over the scene's subtrees on workers (each cell's subtree a job, the visible lists merged in
     the original's order) - the walk writes the accumulator's lists, which is the hard part;
   - the skinned draws' bone matrices computed ahead on workers (per skin instance: bones' worlds times the skin's
     bind transforms - no device, no shared write but the palette's buffer);
   - the per-record constants prepared into a buffer on workers and replayed on the render thread.
4. Each as class B with a check mode against the original's output (the device calls' trace for a frame is the
   comparison: the same calls with the same arguments in the same order - `tools/re/capture-frames.py` and the
   stereo draw's trace exist for that).

## Risks

- The original's order of draws is observable (blending, the first record of a pass list, the culler's tests):
  every candidate must hand the render thread the same order.
- The scene graph is written during the walk (fade, LOD switches, the shadow scene node's lights): the map decides
  what is a job's own.
- Other plugins hook the same functions (OBVR, EngineBugFixes, OBME - `[Probe] CodePatches` lists them): a
  replaced walk must keep their call sites.

## What it could gain (derived, not measured)

The horde's 12-14 ms a pass is the target; a walk and bone matrices on four threads could take 3-5 ms of it. Not
before step 1 says where the 12-14 ms are.

## Two stages on top of the draw entries OBNG already owns (2026-11-14, candidates, nothing measured)

The single pass replaced the engine's four draw entries (and with them one cull and one accumulation a frame for
both eyes) - not the path to a draw (the cull walk, the pass lists, per record the shader's, lights' and material's
setup) and not the interface (Direct3D 9, one call a draw from the render thread). Since the entries are OBNG's:

1. **Record, prepare on workers, replay**: the entries note each draw instead of issuing it; bone matrices and
   constants are prepared on worker threads; the render thread replays the calls in the original's order. Large;
   the entries and the instancing's record-and-flush exist. Saves preparation time, the calls stay serial.
2. **One draw an actor**: an actor's skinned parts (about 15 an eye for an armoured NPC; 1 000-1 800 skinned draws a
   pass in a horde) drawn as one, the bones in one palette, a shader of OBNG's own. Very large: vertex data and
   shaders, and meshes of mods. It lowers the number of draws itself - the lever for crowds.

Neither before step 1 of the plan says how the horde's 12-14 ms divide between the walk, the accumulation and the
draws. A renderer of OBNG's own past Direct3D 9 is not planned.

## Step 1's result from the captures in hand (2026-11-14, tools/re/pass-shares.py, exclusive samples)

Nadi's Windows horde, capture OBVR-performance-3 of 13:44 (2 662 world frames, 31.8 ms a frame):

| | ms a frame |
|---|---:|
| **inside the scene pass** | **9.9** (7.9 by OBVR's scene_pass events) |
| Direct3D 9 (D3D9.DLL) | 2.2 |
| NiMain and the renderer (0x0070-0x007A) | 2.0 |
| OBNG.dll (the draw entries, the eye switch, the culler) | 1.9 |
| the shaders and the accumulator (0x007A-0x0088) | 0.9 |
| OBVR.dll | 0.7 |
| **outside the pass** | **21.9** |
| Havok (0x0088A000-0x0097FFFF) | 6.7 |
| waiting in ntdll | 5.7 |
| the game's own code | 2.9 |
| NiMain | 1.3, OBNG.dll 1.2, the heap 1.1, sound 0.7 |

- **The pass has no hot function**: inside it no function of the EXE is above 1.7 % of the pass's samples (0x0077B140,
  RegisterGeometry 0x007AD270, the transform compose, ApplyConstants 0x009A8E30, the sampler states ...). The cost is
  per draw, spread over the whole path to a draw. Rebuilding functions one by one gains tenths; what lowers it is
  fewer draws (stage 2 above) or the path prepared elsewhere (stage 1).
- **On Windows the horde's frame is not the pass's first**: the pass is a third, and outside it Havok alone is
  6.7 ms - 0x00921A40 2.0 ms (under 0x00924000), 0x0094FEF0, 0x008D0290 and a long tail - and the waits 5.7 ms.
  Whether that Havok time is the character controllers' casts (the actors' frame) or the world's own step with the
  horde's bodies, ragdolls and corpses is not told by exclusive samples; the world runs with iSimType=1 and no
  worker threads (knowledge/threading.md), and the one try with iSimType=4 on 2026-11-09 was slower.
- The "12 to 14 ms a pass" above is the slow frames' median (frames over 26 ms); the capture's mean is 8 to 10 ms.
