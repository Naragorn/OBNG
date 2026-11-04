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
