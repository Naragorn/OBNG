# Oblivion gates: open, closed, where

Status: knowledge, written 2026-11-15 from the OblivionWalkers mod
(`../OblivionWalkers`, docs/walkers-spec.md 4.22, `src/GateLogic.h`,
`tools/oblivion-gates.py`). Read in the EXE and the ESM; **not yet seen in
the game** (no gate's flags watched while it opens or closes). Nothing of
this is built in OBNG.

## The task

Know at run time which of Tamriel's Oblivion gates stand open, without a
console line or a script per gate, so that something can come out of them.

## The destroyed flag (read)

- The console's command table holds GetDestroyed at 0x00B0E878 (CommandInfo:
  name 0x00A4EE98, opcode 0x10CB, needs a reference, no parameters, execute
  0x00505660, eval 0x004F82C0). The eval: result 0.0; with a reference,
  `(ref->flags (+0x08) >> 13) & 1` - 1.0 when set. **Destroyed is bit 13
  (0x2000) of TESForm's flags.**
- SetDestroyed at 0x00B0E8A0 (opcode 0x10CC, one parameter), execute
  0x00505680: parses the number, then 0x0046AA50 (thiscall, bool) sets or
  clears 0x2000 in +0x08 and calls vtable +0x40 on the form (the change
  marking, not read further).
- The disabled flag is 0x800 of the same word (as OblivionWalkers reads it).

## The gates (read off Oblivion.esm, 2026-11-13 and -15)

- 103 door references in the Tamriel worldspace (0x3C) whose base carries a
  gate script: 90 of the base `OblivionGatetoOblivion` (script
  `OblivionGateRandomScript` 00084B06), 6 on `MQ11OblivionGateScript` (the
  five cities of MQ11 and MS13's), 7 with quest scripts of their own (MQ10,
  MQ13's four, MS48, MS94). Every one has an enable parent (XESP); 8 are
  initially disabled in the file.
- A random gate opens in its door's OnLoad: under `MQ00.maxOpenGates` it
  rolls `MQ00.randomGateChance` and **enables its parent**. A gate closed
  by its sigil stone reads `GetDestroyed == 1`; the door stays enabled.
  `MQ00.openGates` only counts up ("ever opened").
- So, derived: **open = the door and its parent not disabled, the door not
  destroyed** - three flag reads per gate, by ID, no search.

## What a modern engine does instead

A gate is an entity with a state (closed, open, destroyed) and an event on
each change; whoever cares subscribes - no polling of flags, no script on
the door rolling at every load. Unverified as a comparison: no engine was
read for this.
