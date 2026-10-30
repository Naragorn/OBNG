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
