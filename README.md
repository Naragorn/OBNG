# OBNG - Oblivion Next Generation

Working name for a community modernisation of Oblivion.exe (2006, 1.2.0.416):
read the hot functions with Ghidra, rebuild them in C++, test them against
the original, route the game through them. Separate from OBVR (VR) and from
the mods.

The binary is the Steam GOTY build, md5 `cdd2f0c5eff198d4f26b7b5b54ce4930`,
ImageBase 0x00400000. Nothing here modifies it on disk.

## What is here

- `knowledge/` - one file per engine topic: what was read in the EXE
  (addresses), what was measured in the game, and what a modern engine does
  instead, with sources. The long-lived findings; most come from OBVR's work.
- `re/` - the reverse-engineering work in progress: seed symbols, slices,
  one file per analysed function, the ledger. See `re/README.md`.
- `src/engine/` - the rebuilt code, by subsystem.
- `tests/` - its tests, one executable per `*Test.cpp`, built 32-bit with
  clang:

  ```
  cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja
  cmake --build build-tests
  ctest --test-dir build-tests --output-on-failure
  ```

- `tools/ghidra/` - headless Ghidra scripts: `import.sh` creates the project
  once, `apply-symbols.sh` seeds the names from `re/symbols/`, `export.sh`
  writes a slice's functions to `re/export/` (ignored by Git),
  `headless.sh ReferencesTo.java <addr>` finds who uses an address,
  `headless.sh CreateFunction.java <addr>` defines a function Ghidra missed.
  Needs Ghidra 12.1.4 and a JDK 21; paths in `tools/ghidra/headless.sh`.
- `tools/workflows/re-slice.js` - the agent workflow that works one slice;
  `docs/re-workflow.md` is its contract.
- `AGENTS.md` - the rules: evidence marks, two sources per address, what
  stays out of Git.

## How a slice gets worked

The architect picks the slice and exports it, workers map each function,
three skeptics try to refute each mapping, confirmed functions are rebuilt
tests first, two reviewers try to reject each rebuild, one build runs
everything. A rebuilt function is *reconstructed* when its tests are green
and *verified* only after the in-game differential check. Details in
`docs/re-workflow.md`.
