# The per-geometry light detach: ShadowSceneLight's geometry list and its linear remove

Status: knowledge, written 2026-10-27 from Oblivion.exe 1.2.0.416 (REA/Ghidra 12.1.4,
sha256 a8f31384...2b7dc6), byte scans of the same file, xOBSE headers (deepwiki
llde/xOBSE) and OBVR's crowd capture (`../OBVR` docs/performance-spec.md section 7,
`artifacts/hand-script/perf-walkers-crowd/20261027-074800`). Continues
`shadow-light-geometry-lookup.md` (the state before this analysis; left untouched).
Nothing of this is built.

Marks: **read** = read in the binary (addresses are 1.2.0.416), **measured** = measured
in the game, **derived** = follows from the two, **unverified** = not checked. Names in
`CamelCase` after an address are my labels for what the code does, not symbols.

## Verdict

- **What it is.** `FUN_007D5F80` is `NiTPointerList<NiPointer<T>>::Remove(key)`; the hot
  caller `FUN_007D6940` is `ShadowSceneLight::RemoveGeometry(P)`: it removes the drawn
  geometry `P` (an `NiTriBasedGeom`) from the light's list of lit geometry at
  `ShadowSceneLight+0xE4` (`NiTPointerList<NiPointer<NiTriBasedGeom>>`, read from the
  constructor) by walking that list from the head. It runs once per lit geometry per
  light that lit it, on every frame in which the geometry's world bound changed
  (skinned actors: every frame), because `ShadowSceneNode::RelightGeometry`
  (`FUN_007C6100`) first detaches `P` from all its current lights and then re-adds it
  to every scene light in range (`FUN_007D5790`, an O(1) AddHead). The remove is the
  only super-linear step; everything else in the relight is O(number of lights).
- **How costly a fix is.** *Small.* Option (a), a per-light hash index
  `(light, geometry) -> list node` consulted by a detour of `FUN_007D6940`, needs two
  code hooks plus a purge on the light's destructor, reuses the engine's own
  `RemovePos` (`FUN_007C17D0`) for the unlink, and leaves every engine decision in
  place; the index is a hint validated before use, with the original walk as fallback.
  Verification is the existing crowd harness (bin 0x007D5F00 should fall from 18.4 %
  to about 0 at 100 NPCs) plus a debug self-check that compares index hits with the
  walk. Risk sits in list nodes freed by paths the detour does not see (the destructor
  path is known; any unknown path is caught by the `node->data == P` check and the
  fallback).
