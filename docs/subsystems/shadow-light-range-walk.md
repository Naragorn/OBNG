# Shadow light range walk (replacement of 0x007D59E0)

OBNG's first subsystem replacement: the walk that collects, per shadow light,
the scene geometry within the light's reach. Pure logic with tests; its
in-game gate is OBNG.dll's `RangeWalk` shadow (`plugin/src/ShadowRangeWalk.cpp`,
flow in `plugin/src/ShadowRangeWalkLogic.h`). Code: `src/engine/render/ShadowLightRangeWalkLogic.h` (the two
walks), `src/engine/render/ShadowLightRangeWalk.h` (the engine ports), tests:
`tests/ShadowLightRangeWalkTest.cpp`. Marks: read / measured / derived /
unverified (AGENTS.md).

## Verified in the game (2026-10-30)

Run `artifacts/shadow/obng-range-walk/20261030-120624` (prison empty, then
eighteen Walkers; `plugin/src/ShadowRangeWalk.cpp`'s shadow detour): 74 573
walks compared - 30 996 on the plane path (only with the Walkers' actor-shadow
lights), 43 577 on the distance path -, 3 574 564 AddGeometry calls in
sequence with their masks, the final mask and AL per walk: **0 mismatches**.
Not yet routed: the game still runs the original.

## The unit

One light, one walk: one call of `ShadowSceneLight::CollectGeometryInRange`
0x007D59E0 (proposed name) from its entry 0x007D6900, from the root object
down (read, `re/functions/0x007D59E0.md`). Per visited object one range test
0x007D2EA0; an out-of-range object's subtree is skipped; an in-range node
recurses (an exact NiSwitchNode only into its active child), an in-range
geometry goes to AddGeometry 0x007D5790. Measured: ~2.0-2.4 M visits per 10 s
with eighteen Walkers, and the plane path's ~10 M WhichSide calls per 10 s fit
coming through this walk (`knowledge/culling-and-draw-setup.md`, run
20261030-101556).

## The boundary

- Inputs: the gate [0x00B42F48]; the light's source (+0x100: null, its RTTI,
  its position +0x88 and range +0xF8), plane source (+0x14C), planes (+0x150),
  mask (+0x1B0); the scene tree below the root (node/geometry queries, the
  switch's active child, child arrays, flags +0x18, world bounds +0x20..+0x2C).
- Outputs: the sequence of AddGeometry calls (the geometry and the value
  +0x1B0 holds at the call); +0x1B0 afterwards (equal to its entry value,
  derived from C3); AL (0 only when the gate is off).
- Observed for the comparison: per visited object, in order, the in/out
  decision and the mask before and after its range test.

## What the batched version changes, and why it stays exact

| Change | Why it is exact | Mark |
|---|---|---|
| Plane test as one batch per object: distances of all active lanes in one pass, classification in a second, mask and cull derived by bit operations; no call per plane | each lane is WhichSide's own `SignedDistance` + `Classify` and IsInRange's `ClassifySide` - the same function, the same roundings, evaluated per lane | derived; differential |
| Mask read once per object instead of per plane | inside one test the only write clears the current plane's bit, later planes test higher bits, WhichSide writes no memory but its argument slot | derived from read (0x007D2F3E, B7; 0x0047DA70 mapping) |
| Explicit stack instead of native recursion; the mask travels on the stack, +0x1B0 never written | the original restores +0x1B0 on every exit; children see the parent's narrowed mask, siblings do not see each other's | derived from C3/C4; C4 rests on the helpers not writing +0x1B0 (unverified) |
| Source light read once per walk (null / directional / plane / distance chosen once) | the walk and IsInRange write only +0x1B0 | read for those two bodies; unverified for 0x007D5790, the virtual calls and the two helpers |

Kept on purpose: the number of per-plane classifications (one per active lane,
no early exit - the original does not exit early either, C6 of 0x007D2EA0);
the gate read per call, the child count and slot re-read per child, the double
vt[+0x10] call. Each of those could be dropped only after the callee that might
change the scene (0x007D5790) or return differently (vt[+0x10]) is read.

## What the engine ports must wrap

| Port | Engine operation | Status of the address/shape |
|---|---|---|
| the walk itself (hook site) | 0x007D59E0, `__thiscall(light; object)`, RET 4 | read |
| `addGeometry` | 0x007D5790, `__thiscall(light; geometry)` | call read; body unverified (knowledge/shadow-light-list-remove.md) |
| `activeChild` | 0x007C59E0, `__thiscall(switch)` | call read; body pseudocode only; single code path |
| `hasNonZeroBoundRadius` | 0x0049FFC0, `__thiscall(object)` -> AL | call read; body pseudocode only; single code path |
| `gate` | dword 0x00B42F48 | read; meaning unverified |
| `asNode` / `nodeRtti` / `asGeometry` | vtable slots +0x08 / +0x04 / +0x10 | read; names unverified |
| `childCount` / `child` / `flags` / `bound` | +0xB6 u16, +0xB0 array, +0x18 byte, +0x20..+0x2C | read |
| `LightValues` | +0x100, source GetRTTI, +0x14C, +0x150, +0x1B0, source +0x88/+0xF8 | read (0x007D2EA0 mapping) |
| square root | 0x00982C30 | CRT routine; rounding vs FSQRT unverified |

## How the in-game gate will compare the two

Per light per pass: run the original 0x007D59E0 (with a recording AddGeometry
hook) and BatchedWalk on the same light and root, then compare the AddGeometry
sequences element by element (geometry and live mask) and +0x1B0 afterwards;
optionally the per-visit decisions via a counter at the 0x007D5A12 branch. A
rebuild is **verified** only after that comparison has run clean in the game;
today it is **reconstructed** (tests pass; the oracle is the IsInRange rebuild
verified in the emulator).

## Still unverified

- 0x007C59E0 and 0x0049FFC0: their instructions are now read
  (re/export/range-callers/; signed index bounds against the u16 count, NaN
  counts as a non-zero radius, as B11); each still has one code path only, and
  the walk obeys whatever the port answers.
- +0x14C's meaning (only its zero-ness is used) and what sets +0x14C / +0x1B0
  before 0x007D6900 (0x007D6390, 0x007D5ED0 unread).
- 0x007D5790: whether the insertion order matters to its consumers, whether it
  reads +0x1B0 (the port gets the live mask in case it does), and whether it
  can change the scene or the gate (the walk keeps the re-reads in case it can).
- Whether vt[+0x10] returns `this` (would allow one call instead of two).
- No performance number is claimed; none has been measured for this rebuild.
