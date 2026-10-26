# City gates and defenders that never run out

Status: knowledge, written 2026-10-26 from the OblivionWalkers mod
(`../OblivionWalkers`, docs/walkers-spec.md 4.11b). Built there and run in
the game (harness) on 2026-10-26. Nothing of this is built in
OBNG.

## The task

Hostile groups drift toward a city and hold before its gate; defenders come
out of the gate, as many as the attackers ask for, and keep coming however
many fall; whoever the attackers kill gets up again later, so no quest ever
loses a person to them.

## Where the gates are (read)

- A city gate in Tamriel is a persistent DOOR reference in the Tamriel
  worldspace (form 0x3C) whose teleport (XTEL) leads into the city's own
  worldspace (AnvilWorld, BravilWorld, ...). 22 such doors in Oblivion.esm,
  the Imperial City's district gates among them (`src/CityGates.h`).
- The spot in front of a gate, on the ground, is the arrival of the
  city-side door's XTEL (the door that leads back out): XTEL = destination
  door form ID (4 bytes) + position and rotation (6 floats). It lies 44 to
  356 units off the Tamriel door and 170 to 300 units below it (the door's
  own position sits up in the gate's frame). Read with
  `tools/city-gates.py` (2026-10-26).

## How Oblivion forces it to be done (what the mod does)

- **No new actors at runtime.** PlaceAtMe's actors stay in the save for good
  (the CS wiki's PlaceAtMe page); an endless supply of defenders made that
  way grows the save without bound. The mod writes a fixed pool into its
  ESP instead: 21 persistent, initially disabled references of one
  guard record per city, in an unused interior (in banks of six, so a
  bigger pool keeps the form IDs of the smaller one and its saves). Sent out: `Enable`, `MoveTo
  <door> dx dy dz` onto the spot in front of the gate; the frame after,
  `Resurrect 0` if it lay dead, `ResetHealth`. Taken in: `Disable`. One who
  falls is replaced by a waiting one; with none waiting the dead are raised
  again - the pool is the whole supply.
- **Disabled is a reference flag:** form flags (+0x08) bit 0x800
  (TESObjectREFR::kFlags_Disabled, xOBSE GameObjects.h IsDisabled). After a
  load the pool's state is read back off the world from it.
- **Health on the record, not SetAV.** SetActorValue changes the base
  actor's value, every reference of it (the CS wiki's SetActorValue page).
  The mod writes the guard record's TESHealthForm (TESActorBase +0x84)
  before each sending. GetBaseActorValue (0x005F1910) calls 0x005EAD00,
  which reads the reference's base form (actor vtable +0x170) and asks the
  base's own value through the base's vtable +0x128 (read with REA,
  2026-10-26); for these auto-calc guards it answers from +0x84 (measured,
  below).
- **A fight on order:** console `StartCombat <target>` on the defender (the
  CS wiki: "The actor will always keep the target actor at the top of its
  target list").
- **The safeguard:** xOBSE's OnDeath hands the killer; a victim of the
  attackers goes on a list in the co-save and gets up through the actor's
  Resurrect (vtable +0x20C, (1, has3D, animate)) once dead long enough, its
  body loaded and no attacker near.

- **A follow package aimed at its own runner froze the game** (derived,
  2026-10-26: twice the main thread stopped right after a step that aimed
  a group's follow package - a package of the ESP whose target the plugin
  writes, TESPackage +0x28 - at a leader that still ran that same package,
  or leaders following each other round; with the aim held back until the
  leader runs no follow package, three runs of three went through). Not
  read in the engine where it loops.
- **Measured:** the guard record's +0x84 written to 60 (and to 2) read back
  as base and current health of the sent guards, auto-calc guards included
  (GetBaseActorValue / GetActorValue_F, 2026-10-26).

## What a new engine should do instead

- Spawn and despawn defenders as transient actors that never enter the save
  - a save should hold the *state* of a siege (how many defenders a gate
  has left to give, if any limit), not every body ever made.
- Give an actor a per-instance health maximum; health set on the shared
  record changes every instance of it.
- Keep a door's "outside" spot as data on the door (Oblivion has it only on
  the other door's teleport).
- Let a quest mark the people it needs; a system that kills at large (an
  apocalypse) can then leave them alone or restore them, instead of
  restoring everyone it killed.

## Sources

- `../OblivionWalkers/src/CityGates.h`, `tools/city-gates.py`,
  `src/GuardLogic.h`, `src/Horde.cpp`, `src/SafeguardLogic.h`,
  `src/Rise.cpp` (2026-10-26).
- cs.uesp.net/wiki/PlaceAtMe, /SetActorValue, /Resurrect, /StartCombat,
  /ResetHealth (read 2026-10-26).
- deepwiki llde/xOBSE: TESObjectREFR::kFlags_Disabled = 0x800.
- OBVR `src/game/EnemyHealth.cpp` (GetActorValue_F, GetBaseActorValue).
