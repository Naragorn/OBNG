# Culling walk and the per-draw shader setup

Status: knowledge, written 2026-10-30 from the draw-setup slice (instruction-level
mappings held by three skeptics each: `re/functions/0x0070DFB0.md`,
`0x007D2EA0.md`, `0x007C9230.md`; the plane test itself is
`0x0047DA70.md`, knowledge/bone-palettes.md). Nothing of it is built in OBNG
yet; the plane test is being rebuilt.

Marks: **read** = read in Oblivion.exe 1.2.0.416 at instruction level,
**measured** = seen in the running game, **derived** = follows from those,
**unverified** = not checked.

## The frustum cull: NiCullingProcess::Cull 0x0070DFB0 (read)

Slot 1 of the `NiCullingProcess` vtable 0x00A7E610 (RTTI; OBVR's
GameAddresses.h names the slot the same). Ghidra split it in two
(0x0070DFB0 the entry, 0x0070DFD3 the loop, reached only by the JNZ at
0x0070DFC0): one function, `__thiscall(object)`, `RET 4`, no return value.

- Per visited scene-graph object it tests the object's world bound
  (object+0x20 centre, +0x2C radius) against the process's six frustum
  planes (this+0x2C, 16 bytes each) under the active mask at this+0x8C, in
  plane order 0..5, through 0x0047DA70 (the sphere against the plane).
- Result 2 (wholly outside) culls: the loop stops, the object is not
  visited. Result 1 (wholly inside) clears that plane's bit for the rest of
  this visit. Result 0 (straddling) changes nothing.
- When no plane said 2, or the mask was 0 on entry, the object's vtable
  slot +0x7C is called with the process (`OnVisible`, a proposal): an
  `NiNode` (0x0070AB40) walks its children through 0x007073D0 back into this
  function with the **reduced** mask; an `NiGeometry` (0x007227A0 ->
  0x0070E1A0) appends itself to the array at process+0x08 when that pointer
  is non-null and byte process+0x04 is 0; `NiAVObject` itself does nothing.
- On return the mask is restored to its entry value. So the mask is the
  hierarchical plane coherence of the walk: a child is tested only against
  the planes its parent's bound straddled (derived).
- The planes and the mask come from 0x0070E040 (copies the frustum, builds
  the planes through 0x00717A40, sets the mask to 0x3F). `TESWaterCullingProcess`
  (vtable 0x00A3F380) overrides slot 1 with 0x0049CBF0: an extra-data scan,
  one plane at its +0x90, then this function.
- Cost shape (derived): at most six plane tests and one virtual call per
  visit; fewer tests deeper in the tree when parents lie inside planes.
  BSFadeNode's +0x7C override 0x004A0920 (distance fade) is unread and
  decides what most game objects do once visible.

## The shadow light's range test: ShadowSceneLight::IsInRange 0x007D2EA0 (read)

`__thiscall(object, sourceLight)`, `RET 8`, bool in AL. Null light: false.
A light whose RTTI is `NiRTTI_NiDirectionalLight` (0x00B3FCFC): true. Otherwise
the object's bound, copied to the stack, goes one of two ways by this+0x14C:

- **+0x14C != 0, the plane path**: the six planes at this+0x150 under the mask
  at this+0x1B0, same layout as the culling process's; a 2 makes the result
  false **but the loop goes on**, a 1 clears the bit, a 0 does nothing.
- **+0x14C == 0, the distance path**: `light+0xF8 > sqrt(|light.pos(+0x88) -
  centre|^2) - radius`, every step rounded to single, false on equality and
  NaN; the mask is untouched. The square root is the CRT's 0x00982C30.

The constructor sets +0x14C = 0 and the mask to 0x3F (read), so a new light
takes the distance path. The recursive walk 0x007D59E0 saves the mask before
the call and writes it back on every exit; the per-geometry caller
0x007C6100 does **not** restore it. Derived: if any light reached through
0x007C6100 ever has +0x14C != 0, its cleared bits persist across geometries
and later objects are tested against fewer planes - an engine defect, or a
reset that sits in an unread writer (0x00717A40 called with the planes'
address at 0x007D4D11 is the candidate). **Finding for OBVR's shadow work,
unverified in the game.** What +0x14C points to and who sets it non-zero is
unread.

The 3.8 % "culling" share OBVR's sampler attributed to this caller is a
measurement without a per-caller split (unverified which of 0x0070DFB0 and
0x007D2EA0 carries it); a counter on 0x0047DA70 split by the return addresses
0x007C61C7 and 0x007D5A10 (and 0x0070DFFC) would settle it. A batched
rebuild of either loop must keep its own handling of result 2: early exit in
the culling process, continue in the range test.

## The per-draw shader setup: ShadowLightShader::SetupTransformations 0x007C9230 (read)

Vtable slot 13 (+0x34) of `ShadowLightShader`, `ParallaxShader`, `SkinShader` and
`SpeedTreeBranchShader` (COLs, read); `HairShader`'s own slot 13 (0x0080B7A0)
calls it last. `__thiscall` with **eight** stack arguments (`RET 0x20`; the
decompiler shows seven), of which only 2 (the skin instance record), 3 (the
partition), 5 (the property state) and 7 (the world NiTransform) are read;
returns 0 always. The method name is a proposal (Gamebryo's NiShader slot
with that signature is recalled, not read).

Per drawn geometry, in order:

1. Skin gate: when the shader property (`[arg5+0x18]`) has bit 1 of its +0x1C
   flags, a missing partition ends the call; otherwise
   **BuildBonePalette** runs on the object at `[0x00B43104]` with
   `(skin = arg2, world = arg7, rowTranslation = 0, mode = 3,
   separateSkinToWorld = 1)` unless this+0x34 is set, then
   `[[this+0x18]]+0x30(partition.+0x24)`.
2. Model transform `0x00765480(world, 0)` on this+0x14 (packs the transform
   into renderer+0x940; pseudocode).