- **Recommendation.** Build option (a). Option (b) as asked ("skip the remove when the
  engine clears the list anyway") does not exist for these lights: the geometry list
  of a non-shadow light is persistent and maintained incrementally; only shadow lights
  get a per-frame rebuild, and those are excluded from the remove by the `+0xF4` gate.
  Option (c), keeping the membership when the light set did not change, saves nothing
  measurable beyond (a) and must replicate engine gates, so it carries more risk.

## Measured (unchanged from the predecessor file)

- 25 / 50 / 100 OblivionWalkers in one heap, AI off, Linux, Proton, DXVK, 75 Hz, OBVR
  dual pass, headless: the render-thread sampler's 256-byte bin 0x007D5F00 holds
  5.5 % / 11.3 % / 18.4 % of the in-scene-pass samples, about 0.16 / 0.58 / 1.97 ms a
  pass; 2532 samples on 0x007D5FC3 (`CMP EDI,[EAX+8]`, the key compare of the walk).
- Not measured: whether OBVR's second eye pass pays it again (see "Unverified").

## Read: the objects

**ShadowSceneLight** (RTTI `.?AVShadowSceneLight@@`, type descriptor 0xB2D3C4, COL
0xAD0060, vtable 0xA912B4; xOBSE: "derives from NiRefObject", no members declared).
Constructor `FUN_007D6060` (writes `[this]=0xA912B4` at 0x7D60A3), destructor
`FUN_007D54B0` (writes the same vtable at 0x7D54DD, then releases members). Members
seen in code:

| offset | read as |
|---|---|
| +0xD4, +0xD8 | floats, fade factors written by `FUN_007D6390` (+0xD8 compared with 0.05 and 0 in 0x004073D0) |
| +0xDC, +0xE0 | floats (fade timer; +0xE0 accumulates a per-frame delta) |
| **+0xE4** | `NiTPointerList<NiPointer<NiTriBasedGeom>>` (ctor: `mov dword [esi+0xE4], 0xA91294` at 0x7D60BD; 0xA91294 is the vtable whose COL 0xACFFC0 names TD 0xB2D348 = `.?AV?$NiTPointerList@V?$NiPointer@VNiTriBasedGeom@@@@@@`). Head +0xE8, tail +0xEC, count +0xF0 (xOBSE NiTListBase: start +4, end +8, numItems +0xC). The lit geometry of this light. |
| +0xF4 | byte; when non-zero, `FUN_007C5750` skips the remove for this light, `FUN_007D5790` skips the bound expansion `FUN_007ECEC0`, `FUN_007D6390` takes the "planes" branch. Consistent with "shadow-casting light" (unverified name) |
| +0xF5 | byte, checked in `FUN_007D6390` with DAT_00b42f40 bit 0x10 |
| +0xFC, +0x104 | bytes gating `FUN_007C6020` (rescan on light movement) |
| +0x100 | `NiPointer<NiLight>` source light (`FUN_00405AD0` copies it; `FUN_007D6390` reads its world translate +0x88 and radius +0xF8) |
| +0x114 | pointer, removed via `FUN_007C1A30` in the destructor and in `FUN_007D6390` when the fade reaches 0 |
| +0x118 | 16-bit, 0xFF means "culled/dark" |
| +0x130 | pointer compared with the caller's `NiLight*` in `FUN_007C76D0`; its +0x18 flags tested in 0x004073D0 (an NiAVObject) |
| +0x134 | `NiTPointerList<NiPointer<NiAVObject>>` (ctor: `mov dword [esi+0x134], 0xA912A4` at 0x7D60F7; COL 0xAD0010, TD 0xB2D388). Not on the hot path |
| **+0x144** | list-node cursor into the +0xE4 list for the rescan flow (writers: ctor 0x7D62CC = 0, dtor 0x7D5603 = 0, `FUN_007D5ED0` 0x7D5F5C = head, `FUN_007D6A40` 0x7D6A67 = 0, `FUN_007D5320` 0x7D5381 = 0, `FUN_007D4570` 0x7D46A3) |
| +0x148 | `NiPointer` to a sentinel object named "fence" created by `FUN_007D5ED0` on first use (`FUN_00401F00(0xC0)` + `FUN_00717570`, `FUN_006FF420("fence")`) |
| +0x14C | pointer; when non-zero `FUN_007D2EA0` tests the geometry against six planes at +0x150.. (16 bytes each) with a sticky bitmask at +0x1B0 |

The three list vtables 0xA9048C (`NiTPointerList<NiPointer<ShadowSceneLight>>`,
COL 0xACFD30, TD 0xB2D128; constructed at 0x7C7396/0x7C73AC inside the
ShadowSceneNode constructor region), 0xA91294 and 0xA912A4 share slot +4 `NewNode` =
0x004F0F60 and slot +8 `DeleteNode` = 0x007C3190 (read from the vtables). Derived:
that is why one `Remove` body (`FUN_007D5F80`) and one `AddHead` body (`FUN_00749800`)
serve all three instantiations (identical-code folding); a detour of `FUN_007D5F80`
therefore cannot be made light-specific, a detour of `FUN_007D6940` can.

**ShadowSceneNode** (xOBSE NiObjects.h, size 0x130): `lights` at +0xE4 and `lights2`
at +0xF4 (`NiTPointerList<ShadowSceneLight*>` in xOBSE; the RTTI names and the
ref-counting in `FUN_007C76D0`/`FUN_007C77C0`/`FUN_007C78D0` show they are the
`NiPointer` variant), `UInt8 unk11C` = the node's index (compared with the property's
top nibble, see below). `FUN_007B4280(index)` = `DAT_00b42f54[index]` returns the
ShadowSceneNode for an index.

