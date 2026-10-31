# Blood on hits: spray particles and blood decals

Status: knowledge, written 2026-10-31 from OBVR (decapitation wants blood at
the neck). Nothing of this is built. Read from Oblivion.exe 1.2.416 (Steam,
sha256 74db8031...) with the rea-all decompile, Ghidra assembly through REA
and the EXE's raw bytes. Marks: **read** = seen in code/bytes, **derived** =
concluded from what was read, **unverified** = not confirmed.

## Where the paths come from

| What | Address | Text / meaning | Mark |
|---|---|---|---|
| `sBloodParticleDefault` setting object | 0x00B371B0 (value ptr), name 0x00A59398 | default `Effects\BloodSpray.nif` (string 0x00A593B0) | read (static init 0x009EA0D0: `push 0xA593B0; push 0xA59398; mov ecx,0xB371B0`) |
| `sBloodParticleExtra1` / `Extra2` | 0x00B371B8 / 0x00B371C0 | `Effects\lichBloodSpray.nif` (0x00A593E0) / `Effects\skeletonBloodSpray.nif` (0x00A59414) | read |
| `sBloodTextureDefault` | 0x00B371C8 | `Effects\blooddecal.dds` (0x00A4D1F8) | read |
| `sBloodTextureExtra1` / `Extra2` | 0x00B371D0 / 0x00B371D8 | `Effects\lichBloodDecal.dds` (0x00A59460) / `Effects\willothewispBloodDecal.dds` (0x00A59490) | read |
| Extra1/2 are used only by the preload functions 0x00443550, 0x00443C70, 0x00444A10, 0x004464F0 | | | read (grep: no other users) |

The actor base form's `TESActorBaseData` sub-object (base + 0x24) answers
through its vtable:

| Slot | NPC (vtables 0x00A52F1C, 0x00A53D6C) | Creature (vtable 0x00A531E4) | Mark |
|---|---|---|---|
| +0x28 "no blood spray" | 0x0069D990 (false, unverified body) | 0x0051CC80: `(flags@+4 >> 11) & 1` = ACBS flag 0x800 | read |
| +0x30 "no blood decal" | 0x0069D990 | 0x0051CCC0: flag 0x1000 | read |
| +0x38 blood decal path | 0x004677D0 -> `sBloodTextureDefault` | 0x0051E1E0: string at sub+0x114 (creature+0x138), empty -> default; null if flag 0x1000 | read |
| +0x40 blood spray path | 0x004677E0 -> `sBloodParticleDefault` | 0x0051C650: model at sub+0xF8 (creature+0x11C), its vt+0x14 = path; empty -> default; null if flag 0x800 | read |

Actor-side wrappers (thiscall on the Actor, no args):
0x005E1BF0 = blood spray path, 0x005E1BB0 = blood decal path, 0x005E1B30 =
"bleeds at all" (false only when both flags are set). Read. So every NPC
uses `Effects\BloodSpray.nif`; a creature uses its own CREA path or that
default (derived).

## The melee chain

- 0x005FEBF0 (the melee hit handler; name unverified) calls the victim's
  virtual **Actor vt+0x398** (0x005FEBF0.c line 839). Read.
- vt+0x398 = **0x005EE760** in the Character, Creature and PlayerCharacter
  vtables (refs 0x00A70034, 0x00A7148C, 0x00A73DA4; also 0x00A6E40C). Read.
  `__thiscall(Actor* victim, Actor* attacker, float damage, float p3,
  NiPoint3* a, NiPoint3* b)`, `RET 0x14`. Read (assembly). `a` and `b` are
  transformed by the attacker's 3D world transform (attacker vt+0x154 node,
  +0x64) and the hit direction is `normalize(T(b) - T(a))` (derived; the
  callers pass weapon-line points, unverified). 0x00699900 calls it the same
  way with `(actor, 2*x, 0, &v1, &v2)`.
- 0x005EE760 first does the ragdoll hit impulse (`Hit At %s`), then, if
  0x005E1B30 is true **and** `damage > max(fMinBloodDamage, 1)` **and**
  (no `DAT_00B333B8` and hi-perf combat count `DAT_00B3B914 <=
  iMaxHiPerfCombatCount` (0x00B148E4, default 4), or the player is either
  side), it loops: per iteration one ground decal plus one spray, damage
  `*= 0.25` until it falls under the threshold. Read.
  `fMinBloodDamage:Combat` = INI setting 0x00B148DC, default 1.0. Read.

### Spawning the spray (0x005EF0A3-0x005EF22E, read)