3. Three early-out pass kinds (render mode `[0x00B42EAC]` 5 with g = 0; pass
   id `[0x00B42E90]` 6..9) write only the vec4 at 0x00B44FD8 and return.
4. The inverse world `Minv`: D3DXMatrixInverse of the skin's +0x28 matrix with
   the offset vector 0x00B3F92C added (skinned), or the NiTransform inverse
   0x00718A80 of `world` rebuilt as a 4x4 (not skinned).
5. Two object-space light directions into 0x00B454D8 (descriptor 0x00B45030).
6. **UpdateLightPositionConstants(&Minv, world.scale, passId, skin)** - on
   every draw that reaches here. The seed's "scale +0x0C of a transform
   record" was wrong: it is the NiTransform's scale at +0x30.
7. The point constant 0x00B44F08 (and 0x00B44F18 in passes 0x180..0x187), the
   shadow projection 0x00B45498, the packed world 0x00B45560, the transposed
   skin matrix 0x00B44F98 - each behind its descriptor byte +0x08.
8. Passes 0x177..0x17A: six `SetClipPlane` calls (device slot +0xDC) with
   planes computed once per shadow light and cached at light+0x1B4/+0x214.

What is per draw and what is not (derived): the skin, the partition count,
the property, the world transform and `this` (+0x14, +0x18, +0x34) change per
draw; pass id, render mode, g (0x00B44EE4), the offset vector, the light
tables, P (0x00B46658) and the shadow light are globals or per light whose
writers are unread, so "constant per frame" is **unverified** for each. A
per-draw cache of Minv, the directions, the point constant and 0x007C9140's
output pays off only when the same object is drawn again in another pass
with unchanged globals; it never repeats across objects. The palette call is
already gated by its own stamp. The lever this leaves: the call count of
slot 13 per frame (who calls it per geometry is not in the export) and the
cost split inside 0x007C9140 (three D3DX transforms) - neither measured.

## The scene pass as a boundary (read 2026-10-30, scene-pass slice, six mappings held)

From the world render down to the draw, the chain is (every hop read at
instruction level except where marked):

1. **OSGlobals::RenderScene 0x0040C830** (`__thiscall(BSRenderedTexture*)`,
   RET 4): guard byte 0x00B33395, a water pre-pass for interior cells with
   water, the actor shadow maps 0x004073D0 (unconditional, before the
   scene), the first-person node hidden, the follower roots updated, the
   render target chosen (texture manager with a scaled port when byte
   0x00B42F3E is set), then **the main scene through
   `0x0070C0B0(camera, sceneGraph, cullingProcess = [sceneGraph+0xE4], 0)`**,
   a gated first-person pass with a swapped accumulator, the device copy,
   image space 0x007B48E0, port reset, release. It issues no draw itself.
2. **NiDrawScene 0x0070C0B0** (read 2026-10-30, draw-hop slice):
   `__cdecl(camera, scene, cullingProcess, visibleArray)`. SetCameraData
   0x00701970 hands the camera's rotation, translation, frustum and
   viewport to the renderer's slot 80 (the view/projection builder,
   unread); then `process->Process(camera, scene, array)` (slot 2,
   0x0070E0A0) runs the culling walk (NiCullingProcess::Cull per object,
   above). With an array (never on the world pass: all nine call sites
   pass 0 and OBVR measured process+0x08 null there) NiDrawVisibleArray
   0x0070C000 draws it between the accumulator's +0x4C and +0x50; **on the
   world pass each geometry that reaches 0x0070E1A0 is registered with the
   accumulator [renderer+0x08] (+0x58) or rendered at once through its own
   slot 33** (+0x84, the renderer as argument -> Do_RenderShape below). Where
   the accumulator's finish (+0x50) drains its registered geometries into
   slot 33 is the one hop still unread (BSShaderAccumulator per OBVR's
   wording).
3. Per geometry, through NiDX9Renderer vtable slots 88/89:
   **Do_RenderShape 0x007693E0** - early out on renderer byte +0x6F0 or a
   zero vertex count, the world bound and transform copied to the stack,
   0x00768890 decides "by skin partitions", 0x00778DB0 builds missing
   renderer data (per partition at +0x28, or data+0x38), then
4. **DrawSkinnedPrimitive 0x00767520** (skinned) or 0x007672F0 (whole
   geometry): shader = geometry+0xBC (NiD3DShaderInterface) or the default
   at renderer+0xA94; slots 10/11 open the pipeline; **per pass (slots
   18..19) and per partition: slot 13 SetupTransformations 0x007C9230 (the
   per-draw constants and the bone palette), slot 15 (the buffer record;
   null aborts), slot 14 SetupShaderPrograms 0x0077A1F0, the
   [[renderer+0x8AC]+0xFF0] slot-1 call, one DrawIndexedPrimitive per strip
   or array, slot 16**; slot 17 closes, then [renderer+0x8AC] slot +0xFC(0).
   Slot 15 is **PrepareGeometryForRendering 0x0077A310** (read 2026-10-30):
   it decides whether to pack, calls PackSkinnedGeometryBuffer (record =
   [partition+0x28]) or the whole-geometry pack 0x00767B40, then binds the
   record on the device (SetStreamSource per stream, SetIndices) and
   returns it - so the pack sits inside the draw, per partition and pass.
5. **SetupShaderPrograms 0x0077A1F0** is slot 14 of NiD3DShader, inherited by
   30 shader classes - **not the "render walk" OBVR's sampler table named**
   (correction to knowledge/skinned-vertex-streams.md): it resets the
   `__NDL_SCMData` extra data (through NiObjectNET::GetExtraData 0x006FF9C0,
   whose critical section is the "lock" of that table), forwards the
   arguments and the pass index to the pass object's slot 1 (binds the
   programs - pseudocode) and applies the constant maps through their slot
   16 (the constant uploads - unverified). Its 49 % on-stack share is the
   inclusive time of those virtual calls, i.e. of program binding and
   constant upload per partition.
