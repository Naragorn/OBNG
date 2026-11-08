# Single-pass stereo: the eye pass as one unit

Status 2026-11-04 15:05: **built on both sides and measured in the game: one pass a frame, the second eye's picture equal to the dual pass's, about 2.0-2.3 ms of CPU and 2 ms of GPU a frame saved on the Walkers save (OBVR's step 6).** The pose wait still holds the frame on that scene; the outdoor Windows scene is where it shows. The engine side is read (slice
eye-pass, five mappings held: `re/functions/0x00762640.md`, `0x007C78D0.md`,
`0x0070E0A0.md`, `0x007672F0.md`, `0x0070E040.md`); the numbers are measured
on the fixed Walkers scene; OBVR's side is read in its code. Marks as in
AGENTS.md.

## Why

OBVR renders a frame as two full passes of OSGlobals::RenderScene 0x0040C830
(read: `../OBVR/src/render/SceneRenderHook.cpp` HookedRenderScene calls the
original twice; between them BetweenScenePasses in `camera/CameraHook.cpp`
captures the first eye, moves the camera node by the eye shift and rebases
the bone palettes; CullingSync replays the first eye's cull results,
ShadowShareHook keeps the first eye's actor shadow maps). Measured
(knowledge/culling-and-draw-setup.md, round two): the two eye passes hold
4.98 of the 5.36 ms of draw work a frame; per partition the shader slots
13/15/14 take 2.85 ms for both eyes, the device draws with the palette build
about 2.1 ms, the accumulator's lists 0.44 ms. The draws cannot be halved -
each eye needs its own - but everything around them can: one cull, one
registration, one setup per partition, two draws. On the prison scene that
is CPU headroom only (the frame is held at 13.3 ms by the pose wait); on the
Windows outdoor scene the frame is CPU-bound at 90 Hz and the draw side scales
with the draws (knowledge/culling-and-draw-setup.md, 2026-11-02): there the
second eye's setup is frame time.

## The contract of one eye pass (read)

1. **Eye-dependent state** is produced by NiDX9Renderer::Do_SetCameraData
   0x00762640 (slot 80), once per NiDrawScene from the camera: the view
   matrix +0x980 and its inverse +0xA00, the projection +0x9C0, camRight /
   camUp +0x628 / +0x634 with the model copies +0x640 / +0x64C, near +0x678
   and depth range +0x67C, the global camera origin 0x00B3F92C, and three
   device calls - SetTransform(VIEW), SetTransform(PROJECTION), SetViewport.
   Nothing downstream caches a product: the constant map multiplies +0x940 /
   +0x980 / +0x9C0 per request (0x009A56D0.md C4), the light constants and
   the clip planes are rebuilt per draw (0x007C9140.md, 0x007C9230.md).
2. **The origin must stay one for both eyes** (C14 of 0x00762640.md,
   derived): the world matrices (0x00761AE0) and the bone palettes
   (0x007655F0) subtract 0x00B3F92C when they are packed, so a per-draw eye
   switch changes the view's translation row only (the eye's position
   relative to the shared origin), never the origin. OBVR's dual pass moves
   the camera node instead and rebases the palettes for it (BoneRebase.h);
   that machinery is what the single pass retires.
3. **The pass driver** ShadowSceneNode::OnVisible 0x007C78D0 runs once per
   eye today: light maintenance, the camera-derived constant entries
   0x1C..0x1E, the fog, then the accumulator's begin, the child walk (the
   registration) and the finish (the draws). In a single pass everything but
   the camera entries and the draws runs once.
4. **The per-draw sequence** in both draw functions (DrawSkinnedPrimitive
   0x00767520 per partition, DrawPrimitive 0x007672F0 per geometry), per
   pass: slot 12, slot 13 SetupTransformations, slot 15 the record (the
   packing and the binds), slot 14 SetupShaderPrograms (the program bind and
   the constant maps), the render-state slot-1 call on
   [[renderer+0x8AC]+0xFF0], the draw(s), slot 16. **The second eye's draw
   goes between the slot-1 call and slot 16**: switch the eye state, re-apply
   what depends on the view, set the other viewport, draw again, switch back.
5. **What depends on the view per draw** (derived from the constant map's
   id table, 0x009A56D0.md): the defined ids 3-4 / 15-16 (V), 7-8 / 19-20
   (W x V), 9-10 / 21-22 (V x P), 11-12 / 23-24 (W x V x P), 25-32 (S x V,
   S x V x P), the light constants when they are view-space (0x007C9140,
   unverified), the clip planes of passes 0x177..0x17A (0x007C9230 C14). The
   bones (ids 33-36), W, P, materials and texture transforms do not.

## The design, in five steps

| # | Step | Side | Cost | What it rests on |
|---|---|---|---|---|
| 1 | Close the contract's open callees: the render-state object's +0x44(near, far) and +0x48 (the mirror), its +0xFF0 slot 1 (per draw), shader slots 16 / 17, the +0x940 packer 0x00765480 (re-derives +0x640 / +0x64C?) | OBNG, map slice | cheap, one workflow run (five small functions) | the eye-pass maps' open questions |
| 2 | The view-only re-apply: a unit that, given the renderer and a partition's vertex constant map, uploads only the view-dependent entries (the ids above) from the other eye's view - the constant maps block, built for stereo rather than for 0.4 ms | OBNG, rebuild with an emulator gate and a register-content shadow | medium | 0x009A8E30.md, 0x009A56D0.md, 0x00783180.md (all mapped) |
| 3 | The stereo draw: the two draw functions rebuilt as one unit over ports (the shader slots, the device, the render state), with the double draw inside; routed at 0x007672F0 and 0x00767520; verified by a recorder of the device sequence (constants, viewport, draws) per eye against the dual pass on the fixed scene | OBNG, rebuild | medium to large | 0x007672F0.md, 0x00767520.md; the recorder is new |
| 4 | One RenderScene a frame into a double-wide target with one viewport per eye, the Submit with texture bounds per eye (OBVR's Submit already takes bounds), the capture per half, the second-pass machinery (camera shift, bone rebase, culling replay, shadow share, water stereo pass, HUD between the passes) off when the single pass is on; the cull from a union frustum (GetHeadsetFrustum) | OBVR | medium to large | OBVR's SceneRenderHook, HeadsetRenderer, EyeTextures |
| 5 | Measure: the fixed scene for the CPU split (pass-steps probe), then the Windows outdoor scene for the frame | both | cheap per run | the probes and scenarios of 2026-10-30 / 11-04 |

Ceiling (derived from round two): the second eye's slots 13 / 15 / 14 minus
the view-only re-apply (about 1.4 - 0.3 ms), the accumulator's second
registration and lists (0.2 ms), the second cull where OBVR's replay does not
cover it: **about 1.3 to 1.6 ms a frame of CPU on the prison scene**, more
outdoors where the draw count is higher. The draws and the GPU stay.

## Step 1, done by reading (2026-11-04 10:10)

The open callees were small enough to read without a workflow (ledger rows
draw-state, read by the architect, no refutation round):

- Shader slot 16 (0x007790E0) is a no-op for every engine shader but the
  water, pixel and HLSL classes; slot 17 (0x0076C7D0 for the lit shaders)
  restores D3DRS_COLORWRITEENABLE from the shader's +0x60 and ends the pass
  object (0x00779790 -> 0x00772720). Neither carries per-draw state the
  second eye needs: the second draw goes before slot 16 as planned.
- The render-state object's +0x44 is SetNearFar (state only, +0x78 / +0x7C /
  +0x80), +0x48 reads the mirror flag [+0xF4], +0xFC stores a draw-flag byte
  at [+0xFF5]. Near/far and the mirror are the same for both eyes.