**BSShaderLightingProperty** (xOBSE declares the hierarchy NiShadeProperty >
BSShaderProperty > BSShaderLightingProperty > BSShaderPPLightingProperty, no members).
Members seen: +0x1C flags (bit 0x1000 tested in `FUN_007D5790`; top nibble = index of
the ShadowSceneNode the geometry was last lit under; `FUN_007C5750` rewrites it), +0x1F
byte, +0x24 dword (0 after any light-set change, -1 when the node index changes; read
"light constants dirty", unverified), **+0x70** `lights` list (head +0x74; 16-bit count
+0x78 via `FUN_007ED5C0`; iteration cursor +0x7C via `FUN_007ED160`/`FUN_007ED180`),
kept sorted by `FUN_007ECC60`: key = `(|lightPos - boundCenter| - boundRadius) /
lightRadius(NiLight+0xF8)`, smaller first.

**NiGeometry** (xOBSE NiObjects.h, verbatim): `NiPropertyState* unk0AC; // 0AC`,
`NiDynamicEffectState* unk0B0; // 0B0`, `NiGeometryData* geomData; // 0B4`,
`NiObject* skinData; // 0B8`, `NiObject* shader; // 0BC`, size 0xC0.
`*(P+0xAC)+0x18` in `FUN_007D5790` = `NiPropertyState` slot 4 (8 + 4*4), i.e. the same
type-4 property `GetProperty(4)` returns (derived).

## Read: property type 4 and vtable +0x54 (question 6)

`FUN_00707530` = `NiAVObject::GetProperty(type)`: returns null for type > 9, else walks
the property list from +0x9C and returns the first whose vtable slot +0x4C (`GetType`)
equals `type`. Slot +0x4C bodies, decoded from the vtables found via RTTI
(TD -> COL -> vtable), all `mov eax, N; ret`:

| GetType | classes (vtable) |
|---|---|
| 0 | NiAlphaProperty (0xA3D604) |
| 1 | NiFogProperty (0xA81984), BSFogProperty (0xA90584) |
| 2 | NiMaterialProperty (0xA7E1F4) |
| 3 | NiRendererSpecificProperty (0xA81784) |
| **4** | NiShadeProperty (0xA81584), BSShaderProperty (0xA91D14), BSShaderLightingProperty (0xA92524), BSShaderPPLightingProperty (0xA91384), SpeedTree*/Sky/Water/Particle/TallGrass/DistantLOD/Hair/Lighting30/GeometryDecal/Precipitation/Bolt ShaderProperty - all share `FUN_0073DAA0` = `mov eax,4; ret` |
| 5 | NiStencilProperty |
| 6 | NiTexturingProperty |
| 7 | NiVertexColorProperty |
| 8 | NiWireframeProperty |
| 9 | NiZBufferProperty |
| 11 | NiDitherProperty |
| 12 | NiSpecularProperty |