6. Packing, on the shader side: shader slot +0x3C (0x0077A310, 32 vtables)
   calls **PackSkinnedGeometryBuffer 0x00763130** with the record loaded
   from partition+0x28; when the dirty mask has a bit other than 0x10 it
   re-packs every vertex stream through the declaration's slot +0x6C
   (0x0076FE20, NiDX9ShaderDeclaration), which calls **PackEntry 0x0076F590**
   once per declaration entry (`__stdcall`, seven arguments): destType
   picks the routine row and element size (tables [0x00B42700]+4 and
   0x00B42708), the selector picks the source array and format column,
   the 0x28-byte descriptor is built on the stack and the routine
   (CopyVertexStreamElements 0x0076E610 for the same-format case) is
   called once. **PackEntry never passes a null source** (read): the
   zero-fill path of 0x0076E610 is unreachable from here, so OBVR's
   zero-fill size difference cannot matter on this path. Static geometry
   (consistency 0x4000) with all buffers present returns at once; volatile
   (0x8000) or forced repacks everything. **PackStream 0x0076FE20** (slot
   +0x6C, read 2026-10-30) packs one stream per call: validates or
   reallocates the chip (a volatile geometry always reallocates, dirty =
   0xFFF after it), **locks once** (0x00776C90, the staging block), runs
   PackEntry per declaration entry the dirty mask lets through by selector
   group, **unlocks once** (0x00776D80: memcpy staging -> mapped, D3D
   Unlock). So the 135 unlocks a pass are one per packed stream, and a
   volatile body repacks every stream every pass by construction - the
   structural reason OBVR's StreamReuse gained nothing. Which consistency
   nibble the skinned bodies carry per frame (0x8000, or mutable with dirty
   bits) is still the one runtime fact to log; it decides whether a
   replacement may keep packed streams across passes at all.

What the boundary's output is (derived from 4 and 6): per visible skinned
geometry, per pass, per partition one constant setup, one program bind,
one stream pack when dirty, one indexed draw. A replacement of the pass
can be checked at exactly these points: the constants written
(0x00B44FD8 and its siblings), the packed bytes, the draw-call sequence.

## Measured in the game (2026-10-30, OBNG.dll's call counters, Linux, 75 Hz, OBVR dual pass)

Run `artifacts/shadow/obng-scene-pass/20261030-101556` (prison empty for
30 s, then eighteen Walkers standing round the player for 45 s; counts per
10-second summary, frames from OBVR's "frames shown since the last mark":
75 a second empty, 70 with the Walkers; every number is calls per frame,
both eyes and the shadow passes together):

| function | empty prison | 18 Walkers | from where |
|---|---:|---:|---|
| shader slot 13 `SetupTransformations` 0x007C9230 | ~670 | ~2 500 | 0x007673C6 (in the skinned draw 0x00767520) and 0x007676A1 |
| `NiCullingProcess::Cull` 0x0070DFB0 | ~1 080 | ~2 800 | 0x0070AB73 (the NiNode child walk) 99.7 %, 0x0070E145 the rest |
| `ShadowSceneLight::IsInRange` 0x007D2EA0 | ~400 | ~7 400 | 0x007D5A10 (the recursive walk) and 0x007C61C7 (per geometry) about half each |
| `NiBound::WhichSide` 0x0047DA70 | ~2 840 | ~20 900 | **0x007D2F2B, IsInRange's plane path, 70 %**; the frustum culler 0x0070DFFE 28 %; 0x0047F889 and 0x007D647F the rest |

Read off that (measured, derived where said):

- **IsInRange's plane path runs - from the shadow-map walk, not from the
  per-geometry caller** (corrected 2026-10-30 by the range-callers
  skeptics against the same log): 0x007D2F2B is its WhichSide call site,
  so lights with `+0x14C != 0` exist; but in the windows where every
  IsInRange call came from 0x007C61C7 (the empty prison, ~277 000 calls a
  window) there were **zero** plane-path tests, and with the Walkers the
  plane-path count never exceeds six times the 0x007D5A10 count
  (10 202 994 against 2 412 800 x 6). So the data fits every plane test
  coming through 0x007D59E0 (the walk, which saves and restores the mask)
  and the per-geometry caller 0x007C6100 taking the distance path
  (`+0x14C == 0`). **Settled by the counter (measured 2026-10-30 14:50,
  `artifacts/shadow/obng-scene-pass/20261030-145038`, IsInRange split by
  `[this+0x14C]` and return site): the plane path's 1.8-1.95 million calls
  per 10 s come entirely from the walk 0x007D5A10; the per-geometry caller
  0x007C61C7 makes 2.8-3.0 million calls per 10 s, every one on the
  distance path.** The unrestored mask under 0x007C6100 is dead in this
  scene; whether any scene gives a relit geometry's light planes is not
  shown. Derived: with eighteen bodies the shadow
  lights' range test still costs more plane tests than the frustum cull
  itself - in the shadow-map pass.
- The per-frame counts scale with the bodies: slot 13 and IsInRange about
  four and eighteen times the empty prison, WhichSide seven times. The
  3.8 % "culling" of OBVR's sampler is therefore mostly 0x007D2EA0's loop,
  as the sampler said, not the frustum walk.
- Slot 13 is called about twice per OBVR's draw count (1186 a pass): the
  shadow passes draw the casters again (derived from the counts and the
  mapping; a split by pass id would settle it).
