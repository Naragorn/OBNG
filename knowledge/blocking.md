# Blocking: the hit handler's block path, and what stops a blow whole

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`: `src/game/ParryLogic.h`, `src/game/Parry.cpp`,
`src/game/BlockCone.cpp`, docs/combat-comfort-spec.md "How vanilla decides a block",
docs/weapon-collision-spec.md sections 11 and 22). OBVR parries by touch: the player's blade, shield or
torch meeting an attacker's blade in their swing. None of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (objdump, dumpbin), **measured** = measured in the game
(OBVR's hand-script harness), **derived** = follows from the two, **unverified** = not checked.

## The block in the hit handler 0x005FEBF0 (read)

The handler is a thiscall on the attacker (`edi`); the target is `esi`.

| Site | What | Answer |
|---|---|---|
| 0x005FF7DF | `call 005E5670`, thiscall(target): is the target in the Block action (6) | `al` |
| 0x005FF7F5..0x005FF824 | the target's process vtable +0xF8 (equipped shield) and +0xEC (equipped weapon) | `[esp+74h]` = blocking with neither: a hand-to-hand block |
| 0x005FF83E | `call 006131D0`, cdecl(target, attacker or the arrow, angle*): the attacker in front | `al` |
| 0x005FF84E..0x005FF879 | the same two slots again | `[esp+20h]` = a weapon and no shield: a weapon block |
| 0x005FF87E | `cmp byte [esp+74h], 0` / `je 005FF894` | not hand to hand: to the share |
| 0x005FF885..0x005FF892 | `[esp+2Ch]` (the attacker's equipped weapon's form, set at 0x005FEFA9) or `ebp` (an arrow) | either: `fldz`, nothing blocked |
| 0x005FF8A0 | `call 005F4880`(weapon block, hand to hand): the item factor | st0 |
| 0x005FF8C7 | `call 005474A0`, cdecl(Block 0x0F, Luck 7, the factor, two flags): the share blocked | st0 |

The share is min(fBlockMax 0.75, (base + mult x the luck-modified Block / 100) x item x the fatigue factor);
the item is 1 with a shield, fBlockAmountWeaponMult (0.5) with a weapon and no shield, and
fBlockAmountHandToHandMult with neither (combat-comfort-spec). A blow is never stopped whole in vanilla,
and a hand-to-hand block against a weapon or an arrow stops nothing (UESP, Oblivion:Block).

- A torch is neither a shield nor a weapon (read: it is the process's light, vtable +0xF0), so a player
  with a torch and bare fists blocks hand to hand. Derived: in vanilla, such a block stops nothing against
  an armed attacker.

## What OBVR changes (read in its code)

- The block check answers yes for the player while a parried attacker's blow is in its 0.8 s window, or
  while the off hand holds the attacker by the head, neck or weapon arm.
- The cone answers "in front" for that attacker, and "not in front" for any other attacker during the
  forced block.
- The share is 1 for a stopped blow, with ParryStopsAll on.
- Since 2026-10-25 the compare at 0x005FF87E goes through an 18-byte stub. With a stopped blow it answers
  "equal", so the handler goes on to the share; otherwise it compares the handler's own byte, one return
  address further down the stack. Without the stub, a stopped blow at a player with no weapon and no
  shield equipped landed whole against an armed attacker. That held for the torch with the fists, and for
  someone held while the weapon slot is empty (derived from the read above; the harness runs are in
  OBVR's weapon-collision-spec section 22).

## Measured (OBVR harness, 2026-10-25, torch-parry.txt and torch-parry-sword.txt)

- Nothing in the weapon slot, a torch in the left hand, an armed bandit: one blow parried by the torch,
  and with the stub "blocked 1.00 of it". The engine's own share for it was 0.01, at Block 10. The share
  function was reached, which a hand-to-hand block against a weapon never does without the stub.
- With a sword equipped, four blows were parried by the torch, each stopped whole the same way.
- Torch02's shaft as read off its model: 368 vertices, 47.9 units along the hand's y axis.

## For OBNG (derived)

The block is three questions asked in one function, each answered from a different place: the action,
the facing, and the item via two virtual calls. A stopped blow has no way in except through all three.
A block should take one answer from whatever met the blow: the item, its share, and the reaction. Whether
an attacker was parried belongs to the contact, not to the target's action.