So type 4 is the shade/shader property; the matching enum symbol name in Bethesda's
Gamebryo headers is unverified (NetImmerse 4.x as documented by MWSE numbers shade = 10,
so the Morrowind-era table does not apply; CommonLib's Skyrim table does not either).

Slot +0x54 is a per-class constant, a **shader-type id** (my name `GetShaderType`):
NiShadeProperty -1 (`or eax,-1`), BSShaderProperty 0, BSShaderLightingProperty 1,
SpeedTreeShaderLightingProperty 1, DistantLODShaderProperty 2,
GeometryDecalShaderProperty 3, TallGrassShaderProperty 4, BSShaderPPLightingProperty 5,
SpeedTreeShaderPPLightingProperty 5, HairShaderProperty 6,
SpeedTreeBranchShaderProperty 7, SpeedTreeLeafShaderProperty 9,
Lighting30ShaderProperty 10, SkyShaderProperty 11, WaterShaderProperty 12,
BoltShaderProperty 13, ParticleShaderProperty 14, PrecipitationShaderProperty 15. The
"1..10" gate in `FUN_007C5750`/`FUN_007D6940`/`FUN_007D5790` therefore means "a lit
shader"; the `== 4` special case in `FUN_007D5790` is tall grass; the
`0xB/0xC/0xD/0xE` cases in `FUN_007AD270` are sky, water, bolt, particles. The
BSShaderAccumulator vtable (0xA8CC5C) has `FUN_007AD270` at slot +0x58.

## Read: the functions

- `FUN_007D5F80` **Remove(key)**, thiscall on the list, `ret 8`, args `(&outNiPointer,
  &key)`: walks from head comparing `node+8 == key` (0x7D5FC0..0x7D5FCE), on a match
  calls `FUN_007C17D0` (RemovePos) and returns the element in the out pointer with one
  extra reference; on a miss returns null. Callers: `FUN_007C76D0`, `FUN_007C77C0`,
  `FUN_007D6940`.
- `FUN_007C17D0` **RemovePos**(`&out`, `&nodePtr`), thiscall on the list: head case
  `FUN_007C1740` (RemoveHead), tail case `FUN_007D1FD0` (RemoveTail), otherwise relinks
  prev/next, takes a reference on the data, frees the node via vtable +8, decrements
  count (+0xC), stores the data into `*out` with a reference. Callers: `FUN_007C1A30`,
  `FUN_007C71B0`, `FUN_007D5F80`.
- `FUN_00749800` **AddHead**(`&data`), thiscall on the list: `node = NewNode()
  (vtable+4)`; `node.data = data` (ref +1); `node.prev = 0; node.next = head; if head:
  head.prev = node; head = node (and tail if empty); count++`. (The predecessor file's
  "AddTail" reading was wrong: `*piVar3 = in_ECX[1]` stores the old head as `next`.)
- `FUN_007C58F0` **MoveBefore**(`node`, `before`), thiscall on the list: unlinks `node`
  (fixing head/tail) and relinks it immediately before `before`.
- `FUN_007D6940` **ShadowSceneLight::RemoveGeometry(P)**, `ret 4`: `prop =
  P->GetProperty(4)`; if `prop` and `1 <= GetShaderType <= 10`: `FUN_007EE720(prop,
  this)` (property drops this light from +0x70 via `FUN_00776690` find + `FUN_007AA860`
  remove, then `prop+0x24 = 0`); then `AddRef(P); FUN_007D5F80(&this->+0xE4, &out,
  &P)`; release `out` and the extra reference. Callers: `FUN_007C5750` (hot),
  `FUN_007C5D90` (subtree detach), `FUN_007D6A40` (rescan end).
- `FUN_007D5790` **ShadowSceneLight::AddGeometry(P)**, `ret 4`: gates: `FUN_007B8150(P)
  != 0` (P+0xBC shader object passes an RTTI check against DAT_00b42884 and a map lookup
  in DAT_00b4257c) and `P+0xAC != 0`; under the critical section DAT_00b3fa00 reads `prop
  = (*(P+0xAC))+0x18`; gate `1..10`; then **scans the +0xE4 list from the cursor
  `this+0x144`** for `P` (0x7D58C2..0x7D58E2); found: `MoveBefore(node, cursor)` and,
  if `+0x104 && !+0xF4`, `FUN_007ECEC0(&P->worldBound)`; not found: if `(prop+0x1C &
  0x1000) == 0 || GetShaderType == 4`: `FUN_007EE330(prop, this, &P->worldBound)`
  (sorted insert into +0x70, or `FUN_005B1E20` AddTail when it sorts last; `prop+0x24 =
  0`) and `AddHead(&this->+0xE4, P)` at 0x7D5935. When the cursor is 0 the scan is
  skipped and the add is O(1). Callers: `FUN_007C6100`, `FUN_007D59E0`.
- `FUN_007D2EA0` **ShadowSceneLight::IsInRange(P, srcLight)**: true if `srcLight`'s
  RTTI is DAT_00b3fcfc (a light type that lights everything, unverified which); else if
  `this+0x14C` (planes) tests `P`'s bound against six planes with the sticky mask
  +0x1B0; else `sqrt(|srcPos - P.boundCenter|^2) - P.boundRadius(+0x2C) <
  srcLight+0xF8`.
- `FUN_007C5750` **ShadowSceneNode::DetachGeometryFromItsLights(P)**, `ret 4`: `prop =
  GetProperty(4)`, gate 1..10; if the top nibble of `prop+0x1C` differs from
  `this+0x11C`: `prop+0x1F = 0`, write the nibble, `prop+0x24 = -1`; `alive =
  prop.lights.count < P.refcount` (derived: every light holds one NiPointer to P, so
  equality means nothing but lights hold P); then for every light in `prop.lights`
  with `light+0xF4 == 0`: `light->RemoveGeometry(P)`. Returns `alive`. Only caller:
  `FUN_007C6100`.
- `FUN_007C6100` **ShadowSceneNode::RelightGeometry(P, nodeOverride)**, `ret 8`
  (asm: `this` in ECX, `[esp+0x24]` = P, `[esp+0x28]` = override): if no override, the
  node comes from `FUN_007B4280(prop+0x1C >> 28)` when `GetShaderType != -1`; `if
  (!DetachGeometryFromItsLights(P)) return 0`; for every light in `node.lights`
  (+0xE8): `if (light->IsInRange(P, light+0x100)) light->AddGeometry(P)`; return 1.
- `FUN_007C7050` **ShadowSceneNode::RelightSubtree(obj, nodeOverride)**: recursive over
  `NiNode::m_children` (+0xB0 data, +0xB6 count; skips children with flag 1 =
  APP_CULLED), geometry (vtable +0x10 non-null) goes to `RelightGeometry`; an
  NiSwitchNode-like node (RTTI DAT_00b3fd70, active index +0xE0) descends only into
  the active child. Callers: 0x0040C830 (main render, whole scene root
  `FUN_00660110(1)`, only when DAT_00b42cdb == 0), 0x00441EF0, 0x004EBDA0 (grass),
  0x0057D5B0 (a UI/preview render: `FUN_007B4280(1)` = node index 1), 0x00655FE0
  (light-level query, when the player's property list is empty), and
  **`FUN_007AD270`**.
- `FUN_007AD270` = **BSShaderAccumulator vtable +0x58** (per-geometry registration;
  `this` is the accumulator with the +0x21E0..+0x21E3 bytes also used by
  `FUN_007C78D0`): for a geometry `P` with `prop = (*(P+0xAC))+0x18`, when
  `GetShaderType >= 1` and not one of the 0xB/0xC/0xD/0xE special shaders: `if
  ((P->flags(+0x18) & 0x80) == 0 && DAT_00b42eac != 5) { node =
  FUN_007B4280(prop+0x1C >> 28); if (!RelightSubtree(P, node)) return; P->flags |= 0x80;
  }` and goes on to build render passes. **This is the per-frame path.**
- `FUN_00722AA0` (called from `FUN_00749550`, called from 0x0074EAA0; a NiGeometry
  method using +0xB4 geomData / +0xB8 skinData / +0x64 world rotate): recomputes the
  world bound (+0x20..+0x2C) and then, if flag 0x80 is set, keeps it only when
  `FUN_0072A0A0(&oldBound)` says the four floats are **exactly equal**, else clears it
  (`and word [ecx+0x18], 0xFF7F` at 0x722B21). Other clear sites of that bit (byte scan
  of .text for `and dword [r+0x18], 0xFFFFFF7F`): 0x4445B5 in `FUN_00444340`, 0x4639A9
  in `FUN_004637C0`, 0x4CDA79 in `FUN_004CD960` (game code, purpose unverified). The
  bit is set only by `FUN_007AD270` and `FUN_00722AA0` on the code paths read here.
- `FUN_007C78D0` **ShadowSceneNode per-frame render**: for every light in `lights`:
  drop it (`FUN_007C77C0`) when its source NiLight is gone or has refcount 1; else if
  `+0x104`: `FUN_007C6020(light)`; if `+0xF4 == 0 || !FUN_00405A80()`:
  `FUN_007D6390(light, camera)` (fade/cull update; when the light goes dark it walks
  its +0xE4 list and sets each geometry's `prop+0x24 = 0`). Then `FUN_007C71B0`
  (insertion sort of `lights` by `light+0xD0`, using `RemovePos` + `FUN_007C6A20`
  insert), then the accumulator: `vtable+0x4C(node)`, `FUN_0070AB40` (cull, which
  calls `FUN_007AD270` per visible geometry), `vtable+0x50`.
- `FUN_007C6020(light)` (only caller `FUN_007C78D0`): when `+0xFC && +0x104 && !+0xF4
  && FUN_008AA390(&src->worldTranslate) && +0x118 != 0xFF`: `FUN_007C5720(pos)` then
  `FUN_007C5F60(node, light)`.
- `FUN_007C5F60(light)` **rescan after the light moved** (callers 0x7C6020, 0x7C6AE0,
  0x7C6FF0, 0x7C7EB0): if the source light is APP_CULLED: `FUN_007D21F0(light)`
  (property-side detach from every geometry in +0xE4; leaves the list itself intact);
  else `FUN_007D5ED0(light)` (create "fence" once, `AddHead(fence)`, cursor = head),
  for every child of the node `FUN_007D6900` -> `FUN_007D59E0(light, subtree)`
  (recursive: in-range geometry -> `AddGeometry`, which here moves found nodes before
  the cursor or adds new ones at the head), then `FUN_007D6A40(light)` (walks from the
  cursor to the tail calling `RemoveGeometry` on each, cursor = 0). The same
  `FUN_007D5ED0` / `FUN_007D6900` / `FUN_007D6A40` triple runs per frame for the
  shadow lights in 0x004073D0 (called from 0x0040C830 before the main cull; OBVR's
  ShareActorShadows already skips 0x004073D0 for the second eye).
- Node-side removers (question 4): `FUN_007C77C0(light)` = `lights.Remove(light)`
  (`LEA ECX,[ESI+0xE4]` at 0x7C7805), used by `FUN_007C78D0` for dead source lights;
  `FUN_007C76D0(NiLight*)` = find the ShadowSceneLight in `lights2` whose +0x130 equals
  the argument and `lights2.Remove` it (`LEA ECX,[ESI+0xF4]` at 0x7C774A);
  `FUN_007C78D0 -> FUN_007C71B0` is the per-frame **sort** of `lights` by +0xD0, which
  removes and re-inserts positions (`RemovePos` + `FUN_007C6A20`). None of them is a
  clear-all.
- `FUN_007C5D90(node)` (callers: itself, `FUN_007C5E70`): recursive; for each geometry
  with a lit shader, for each light in `node.lights`: `light->RemoveGeometry(P)`. A
  subtree detach (when the geometry leaves the scene; unverified which event).
- `FUN_007D54B0` **~ShadowSceneLight**: `FUN_007D21F0`, releases +0xF8/+0x100/+0x114
  (after `FUN_007C1A30`), +0x11C.., +0x14C, +0x148 (fence), +0x130; `FUN_007D53F0` and
  `FUN_007D5390` (member destructors; derived: these free the remaining list nodes
  without passing `FUN_007D6940`), then the NiRefObject vtable.

## Derived: what the engine achieves (questions 1-5)

1. **P is the geometry being drawn** (an NiTriBasedGeom: the +0xE4 list element type),
   never a light. `*(P+4)` in `FUN_007C5750` is indeed P's refcount; it is compared with
   the property's light count as a liveness test (a geometry held only by its lights is
   dead and is not re-lit or registered).
2. **The items of the property's +0x70 list are ShadowSceneLights** (bytes +0xF4,
   +0x104, lists at +0xE4/+0x134, NiPointer at +0x100 all belong to the class whose
   constructor is `FUN_007D6060`); the +0xE4 list searched belongs to the
   **ShadowSceneLight**, element type `NiPointer<NiTriBasedGeom>`. xOBSE's
   `ShadowSceneNode::lights` at +0xE4 is a different object at the same offset.
3. **Insert sites**: `FUN_007D5790` (`AddHead` at 0x7D5935) from `FUN_007C6100`
   (per relight) and from `FUN_007D59E0` (rescan); the sentinel `AddHead` in
   `FUN_007D5ED0`. Timing for ordinary lights: each time a geometry is registered with
   flag 0x80 clear, i.e. **every frame for every geometry whose world bound changed**
   (skinned actors, moving objects, anything animated) and once after load/attach for
   static geometry. For shadow lights additionally every frame via 0x004073D0.
4. **Other removers**: see above; none clears the +0xE4 list per frame for ordinary
   lights. The list is persistent and incremental; removal is the mechanism that keeps
   it exact when a geometry moves out of a light's range or is detached.
5. **Purpose**: `RelightGeometry` recomputes, per geometry, the set of lights that
   affect it and keeps two mirrored structures consistent: the property's sorted
   `lights` (+0x70; what the shader reads) and each light's `geometry` list (+0xE4;
   what the light uses to invalidate `prop+0x24` when it fades, to rescan when it
   moves, and - through the NiPointers - to keep the geometry alive). It does so as
   "detach from all, re-attach in range", so every visit pays one list walk per light
   that previously lit the geometry; in a crowd under a handful of lights every actor
   shape is in every nearby light's list, so the walk is O(shapes) and the frame pays
   O(shapes x lights x shapes): the measured 5.5 -> 11.3 -> 18.4 %. Yes, the same list
   gets P re-added later in the same `RelightGeometry` call (0x7D5935), in O(1).

