# Skinned part attach: loading a NIF and binding its skin to an actor's skeleton

Status: knowledge, written 2026-10-31 from OBVR (a wound cap - vanilla
`meshes\creatures\zombie\neckplug.nif` - on a cut body's neck). Read from
the decompiled EXE (`re/export/rea-all/functions/`) and raw bytes of the
Steam Oblivion.exe 1.2.416; nothing here was measured in the game yet.

Marks: **read** = seen in decompile or bytes; **derived** = concluded from
what was read; **unverified** = not confirmed.

## 1. Loading a NIF by path

- `0x00439EB0` ModelLoader load, **thiscall, 4 args, RET 0x10** (read:
  epilogue `C2 10 00`). ECX = `*(void**)0x00B33A1C` (read: `mov ecx,[0xB33A1C]`
  at 0x0057147C). Args `(const char* path, a, b, c)`; 23 of the 45 call
  sites pass `(path, 0, 0, 1)`, 6 pass `(path, 1, 3, 1)` (read); what a/b/c
  mean is unverified.
- Returns the **shared cached `NiNode*`**: it looks the path up in the
  cache (vt+4 of `*ECX`), loads on a miss (0x00437250, 0x00439400,
  0x004395D0), then `InterlockedIncrement(entry+4)` and returns
  `entry+8` (read). So the reference taken is on the cache entry, not on
  the NiNode (derived). Nothing in BSTempEffectParticle releases that
  entry reference again (derived; unverified whether that is a leak or
  intended).
- **Clone before use** (derived): BSTempEffectParticle 0x005713F0 clones
  unless it explicitly wants the shared instance. Mandatory for a skinned
  part, because binding overwrites the skin instance's bone pointers.
- `0x00700900` clone, **thiscall, no stack args, plain RET** (read: call
  site `mov ecx,eax; call 0x700900`; epilogue `C3`). ECX = source
  NiObject. Builds a NiCloningProcess (0x00478B90), calls vt+0x18
  (CreateClone) and vt+0x38 (ProcessClone) of the source, returns the new
  object in EAX (read). The clone comes back unreferenced; the caller holds
  it through the NiPointer assign `0x0075FA90` (thiscall, ECX = `NiObject**`
  slot, arg = new object: decrement-old/delete-at-0, store, increment new;
  read). Release = `InterlockedDecrement(obj+4)`, at 0 call vt+0 with 1
  (read, same function).

## 2. Engine functions that bind a skinned part by bone name

- **`0x0055D090` = BSFaceGenNiNode vtable slot +0xC4** (read: pointer at
  0x00A65020 in vtable 0x00A64F5C, RTTI `.?AVBSFaceGenNiNode@@`).
  **thiscall, RET 8**: `(NiNode* skeletonRoot, bool warn)`. ECX = the part's
  root NiNode. For each *direct* child (array `+0xB0`, count `ushort +0xB6`)
  whose vt+0x10 returns a geometry with a skin instance (`geom+0xB8`) and
  skin data (`skin+0x08`): for i < `skinData+0x40` (bone count), look up
  `skeletonRoot->vt+0x58(bones[i]->name)` (`bones = skin+0x14`, name at
  `bone+0x08`, a `char*`); found -> `bones[i] = found`, else log "Could not
  find bone ... for the head node." if `warn`. Then `skin+0x10 (root
  parent) = ECX` (read). No direct caller; reached virtually (read: head
  builders 0x00525A70 and 0x00528A10 call vt+0xC4 right after attaching the
  head node with vt+0x84).
  It touches only NiNode fields of ECX, so it can be **called directly**
  (not through the vtable) on a plain cloned NiNode such as the neckplug's
  "Scene Root" whose one child is the skinned "NeckPlug:0" (derived;
  unverified in game). Lookup misses leave the old pointer, which after a
  clone points at the clone's own bone nodes (derived).
- `0x0047B090` biped skinned attach, thiscall with ECX = the biped
  structure (ECX[0] = skeleton root, ECX[0x54] = owner), 5 args
  `(NiNode* partRoot, NiNode* attachParent, int slot, bool, int)` (read from
  decompile; RET not checked). Per child geometry: DetachChild (vt+0x88)
  from the part root, bind each bone by `0x006F94A0(skeletonRoot, name)`,
  `skin+0x10 = attachParent`, `attachParent->vt+0x84(geom, 1)` (read).
  Called from `0x0047B5B0` (attach into a "SkinAttachment" node). Needs the
  engine's biped object, so it is not a clean entry point for a plugin
  (derived).
- `0x006F94A0` **cdecl** `(NiNode* root, const char* name)`: first a
  name-map lookup through an extra-data object (0x00700010 with
  0x00B3CAC0, its +0x7C, vt+0x4C), else `root->vt+0x58(name)`
  (GetObjectByName; the name is from corpse-becomes-zombie.md, that it
  searches the subtree is unverified) (read; extra-data type unverified).

## 3. Binding by hand (what 0x0055D090 does)

```
skin  = *(geom + 0xB8)          // NiSkinInstance*
data  = *(skin + 0x08)          // NiSkinData*, bone count *(UInt32*)(data + 0x40)
bones = *(NiAVObject***)(skin + 0x14)
for i: b = root->vt+0x58(*(char**)(bones[i] + 8)); if (b) bones[i] = b;
*(skin + 0x10) = partRoot       // root parent; the biped path uses the attach parent
```
(read.) Only direct children are visited; nested shapes would need their
own pass (derived).

## 4. Attaching and updating

- AttachChild = `parent->vt+0x84(child, 1)`, thiscall (read at 0x005715F6).
- BSTempEffectParticle's sequence after it (read, bytes 0x005715DD on):
  `0x00707610` (thiscall, ECX = new node, no args;
  UpdatePropertiesUpward - takes a critical section 0x00B3FA00, derived
  name), then `0x00707370` (thiscall, `(float time, int flags)`, RET 8;
  calls vt+0x60 UpdateDownwardPass, then the node's +0x1C object vt+0x94)
  with `(*(float*)0x00B33A30, 1)`. The head builders call `0x00707980`
  (vt+0x70, UpdateEffects - derived name), `0x00707610`, `0x00707370(0,0)`.
- Note: OBVR `GameAddresses.h` says 0x00707370 dispatches through vt+0x64;
  the bytes show vt+0x60 (`8B 50 60`); vt+0x64 belongs to the next function
  0x007073A0 (read).

## Modern engines

A modern engine binds a skinned attachment once by bone index against a
shared skeleton asset (e.g. Unreal's leader-pose component), not by
string lookup at attach time (unverified here; no source fetched).

## Measured 2026-11-03: the zombie neck plug faces up the neck (OBVR)

- **Measured** (OBVR harness dismember-corpse, mean of the plug's 87 vertex
  normals, NiGeometryData +0x20, under the skin-to-bone): `creatures\zombie\
  neckplug.nif`, skinned to `Bip01 Head`, faces along the head bone's way up
  the neck (0.82-0.92 of its mean normal) - it closes a body's neck from
  above. Bound to a severed head it faces into the head, so the head's
  underside shows the plug's back (culled): see-through from below.
- **Derived:** reused for the other side of a cut, a one-sided plug must be
  turned half round about its centre (a node following the bone, its
  rotation times a 180-degree turn about an axis across the neck, its
  position moved so the centre stays) - OBVR `game::FlippedCap`.
- **A modern engine instead:** fills the cut from inside (Blade & Sorcery's
  `RagdollPart.sliceFillMaterial`, "the material applied to the inside of
  this part when it gets sliced", kospy.github.io/BasSDK) or draws the cap
  two-sided; no per-side asset orientation to get right.