- `CopyVertexStreamElements` could not be counted: OblivionFrameFixes'
  `FastStreamCopy=1` holds that entry (and `PreciseFrameClock=1` holds
  TimeInfo::Update's) on the Linux install, independent of OBVR's own
  switches; a count needs that plugin's switch off for the run.

## The two callers of the range test (read 2026-10-30, range-callers slice, both held)

- **ShadowSceneNode::RelightGeometry 0x007C6100** (`__cdecl(geometry P,
  ShadowSceneNode* node or null)`, plain RET, AL): finds the node from P's
  type-4 property when none is given, detaches P from its lights
  (0x007C5750; a 0 ends the call), then walks the node's light list
  (+0xE4/+0xE8) and for every light with data calls
  `light->IsInRange(P, sourceLight)` with a temporary NiPointer copy of
  light+0x100, releases it, and `light->AddGeometry(P)` (0x007D5790) when in
  range. ~4 000 IsInRange calls a frame with the Walkers (lower bound on
  lights x relit geometries). It never saves or restores the mask - which
  only matters if one of these lights ever has `+0x14C != 0` (see above).
- **ShadowSceneLight::CollectGeometryInRange 0x007D59E0** (`__thiscall(light;
  object)`, RET 4, AL = 1 unless the gate [0x00B42F48] is 0): the recursive
  walk the per-light shadow draw 0x007D6900 starts at the scene root (and
  at the caster with self-shadowing): saves +0x1B0, tests IsInRange(object,
  light+0x100), skips the subtree when out of range; an in-range node
  recurses - an exact NiSwitchNode (RTTI 0x00B3FD70) only into its active
  child (0x007C59E0, 0x0049FFC0), any other node into every non-null child
  with radius +0x2C != 0.0 and flags bit 0 clear - and an in-range geometry
  goes to AddGeometry 0x007D5790; the mask is written back on every exit.
  So the children start with the parent's narrowed mask and the narrowing
  is undone per subtree: a hierarchical plane cull per light over the
  scene, ~2.0-2.4 M visits per 10 s with the Walkers. **This walk is where
  the plane tests are, and it is the unit of the batched rebuild**: one
  light, one scene walk, six planes - the same shape as
  NiCullingProcess::Cull, so one batched plane test serves both.

## Open

- Which render-loop function calls slot 13 per geometry, and how often per
  pass (the count is the lever).
- What 0x00B42E90 (pass ids 6..9, 0x77, 0x85, 0x177..0x17A, 0x180..0x187) and
  0x00B42EAC (mode 5) enumerate, and who writes them.
- The table at 0x00B465A8: a w per slot (0x007C9140) or direction vectors
  (0x007C9230, entries 0 and 1) - one layout, two readers; its writer decides.
- Whether `[0x00B43104]`, the shader's this+0x14 and OBVR's renderer
  0x00B3F928 are one object.
- +0x14C of a ShadowSceneLight and the unrestored mask under 0x007C6100.

## Where a scene pass goes on the fixed Walkers scene (measured 2026-10-30)

OBVR's render sampler (`../OBVR/tools/perf-functions.py <run>/OBVR-samples.csv
<run> settled watched --in-pass only`) over OBNG's fixed-scene run
`artifacts/shadow/obng-walkers-original/20261030-151727` (ImperialDungeon01,
eighteen Walkers standing, dual pass, scene_pass mean 2.68 ms of a 13.3 ms
vsync-held frame; 21 666 in-pass samples in the sixty seconds watched).
Exclusive, by module and function: engine code 51.7 %, spread - no single
function above 2.4 % (NiBound::WhichSide 0x0047DA70 2.4 %, 72 % of it from
IsInRange 0x007D2EA0 and 27 % from Cull 0x0070DFD3; 0x0077B140 2.4 %;
0x007732B0 1.9 %; 0x0076F3B0 1.8 %; the bone compose 0x0053D7A0 1.5 %);
D3D9.DLL 16.7 % (30 % of it called from the light-constant upload 0x007C9140,
16 % from the inverse 0x00718A80, 12 % from the palette builder 0x007655F0);
OBVR.dll 11.2 % (42 % under 0x007655F0, 30 % under 0x007C9140: OBVR's own
detours there); Oblivion Frame Fixes' fast copy 6.8 %; the CRT memcpy 4.4 %
(91 % from the unlock 0x00776D80); d3dx9 2.6 %. Inclusive (on the stack):
Do_RenderShape 0x007693E0 44 %, DrawSkinnedPrimitive 0x00767520 36 %,
0x0077A1F0 31 % (SetupShaderPrograms plus the packing: the function table
has no start between 0x0077A1F0 and 0x0077A4A0), 0x009A56D0 27 %,
0x007A9820 26 %, 0x007830C0 23 %, 0x00717690 23 %, 0x007AC9A0 21 %,
PackStream 0x0076FE20 13 %, PackSkinnedGeometryBuffer 0x00763130 12 %, the
range walk 0x007D59E0 3.1 %. Over the whole frame 52 % of the render
thread's samples are in vrclient.dll (the pose wait).

The unnamed frames were mapped on 2026-10-30 (slice pass-drivers,
re/functions/0x009A56D0.md, 0x007A9820.md, 0x007830C0.md, 0x007AC9A0.md,
0x007AD270.md, 0x00773330.md; names are proposals that held their refutation
rounds). The chain of the pass, as read there:

1. ShadowSceneNode's render 0x007C78D0 calls the shader accumulator's begin
   (+0x4C), then the cull walk 0x0070AB40, which registers every visible
   geometry through **BSShaderAccumulator::RegisterGeometry 0x007AD270**
   (slot +0x58): classification by shader id and property type, relight
   against the shadow scene node, the property's pass list filed into one
   list per pass id at accumulator+0x104+0x14*pass (opaque) or the blended
   lists; an 0x18-byte FormHeap allocation per multi-pass geometry with a
   morpher controller.
2. The accumulator's finish (+0x50) is **0x007AE070**, unmapped and missing
   from the sampler's function table (its frames are filed under
   0x007AD270: 3806 of the 4419 samples); it draws the lists pass by pass
   through **RenderPassList 0x007AC9A0** (begin the shader state, per item
   **DrawRenderPass 0x007A9820**: pass id to 0x00B42E90, light constants,
   the geometry's slot 33 -> Do_RenderShape 0x007693E0 -> DrawSkinnedPrimitive
   0x00767520; end the state, retire the list).
3. Per partition under the draw: slot 13 SetupTransformations 0x007C9230 (with
   the light-constant upload 0x007C9140), slot 15 PrepareGeometryForRendering
   0x0077A310 (the packing), slot 14 SetupShaderPrograms 0x0077A1F0, whose
   constant-map apply loop 0x009A8E30 calls **NiD3DSCM_Vertex::SetDefinedConstant
   0x009A56D0** per "defined" entry (world / view / projection products,
   inverses and transposes through D3DX, the bone matrices through
   0x00783180 as **one SetVertexShaderConstantF per bone**, texture
   transforms, material colours) and **NiD3DVertexShader::SetVertexShaderConstant
   0x007830C0** per entry (one device call each, no compare, no cache; OBVR's
   SetVsConstantF hook sits below it).
4. **TextureStageGroup::ApplySamplerStates 0x00773330** is five cached
   sampler-state calls per stage and no stream work; its 15.9 % is a stale
   return word (every one of its 4748 samples carries 0x00773369 with the
   eip in memcpy or the copier), real share 1.5 to 3.5 %.

What the maps say about the cost (measured in the same run): the engine's own
instructions are 1.1 % per frame everywhere the maps looked, the D3DX matrix
math about 0.7 %; the inclusive numbers are device calls (one per constant
entry, one per bone, the draws) and the packing's memcpy. And the sampler's
inclusive table over-reports through stale return words (0x009A56D0's 27.2 %
is an upper bound, 3481 of its 5870 samples have no caller word above it), so
the next subsystem is chosen from a direct measurement of the candidate steps
(call counts plus a timer on 0x009A8E30, 0x00783180, 0x007830C0, 0x007AE070),
not from this table.

### The steps timed in the game (measured 2026-10-30 19:01)

OBNG.dll's pass-steps probe ([Probe] PassSteps=1, Probes.cpp: one RDTSC pair
per call, inclusive and nested, calibrated against the performance counter
per 10 s summary window) on the fixed Walkers scene, nothing routed or
shadowed (`artifacts/shadow/obng-walkers-steps/20261030-190102`; OBVR's
capture of the same run: 4497 frames in the 60 s watched, 75.0 a second,
frame 13.34 ms, scene_pass 2.85 ms against 2.78 ms in the run without the
probe - the probe's own cost is inside the run-to-run spread). The steady
windows (the 5th to 11th of the run; 750 frames each):

| Step | Calls per frame | ms per frame | us per call | Share of the finish |
|---|---:|---:|---:|---:|
| FinishAccumulating 0x007AE070 (every draw of every pass) | 18.0 | 5.17 | 287 | 100 % |
| NiD3DShaderConstantMap::ApplyConstants 0x009A8E30 (per partition per pass, vertex and pixel maps; re/functions/0x009A8E30.md) | 4 548 | 1.17 | 0.26 | 23 % |
| SetVertexShaderConstantArray 0x00783180 (the per-bone upload loop, inside the above) | 1 623 | 0.52 | 0.32 | 10 % |
| SetVertexShaderConstant 0x007830C0 (one device call per entry, inside the above) | 2 320 | 0.07 | 0.03 | 1.4 % |

What that settles:

- The accumulator's finish holds 5.17 ms of the 13.34 ms frame - about three
  quarters of the frame's CPU work once the 6.3 ms pose wait is taken out -
  over eighteen finishes a frame (two eyes, the actor-shadow and other passes;
  which finish costs what is the next probe's question: return sites and
  time per site).
- The constant maps are 1.17 ms a frame, 23 % of the finish: 4 548 applies a
  frame in this round (4 564 in the second round, the same 1.17 ms). Inside them the per-bone upload loop is 0.52 ms (1 623 partitions
  with bones a frame, 0.32 us each with N device calls per partition) and the
  plain uploads 0.07 ms (2 320 device calls at 30 ns each - D3D9's client-side
  set is cheap; the "uncached constant upload" lever is worth 0.07 ms, not
  the 16.7 % the sampler's module table suggested for D3D9.DLL). The other
  0.58 ms are the apply loop itself and SetDefinedConstant's matrix work
  (D3DX multiply / inverse / transpose per entry, the pass-constant ones
  included) and the pixel-map uploads.
- What is left (about 4.0 ms a frame) sits below DrawRenderPass outside the
  constant maps; the second round below splits it.

### Round two: the draw side split (measured 2026-10-30 19:44)

The same probe with nine steps and the finish summed per return address
(`artifacts/shadow/obng-walkers-steps/20261030-194432`, 4485 frames in the
60 s watched, 74.8 a second, scene_pass 3.04 ms against 2.78 without any
probe: nine detours on about 19 000 timed calls a frame cost about 0.25 ms,
which sits inside the outer sums). Medians of the steady windows (the 3rd to
9th), per frame:

| Step | Calls per frame | ms per frame | us per call |
|---|---:|---:|---:|
| FinishAccumulating 0x007AE070 | 18 | 5.36 | 297 |
| Do_RenderShape 0x007693E0 (every shape draw) | 2 291 | 4.92 | 2.15 |
| DrawSkinnedPrimitive 0x00767520 (the skinned ones) | 1 588 | 3.50 | 2.20 |
| slot 13 SetupTransformations 0x007C9230 | 2 230 | 0.43 | 0.19 |
| slot 15 PrepareGeometryForRendering 0x0077A310 (the packing) | 2 331 | 1.00 | 0.43 |
| slot 14 SetupShaderPrograms 0x0077A1F0 (the constant maps inside) | 2 331 | 1.42 | 0.61 |
| ApplyConstants 0x009A8E30 | 4 564 | 1.17 | 0.26 |
| SetVertexShaderConstantArray 0x00783180 | 1 628 | 0.53 | 0.33 |
| SetVertexShaderConstant 0x007830C0 | 2 328 | 0.07 | 0.03 |

The finish by return site (per frame): 0x007C7D56 in ShadowSceneNode's
render 0x007C78D0, 2 calls, **4.98 ms** - the two eye passes hold 93 % of
the finish, about 2.5 ms each; 0x0070E152 in 0x0070E0A0, 10 calls, 0.38 ms -
the actor-shadow map passes, 38 us each; 0x0040CE6D in RenderScene
0x0040C830 and 0x007D4EA1 in 0x007D46C0, 2 and 4 calls, empty.

Derived from the table (the probe's 0.25 ms inside the outer numbers):

- Per partition the three shader slots take 2.85 ms a frame: the programs
  with the constant maps 1.42, the packing 1.00, the transformations 0.43.
- Do_RenderShape minus the three slots is 2.07 ms: the device draws (about
  2 330 DrawIndexedPrimitive a frame, so under 0.9 us each with the driver),
  the bone palette build (OBVR's detour sits there) and the dispatch.
- The finish minus Do_RenderShape is 0.44 ms: the accumulator's list walk,
  DrawRenderPass's per-light writes, the shader state begin and end.
- The eye pass is the unit that holds everything: 2.5 ms of CPU per eye.
  Single-pass stereo's ceiling on this scene is one of them.

## Outdoors on Windows: the cull walk stays small, the draw side scales with the draws (measured 2026-11-02)

OBVR's render sampler on Nadi's live Windows session (Pimax, 90 Hz, dual pass,
capture `OBVR-performance-1-23`, 142 s in Tamriel cells 0000AC08 and 00005EFB,
no Walkers, 33 183 in-pass samples; against the interior capture `1-22` of the
same evening, 19 502 samples; the sampler's rows now land in the capture's
folder, OBVR 81de7e8). Read with the stale-return caveat above (0x007AE070's
frames file under 0x007AD270):

- Exclusive by module, outdoors: Oblivion.exe 52-54 %, D3D9 (DXVK) 15-18 %,
  OBVR.dll 7-10 %, Oblivion Frame Fixes' copy 4.5 % overall but 11 % in the
  frames with 5 000+ dynamic-stream draws, FoliageAA 2.5 %. The shares are
  the same in frames with few and with many draws (OBVR 0.27 -> 0.51 ms a
  frame, DXVK 0.47 -> 1.06): nothing in the mod stack grows faster than the
  engine's own draw path. (measured)
- On the stack: the cull walk 0x0070AB40 without the accumulator's finish or
  Do_RenderShape under it is 5 % of the pass, indoors and outdoors alike;
  the finish/dispatch side 69-72 %; the rest 23-27 %. Culling the second eye
  again is therefore not the dual pass's cost - drawing it again is. (derived
  from the sampler; the cull walk's own frames may be under-counted by stale
  words like every inclusive number here)
- Per 1 000 dynamic-stream draws a pass the two passes together cost about
  1.5 ms of CPU (fit over 76 heartbeat windows, r 0.76; 1 400 to 10 000 draws
  a pass in this session) and the compositor's app GPU time about 1.8 ms
  (r 0.66). In the heavier Unique-Landscapes sessions of 2026-10-29/30
  (15 000-73 000 draws a pass, no sampler kept) the passes reached 4.7-6 ms
  each and the frame was CPU-bound at 90 Hz. (measured; the draw counter is
  OBVR's dynamic-buffer draw count, not every draw)
- What a modern engine does instead: one culling and one draw list for both
  eyes (multiview / single-pass stereo), instancing for the vegetation that
  makes up those tens of thousands of draws, and a per-draw cost of
  nanoseconds in a command buffer rather than the 2 us per shape measured
  under Do_RenderShape on 2026-10-30. (reference: the engine behaviour notes
  in ../OBVR/docs/vr-modding; source for the 2 us: round two above)

### The two redundancy probes (measured 2026-11-04 08:31)

Run `artifacts/shadow/obng-walkers-probes/20261104-083110` on the fixed
Walkers scene, nothing routed, per frame (medians of the steady windows):

- **Packing** (docs/subsystems/stream-packing.md, "The cross-frame probe,
  run"): 14 volatile packs a frame of 4.15 MB, 71 % byte-identical to the
  previous pack of the same stream; 1 610 static early-outs.
- **Constant uploads** through the program's slots 0x007830C0 and
  0x00783180: 14 046 uploads a frame (2 320 single entries, all of type 2
  "defined"; 1 629 bone palettes with 11 717 bones, 7.2 a palette, 77 % of
  the palettes with one consecutive index run), 0.68 MB; **39 % of the
  uploads (41 % of the bytes) carry what the register already held, every
  one of them within the same pass** - the previous upload to that register
  by another partition of the same pass. So a check of the inputs before
  computing an entry could skip about four in ten of the constant maps'
  1.17 ms, the single call per palette needs a gather for 23 % of them, and
  a per-pass cache of the view and projection ids is not what repeats here
  (those repeat across partitions, which the register check already sees).

## What the frustum cull lets through: the occlusion probe (measured 2026-11-05)

The engine culls against the frustum only (0x0070DFB0 above). How much of
what it then draws lies wholly behind other geometry was measured with
`[Probe] Occlusion` (plugin/src/OcclusionProbe.cpp, the measure's reasoning
in OcclusionProbeLogic.h): one eye pass in 31 is sampled (the finish
0x007AE070 called from 0x007C78D0), and every geometry drawn in it through
Do_RenderShape 0x007693E0, DrawShapeDirect 0x007694F0 or DrawTriStripsDirect
0x00769700 gets four occlusion queries - the screen rectangle of its world
bound (geometry+0x20) at the bound's nearest depth right before the draw
(A), the draw itself (B), the rectangle again on the pass's finished depth
buffer (C) and the same rectangle at depth 0 as a control (D).

Run `artifacts/shadow/obng-occlusion/20261105-091608` (Linux, DXVK, OBVR's
dual pass at 2568x2568 an eye, the save OBVRHarnessMOO with its mods, the
head turned through eight directions for 20 s a place; the places confirmed
by the cell ids and eye positions in OBVR.log). Per sampled eye pass:

| place | draws | bound hidden at the pass's end (of the testable) | of the draw time | draw time | hidden | without a pixel |
|---|---:|---:|---:|---:|---:|---:|
| Market District (coc ICMarketDistrict01) | 589 | 50.4 % | 33.8 % | 1.67 ms | 0.57 ms | 1.09 ms |
| Talos Plaza (coc ICTalosPlazaDistrict01) | 328 | 53.8 % | 33.3 % | 0.79 ms | 0.26 ms | 0.47 ms |
| dungeon (coc ImperialDungeon01) | 510 | 59.5 % | 39.2 % | 1.72 ms | 0.67 ms | 1.33 ms |
| house (coc WeynonPrioryHouse) | 540 | 54.3 % | 38.7 % | 1.52 ms | 0.59 ms | 1.24 ms |
| Gold Road (cow Tamriel -27 -3) | 822 | 58.4 % | 39.4 % | 1.05 ms | 0.41 ms | 0.80 ms |

- **The gates held (measured):** 0 contradictions (a bound hidden at the
  draw whose geometry drew pixels) in 83 905 bounds hidden at the draw, and
  0 failed controls. So the rectangle encloses the geometry as drawn: world
  positions are relative to the origin global 0x00B3F92C, the view at
  renderer+0x980 has a zero translation row in the dual pass (logged: "view
  row 4 -0.000 -0.000 -0.000"), the projection at +0x9C0 is the one the
  draws use. This settles re/functions/0x00762640.md C4 for the eye pass:
  camera-relative rendering, measured.
- **What the number is:** an upper bound for a culler that tests bounding
  spheres - it uses the same frame's finished depth at full resolution. A
  culler working from an earlier frame's coarse depth with margins reaches
  less (how much less: unverified). 17 to 27 % of the draws have a bound
  that reaches the near plane (large objects around the camera) and cannot
  be tested by a bound at all.
- **Between the two columns:** 50 to 73 % of the draws put no pixel down at
  the draw (query B), more than the bounds show hidden. The difference is
  draws whose sphere is visible or untestable while the geometry is not:
  flat or long geometry in a large sphere, objects around the camera. What
  share a tighter bound would recover is unverified.
- **The time columns are from disturbed passes** (a state block applied
  before every testable draw makes the device re-emit its state): the shares
  are the finding, the milliseconds an indication. They cover the three draw
  entries only, not the cull walk or the accumulator's lists for the same
  geometries.
- **Skinned draws** are 24 to 49 % of the draw time and mostly put no pixel
  down (53 to 96 %): 0.21 to 0.71 ms a pass. About 10 to 32 of them a pass
  have a bound around the camera or none (the player's own body, by their
  count and place - unverified by name); on the Gold Road with the head
  level those are nearly all of the 0.25 ms. **For OBVR:** the body's parts
  are drawn while none of them is in view.
- Repeat (run 20261105-085711, the same places without the control query):
  dungeon 60.6 %, house 65.4 %, Gold Road 59.9 % of the testable; the save's
  start cell 000231AD (an interior) 30.8 %. `coc ICMarketDistrict` (the
  worldspace's id, not a cell's) leaves the player where he is.

`tools/occlusion-summary.py <run>` sums the probe's lines per place. The
probe shares entries with PassSteps, DrawEntries, ConstantUploads and the
stereo route and is off beside them; its queries are never released, so it
is a measurement and never on in play.

### Where the draws without a pixel come from (measured 2026-11-05, run 20261105-095111)

The probe classifies every sampled draw by the shader pass id it ran under
([0x00B42E90]), the bound's radius, the geometry's name (+0x08) and its
owner - the player's third-person node ([[0x00B333C4]+0x3C]) or first-person
node (+0x5D0) among its ancestors (+0x1C) - and a draw without a pixel by
why its bound did or did not show it. Third run, the same five places, the
gates clean again (0 contradictions, 0 failed controls). Per sampled eye
pass, the time in the three draw entries:

| place | draw time | without a pixel | bound hidden at the draw | bound seen | bound at the near plane or none | of that the player's body |
|---|---:|---:|---:|---:|---:|---:|
| Market District | 1.86 ms | 0.92 ms | 0.28 ms | 0.30 ms | 0.34 ms | 0.15 ms |
| Talos Plaza | 0.79 ms | 0.42 ms | 0.12 ms | 0.07 ms | 0.23 ms | 0.16 ms |
| dungeon | 1.74 ms | 1.35 ms | 0.60 ms | 0.13 ms | 0.62 ms | 0.54 ms |
| house | 1.45 ms | 1.17 ms | 0.49 ms | 0.11 ms | 0.57 ms | 0.51 ms |
| Gold Road | 0.95 ms | 0.69 ms | 0.30 ms | 0.15 ms | 0.24 ms | 0.18 ms |

- **The player's own body** (owner: the third-person node; no draw was owned
  by the first-person node): 13 draws a pass outdoors and in the cities, 54
  in the two interiors (more passes a geometry under more lights), 95 % and
  more of them without a pixel while the head is level - 0.15 to 0.56 ms an
  eye pass. Most of it is the head: `FaceGenFace` (skinned) alone 0.06 to
  0.37 ms a pass at 45 to 78 us a draw, then `FaceGenEyeLeft/Right`,
  `FaceGenEars`, `FaceGenMouth`, `FaceGenTeethUpper/Lower`, `FaceGenTongue`,
  `FaceGenHair`; the listed FaceGen parts sum to 0.08 to 0.45 ms a pass
  (a lower bound: the probe lists its 16 costliest names a summary). A head
  the first-person view can never show is drawn in every eye pass. Its cost
  per draw fits the volatile pack of the packing probe (36 us a pack,
  docs/subsystems/stream-packing.md) - derived, not shown to be the same
  draws. **For OBVR:** leaving the player's head geometries out of the eye
  passes (not out of the shadow pass) is worth up to about 0.45 ms an eye
  pass indoors; how OBVR hides the head today is not read here.
- **Bound hidden at the draw** (what a sphere test against the depth drawn
  so far finds; the end-of-pass figure above adds the draws covered later):
  0.12 to 0.60 ms a pass.
- **Bound seen** (the sphere is larger than what the geometry covers; a
  tighter bound would be needed): 0.07 to 0.30 ms a pass, largest in the
  Market District.
- NPC heads are the costliest hidden draws of the others (`FaceGenFace`
  0.12 to 0.16 ms a pass in the interiors); distant-tree `Billboard`s on the
  Gold Road are 83 draws a pass, 78 without a pixel, 0.04 ms.
- The Market District's hidden share moved between the runs (50.4 % of the
  testable in the second, 37.6 % in the third; people walk there); the other
  places repeat within three points.
- The times are from disturbed passes (see above); draws with an untestable
  bound, the body's among them, have no state block applied right before
  them and are the least disturbed.

### OBVR's head cull, counter-measured (2026-11-05)

OBVR 6d055dd (DLL e3bf3e61) sets the app-cull bit on the player's two
FaceGen nodes for each world render (`[Body] CullHiddenHead`, default 1).
The same DLL with the key 0 and 1, the probe's scenario in both (runs
`obng-occlusion-headcull-off/20261105-104658` and
`obng-occlusion-headcull/20261105-103457`), per sampled eye pass:

| place | the player's draws, off / on | their time, off / on | all draw entries, off / on |
|---|---:|---:|---:|
| Market District | 16.4 / 5.9 | 0.168 / 0.017 ms | 1.43 / 1.44 ms |
| Talos Plaza | 17.2 / 6.1 | 0.192 / 0.021 ms | 0.87 / 0.65 ms |
| dungeon | 53.6 / 25.0 | 0.538 / 0.073 ms | 1.45 / 1.01 ms |
| house | 56.0 / 20.3 | 0.517 / 0.059 ms | 1.36 / 0.96 ms |
| Gold Road | 13.3 / 6.0 | 0.178 / 0.018 ms | 0.96 / 0.86 ms |

With the key on no FaceGen geometry of the player is drawn in an eye pass
(the name table lists none); 0.15 to 0.47 ms an eye pass are gone, in the
two interiors about 30 % of the time in the draw entries. What is left of
the body (6 draws a pass outdoors, 20 to 25 indoors, most still without a
pixel while the head is level) is 0.02 to 0.07 ms. The gates were clean in
both runs. Not equal between the two runs: the off run had the NPCs' AI off
(`tai`, added after a passer-by's greeting opened the Dialog menu and ended
run 20261105-103908 in the Market District); the player's own draws do not
depend on it (derived). The times are from probe passes, an indication.

### View-dependent constants are not uploaded for every draw (2026-11-06)

A pass may draw with a matrix an earlier setup left on the device.
**Measured** (OBNG's eye constants trace, run
`obng-grass-turn-single/20261106-084429`, single pass, Linux): a terrain
block ("Block (0, 0)", ShadowLightShader, through the renderer's direct
entry) is drawn several times in a frame; at its first draw the shader's
vertex constant map (`shader+0x30`, NiD3DSCM_Vertex) holds the live entries
`c19 c20 c21 c0x4 (defined id 23, ModelViewProj) c13 c28x4 c33 c32`, at the
later draws `c19 c20 c21` only - the later texture passes draw with the
c0..c3 of the first. **Unread**: where the map's entries are switched per
pass. Whoever draws a geometry a second time with another view (a second
eye inside one pass) cannot rely on the current map alone; OBNG's eye
switch remembers the pair of values per register
(`plugin/src/StereoEyeSwitchLogic.h`, RegisterPairs).

The shader classes by vtable, **read** from each vtable's Complete Object
Locator in the EXE: 0x00A896EC NiD3DDefaultShader, 0x00A90634
ShadowLightShader, 0x00A9170C WaterShader, 0x00A91E4C ParticleShader,
0x00A9202C TallGrassShader (slot 13 0x007E6710, not a function in the
Ghidra export), 0x00A92844 SpeedTreeLeafShader, 0x00A930C4
Lighting30Shader, 0x00A93F2C GeometryDecalShader, 0x00A9405C
ParallaxShader, 0x00A941AC SkinShader, 0x00A9459C SpeedTreeBranchShader,
0x00A946B4 DistantLODShader.

### The grass fades by the length of the clip-space position (2026-11-06)

**Read** (shader package 019, all 24 `GRASS20xx.vso`, disassembled with
`vkd3d-compiler -x d3dbc -b d3d-asm`): the blade's alpha fade is
`dp4 r2.w, r0, r0` on r0 = ModelViewProj (c9..c12) x position, the square
root of it against AlphaParam c5 (`(len - c5.x) / c5.y`, `(len - c5.z) /
c5.w`, the product of the first and one minus the second to oT5.w). The fog
of the same shaders is the three-component length of the same vector
against FogParam c15. **Derived**: the fade depends on the projection and
on the blade's place in the picture - straight ahead the length is 1.41
times the distance, at the horizontal edge sqrt(3 / (1 + tanW^2)) times
(1.22 at the game's 75 degrees, 1.01 at a headset frustum of tangent 1.4),
so in a wide frustum grass reaches about 40 % further at the edge of the
view than in its middle and clumps in between come and go as the view
turns. **Measured** (OBNG's grass probe: the device's c5, c9..c12 and the blade
positions c20..c42 read back at every grass draw and the shader's fade
computed from them; runs `obng-sewer-exit-single/20261106-111726` and
`obng-sewer-exit-dual/20261106-111906`, frustum tangent 1.152,
fGrassEndDistance 8000 / start 7000): between 5000 and 6000 units from the
camera the mean fade is 0.53 to 0.57 in the middle third of the picture and
0.99 to 1.00 in the third beside it; 1.00 everywhere nearer, 0.00
everywhere beyond 6000 - the single pass and the dual pass alike.
**Unverified**: c5's four values themselves were not printed. The tree leaves' vertex shaders
(`STLEAF000..003.vso`) take the same four-component length of a vector;
what it feeds there is unread.
