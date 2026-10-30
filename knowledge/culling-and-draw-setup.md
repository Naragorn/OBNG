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
2. **0x0070C0B0** (not mapped yet): the hop that runs the culling walk
   (NiCullingProcess::Cull, above) and hands the visible geometries to the
   renderer - the next slice's first function.
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
   (0x8000) or forced repacks everything - which of the two the skinned
   bodies carry per frame is the open question behind OBVR's StreamReuse
   result.

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
  (`+0x14C == 0`). The unrestored mask under 0x007C61C7 is therefore
  **not shown to be live**; a counter of `[light+0x14C] != 0` at
  0x007C61C2 would settle it. Derived: with eighteen bodies the shadow
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
