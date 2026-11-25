# A shot arrow: what it is after it lands, and how it is picked up

Found during OBVR's work on a tester's report (2026-11-25): arrows stuck in
targets and wood could not be recovered by hand.

## Read in the EXE (Ghidra 12.1.4 via REA export, raw bytes of the Steam build)

- **One class for its whole life.** A shot arrow is an `ArrowProjectile`
  reference (vtable `0x00A6F08C`; constructors `0x0060C940` at the launch,
  `0x006078E0` from a save; destructor `0x00608B60`) - flying, stuck or
  bounced. Nothing converts it to a plain reference. Its base form at +0x1C is
  the TESAmmo (form type 0x22), it is put into its cell's object list
  (`0x004D35D0`, called at `0x0060CD19`) and has a scene node at +0x3C.
- **State at +0x60** (the update `0x0060C170`, reached through vtable +0x70's
  thunk `0x0060CD70`): 0 flying, 1 hit this frame, 2 at rest, 3 fading. Once
  the age at +0x68 passes `fArrowAgeMax` (`0x00B37048`, 90.0) the state goes
  to 3; the alpha at +0x64 then falls by a third a second and at 0 the
  reference deletes itself (vtable +0x8C with 1).
- **What it hit: the record at +0x5C**, a `CollisionData` whose first word
  is the kind (`0x0060BE90` switches on it): 0 an actor (`0x00608FC0`),
  1 another reference's body - a wall, a post, a target (`0x00609150`),
  2 the land (`0x006094C0`), 3 bounced off (`0x006095B0`), 4 the flight ran
  out (`0x006098C0`). xOBSE's `CollisionData` starts with nine floats at +0;
  by this read the kind is at +0, so its `refr` is at +0x28 and its node at
  +0x2C. `0x0060BE90` frees the record and sets the state back to 0 when the
  handler answers false.
- **Havok.** The arrow's body is set keyframed (motion type 6) at the launch
  (`0x0060A230` calling `0x0088D070(node, 6)`) and stays so for kinds 1 and 2:
  kind 1 takes the host body's collision group with layer 15 (no collision),
  kind 2 layer 15 in the arrows' own group. Kinds 3 and 4 become dynamic
  (type 1) on layer 4, clutter.
- **A kind 1 arrow follows its host's fate**: in state 2, when the host
  reference (+0x28 of the record) has lost its 3D (+0x3C of it is 0), the
  arrow deletes itself (`0x0060C170`).
- **In an actor (kind 0)** the node hangs under the actor's bone, and with
  `iArrowInventoryChance` (`0x00B370C8`, 50) the arrow was already added to
  the actor's inventory at the hit (flag at +0x95, the tail of `0x0060A640`).
- **The cap.** With more than `iArrowMaxRefCount` (`0x00B370D0`, 15) arrows
  alive, the oldest resting one fades (`0x00608120`).
- **Picking one up is its activation.** ArrowProjectile has no activate of
  its own: `TESObjectREFR`'s activate `0x004DD260` calls the base form's
  (TESAmmo vtable `0x00A4068C` +0xCC = `0x004B28E0`), which takes the count
  from ExtraCount (1 without) and calls the player's pick-up `0x00660910`:
  the pick-up sound (`0x005E99C0`), the ammo into the inventory, the
  reference destroyed. The owner test skips anything that casts to
  ArrowProjectile (RTTI `0x00B05AD0`), so no theft.
- **The grab cannot hold a stuck one.** The grab's start `0x0066D120` ends
  with three refusals: the body in no world (`0x0066D5C4`), motion type 7
  (`0x0066D5DD`), motion type 6 (`0x0066D5EF`). A stuck arrow is 6.
- **The crosshair pick finds it** (derived, not run in vanilla): the pick
  `0x005806D0` -> `0x00538EC0` sweeps a phantom on layer 0x1D; `0x00538920`
  turns the pair 0x1D against 15 on before each cast.

- **Stick or bounce on the land** (`0x0060BAC0`): the record is made with
  kind 2, then the land's Havok material at the point is asked
  (`0x00440AC0`) and for the codes 0, 3, 5, 10, 11, 13, 15, 18, 20, 25, 26,
  28 and 30 the bounce `0x00608DA0` runs (kind 3). By NifSkope's nif.xml
  (`OblivionHavokMaterial`) those are stone, glass, metal, heavy stone, heavy
  metal, chain, their stair kinds and the elevator; cloth, dirt, grass,
  organic, skin, water, wood, heavy wood and snow keep the arrow. The hit on
  another reference's body is decided in `0x0060B120` (407 lines, not read
  through).
- **The impact dispatcher** `0x0060D950`: a hit on a body of the shooter's
  own collision group, or on what the player's grab holds, is ignored; land
  goes to `0x0060BAC0`, an actor to `0x0060A640`, any other reference to
  `0x0060B120` with the body's material.

Source of the read: a research pass of 2026-11-25 over the decompile export
and the EXE's bytes. Checked a second time: the update's states, the age
test, the fade, the kind switch, the land handler's material switch and the
dispatcher (`0x0060C170`, `0x0060BE90`, `0x0060BAC0`, `0x0060D950`). Read
once only: the Havok layers and groups, the actor and inventory part, the
activation chain, the pick's layers.

## Measured in the game (OBVR harness, Linux, 2026-11-25)

OBVR logs every ArrowProjectile of the player's cell at a scenario's mark
("Arrow take: at the mark arrow ... state, hit record, kind"; OBVR's
game/StuckArrow.h, runs under OBVR's artifacts/hand-script/arrow-take).

- Shot from a metre into the Chorrol Fighters Guild's chained doll
  (ChainDoll01, reference 0001DB3F): state 2, a hit record of kind 1. The
  reference's position moved between marks (7 -462 -533, 6 -460 -532,
  6 -457 -534): the arrow goes with the hanging part it sticks in.
- Shot level past the doll into the basement's far wall or floor: state 2,
  kind 3, lying loose.
- Shot steeply into the ground of the meadow at Tamriel -13 21 and into the
  harness save's stone floor: kind 3 both times. Which material the meadow's
  land answered was not read.
- The crosshair pick finds a stuck arrow (the HUD's target was the arrow's
  reference with the hand at it).
- The engine's grab on a stuck one (kind 1): the grab key down, nothing held
  ("the engine did NOT take it"), the arrow still there. On a bounced one
  (kind 3): held like any item, thrown at the release.
- The reference's activate on a stuck one (kind 1, state 2) answers taken;
  at the next mark the cell has no arrow projectile left.

## What a modern engine does instead

A projectile that has come to rest stops being a projectile: it is handed to
the item system as a pickup (or welded to what it hit as an attachment with a
"pull out" interaction) and the projectile object is recycled. Its physics
state then says what it is - static while stuck, dynamic once loose - instead
of one class carrying a private state word that every other system (the
grab, the pick, the hands) has to know about. Marked **derived**: common
practice, no single source checked.