- The world packer 0x00765480 (from slot 13) rebuilds modelCamRight / Up
  from the camera axes +0x628 / +0x634 per draw; those axes are identical for
  two eyes that differ by a translation (OBVR's dual shift is a pure
  translation, read in BetweenScenePasses). **So the per-draw eye switch is:
  the view's translation row in +0x980 and the inverse's in +0xA00, the
  device VIEW transform, the viewport, and the re-apply of the V-dependent
  constant ids.** Nothing else.
- The render-state sub-object at +0xFF0 is a NiDX9ShaderConstantManager
  (RTTI of vtable 0x00A8B3BC, created in the render state's constructor
  0x0077BB00 through 0x00780B30); its slot 1, called before every device
  draw, is the empty stub 0x0060D0A0 in this build. Nothing to redo for the
  second eye. Step 1 is closed.

## Risks and what settles them

- The render-state slot-1 call and slots 16 / 17 may carry per-draw state
  that the second draw needs again (open; step 1 reads them).
- The light constants may be view-space (then they are in the re-apply) -
  0x007C9140.md says which inputs it reads; a run with the recorder settles
  the rest.
- Depth: a double-wide target shares one depth buffer across two viewports -
  fine for D3D9 viewports; image-space effects that read the whole target
  (0x007B48E0 image space, the water reflection, OBVR's own post work) see
  both eyes at once and need the bounds too (OBVR's side).
- The fixed scene cannot show the frame gain (pose-wait held); only the
  outdoor Windows scene can, and that run needs the Windows machine.

## Step 3, built and run (2026-11-04 11:30)

Both draw functions are reconstructed as units with the two-eye plan (slice
stereo-draw, wf_abe17fcf-c30: 7 agents, 16 min, 1.0 M tokens; both reviews
approved first time; emulator gates green) and routed in the game through
`[Route] StereoDraw=1` with `[Route] StereoEyes` (RouteStereoDraw.cpp: the
rebuilds over the game's objects, the eye switch counting only).

| Run | Plan | scene_pass | p95 | Draws per 10 s through the rebuilds |
|---|---|---:|---:|---|
| obng-walkers-stereo/20261104-112022 | one eye (the 1:1 gate) | 2.99 ms | 3.68 | 523 551 whole + 1 184 918 skinned, 0 refused, 0 null record |
| obng-walkers-stereo/20261104-112320 | two eyes (double draw, same target) | 2.98 ms | 3.53 | 523 788 + 1 185 567, 1.74 M eye switches |
| obng-walkers-original/20261104-112634 | the original | 2.80 ms | 3.33 | - |

- **The gate holds in the game** (measured): every draw of the scene goes
  through the rebuilds, none refused, the counts equal the pass-steps
  probe's (700 whole + 1 588 skinned a frame). The headless screenshots show
  the 2D layer only; the picture in the headset is still to be looked at.
- **The second draw costs nothing measurable** (measured): two eyes 2.98 ms
  against one eye 2.99. The device's DrawIndexedPrimitive is cheap here
  (Proton's D3D9 over DXVK); the 2.1 ms the pass-steps round left "under the
  draws" are the palette build, the dispatch and OBVR's hooks, not the
  draws. For the single pass that means the second eye costs only its eye
  switch.
- **The route costs 0.19 ms a pass** over the original (its per-call lock
  and counters); to be trimmed when the route becomes the block.

Step 4 is OBVR's: the double-wide target, one viewport per eye, the submit
with bounds, and the eye switch's body - the second eye's view translation
in renderer +0x980 / +0xA00, SetTransform(VIEW), SetViewport, and the
constants re-applied (ApplyConstants for both maps through a port). The eye
data (the IPD shift, the viewport rects, the render target) lives in OBVR;
how it reaches OBNG's eye switch (an exported function, a shared memory
block, or the switch built on OBVR's side against OBNG's port) is the design
decision before step 4.

## Step 4, OBNG's half (built 2026-11-04 12:05; not yet run)

**The interface decision.** The eye data lives in OBVR and reaches OBNG
through three C functions exported by OBNG.dll (`plugin/src/StereoEyesApi.h`,
bound by GetProcAddress, no other OBNG header needed):

| Function | Does |
|---|---|
| `ObngStereoPlan()` | 0 = the stereo draw route is off, 1 = on with one eye (the 1:1 gate), 2 = on and allowed to draw twice. OBVR renders one pass only when this is 2 and SetEyes returned ok. |
| `ObngStereoSetEyes(const ObngStereoEyes*)` | the eye data for every draw until Clear: `secondEyeOffset[3]` (world space, game units, from eye 0 = the pass camera to eye 1 - OBVR's g_dualShift as it stands), `viewport[2]` in pixels of the target (D3DVIEWPORT9 order; eye 0's must be the one the engine's own SetViewport leaves, as the first draw of every partition runs before any switch). Copied; validated (size, version, viewports, finite offset, the route's plan); a refusal keeps the previous data. |
| `ObngStereoClearEyes()` | back to one draw per partition. |

Why this and not a shared memory block or the switch on OBVR's side: OBVR
stays the owner of the VR quantities (the pupil distance, the target, the
viewports) and OBNG the owner of the draw and the engine state it touches;
each side builds and tests alone (OBNG's eye switch runs on its own test
eye data, OBVR can call the interface against a stub); the three functions
are the whole coupling and are versioned (`size`, `version`). Scoping is the
caller's: with eyes set, every draw through the route doubles, in every
pass - OBVR sets the eyes right before the world render and clears them
right after, so the 2D layer, the water reflections and the menus draw once.

**The eye switch's body** (`plugin/src/StereoEyeSwitchLogic.h` the decisions,
tested in `plugin/tests/StereoEyeSwitchLogicTest.cpp` over a recording port;
`plugin/src/RouteStereoDraw.cpp` the engine port). Switch(1), in this order:
read the view +0x980 (the axes from its columns, the translation row) and
the inverse view's row 4 at +0xA00; write the second eye's rows
(`_4k - axis_k . e`, `T + e`, derived from 0x00762640.md C2 and C3); device
SetTransform(VIEW, +0x980) as the engine's K1; SetViewport(eye 1);
re-apply the constants. Switch(0): the saved rows back bit for bit,
SetTransform(VIEW), SetViewport(eye 0). Without eye data Switch(1) only
counts, so the draws stay what step 3b measured, and the plan per draw
falls back to one eye (no second draw without data: a second draw into
the same viewport is a double alpha blend at worst).

**The constants re-apply, first version:** the shader's slot 14 called
again with the partition's own arguments - the whole of
NiD3DShader::SetupShaderPrograms 0x0077A1F0 (the extra-data cursor reset,
the program bind, both constant maps), captured by the route's shader port
when the rebuild makes the call. Chosen over a direct ApplyConstants pair
because the cursor reset (0x0077A1F0.md C12) must precede the maps and its
meaning is unverified, and over the view-only re-apply (step 2 of the
design) because that needs the entry types mapped first. Its cost is the
measured slot 14: 1.42 ms a pass on the fixed scene - the upper bound of
what the single pass gives back; step 2 takes most of it. Unverified and
settled by the first run: that 0x0077A1F0's pass-0 tail (this+0x24's slot
29 into this+0x18's slot 42) and SpeedTreeBranchShader's own slot 14
0x0080FC20 are harmless when repeated; that the shadow-light clip planes of
slot 13 (0x007C9230 C14), which stay eye 0's, do not show; that the light
constants are not view-space (0x007C9140, unverified).

**The test mode** `[Route] StereoTestEyeShift=<units>`
(tools/hand-scripts/obng-measure-stereo-split.ini: 4.5 units, about 64 mm at
OBVR's 69.99 units a metre, read in OBVR's HeadTracker.h): the viewport the
engine set is split in halves (eye 0 left, eye 1 right) and eye 1 sits the
shift along the camera's right axis. It runs the whole switch without
OBVR's side, so the fixed scene can cost it now: scene_pass against the
two-eye run of step 3b (2.98 ms) is the switch's price per pass, an upper
bound (the test mode reads the viewport from the device per switch; the
interface mode does not). The picture in the headset: each eye shows two
squeezed half-views with the parallax between them - a sanity check of the
sign and size of the shift and of the constants, not a VR frame. What it
cannot show: the frame gain (the pose wait holds the prison scene).

**OBVR's half (step 4b), what is read and what is OBVR's to read.** Read in
OBVR's code: HookedRenderScene (`src/render/SceneRenderHook.cpp`) renders
the world twice with betweenPasses in the middle and skips the second pass
for renders into a texture; BetweenScenePasses (`camera/CameraHook.cpp`)
moves the camera node by g_dualShift = finalRotation x (toSecondEye, 0, 0)
and rebases the palettes; HeadsetRenderer keeps m_boundsLeft / m_boundsRight
(VRTextureBounds) for the submit; ResolutionHook, EyeMirror, InteropBracket
and WaterReflectionHook exist by name - their bodies are unread here. The
task, as far as OBNG can state it: bind the three functions (absent OBNG.dll
or plan != 2: the dual pass as today); a double-wide game target with the
camera's port rect on its left half so the engine's own viewport is eye 0's;
SetEyes before the world render with g_dualShift and the two halves, Clear
after it; one RenderScene a frame; the capture per half and the submit with
the bounds; the dual-pass machinery off on that path (the camera shift, the
bone rebase, the culling replay, the shadow share, the water stereo pass);
the cull from a union frustum. Cost: medium to large - the pieces exist,
new is the single-pass branch and switching five mechanisms off, the risk
sits in the capture and the HUD path, the test is the harness and the
headset.

## Step 4, the switch run in the test mode (2026-11-04 12:17, run obng-walkers-stereo/20261104-121654)

Same scene, same OBVR.dll and hour as the step-3 runs; `[Route] StereoEyes=2
StereoTestEyeShift=4.5`; phase settled to watched, 4 491 frames.

| Run | Switch | scene_pass mean / p95 | app GPU before submit | poses_wait | frame |
|---|---|---:|---:|---:|---:|
| -121654 split | the real switch, 1 742 692 applied per 10 s, 0 refused | 3.93 / 4.38 ms | 8.80 ms | 4.14 ms | 13.36 ms |
| -112320 two eyes | counting only | 2.98 / 3.53 | 6.86 | 6.01 | 13.39 |
| -112634 original | - | 2.80 / 3.33 | 6.56 | 6.34 | 13.36 |

- **The switch costs 0.95 ms a pass** in this version (measured: 3.93
  against 2.98), the upper bound: slot 14 again is most of it (1.42 ms
  measured for the first call, so the second is cheaper, the programs
  already bound), plus the device GetViewport the test mode alone makes.
  Every switch applied, no refusal, no crash, PASS: the repeated slot 14
  (its pass-0 tail, SpeedTree's own) is harmless on this scene (measured).
- **The GPU took 1.9 ms more a frame** (8.80 against 6.86): the test mode
  draws every partition's vertices twice in each of OBVR's two passes, so
  four eye-views of vertex work a frame against two. In the single pass it
  is two again (each eye once, as today) - neutral by construction, to be
  measured when OBVR's half exists. The frame stayed at the pose wait.
- **Projected for the single pass** (derived): one pass 2.80 + the switch
  0.95 + the route's 0.19 = about 3.9 ms against two passes 5.6 ms today -
  about 1.7 ms a frame of CPU on the prison scene before the view-only
  re-apply (step 2), which takes most of the 0.95 ms back; the ceiling then
  is about 2.3 ms.
- The picture: the headless screenshots show the 2D layer in the left half
  (the test mode doubles the 2D pass too, as the contract says); the world
  is in the headset and still to be looked at.

## Step 2 measured: which constants the second eye needs (2026-11-04 12:48, run obng-walkers-stereo/20261104-124811)

`[Probe] ViewConstants` (Probes.cpp, ProbeLogic.h ViewStats) on the split
test mode: the eye switch marks its re-apply (slot 14 again), the apply's
entries are tabled at ApplyConstants 0x009A8E30's entry, and every upload
through the program slots 0x007830C0 / 0x00783180 inside the re-apply is
compared with the register's bytes from the first eye and keyed by its
entry (type nibble, predefined id). Per 10 s, 3.39 M applies, 10.4 M
re-apply uploads, 0 unknown:

| Key (type:id) | changed (view-dependent) | same |
|---|---:|---:|
| t2:i31 | 1 211 882 | 0 |
| t2:i23 | 471 121 | 0 |
| t2:i27 | 47 936 | 0 |
| t2:i19 | 2 996 | 0 |
| t2:i33 (the bone palettes) | 0 | 8 700 384 |

- **Only defined entries (type 2) with the ids 19, 23, 27, 31 change with
  the eye** (measured): the odd members of the map's W x V (19-20),
  W x V x P (23-24) and S x V (x P) (27-28, 31-32) pairs (0x009A56D0.md id
  table). No entry of the types 1, 3, 4, 5, 6 uploaded through the probed
  slots in the re-apply at all on this scene; the pure V and V x P ids
  (3-4, 9-12, 15-16, 21-22) are not in these shaders' maps.
- **83 % of the re-apply's uploads are the bone palettes** (t2:i33, 8.7 M
  per 10 s), re-uploaded unchanged - that is where the switch's 0.95 ms go,
  with the program rebind and the extra-data lookup. The view-dependent
  share is 1.7 M uploads per 10 s, about 1 900 a frame.
- Unverified, settled by the next run (the probe now also counts the keys
  tabled without an upload): whether the pixel maps hold view-dependent
  ids - their uploads go through the pixel program's own slots, which the
  probe does not hook. Type-1 entries upload fixed buffers ([entry+0x30],
  no argument) and cannot depend on the eye (read, 0x009A8E30.md C10).

**So step 2 is:** a re-apply that walks the vertex map once and dispatches
only the type-2 entries whose id is in the view set {19, 23, 27, 31} plus
the map's other view ids for shaders this scene lacks {3, 4, 7, 8, 9, 10,
11, 12, 15, 16, 20, 21, 22, 24, 25, 26, 28, 29, 30, 32} (derived from the
id table; harmless when absent) through the map's slot +0x54
(SetDefinedConstant) with the apply's own arguments - no palette, no
program rebind, no extra-data lookup. Expected (derived from the
constant-uploads measurements, 0x007830C0 at about 30 ns a call and
0x009A8E30 at 0.26 us inclusive): the switch falls from 0.95 ms to about
0.3-0.4 ms a pass.

## Step 2 built and run: the view re-apply (2026-11-04 13:35, runs obng-walkers-stereo/20261104-133458 and -133744)

ApplyConstants 0x009A8E30 reconstructed as a unit with the entry filter
(slice view-reapply, wf_0fc7705c-dd3: 4 agents, 15 min, 0.56 M tokens;
emulator gate green, every branch; ledger row). The eye switch's re-apply
is now `[Route] StereoReapply=view`: the rebuilt walk with the view filter
on the map(s) whose vtable is NiD3DSCM_Vertex, against the pass's programs,
with SetupShaderPrograms' own argument list (0x0077A1F0.md C14 / C19).

| Run | Re-apply | scene_pass mean / p95 | Switches per 10 s |
|---|---|---:|---:|
| -133458 | view (the rebuild, view ids only) | 3.62 / 4.02 ms | 1 728 211 applied, 0 refused |
| -121654 | slot14 (everything again) | 3.93 / 4.38 | 1 742 692 |
| -112320 | none applied (counting only, no switch) | 2.98 / 3.53 | - |

- **The switch costs 0.64 ms a pass with the view re-apply** (measured:
  3.62 against 2.98), down from 0.95 with slot 14. Less than the 0.3-0.4
  derived before: the remainder is the per-partition device calls
  (SetTransform and SetViewport twice each) and the view walk's own
  products; `StereoReapply=none` (the rows and the device calls only, no
  constants) measures that floor next.
- **The pixel maps hold no defined entries on this scene** (measured, run
  -133744 with the view probe counting the tabled keys: the only entries
  tabled without a seen upload are type 1, 10.9 M + 1.6 M per 10 s, the
  fixed-buffer constants; no type-2 key without uploads). So there is
  nothing view-dependent to re-apply on the pixel side here; type-1 entries
  cannot depend on the eye (0x009A8E30.md C10). Run -133744's scene_pass
  (4.72 ms) carries the two probes' hashing and is not a cost figure.
- **Projected single pass** (derived): 2.80 + 0.64 + 0.19 = about 3.6 ms
  against 5.6 ms for the two passes today, about 2.0 ms a frame of CPU on
  the prison scene.

## The switch's floor (2026-11-04 13:43, run obng-walkers-stereo/20261104-134331)

`StereoReapply=none`: the rows, SetTransform(VIEW) and SetViewport twice
each per partition, GetViewport once (test mode), no constants.

| Run | Re-apply | scene_pass mean / p95 |
|---|---|---:|
| -134331 | none (the floor) | 3.35 / 3.68 ms |
| -133458 | view | 3.62 / 4.02 |
| -121654 | slot14 | 3.93 / 4.38 |
| -112320 | no switch | 2.98 / 3.53 |

- **The floor is 0.37 ms a pass, the view re-apply itself 0.27 ms**
  (measured). Of the 0.64 ms, the per-partition device calls are the larger
  part: about 160 ns per partition for five dxvk calls, about 30 ns each
  (derived). The constants are where they should be.
- Trims left for later, in order of evidence: the interface mode makes no
  GetViewport (one call of five gone, derived); SetTransform(VIEW) can go
  only if nothing in the programmable pipeline reads the device's VIEW
  transform - the constant maps read renderer +0x980 directly (read) - but
  whether any fixed-function or engine path reads it back is unverified; a
  headset look with it skipped would settle it. The switch is good enough
  for OBVR's half as it stands.

## The first single-pass frames in the game (2026-11-04 14:30, OBVR's step 4; run artifacts/shadow/single-pass-one/20261104-143042)

OBVR's SceneRenderHook hands the eyes over before the world render (eye 0
the left half, eye 1 the right half of the 5136 x 2568 target, the offset
g_dualShift = 4.339 units along the head's right) and renders once; OBNG's
route with `[Route] StereoDraw=1 StereoEyes=2 StereoReapply=view` draws
every partition twice. OBVR's own harness scene (its test hall, not the
Walkers save), PASS:

