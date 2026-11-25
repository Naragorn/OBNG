# The grab (Z) and things made of several bodies: dummies, bags, hinged signs

Found during OBVR's work on a tester's report (2026-11-25): a fist at a
training dummy put the dummy's beam into the hand.

## Read in the game's data (Oblivion.esm, Oblivion - Meshes.bsa)

- **One reference, many bodies.** The practice dolls, bags and hinged signs
  are single references (statics and activators) whose mesh carries several
  bhkRigidBody blocks joined by constraints. A few of the bodies are fixed
  (motion system 7, quality FIXED, layer STATIC, mass 0) - the beam, the
  post, the bracket - and the rest hang from them.

  | Base form | Mesh | Bodies | Fixed | Hanging | Constraints |
  |---|---|---|---|---|---|
  | ChainDoll01, STAT 00000E06 | clutter\fightersguild\chaindoll01.nif | 23 | 5: the root node's beam, 2 supports, 2 feet | 18: box inertia, layer PROPS, mass 5 | 20 ragdoll, 1 limited hinge |
  | ChainDoll02, STAT 000521F3 | ...\chaindoll02.nif | 23 | 5, all on child nodes (the root has no body) | 18 | the same |
  | ChainDollArena01, STAT 00027FD8 | architecture\arena\chaindollarena01.nif | 27 | 2 | 25 | 25 ragdoll, 1 hinge |
  | ArenaHeavyBag01, STAT 00159450 | architecture\arena\arenaheavybag01.nif | 5 | the root's mount | 3 chain links and the bag | 4 ragdoll |
  | TargetHeavy01 / ArenaDummy1, STAT | clutter\fightersguild\targetheavy01.nif | 14 | 6 | 8, layer ANIM_STATIC | 9 ragdoll |
  | hinged signs, ACTI (some 50 meshes: clutter\signs\sign*.nif, architecture\chorrol\sign*.nif ...) | | 2 | the post or bracket, mostly the root's | the board, layer ANIM_STATIC, mass 9.8 to 47 | 1 limited hinge or 1 ragdoll |

  Built the same way: PrisonCellChains01 (4 fixed, 28 hanging; in
  ImperialDungeon01, the game's first cell), the hanging iron lamps, the
  buckets, rocks and skulls on ropes. The Imperial City's district signs are
  one fixed body with a wind animation and are not of this kind.
- **Several bodies does not mean anchored.** 329 meshes have two or more
  rigid bodies and a constraint; 205 of them are items whose bodies are all
  free (amulets, the ground models of cuirasses and greaves).
- **Where they stand** (placed references, for a test): ChainDoll01 0001DB3F
  in ChorrolFightersGuildBasement and 00037F72 in ICArenaBloodworks (both
  persistent, so `player.moveto` finds them), TargetHay01 000378EC and
  ArenaDummy1 00132A90 in ICArenaBloodworks.

## Read in the EXE

- **The grab key's handler** `0x00671170` takes the HUD's crosshair
  reference, tests the mass of its first collision object (a fixed root's is
  0 and passes) and calls the grab's start `0x0066D120(ref, 1, 1000)`.
- **The grab's start** `0x0066D120` casts its own ray (the item pick), takes
  the body hit, else the heaviest body under the node (`0x004803C0`), and
  ends with three refusals: the body in no world (`0x0066D5C4`), motion type
  7 (`0x0066D5DD`), motion type 6 (`0x0066D5EF`). Then it builds the mouse
  spring. Neither it nor the update `0x0066D930` nor the release `0x0066A670`
  writes a motion type, a quality or a collision filter.
- **So on such a reference vanilla's grab holds one hanging part** on its
  spring; the fixed frame is never touched.
- **A fixed body's node is never moved back**: the collision object's update
  `0x00897510` copies a dynamic body to its node (`0x0089EA70`) and a node to
  its keyframed body (`0x0089EAE0`) and skips a fixed body that is in a
  world. A node of a fixed body moved by someone else stays moved.
- **The collision filter's group rule** `0x008A7F70`: bodies of one group
  that are both "linked" do not touch when their part numbers differ by one;
  others of one group do not touch at all; across groups the layer matrix
  decides. A hanging part put into another group meets its own neighbours.

Source: a research pass of 2026-11-25 over the game's data (pyffi on the
meshes, a scan of the ESM) and the decompile export; the grab's three
refusals were already in OBVR's game/GrabPhysics.h. Not read: who else calls
the motion type setter (`0x0089ED20`), and what the handler's call of the
reference's vtable +0x40 with 8 after the grab means.

## Measured in the game (OBVR harness, Linux, 2026-11-25)

- ChainDoll01 0001DB3F as loaded: 23 rigid bodies under its node, 5 of them
  fixed or keyframed (OBVR's walk of the node tree, "5 of its 23 bodies
  fixed") - the mesh's numbers.
- A closed grip a hand's breadth from a hanging part, the grab key down: the
  engine's grab takes the doll ("the engine TOOK it", the grabbed reference
  the static itself, form type 1C, bound radius 168 units).

## What a modern engine does instead

A grab addresses a body, not a reference: the interaction system gets the
rigid body the hand is on and its constraint island, and decides from the
island - free, hinged, anchored - whether it is carried, swung on its
constraint or left alone. A scene object's "root transform" and its physics
bodies are separate things there, so no hold can move a frame by writing the
root. Marked **derived** (Half-Life: Alyx and Blade & Sorcery behave that
way; no source of theirs was read).
