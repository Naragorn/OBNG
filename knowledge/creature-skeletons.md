# Creature skeletons: which bones a "head" or a "top" can be found by

Status: knowledge, written 2026-10-25 from OBVR (`../OBVR`,
docs/hud-on-hands-spec.md "The bars only when they tell something",
game/EnemyHealthLogic.h `EnemyHealthAnchorOf`). OBVR hangs an enemy's health
bar over its own top; the vanilla rule it replaced put a rat's bar 2 m up.

Marks: **read** (in a file or a source), **measured** (in the game or on
its files), **derived**, **unverified**.

## What the vanilla skeletons carry

- **Measured (pyffi on `Oblivion - Meshes.bsa`: 31 of its 33
  `meshes/creatures/*/skeleton.nif` - boxtest and mehrunesdagon left out -
  and `characters/_male/skeleton.nif`, bind pose, units over the root):** most creatures name their head bone
  "Bip01 Head" as people do - bear 128.9, boar 42.4, dog 56.7, deer 85.9,
  goblin 113.5, ogre 138.4, troll 130.3, minotaur 117.5, scamp 117.3,
  skeleton 122.1, zombie 112.7, lich 110.2, the atronachs, imp, spriggan,
  spider daedra, wraith, xivilai, will-o-the-wisp, slaughterfish 12.2; the
  man 112.4.
- **Measured: five have none.** The rat (its highest bone "Bip01 Spine" at
  27.6, neck 24.0, pelvis 27.3), the mudcrab (no trunk bones at all - only
  "Bip01" at 40.0 and its claws', the highest "Bip01 L UpperArm" at 67.5),
  the clannfear and the daedroth ("Bip01 Skull" instead, 89.3 and 181.8),
  and the horse (every bone "Bip02 ...", "Bip02 Head" among them).
- **Measured: the highest node is often not the head.** Wings (imp 177.4
  against its head's 114.2, land dreugh), cloaks (the wraith's "Cloak01" at
  235.4), tails and ponytails (dog, deer, mountain lion, flame atronach),
  twigs (spriggan), "magicNode", "Camera01" and "BackWeapon" on the man.
  A rule "the highest node" needs a list of which nodes are body.

## What OBVR does with it (derived, built 2026-10-25)

- The head bone when there is one; else the highest of the bones its blade
  code already keeps by name (neck, spine 0-2, pelvis, the limbs' "Bip01 L/R
  ..."), read in the current pose; else the root's world bound (centre plus
  radius); else nothing to show. Only creatures without a head bone take
  the highest bone, so a person's raised arm never counts.
- **Unverified:** how high the bound's top sits on a horse in the game.

## What this means for a reverse-engineering pass

- The engine names bones by convention, not by role: a "head" lookup by
  the string "Bip01 Head" misses five of the 31 creature skeletons
  read. **Derived:** a role table per skeleton (head, trunk, limbs),
  filled once per skeleton file, would answer "where is its head" for
  every creature and every mod's; how modern engines map bone roles was
  not looked up for this note.