- ObngStereoPlan returned 2; ObngStereoSetEyes accepted every frame (746
  per 10 s, 0 refused); 253 640 eye switches per 10 s, all applied; 799
  draws per 10 s outside eye 0's viewport (the actor shadow maps and the
  pre-passes, drawn once - the gate works). One RenderScene a frame
  (measured, OBVR's log).
- **The pictures - corrected by OBVR (14:40):** OBVR's eye captures
  (OBVR-Eye-Left-world.png / OBVR-Eye-Right-world.png in the run) show the
  hall correctly, but both are the SAME left half: CaptureEye(right) still
  reads m_eye[1].source within m_frameWidth, the left half, and the
  difference between the two files is OBVR's lens placement (each eye
  cropped at the other edge), not parallax. OBVR measured it: the best x
  shift left to right is 101 px far and 101 px near (equal, so no depth),
  against 103 / 113 and 103 / 167 px in the dual-pass runs. My first
  reading of these two pictures as a consistent parallax was wrong; the
  second eye's half is unseen until OBVR's step 5 captures the right half.
  The phase table (frame, scene_pass, GPU) comes with its step 6.

## The second eye verified in the picture (2026-11-04 14:40, OBVR's step 5; run artifacts/shadow/single-pass-one/20261104-143625)

OBVR's capture now takes the right eye from the right half of the target.
Measured by OBVR against its dual-pass run of the same scenario (the
Esc-menu mark): the parallax left to right is 103 px far and 167 px near,
exactly the dual pass's; the eye dumps differ from the dual pass by an
RMSE of 0.07 % (left) and 0.26 % (right). **So the eye switch draws eye 1
right** - the view translation, its sign and size, and the re-applied
constants - in the game, in one pass. Step 6 (the numbers, on against off)
is next on OBVR's side.

