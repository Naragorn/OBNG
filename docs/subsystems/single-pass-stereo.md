# Single-pass stereo: the eye pass as one unit

Status 2026-11-04 12:10: **steps 1 and 3 done, OBNG's half of step 4 built (the interface and the eye switch), OBVR's half open.** The engine side is read (slice
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
