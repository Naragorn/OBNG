# Where a spell projectile starts and which way it flies

Found during OBVR's spell from the casting hand (2026-10-28).

## Read in the EXE (Ghidra 12.1.4 via REA, Steam GOTY build)

- `0x0069A060` - the magic projectile factory, called once directly from
  `ApplyActiveMagicItem` at `0x0069B996` with `ecx` = the MagicCaster
  (actor + 0x5C). It first works out a start from the caster's position and
  bound, then calls the caster's virtual `+0x24`; if that answers a node,
  the start is overwritten with the node's world translation
  `[node+0x88]`, `[node+0x8C]`, `[node+0x90]`. The heading comes from the
  caster's rotation fields (actor +0x20 rotX, the heading virtual +0x1E0).
  Its effect walk compares each EffectItem's first word with `'TELE'`
  (0x454C4554): the effect code at +0x00. The list: head embedded at
  item+0x0C, a node's effect at node+4, the next node's pointer at node+8,
  pointing past the next node's first word.
- The player's MagicCaster vtable is at `0x00A739BC` (RTTI locator
  `0x00AC3E04` just before). Slot +0x24 is `0x005EDC40`: for the player,
  if the process's byte +0x18 has bit 0 (`0x00660110(0)` -> `0x00612220`)
  it takes the first-person anim data at player+0x5CC (`0x0065D750(1)`),
  otherwise the actor's virtual +0x164; then the anim data's +0x98 -> +0x7C
  root is asked for "magicNode" by name (`vtbl+0x4C`).

## Measured in the game (OBVR harness, spell-casting-hand, 2026-10-28)

- The EffectItem's range is at +0x10 (0 self, 1 touch, 2 target): Flare
  (000A97DF) read "target", the save's previous active spell "self".
- At the factory call the magicNode stood 38-42 units from the palm of the
  hand OBVR draws for the cast: the start is wherever the cast animation
  left that node, not the hand the player sees.

## What a modern engine does instead

The spell's origin and direction are inputs of the cast (a socket or a
tracked hand pose and an aim ray) rather than a node position read back out
of the animated skeleton at the moment of the launch. VR titles (Skyrim VR's
spell origin at the controller) take both from the hand.

Marked: the addresses and the walk are **read**; the range offset and the
node's distance are **measured**; the VR comparison is **derived**, not
verified against Skyrim VR's code.
