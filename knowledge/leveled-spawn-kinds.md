# Leveled spawns: telling a creature's or person's kind

Status: knowledge, written 2026-10-29 from Oblivion Walkers
(`../OblivionWalkers`, docs/walkers-spec.md 4.6a, src/CategoryLogic.h,
src/Replace.cpp). Walkers turns the spawns of chosen kinds into zombies -
"all skeletons", whatever a record is called - and switches them at
runtime by rewriting the leveled lists' entries.

Marks: **read** (in a file or a source), **measured** (in the game or on
its files), **derived**, **unverified**.

## Where the kind lives

- **Read (xOBSE `obse/GameForms.h`, llde/xOBSE master, 2026-10-29):**
  TESActorBase carries TESFullName at +0xA0 (its text +0xA4), TESModel at
  +0xAC (its nifPath BSStringT at +0xB0, the text first), TESScriptableForm
  at +0xC4. TESActorBaseData sits at +0x24; its faction list
  `FactionListEntry {data, next}` is inline at +0x18 of it (+0x3C of the
  actor base), data `{TESFaction*, rank}`. TESNPC's class pointer is at
  +0x104 - the same offset as TESCreature's type byte. TESClass' TESFullName
  at +0x18.
- **Measured (Oblivion.esm's records, `tools/esm-spawns.py`):** a creature's
  MODL is its skeleton, and its folder names the kind exactly: the leveled
  lists spawn from 39 folders under `Creatures\`; every skeleton (Skeleton,
  Champion, Guardian, Hero, Bregor the Cremator) is `Creatures\Skeleton\`.
  One folder holds two kinds: `Creatures\Dog\` is wolves and dogs (type
  creature) and the Shivering Isles' skinned hounds (type undead).
- **Measured:** a person's class does not name its kind - the 75 vampires of
  the lists have some twenty classes (Acrobat, Agent, Archer, ...), a Bandit
  Hedge Wizard is "Zealot Wizard". The factions do: every vampire is in
  VampireFaction (033F53), every bandit in BanditFaction (0177E3).
- **Measured (MOO's ESP):** a large overhaul keeps both: its creatures in
  `MOO\Skeleton\`, `MOO\Zombie\` (the last folder the same), its people in
  the game's factions (703 vampires, 229 bandits). MOO itself tells its
  zombies by the path `\Zombie\`.

## Switching a spawn at runtime (derived, built 2026-10-29)

- A TESLevCreature's entries `{ListData*, next}` start inline at +0x28;
  ListData `{level, pad, form, count}` (xOBSE). Pointing `form` at another
  leveled list makes the engine resolve that list in its place (measured
  2026-10-24, spawn-replace.txt). Walkers keeps a map ListData -> the form it
  named, so a later pass can point it back: the switch is reversible within
  a session without touching the records' files.
- **Unverified:** whether a reference that spawned from a list keeps its
  chosen base until its cell resets (Walkers assumes so - "already spawned
  ones stay"); not measured.

## What a modern engine does instead (derived)

- Tags on the record (Skyrim's keywords, Unreal's gameplay tags) instead of
  inferring a kind from a mesh folder or a faction; spawn tables filter by
  tag at spawn time rather than being rewritten in memory.