## Step 5 measured: the single pass against the dual pass (2026-11-04 14:57-15:03, OBVR's step 6; Walkers save, settled to watched, Linux, MSAA 4, HDR off)

Runs under artifacts/shadow/: single-pass-measure-on/20261104-145734 and
-150318 (SinglePassStereo on, OBNG `StereoReapply=view`),
single-pass-measure-off/20261104-150032 (the dual pass).

| | Dual pass | Single pass |
|---|---:|---:|
| scene_pass per frame | 2 x 3.23 = 6.46 ms | 4.26 / 4.19 ms (one pass) |
| app GPU before submit | 7.10 ms | 5.07 ms |
| frame mean | 13.6 ms (held by the pose wait) | 13.57 ms |
| OBNG's switch (TSC timer in the route's line) | - | 0.27 us per switch, re-apply 0.10 us; 808 k switches per 10 s, eyes set 741 per 10 s, 0 refused |

- **About 2.0 to 2.3 ms of CPU and 2 ms of GPU a frame** (measured by
  OBVR, two "on" runs). The frame mean stays at the pose wait on this
  scene; the gain is headroom here and frame time where the CPU bounds
  the frame (the outdoor Windows scene, knowledge/culling-and-draw-setup.md
  2026-11-02).
- The switch's own cost in OBVR's mode equals OBNG's split-mode baseline
  (run obng-walkers-stereo/20261104-145435: 0.28 / 0.09 us): the first "on"
  run's 14.78 ms (single-pass-measure-on/20261104-143932) is **not
  reproduced**; its cause is open (OBVR's guess, unverified: DXVK pipeline
  compilation on the first use of the 5136-wide MSAA frame in that scene).
  A first-run effect to watch for in later measurements.
- Still owed: the look in the headset (Nadi), the Windows outdoor run, and
  OBVR's switch of SinglePassStereo to its default.

## Outdoors on Linux (2026-11-04 15:40, OBVR's gold-road runs, Unique Landscapes installed)