```
path   = victim->0x005E1BF0()                  // spray .nif
cell   = victim->parentCell                    // 0x006ECC80 = [ecx+0x40]
quad   = 0x004C9BE0(victim)                    // cdecl, 1 arg; 0..3, 0 for interiors
parent = cell->0x00441800(quad, 3)             // thiscall, RET 8: cell3D->child[quad+2]->child[3]
mem    = 0x00401F00(0x20)                       // cdecl alloc (FormHeap), 0x20 bytes
eff    = BSTempEffectParticle::ctor 0x005713F0 // thiscall, 12 dwords, RET 0x30
0x00678D30(eff) with ECX = 0x00B3BD00          // thiscall, 1 arg: adds to the temp-effect list
```

`0x005713F0(this, TESObjectCELL* cell, float lifetime, NiNode* parent,
const char* nifPath, float dirX, dirY, dirZ, float posX, posY, posZ,
float scale, bool useShared)` - read from the assembly:

- `cell` -> this+0xC, `lifetime` -> this+0x8 (then overwritten by the
  longest controller span of the loaded model, 0x00570B40, -1 if none).
- the model is loaded by `ModelLoader(0x00B33A1C)->0x00439EB0(path,0,0,1)`;
  `useShared=0` clones it (0x00700900), `1` goes through 0x00441760 on
  `DAT_00B333A0` (melee passes 0, the projectile path 0x005F9620 passes 1 and
  lifetime 5.0). Node kept at this+0x18, path at this+0x1C (save code
  0x00570DA0 writes both).
- dir -> yaw/pitch by atan2 (0x0098598A) -> rotation via 0x007118E0; pos ->
  node local translate (+0x54); `fabs(scale)` -> +0x60;
  `parent->vt+0x84(node, 1)` = AttachChild, then update/controllers start.
- **`parent` is attached unconditionally** if the model loaded; the melee
  caller does not null-check 0x00441800's result. A plugin must.
- Melee values: lifetime 1.0, scale 1.0, useShared 0, dir = hit
  direction, pos = a point about 6 units along the attacker->victim vector
  from the victim's node (0x004BF9B0 by 6.0); exact meaning derived.

Vtable 0x00A686CC = BSTempEffectParticle (the save functions 0x00570CA0 /
0x00570DA0 name `BSTempEffectParticle.cpp`). Read.

## Blood decals (read)

- Ground/world decal: `TESObjectCELL::0x004D3B10` (thiscall, ECX = cell,
  10 dwords, callee-cleaned): `(pos.xyz, dir.xyz, const char* texture,
  0, -1, 0)` from both callers 0x005EE760 and 0x00510580 (the latter passes
  `Effects\blooddecal.dds` and the camera node's position and first rotation
  column). It ray-picks within 80/128 units and calls 0x004CF1A0 per hit
  object. Derived from the call sites; arg 8-10 meaning unverified.
- 0x004CF1A0 (12 args) places on one NiAVObject: static geometry ->
  `BSTempEffectDecal` (alloc 0x1C, ctor 0x0056BE50); skinned geometry only if
  `bDecalsOnSkinnedGeometry:Display` (0x00B06F5C) == 1 ->
  `BSTempEffectGeometryDecal` (alloc 0x54, ctor 0x0056CE90). Both go to
  0x00678D30 like the spray.
- Gate: `fDecalLifetime:Display` (0x00B097C8) > 0 and decals this frame
  `DAT_00B35C04 <= iMaxDecalsPerFrame:Display` (0x00B097D0). There is **no
  `bDecals` string** in the EXE; the settings menu (0x005DF160) turns decals
  off by setting fDecalLifetime to 0 (on = 10.0). Read.
- The body decal on the victim is 0x004CF1A0 called at the end of
  0x005EE760 with the decal path and the actor's node. Read; args derived.

## For a plugin (derived, not run)

Cheapest: reproduce the melee spawn above - 0x00401F00(0x20), ctor
0x005713F0 with the victim's cell, the victim's spray path (0x005E1BF0 on
the victim, or `*(char**)0x00B371B0`), parent from 0x00441800 (null-check),
own pos/dir, scale 1, lifetime 1, useShared 0, then 0x00678D30 on
0x00B3BD00. Main thread only (unverified that other threads crash, but every
caller is game-thread code). Calling 0x005EE760 instead also fires the
ragdoll impulse and depends on `damage`/combat-count gates: not a clean
entry. Decal: `cell->0x004D3B10(pos, dir, path, 0, -1, 0)`. Not run in game.

## What a modern engine does instead

A data-driven impact-effect table (surface/material x damage type ->
particle + decal), pooled GPU particles and a deferred/projected decal
system with a budget, instead of per-hit NIF clones attached to scene
nodes. Unverified against any specific engine here.
