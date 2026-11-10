# Repository instructions

## Quality and scope rules

- Prefer root-cause fixes over workarounds.
- Require hard, reproducible proof and state assumptions explicitly.
- Exercise every meaningful logic flow in tests, including relevant branch combinations, refusal paths, and fallback paths. Extract decision logic into testable units when needed.
- Preserve existing behavior unless a change is explicitly intended.
- Keep changes focused and minimal.
- Do not hide unexpected failures or claim coverage that was not run.
- Code, comments, documentation, and Git commit messages must be in English.

## Reading Oblivion.exe

Every function of the EXE is already decompiled in `re/export/rea-all/functions/0x<address>.c` (see `re/README.md`; on the shared disk only, not in Git). Grep it before starting a Ghidra or REA session; start one only for what the files do not answer (assembly, cross-references, data).

## What OBNG is

OBNG (Oblivion Next Generation) reverse engineers Oblivion.exe 1.2.0.416 (the
Steam GOTY build, md5 `cdd2f0c5eff198d4f26b7b5b54ce4930`, ImageBase 0x00400000)
and rebuilds its hot paths in modern C++, tested against the original, so the
game can later be routed through them. OBVR (`../OBVR`) is the VR mod and the
main source of measured engine facts; it stays a separate project.

## Evidence rules for reverse engineering

- Every claim carries one of four marks: **read** (seen in the binary, with the
  address), **measured** (seen in the running game, with the run), **derived**
  (follows from read or measured facts, say which), **unverified** (not checked).
  A claim without a mark is not a finding.
- An address goes into a header only when the disassembly of the actual binary
  and an independent source agree (xOBSE, TES Reloaded, OBVR's
  `GameAddresses.h`, or a second code path in the same binary). Say which two.
- Ghidra pseudocode is a reading aid, never the proof. When the pseudocode and
  the instructions disagree, the instructions win; say so in the finding.
- Names are proposals until a slice's refutation round confirms them. Keep the
  address next to every name, always.
- The Ghidra export under `re/export/` and the Ghidra project are local working
  copies of the binary and stay out of Git. What goes into Git is names,
  layouts, behaviour descriptions, OBNG's own code and its tests.
- A rebuilt function is **reconstructed** when its tests pass and
  **verified** only when the in-game differential check (original and rebuild
  run side by side, outputs compared) has run clean. The ledger keeps the two
  apart.
- The goal is a modernised engine, not a bit-identical one (decided
  2026-11-10). Bit identity is the gate where it is free and a tool
  everywhere else. Every rebuild is one of three classes, named in its
  ledger row, and each class has its own gate:
  - **A - the same computation, faster** (the skeleton update, the square
    root, the volatile pack): bit-identical in the shadow run -> **verified**,
    as above. Not given up: it is the only automatic finder of errors.
  - **B - another order, or several threads** (the actors' batch): not
    comparable with the original as a whole. Bit-identical per unit in a test
    (one unit, the same inputs, the same result), then in the game: the
    scenarios pass, a long run without a crash, Nadi's look in the headset,
    the peer sessions' regression tests -> **accepted**.
  - **C - a different computation on purpose** (SSE in a free order, a new
    algorithm): an exact version first as the reference (class A or the
    original), the fast one compared against it within a stated tolerance,
    then the game gates of class B -> **accepted**. The tolerance and what it
    was measured on go into the ledger row.
  Say which class a rebuild is before building it; a B or C needs no further
  announcement than that. "Looks the same in the game" alone is never a gate:
  it is not measurable and does not find rare errors.
- Every rebuild is written 64-bit clean from the first line (decided
  2026-10-30, so that each block becomes a part of a later 64-bit host without
  a rewrite): no assumption that a pointer is four bytes, engine addresses and
  engine object references only as explicit 32-bit handles (`std::uint32_t`)
  behind ports, sizes and offsets as `static_assert`ed constants, no raw
  engine pointers in the logic headers. The 32-bit engine-facing shape lives
  in the `<Name>.h` headers and the plugin, nowhere else.

## Where things live

- `knowledge/` - one file per engine topic, the long-lived findings; extend an
  existing file before starting a new one.
- `re/` - the reverse-engineering work in progress: `symbols/` (seed names
  from xOBSE and OBVR), `slices/` (one directory per worked slice with its
  manifest), `functions/` (one file per analysed function), `ledger.md`
  (state of every function touched), `export/` (Ghidra output, ignored).
- `src/engine/` - the rebuilt code, by subsystem; `tests/` - its tests, built
  32-bit with clang (`cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_CXX_FLAGS=-m32`).
- `tools/ghidra/` - the headless scripts that seed names and export slices;
  `tools/workflows/` - the agent workflow that works a slice
  (`docs/re-workflow.md` is its contract).

## Findings from the other projects

OBVR's and OblivionWalkers' `AGENTS.md` send engine findings here as they
come up. The same rule in reverse: when work here corrects something OBVR
relies on (an address, a layout, a cost), say so in the finding so OBVR can
pick it up; do not edit OBVR from here.