## Fix options (question 7)

| option | cost | why |
|---|---|---|
| (a) per-light index `(light, P) -> node`, detour of `FUN_007D6940` | **small** | two hooks (whole-function detour of `FUN_007D6940`; 5-byte call patch at 0x7D5935 to record the new head node after `AddHead`) plus a purge hook on `FUN_007D54B0`; the unlink reuses `FUN_007C17D0`; OBVR's detour infrastructure exists; test = existing crowd harness + a self-check mode |
| (b) skip the remove because the engine clears the list anyway | n/a | the premise is false for the lights that pay (see 4); for shadow lights the remove is already skipped by the `+0xF4` gate |
| (c) keep membership when the light set is unchanged (detour `FUN_007C6100`) | medium | must drive `FUN_007D2EA0` (which mutates +0x1B0), all `FUN_007D5790` gates and the `+0x24` bookkeeping in the engine's order; saves only the O(1) AddHead and a sorted insert of <= 10 beyond (a) |

**(a) in detail.** Detour `FUN_007D6940(this = light, P)`: perform the property part by
calling the engine as before (`GetProperty(4)`, gate, `FUN_007EE720`), then look up
`node = index[light][P]`; if found and `node->data == P` (and, for the head/tail
positions, `list.head == node` / `list.tail == node`), call `FUN_007C17D0(&light->+0xE4,
&out, &node)` and release `out` exactly as the original epilogue does (InterlockedDecrement
on `out+4`, `vtable[0](1)` at zero; then the same for the AddRef'd `P`); erase the entry.
If not found or the check fails, run the original walk (`FUN_007D5F80`) unchanged. At
0x7D5935 (`CALL FUN_00749800` in `FUN_007D5790`, ECX = `light+0xE4`): after the call,
record `index[light][P] = light->+0xE8` (the new head). On `FUN_007D54B0` entry: erase
`index[light]`. What exactly must be reproduced: nothing of RemovePos itself (it is
called); only the two reference releases that follow it in `FUN_007D6940` and the
early-out when the property gate fails (the engine still removes P from the list in that
case - the gate only skips `FUN_007EE720`). What can go wrong: (1) a node of `light+0xE4`
freed by a path not hooked (destructor member teardown is covered by the purge; any
other path makes the `node->data == P` check fail or, if a pooled node is reused for the
same `P` in another light's list, it would pass the data check but fail the head/tail
check only at the ends - to close this, store the list pointer too and verify
`node->prev == 0 ? list.head == node : node->prev->next == node` and the mirror for
`next`; a reused node in another list cannot satisfy both against this list unless it
is genuinely linked in it); (2) thread safety: all sites run on the thread that renders
(cull, `FUN_007C78D0`, 0x004073D0); the destructor may run on the game thread during
cell unload - the engine itself has no lock on these lists, so a light being destroyed
while its geometry is relit would already be an engine race; the purge should take
the same lock as the lookups (a cheap spinlock, or a frame-generation stamp instead of
erase). (3) The MoveBefore in the rescan flow keeps the node identity, so the index
stays valid; AddHead of the fence is recorded like any other (harmless).
Verification: `tools/hand-scripts/perf-walkers-crowd.txt` at 25/50/100 with
`perf-phases.py`/`perf-samples.py`; expect bin 0x007D5F00 below 1 % at 100 and the
scene pass about 1.9 ms shorter; a `Debug.LightIndexCheck` mode that, for N frames,
also runs the walk and logs mismatches (must be 0); headset: NPCs under a torch and
near a moving light (held torch) still light and unlight correctly, lights fading at
range still invalidate (no stale lighting after a light dies).

**Side finding (derived, not measured).** The rescan scan in `FUN_007D5790`
(0x7D58C2..0x7D58E2, from the cursor) is also linear in the light's list and runs per
in-range geometry per shadow light per frame in 0x004073D0 - a second quadratic site
for scenes with many casters near a shadow light. It lives outside the measured bin
(0x7D5790 is bin 0x7D5700/0x7D5800); `perf-samples.py` can confirm or rule it out.

## Unverified

- Whether OBVR's second eye pass repeats the relight (it does only if a world-bound
  update clears flag 0x80 between the passes). Cheap check: count `FUN_007C5750`
  entries per pass in the harness.
