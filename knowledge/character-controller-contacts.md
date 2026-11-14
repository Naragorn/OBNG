# The character controller's contacts: the proxy's integrate and the collector's pass

What one call of the controller's integrate does with the triangles around an actor, and why one actor can cost
20 ms a frame. Names are proposals; Havok's are hypotheses unless the binary's RTTI or label strings back them.
Sources: the decompiled text under `re/export/rea-all/functions/`, the instructions through `tools/re/disasm.py`
(the agent's reading of 2026-11-14; the pass and its predicate read again by hand, 0x008CE690-0x008CE8E2).

## The call path (read)

- **0x00896000**, the controller's update: before the integrate it writes the iteration cap `[0x00B2EFB8]` - 1 when
  bit 20 of the flags at +0x1F4 is set, else 4 (0x00896BC2-0x00896BD9) - calls 0x008902B0 and writes 4 back. What
  bit 20 means is not read.
- **0x008902B0**, on the `bhkCharacterProxy` (vtable 0x00A9667C, RTTI): builds a local `bhkCharacterPointCollector`
  (ctor 0x008CEA50, 0x1C8 bytes) whose +0x1A0 points at the proxy's own collector of the same class at +0x10, calls
  the proxy's integrate 0x008AD7D0 with both, then 0x008CE8F0 on the proxy's one.
- **0x008AD7D0**, the integrate (labels "updateCharacter", "InitialCast", "UpdateManifold", "SlexMove", "CastMove"):
  while time is left and fewer than `[0x00B2EFB8]` iterations: both collectors' hit counts set to 0, a linear cast of
  the phantom with both collectors, the manifold's update, the simplex solve, and - when the solved velocity differs -
  a second cast into the cast collector alone. At most two casts an iteration, eight a call.
- The cast reaches the pair cast **0x00935CC0** per triangle of the shapes around, which calls the collector's
  addCdPoint at most once a triangle.
- **0x008CED20**, `bhkCharacterPointCollector`'s addCdPoint (vtable 0x00A99DD8 slot +4, RTTI): notes the body, appends
  a 0x30-byte hit to the array (+0x10, count +0x14, eight in place, grows, no cap), sets the hit's kind at +0x24 for
  a triangle (shape type 6): 1 when the contact normal lies along the triangle's face normal, else 2 (an edge);
  0 for everything else. For a triangle it then calls **0x008CE770**, its only caller (0x008CF0D2).

## The pass 0x008CE770 and its predicate 0x008CE690 (read, instruction by instruction)

- The predicate `(result*, a, b)`: true when `a.kind == 1` and `b.kind != 1` and `0 < z + (y + x)` of the single
  products `a.normal * (a.position - b.position)` (SSE singles, then an x87 compare of 0 against the sum that a NaN
  fails). A hit that is no face is "behind" a face hit when it lies on the inner side of the face's plane.
- Step 1: for i = n-1 .. 2, for j = i-2 .. 0: when the predicate holds for (hit[j], hit[i]), hit[i] is overwritten by
  the last hit, the count drops, and i goes on. j = i-1 is never asked; the hit moved into i is not asked in this
  pass.
- Step 2, with a collector at +0x1A0 that holds hits: for s = m-1 .. 0, for k = n-1 .. 0 (n as it stands when s
  begins): when the predicate holds for (start[s], hit[k]), hit[k] is overwritten by the last hit.
- The whole pass runs after every triangle hit appended. One pass asks (n-1)(n-2)/2 + m*n pairs; a cast that collects
  N triangle hits about N^3 / 6. Nothing caps n or m.

## What it costs (measured)

- Windows, 2026-11-13, Anvil's harbour: the integrate 0x008902B0 about 19 ms in every frame for 80 s (the controller
  update's longest call 20.8-27.1 ms in every ten-second window), the process manager 25 ms a call, 22 frames a
  second. The support check 0x008AE890 of the same actor up to 1.0 ms: the same neighbourhood without this pass.
- Linux flights, 2026-11-13: the reference xx010E1F of Better Cities Full.esp (`BANAnvilCargoShipSailor03`), 18-28 ms
  a call; the sampler's samples inside lie in 0x008CE690 and 0x008CE770 - the pass, not the geometry query.
- Derived: about 100 triangle hits in one cast - a capsule standing inside or pressed into a dense collision mesh.
  The ship's collision (`cog01full.nif`, 3 559 triangles) has such spots only inside its structures (75-87 triangles
  within reach of an embedded capsule), not at the sailor's placed position (3) or along his carry route (9): he is
  pushed or stuck when it happens (one flight of four). Where he stands then is not measured.

## The rebuild (`[Route] ContactDedup`, class A)

`src/engine/havok/ContactDedupLogic.h`, `plugin/src/ContactDedup.cpp`: the same removals in the same order, asking only
pairs whose answer is not known from an earlier pass over the same bytes (a memo per collector, compared byte for
byte before it is trusted; why that is the same is written at the header's top). 2 = the check: the original on the
game's array, the rebuild on a copy, compared bit for bit. The geometry's own cost stays, and an actor stuck in a mesh
stays a defect of the mod's data.