Measured by OBVR on the Gold Road scene (MOO save), single pass on
against off: scene_pass per frame 3.26 ms against 2 x 2.40 = 4.80 ms,
app GPU before submit 5.54 against 6.38 ms; the frame held at vsync on
this machine. OBNG's switch 0.26 us, re-apply 0.09 us per partition. The
first "on" run's outlier (-143932) stayed unreproduced over four more
runs (3.7-4.3 ms); it ran the whole capture at SteamVR's half rate and
even its full-rate frames were slow (11.15 ms) - cause open. The CPU-bound
frame is the Windows outdoor machine's; that run is still owed.

## People in the second eye (2026-11-04 15:55, OBVR's single-pass-people on / off, Walkers save)

The bone palettes need no rebase in the single pass (derived from
0x00762640.md C14: the origin 0x00B3F92C stays one for both eyes, the
skinned vertex goes palette x the skin constants S x V (x P), ids 25-32,
which the switch re-applies - the probe saw 27 and 31 change with the eye,
33 the palette not). Measured by OBVR: the x shift left to right is 106 px
on the far wall and 114 px on a near Walker, identical on and off; RMSE on
against off 2.4 / 2.2 % (the animations run on between the runs). People
have their depth in the right eye. Next on OBVR's side: the water (its c13
reprojection is built in its SetVertexShaderConstantF hook from the
uploaded view constants) and the path without MSAA.

## A2: the renderer's direct draw entries (2026-11-04 15:50-16:45)

OBVR's water scene (Talos bridge) showed the near terrain and the SpeedTree
trees missing in the right eye while rocks and distant LOD were there. The
gate was clean (its outside viewports were the offscreen targets only), the
slot-14 re-apply changed nothing, and `[Probe] DrawEntries` (run
single-pass-water-on/20261104-161055) named the path: NiDX9Renderer's
vtable slots 90 (0x007694F0, about 30 calls a frame) and 91 (0x00769700,
400-550 a frame) draw with the pipeline the caller set up - NiTriShape's
and NiTriStrips' slot 34 (RenderImmediate, after 0x00722A10 applies the
properties; read by the refuters) - and never pass Do_RenderShape; EndBatch
and slot 93 ran 0 times. Slice draw-entries (wf_aee55f61-eb0: 17 agents, 27
min, 2.09 M tokens) mapped both at instruction level (the maps held) and
rebuilt them as units with the two-eye plan (StereoDrawDirect.h); the
route's third and fourth detours run them with the same eye switch, the
view re-apply built from the geometry (skin +0xB8, world +0x64, bound
+0x20, the partition and record being drawn, the renderer's property and
effect state). Built 16:45.

**Verified by OBVR (run single-pass-water-on/20261104-164829):** "Route: 4
of 4 detours made"; per 10 s 245 315 whole, 11 095 skinned, 23 170 direct
shape and 302 078 direct strips draws, 411 031 eye switches all applied, 0
refused, eyes set 748 / 0 refused; switch 0.24 us, re-apply 0.09 us; the
outside viewports are the offscreen targets only. The right eye shows the
near terrain, the tree and the house again, equal to the dual pass (RMSE
3.7 % in the static upper part; the rest is wind and clouds between the
runs). A2 closed.

## The route's lock trimmed, and the fixed scene after the load-order change (2026-11-04 17:05, run obng-walkers-stereo/20261104-170535)

The route's per-call SRW lock is gone (the stats belong to the render
thread; the summary thread asks for a line and the next routed draw writes
it). Split mode, view re-apply, four detours: scene_pass 3.47 / 4.06 ms
against 3.62 / 4.02 with the lock (run -133458) - 0.15 ms a pass back, of
the 0.19 the route's overhead measured in step 3. Switch 0.29 us, re-apply
0.10 us; 1.77 M switches per 10 s, 0 refused; 0 direct draws in the prison
(expected: no terrain or trees there).

The fixed scene after Unique Landscapes (plugin indices after MOO shifted by
one, OBVR 15:20): 495 569 whole + 1 249 039 skinned draws per 10 s against
523 551 + 1 184 918 on the morning's runs - within the run-to-run spread of
the whole draws (494 639 in the 14:54 run, before the change) and 5 % more
skinned draws; the Walkers are still there. Comparable within that spread;
a new original baseline on this scene is owed before the next fine
comparison.

## The artefact classes of the dual pass, on the single path (2026-11-04 evening, OBVR's measurements)

Measured by OBVR as the best x shift left to right per image band, single
against dual (runs single-pass-effects-on/-off at the Talos bridge,
single-pass-torch-on/-off in its test hall):

- **Water reflections:** not off. The reflection is rendered once a frame
  (WaterStereoPass Single) and serves both eyes; the bands trees / house
  101 / 102 / 103 px on as off, the water 109 / 115 against 106 / 116 (the
  waves run on between the runs).
- **Leaves, distant trees, particles:** face the pass camera, on the single
  path both eyes face eye 0's. The torch in the hand: 109 / 108 / 125 / 125
  / 153 / 169 against 109 / 110 / 129 / 129 / 153 / 169 px - the flame band
  4 px lower, the pure translation expected from the shared facing; the
  flame is in both eyes.
- **People / bone palettes, terrain, SpeedTree geometry:** verified earlier
  today (A1, A2).