- The meaning of `+0xF4` ("shadow-casting light"), of `+0x104`/`+0xFC`, of
  `prop+0x24`, and the RTTI DAT_00b3fcfc / DAT_00b42884 class names.
- The enum symbol names behind GetType 4 and the shader-type ids; the values are read.
- Which of `FUN_007D5390`/`FUN_007D53F0` destroys the +0xE4 list; that one of them does
  is derived from the destructor freeing all members.
- The three game-side clear sites of flag 0x80 (0x444340, 0x4637C0, 0x4CD960) and the
  callers 0x7C6AE0/0x7C6FF0/0x7C7EB0 of the rescan were not read.

## For an engine

- A geometry's light membership must be a set keyed by the geometry (hash set or an
  intrusive back-pointer from the geometry to its node in each light), never found by
  walking the light's list. Here the engine keeps the back-direction (property ->
  lights) but not the forward one (light -> node of this geometry), so each relight
  pays the walk.
- "Detach from all, re-attach in range" per frame is the wrong granularity for
  animated geometry; the relight should be driven by range changes (light moved,
  geometry moved out of range), with membership kept otherwise.
- Bound-equality as the "unchanged" test (`FUN_0072A0A0`, exact float compare) makes
  every skinned mesh "changed" every frame; a tolerance or a per-light range test on
  the moved bound would spare the whole relight for idle actors.
