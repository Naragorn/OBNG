# Repository instructions

## Quality and scope rules

- Prefer root-cause fixes over workarounds.
- Require hard, reproducible proof and state assumptions explicitly.
- Exercise every meaningful logic flow in tests, including relevant branch combinations, refusal paths, and fallback paths. Extract decision logic into testable units when needed.
- Preserve existing behavior unless a change is explicitly intended.
- Keep changes focused and minimal.
- Do not hide unexpected failures or claim coverage that was not run.
- Code, comments, documentation, and Git commit messages must be in English.

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