- Open on OBVR's list: A3 (the texture path without MSAA keeps the dual
  pass), A6 (culling from the pass camera's frustum: very near objects at
  eye 1's right edge), A7 (release).

No visible difference in these classes; OBVR's spec table A8.

## The headset look on Windows and what it found (2026-11-05)

Nadi, Windows, headset, single pass on: the performance good; sky and clouds
"unconnected to the world"; spell effects in the left eye only and most of
the torch; the right eye sometimes with more ground texture than the left;
distant grass coming and going as the head turns. Reproduced and taken apart
on Linux the same evening (the eye pictures are OBVR's EyeDump; the Windows
install has the game's blur shader on, the Linux one off - set for the runs
with tools/with-game-ini.sh).

**The sky (fixed, [Route] StereoFollowers).** The scene render 0x0040C830
puts two roots ([0x00B333D8], [0x00B333DC]) at the camera's position at the
start of every pass (read, re/functions/0x0040C830.md step 6; the sky's node
is attached to the second, re/functions/0x00406360.md). In the dual pass
each eye's pass centred them on its own eye: no disparity, the sky at no
distance. The single pass centres them once, on eye 0, and the view shift
then showed the second eye the sky from 6.4 cm beside its centre. The sky's
meshes are near - measured in run obng-single-pass-sky/20261105-193948:
`Atmosphere Mesh` radius 522 units, `CloudDome:0/1` 515 / 502, `Atmosphere
Quad` 1000, `Sun Geometry` 396 units from the camera - so the second eye saw
the sky some 7 m away, in front of every mountain. A geometry with one of
the two roots among its ancestors (NiAVObject +0x1C) now gets the second
eye's viewport and nothing else (EyeSwitch.viewportOnly). The sky's shift
between the two eye pictures at the five marks: 101.0 / 103.9 / 99.3 /
100.9 / 102.7 px with the rule, 101.0 / 103.9 / 99.3 / 101.0 / 102.7 in the
dual pass (run obng-dual-pass-sky/20261105-200524) - the same to a tenth of
a pixel - and 105.4 / 109.7 / 106.6 / 107.1 / 113.9 without the rule (run
20261105-194137): 7 px nearer in a 642 px picture.

**The ground (fixed, [Route] StereoRestoreDirect).** The route's own fault.
The direct entries (slots 90/91: terrain, SpeedTree, grass) draw with the
constants their caller set up once for a run of geometries. The second eye's
re-apply overwrote the view constants and nothing put the first eye's back
(the draw functions with a shader setup per partition upload everything
again and never showed it), so every later geometry of a run was drawn for
the first eye with the second eye's constants, and a pass that tests for
equal depth failed there. Run obng-grass-turn-single/20261105-194324: the
left eye shows a ground layer that the right eye, the dual pass through the
route and the dual pass without OBNG (run obng-grass-turn-dual/
20261105-194847) do not. EyeSwitch.reapplyOnRestore re-applies the first
eye's view constants after the switch back, for the direct entries; run
20261105-195203 shows both eyes as the dual pass. The switch costs 0.31 us
(re-apply 0.15) per applied switch there against 0.24 (0.10) before; the
fixed-scene number is owed.

**The particles (routed, [Route] StereoParticles).** The
unrouted-draw probe ([Probe] UnroutedDraws: the device's four draw methods
hooked, every call sorted by the route's depth, the eye data and the
viewport, a row per calling site) found one device draw of a single-pass
frame in eye 0's viewport outside the route: DrawIndexedPrimitive at
0x007667FE, 72 calls in 5 s while a spell is cast (run
obng-single-pass-spell/20261105-193815). The site lies in the renderer's
vtable slot 92, 0x00765990 (read in the vtable 0x00A88EA4; Ghidra had no
function there, which is why the survey of 2026-11-04 named six draw entries
and missed the seventh): thiscall(geometry), RET 4, 1 050 instructions that
build a particle system's quads into a dynamic buffer and draw them through
the renderer's shader at +0xA94 with the device's transforms. It is routed
at its one device call - the eight bytes `mov eax,[ecx+148h]; call eax` at
0x007667F6 become a call to the route, which makes the device call once per
eye with the switch between - rather than rebuilt: nothing else of the
function changes, and no float of its vertex building is touched. The quads
face the pass camera in both eyes. Runs obng-single-pass-torch/
20261105-200255 and obng-single-pass-spell/20261105-200413: the hand's
casting effect is in both eye pictures, 860 particle draws in 5 s with the
torches, and the probe counts 0 device draws in eye 0's viewport outside the
route.

**The fixed scene with all of it on** (OBVR's single-pass-measure-on, run
single-pass-measure-on/20261105-200706, settled to watched): scene_pass 3.28
ms (p95 3.58), the frame 13.34 ms, GPU 4.52 ms; switch 0.28 us, re-apply
0.10 us per applied switch. Yesterday's 4.2 ms is not its pair - OBVR's head
cull and the route's lock removal lie between - so the day's three rules
have no measured price of their own yet; the scene has neither sky nor
terrain, where they act.

**The blur shader (no fault; a correction).** An earlier state of this
section, two commit messages (ba9f004, 0bda689) and the messages of the
evening said the bloom reached the left eye only, as measured. It was not
measured: it was read off two eye pictures by eye, and it is wrong. Measured
since, the same scenario with the blur shader off (OBVR's run
single-pass-torch-on/20261104-201829) and on (run 20261105-192827), the mean
brightness of the ceiling band in each eye picture at the four marks: left
+10.5 / +9.5 / +9.8 / +10.4, right +11.3 / +10.2 / +10.5 / +11.2. The bloom
is in both eyes, equally. What the trace of the image space step shows (run
obng-single-pass-torch/20261105-203613): four draws a call through the
route - the frame copy (5136x2568, both eyes) down to 256x256, two blur
passes at 256x256, the composite back onto the 5136x2568 target with the
viewport full and no blending. The step treats the double-wide frame as one
picture; so does the dual pass, whose frame is as wide. Derived, not
measured: each eye's bloom is twice as wide horizontally as vertically
(half the horizontal resolution in the 256x256 picture) and bleeds a little
across the seam between the eyes, in the single pass from the other eye, in
the dual pass from the other half's stale content. Nothing to route.

**The grass (not reproduced).** The grass builds are the same in both modes
(49 patches at every mark of OBVR's grass-turn scenario, single and dual)
and the eye pictures equal within the grass's sway. The direct entries'
restore above touches exactly the grass's draw path; whether it was the
cause on Windows is unverified.
(Settled on 2026-11-06, the section "The second headset look and the
carried registers" below: the ground was not fixed by the restore, and the
grass's reach is the shader's.)

Also seen: FoliageAA.dll hooks the device's DrawPrimitive and
DrawIndexedPrimitive after OBNG's probe; the probe puts its hooks back on
top so the game's call sites stay visible.

## The second headset look and the carried registers (2026-11-06)

Nadi, Windows, headset, single pass, the build of 2026-11-05 20:59: sky,
torches and spells right. Left: "the distance at which grass is still drawn
shrinks and grows as I look left or right, the same for some ground textures
in the distance; the left eye often has the textures, the right one not;
near, such textures look as if at the wrong distance"; and the body shaking
a few times when the weapon is drawn (OBVR's, passed on). The Windows logs
of that session are kept in artifacts/windows/20261105-2130/.

**The ground: the route's fault, found and fixed ([Route]
StereoCarryConstants).** Measured in the eye pictures first: the horizontal
shift between the two eye pictures (OBVR's EyeDump, 642 px, the best
correlation per band of rows) is 102 px for the far background in both
modes; for the ground at the player's feet it is 116 px in the dual pass
(runs obng-grass-turn-dual/20261105-194508 and -194847, correlation 0.75 at
116) and 102 px in the single pass since StereoRestoreDirect (runs
obng-grass-turn-single/20261105-195203, -205351, -205633 and
20261106-082525, correlation 0.71 to 0.82 at 102): that ground layer had no
depth between the eyes. The grass's own matrix was not it: the grass rows
c9..c12 read back from the device differ between the eyes by the view shift
(run 20261106-083722; the grass map's entry is `c9x4`, defined id 23).

The eye constants trace ([Route] StereoTraceConstants: in every sixteenth
single-pass frame the device's vertex registers c0..c47 read back before
the second eye's switch, after it and after the switch back, a line per
shader class and pass) named it, run obng-grass-turn-single/20261106-084429:
`direct draw, shader class 00A90634 (ShadowLightShader), "Block (0, 0)"` -
a terrain block - drawn again and again with **no register changed for the
second eye**, its vertex map holding the live entries c19, c20, c21 only,
where the block's first draw has `c0x4`, defined id 23 (ModelViewProj). The
engine does not upload every view-dependent constant for every draw: the
terrain's later texture passes draw with the c0..c3 the block's first pass
left on the device (measured: the map tables of the two draws; why the
entry is not live there is unread). The second eye's re-apply walks the
current map, so it changed nothing, and the pass was drawn in the second
eye with the first eye's matrix - a texture layer at the first eye's place
in the second eye's picture, and gone wherever its equal-depth test against
the layer below fails, which changes with the terrain's slope against the
view: with the head's turn. Before StereoRestoreDirect the same cause
showed the other way round (the first eye's later passes drawn with the
second eye's c0, which nothing had put back): "the right eye sometimes with
more ground texture than the left" on 2026-11-05. The section above called
the ground fixed on the evidence of which layers each eye showed; the depth
between the eyes was not measured then. It was not fixed.

The fix (StereoEyeSwitchLogic.h, RegisterPairs): the switch reads the
device's vertex registers before it moves the view and again after the
re-apply, and remembers per register the pair - the first eye's value and
what the re-apply made of it. A later switch whose re-apply leaves a
register alone that still holds exactly the remembered first-eye value
gives the second eye the remembered second-eye value; the switch back
writes every register that the re-apply or the carry changed back to the
value read before the switch, on every route (the direct entries' re-apply
on restore is not used then). The values are the engine's own, read back,
never recomputed: a carried layer has the bits of the layer below it. A
pair is dropped when the register holds something else or the eye offset
changes; a device that does not answer GetVertexShaderConstantF (the engine
creates a pure device; DXVK answers) leaves the switch as it was.

Measured after it: run obng-grass-turn-single/20261106-085141 - no draw
without a register of its own, every route's registers after the switch
back equal to before, 72 750 registers carried in 5 s, and the ground at
the feet at 116 px (correlation 0.80), the dual pass's value. Run
obng-single-pass-meadow/20261106-090501 (Nadi's place of 21:29, Tamriel 13
20, the Windows grass distances): no such draw either, 69 047 carried.

The price, fixed scene (single-pass-measure-on, settled to watched, the
route's own clock): 0.50 us per applied switch with the carry (run
20261106-090107) against 0.29 without ([Route] StereoCarryConstants=0, run
20261106-085634) - 0.21 us more, two register reads and one write. Derived
for the Windows outdoor session of 2026-11-05 (1.06 million applied
switches in 10 s): 0.2 to 0.5 ms a frame between 90 and 45 frames a second;
not measured there. The cheaper form is known and not built: a shadow of
the registers kept from the device's SetVertexShaderConstantF (no reads),
checked against the device now and then.

**Outdoors after the tutorial dungeon (Nadi's save 15, 2026-11-06).** The
shore at Vilverin opposite the sewer exit, Unique Landscapes active, grass
and layered ground at the feet; the save copied as OBNGSewerExit, the head
turned all around in 45-degree steps, then ten seconds walked
(tools/hand-scripts/obng-sewer-exit-single.txt / -dual.txt, the Windows
grass distances). Three runs: the single pass with the carry
(obng-sewer-exit-single/20261106-092234), without it (-092428) and the dual
pass (obng-sewer-exit-dual/20261106-092609). Measured: at the eight
standing marks each eye picture of the single pass with the carry differs
from the dual pass's by under one grey level a pixel on the ground half
(0.02 to 0.93), at most 0.05 % of the pixels by more than 40, and the shift
between the eyes per band of rows (tools/eye-shift.py) is the dual pass's
at every mark; no draw without a register of its own. **The place does not
tell the fix from the fault, though:** the run without the carry is as
equal to the dual pass, although its trace has the terrain draws without a
register of their own ("Block (3, 0)", three in the sampled frames) - they
do not land in these pictures. The place where the fault shows on Linux is
the meadow at Weynon Priory (above). Whether Nadi's ground on Windows, with
its mod list, is right now is not measured; the same two scenarios run on
Windows would measure it.

**The grass's reach (read and measured; not a fault of the single pass).**
All 24 grass vertex shaders (GRASS2020..2043.vso, shader package 019,
disassembled with vkd3d-compiler) fade the blade by the length of its
clip-space position: `dp4 r2.w, r0, r0` on r0 = ModelViewProj x position
(c9..c12), then `(length - c5.x) / c5.y` and `(length - c5.z) / c5.w`
against AlphaParam c5. That length is sqrt((x/tanW)^2 + (y/tanH)^2 + z^2 +
w^2) of the view-space position: it depends on the projection and on where
in the picture the blade stands - 1.41 times the distance in the middle of
the view, sqrt(3 / (1 + tanW^2)) times at its horizontal edge (1.22 for the
game's 75-degree monitor frustum, 1.14 for the Linux harness's tangent
1.152, 1.01 for the Windows headset's 1.402; derived).

Measured with the grass probe (`[Probe] Grass`, GrassProbeLogic.h
ShaderFade: for every grass draw of the world pass the device's c5,
c9..c12 and the blade positions c20..c42 read back, the shader's fade
computed from them, sorted by the distance from the camera and the third
of the picture's half-width the blade stands in), Nadi's save 15 at the
shore of Vilverin, the head turned all around, fGrassEndDistance 8000 /
start 7000, the mean fade (1 drawn, 0 gone) over the standing marks:

| distance | single pass: middle / between / outer third | dual pass: middle / between / outer third |
|---|---|---|
| 2000-5000 | 1.00 / 1.00 / 1.00 | 1.00 / 1.00 / 1.00 |
| 5000-6000 | **0.57** (20) / **1.00** (28) / - | **0.53** (1496) / **0.99** (836) / - |
| 6000-7000 | 0.00 (6) / - / 0.00 (6) | 0.00 (176) / - / 0.00 (304) |
| beyond | 0.00 | 0.00 |

(samples in brackets; runs obng-sewer-exit-single/20261106-111726 and
obng-sewer-exit-dual/20261106-111906). Between 5000 and 6000 units a blade
in the middle of the view is half gone on average while one further out in
the picture is fully drawn, in both modes alike: a clump there comes and
goes as the head turns, and the dual pass has it as the single pass has.
These are the shader's numbers computed from the device's constants, not
pixels counted; an attempt to count the clumps in the eye pictures (one
head direction's picture turned into another's view) was too coarse to
tell (four of seven strips in the predicted direction). Derived for the
Windows headset's wider frustum: grass ends about 5 660 units away straight
ahead and about 7 950 at the edge of the view. Why Nadi saw it only in the
single pass is not explained by this. The fix - the grass vertex shaders
fading by the distance to the camera - is `[Fix] GrassFade`,
docs/subsystems/grass-fade.md.

**The player's body as a wedge (open, whose is not known).** In the test
hall the player's upper body stands in both eye pictures as a pale shape
stretched from the camera into the distance; with a shirt equipped through
the console (Sack Cloth Shirt 00027319) the shirt is stretched instead (run
obng-single-pass-torch/20261105-200255; OBVR measured the same with a
cuirass and with another shirt). OBVR's runs date its first appearance
between 2026-11-03 16:04 and 2026-11-04 16:39 and see it in the dual pass
too; in that window the route came into use. With [Route] StereoDraw=1 the
four draw functions are OBNG's rebuilds in every mode, so anything that
relied on the originals' bodies running - a patch inside them, a check of a
return address in them - no longer holds. Not seen in the meadow runs of
the same evening. The run that decides it: the scenario with the route off
(tools/hand-scripts/obng-measure-original.ini).

## The third headset look on Windows (2026-11-07, logs in artifacts/windows/20261107-2345)

Nadi's verdict (headset, not a measurement): the single pass is right, grass
and all. Read from the run's `OBNG.log` (OBNG.dll ace16791383b, the build of
6ec278f, `[Fix] GrassFade=1 GrassFadeReach=game`, `[Route]
StereoCarryConstants=1` by default):

- The grass fade fix bound two twins in the whole session (vertex shaders
  4E8F03B0 and 4E8F14E0, clip position r0 and r1, rows from c9, temps r6 and
  r7) - two grass shaders were all the game asked for at Nadi's settings.
- The carry ran on every exterior summary (300 000 to 820 000 registers
  carried per 10 s), 0 refused draws, 0 re-applies refused.
- The eye switch costs 0.57-0.60 us plus 0.11-0.12 us re-apply per applied
  switch, against 0.43-0.45 + 0.12 us in the look of 2026-11-05 without the
  carry: the carry costs 0.14 us per switch on Windows (measured, the same
  machine, the same DXVK). At 1 400-2 400 switches per frame that is
  0.2-0.35 ms per frame; the whole switch takes 0.7-1.5 ms per frame in the
  late exterior summaries.
- Frames per 10 s summary (`eye data set`): 900 in the interiors at the
  start, 329-676 in the exterior summaries of the second half, against 900
  in every exterior summary of 2026-11-05. Derived: the second half ran at
  33-68 frames per second, the earlier look at the headset's 90. The draws
  per frame were higher (2 200-2 400 against 1 100-1 600), the draws per
  second lower (46 000 against 100 000) - so the frame did not get slower
  by the draw count alone. Between the two looks changed: the carry (+0.2-
  0.35 ms derived above, not enough), the grass twins, OBVR.dll
  (2026-11-07 23:02), OblivionWalkers.dll (2026-11-06), WaterAA.dll
  (2026-11-06) and the place. Which of these holds the rest is not
  measured; the Gold Road on/off harness runs on Windows (OBVR's
  `tools/hand-scripts/single-pass-gold-road-on.txt` / `-off.txt`, save
  OBVRHarnessMOO) with OBVR's phase samples are the measurement that would.
- `Shadow:` lines with nothing after them are the shadow recorder's summary
  when every switch is 0 - noise, harmless.

## The Gold Road frame profiled on Linux (2026-11-08 13:35-13:46, runs obng-profile-gold-road-single/20261108-133456 (carry on), /20261108-133844 (carry off), obng-profile-gold-road-dual/20261108-134233)

Three runs of OBVR's Gold Road scenario (MOO save, three road spots, head
sweep and a walk at each) with OBVR's render sampler on (a sample of the
render thread about every millisecond, `OBVR-performance-1-2/samples.csv`,
read with OBVR's `tools/perf-functions.py`) and GPU timing on. The OBNG
side as Nadi's Windows `OBNG.ini` has it (`tools/hand-scripts/
obng-measure-profile.ini`, the carry off in `-nocarry.ini`); OBNG.dll
ace16791383b; OBVR.dll the build OBVR deployed the same morning (its
worktree, with RealHeight), not the one of Nadi's Windows look. Linux
headless harness at 75 Hz, 2568 px per eye, MSAA 4. A first attempt at
10:02 ran beside Detroit: Become Human and a second at 13:33 beside an
OBVR harness game; both thrown away, the figures below are from the
machine alone. All measured unless marked.

**The frame is at vsync in all three runs**: 13.3-13.4 ms mean at every
spot, p95 14.0-14.6 ms, with 1.8-2.6 ms of the render thread waiting for
poses. The GPU before submit at spot 2: 7.05 ms (single, carry), 6.93
(single, no carry), 7.43 (dual). The scene pass per frame (dual: two
passes, both counted):

| Spot | Single, carry | Single, no carry | Dual (2 passes) |
|---|---|---|---|
| 1 (-33 -4) | 2.88 ms | 2.71 | 3.56 |
| 2 (-27 -3) | 3.90 | 3.73 | 4.92 |
| 3 (-21 -1) | 4.25 | (unusable, see below) | 5.20 |

The single pass saves 0.7-1.0 ms of render-thread time per frame against
the dual pass (19-21 %). The carried registers cost 0.17 ms per frame by
the phases (0.05 ms by the switch's own timer: 0.39+0.09 against
0.29+0.15 us per switch, 1 050-1 180 switches per frame).

**Where the render thread's time goes at spot 2** (single, carry; 21 047
samples over the phase, 6 306 of them inside the scene pass):

- Inside the scene pass (30 % of the samples): engine 39.9 %, D3D9 (DXVK)
  29.3 %, OBNG.dll 9.5 %, OBVR.dll 6.5 %, kernel32 4.5 %, FoliageAA 4.4 %,
  d3dx9 1.6 %, memcpy 1.2 %. On the stack most often: 0x0070AB40 (NiNode's
  walk, 29 %), 0x0070DFD3 (24 %), BSShaderAccumulator finish 0x007AE070
  (21 %), ShadowSceneNode render 0x007C78D0 (20 %), 0x007F6BF0 (18 %),
  the constant-map apply loop 0x009A8E30 (18 %), 0x007AC9A0 (16 %). OBNG's
  9.5 % is the route (0.37 ms per frame, derived), mostly under
  0x009A8E30. In the dual run OBNG holds 5.2 % and the engine 50.6 %.
- Outside the scene pass (70 % of the samples): engine 55.3 %, vrclient
  30.4 % (the poses wait, idle), ntdll 7.5 %, OBVR 3.9 %, Walkers 0.7 %,
  D3D9 0.4 %. On the stack: 0x008E4590 (18.8 %), 0x008DE950 (18.1 %),
  0x00893B90 (16.5 %), 0x0070A0D0 (13.3 %), 0x008CED20 (13.2 %),
  OSGlobals::RenderFrame 0x0040D4D0 (12.7 %), 0x008ABF70 (11.5 %),
  0x00891850, 0x008AC640 (10.2 % each), 0x004823D0 (10.0 %) - unnamed in
  the knowledge base so far.
- Derived from the shares and the 13.4 ms frame: about 2.8 ms idle, 3.9 ms
  scene pass (of which 1.6 engine, 1.1 DXVK, 0.4 OBNG, 0.25 OBVR), 5.2 ms
  engine code outside the scene pass (the game's own frame: update, AI,
  animation, culling before the pass), 1 ms ntdll and the rest. At 90 Hz
  this thread would have about 0.4 ms to spare on this machine - a hair
  from CPU-bound - with the GPU at 7 ms of 11.1.

**What this says about Nadi's Windows frame (derived, not measured
there):** the Windows headset draws 4036x3376 per eye, 2.07 times the
pixels of the Linux harness; if the GPU time scaled with the pixels and
the card were the same, the GPU alone would take about 14 ms, over the
90 Hz budget - the slow exterior (33-68 frames per second on 2026-11-07)
may well be the GPU, not the CPU. The Windows GPU and CPU are not known
here. The run that decides it: the same three scenarios on Windows
(`hand-script-run.ps1` with `..\OBNG\tools\hand-scripts\
obng-profile-gold-road-single.txt`, save OBVRHarnessMOO), read with
`perf-phases.py`: the column "app gpu before submit" against the frame
time.

**Open from these runs:** in the carry-off run the draws collapsed after
the teleport to spot 3 (70 whole draws and 4 eye switches per frame
instead of 300 and 1 100, scene pass 0.45 ms, 1 038 frames dropped by the
compositor over the run); no menu and no refusal in the logs, the monitor
mirror is black in the headless harness either way (both runs show the
game's "Your Health is low" box at spot 3, the save's player is hurt by
the teleport). Whether the route without the carry (the restore through
the re-apply) or the scenario is at fault is not known; the carry-on and
dual runs drew spot 3 normally.

The repeat of the carry-off run (14:04, obng-profile-gold-road-single/
20261108-140426, the machine alone) drew spot 3 normally: 1 225 switches
per frame, scene pass 3.84 ms mean, 81 frames dropped over the run. The
collapse of 13:38 did not come back in one repeat; its cause stays
unknown, it is not reproduced by the route without the carry alone.
Scene pass without the carry over the three spots: 2.79 / 3.74 / 3.84 ms,
so the carry costs 0.1-0.4 ms per frame by the phases (the two carry-off
runs agree within 0.03 ms at spots 1 and 2).
