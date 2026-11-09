# The actors' skeleton update

What: a replacement for the body of NiAVObject::Update 0x00707370 when
ActorAnimData::ApplyAnimData 0x00471F20 calls it - one actor's skeleton
propagated to world space, once per actor and frame (25-35 calls a frame
on the Gold Road, 60-125 with a crowd). Why: the original walks the tree
with a virtual call per child, the controller test per node, the bound
copies and the compose through four calls, 150-400 ns a node; an armoured
NPC's 300 nodes cost 50-60 us, 1.3 ms a frame on the Gold Road and 4 ms
with 120 actors (knowledge/render-thread-outside-the-pass.md, 2026-11-09).
The arithmetic stays the same, operation for operation; the walk goes.

## The shape

- `src/engine/scene/SceneUpdateLogic.h`: the values (Transform, Bound as
  the engine lays them out), the arithmetic in 0x0053D7A0's order
  (MultiplyMatrix, TransformPoint, Compose, UpdateWorld), the merge
  decision (MergeStepFor), the kinds from the vtable slots (Classify), the
  flat order (Build, Matches) and the two implementations over the action
  port: Reference (the original's recursion, the oracle) and Run (two flat
  passes). 64-bit clean: handles, no pointer size.
- `src/engine/scene/SceneUpdate.h`: the 32-bit ports over NiAVObject memory
  (offsets 0x0C, 0x1C, 0x20, 0x2C, 0x30, 0x64, 0x9C, 0xA8, 0xB0, 0xB6; the
  vtable slots 0x50, 0x60, 0x74, 0x78, 0x94; 0x0047C930 and 0x0072A6B0 as
  the engine's own calls), the x87-to-MXCSR rounding copy, UpdateSubtree.
- `plugin/src/FastSceneUpdate.cpp`: the entry detour on 0x00707370 acting
  for the return site 0x00471FBE only; the orders per skeleton root; the
  route (flat run, fallback to the original) and the shadow (save, original,
  save, restore, rebuild, compare, restore the original's).

## Switches

`[Route] ActorUpdate` (the run), `[Shadow] ActorUpdate` (the gate; the shadow
wins when both are on). Neither installs beside `[Probe] SceneUpdate`, whose
detour sits on the same entry. The route's summary line: calls from
ApplyAnimData, routed, the time in them, orders built and rebuilt, the
fallbacks.

## Evidence and state

re/functions/0x0070A0D0.md (the reading, the oracle: 3240 objects bit for
bit under both control words), re/ledger.md. Reconstructed 2026-11-09; the
in-game gate run decides "verified".
