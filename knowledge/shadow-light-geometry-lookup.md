# A per-light geometry lookup that is quadratic in the crowd

Status: knowledge, written 2026-10-27 from OBVR (`../OBVR`,
docs/performance-spec.md section 7, harness run
`artifacts/hand-script/perf-walkers-crowd/20261027-074800`). Nothing of this
is built in OBNG.

Marks: **read** = read in a binary (Oblivion.exe 1.2.0.416, REA/Ghidra),
**measured** = measured in the game, **derived** = follows from the two,
**unverified** = not checked.

## Measured

- Linux, Proton, DXVK, 75 Hz, OBVR's dual pass, headless. 25, 50 and 100
  OblivionWalkers NPCs placed at the player in one heap, AI off (tai).
- Frames 13.7 / 24.2 / 51.0 ms; one scene pass 2.85 / 5.10 / 10.67 ms. A
  person costs about 0.085 ms a pass at 25 and 0.099 at 100: nearly linear,
  a quadratic term on top.
- The render-thread sampler's 256-byte bin 0x007D5F00 held 5.5 %, 11.3 %
  and 18.4 % of the in-pass samples at 25, 50 and 100 - about 0.16, 0.58
  and 1.97 ms a pass. 2532 of its samples sat on one instruction,
  0x007D5FC3, the compare of a linked-list walk.

## Read

- **FUN_007D5F80**: a thiscall on a NiTList (head at +4; nodes are next,
  prev, data) that walks the list comparing each node's data with a key
  passed by pointer, and returns the match through a ref-counted out
  pointer (InterlockedIncrement on +4 of the match). Callers: 0x007C76D0,
  0x007C77C0, 0x007D6940.
- **FUN_007D6940** (the hot caller): a method of an object with a list at
  +0xE4. It calls NiAVObject::GetProperty(4) on the geometry passed
  (FUN_00707530 walks the property list at +0x98, xOBSE NiObjects.h), reads
  the property's vtable +0x54 twice and goes on only when the value is 1 to
  10, calls FUN_007EE720 on the property, and then searches the geometry in
  its own +0xE4 list with FUN_007D5F80.
- **FUN_007C5750** (the caller above, from 0x007C6100, from 0x007C7050 -
  the shader region next to the light constants 0x007C9140): a method of a
  ShadowSceneNode (it reads this+0x11C, a byte in xOBSE's layout). It takes
  the geometry's type-4 property, iterates the property's list at +0x70
  (count +0x78, cursor +0x7C, up to the value vtable +0x54 returns) and
  calls FUN_007D6940 on every item whose byte +0xF4 is zero.
- xOBSE's ShadowSceneNode layout: +0xE4 `lights`
  (NiTPointerList<ShadowSceneLight*>), +0xF4 `lights2`, +0x11C a byte.

## Derived

- For every drawn geometry, for each of its lights, a walk of a list that
  holds every geometry that light touches. In a crowd under one light that
  is every actor shape, so the cost grows with the square of the shapes -
  the measured 5.5 -> 11.3 -> 18.4 % when the crowd doubles twice.

## Unverified

- Whether the +0xE4 list searched belongs to a ShadowSceneLight (its
  geometries) or to the ShadowSceneNode (its lights): the key is the
  geometry pointer, which only the first reading matches, but xOBSE names
  the node's +0xE4 `lights`.
- What the lookup decides (a shadow caster or receiver test, a light's
  registration of the geometry) and what the type-4 property is (its
  vtable +0x54 returns a light count between 1 and 10).

## For an engine

- Membership of a geometry in a light's set must be a hash set or a flag on
  the geometry, never a list walk per draw. Here the walk is paid once per
  shape per light per pass, and the list is as long as the lit scene.
