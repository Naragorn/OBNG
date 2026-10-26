# Consuming items: eating, drinking, poisons

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`, `src/game/TakeItem.cpp`
EatItem/RefIsEdible, `src/vr/Stow.h`, docs/holding-objects-spec.md "Up to the mouth").
OBVR eats and drinks a held world item let go at the mouth; nothing of it is built in OBNG.

Marks: **read** = read in Oblivion.exe 1.2.0.416 (objdump, Ghidra),
**measured** = measured in the game (OBVR's hand-script harness),
**derived** = follows from the two, **unverified** = not checked.

## What is food (read, Oblivion.esm)

Food has no record type of its own. The apple (0003365D), the bread loaf (00023D89),
cheese, carrot, pear and potato are **ingredients** (INGR, form type 0x19); wine and
potions are **potions** (ALCH, form type 0x28). A poison is a potion whose effects are
all hostile; xOBSE's IsPoison says the same ("An alchemy item with even a single
non-hostile effect is not a poison").

## Using an item is Actor::EquipItem (read)

The inventory "uses" an item through Actor::EquipItem (0x005FAEA0):

| Item | Branch | Call |
|---|---|---|
| Ingredient | 0x005FB2BD | 0x005E44C0 eats it (its first effects by the skill), then 0x005E99C0 the item's sound; a vtable +0x78 check on the item shows a message for the player instead |
| Potion, not a poison | 0x005FB356 | 0x005E08C0 drinks it, then 0x005E99C0 the sound |
| Potion, all effects hostile | 0x005FB32C | for the player 0x0066A490: the poison goes on the weapon |

The poison test is 0x00414EB0, thiscall on the item's effect list at +0x30
(`lea ecx, [edi+30h]` at 0x005FB32C): 0 for an empty list or one effect that is not
hostile, 1 when all are. Inside, each effect's setting at +0x1C has its flags at +0x58.

## Measured (OBVR harness, 2026-10-25, eat-at-mouth.txt)

- A world apple taken by activation, then EquipItem on its base form: eaten; a second
  console `player.drop` of the apple found none left.
- A Restore Health potion the same way: health 40 of 50 before, 50 a second on.
- A Weak Poison of Illness (0008DC74): not passed to EquipItem by OBVR's own test.

## Pitfalls (measured)

- A world reference taken into the inventory is deleted: anything still following its
  body (OBVR's wait for a collision group to come back after a release inside the
  player's capsule) read freed memory and took the game down two frames later.
  Everything that follows a reference has to forget it at the take.
- The tutorial quest pops "Your Health is low" / "Your Fatigue is low" message boxes
  when health or fatigue are lowered far (here -40 health of 50, -100 fatigue of 145).

## For OBNG (derived)

Using an item should be one call taking the item and the user, not an equip with a
type switch inside; a "consume from the world" path should not need the
take-into-inventory-then-use round trip and its deleted-reference hazard.
