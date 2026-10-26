# The slice workflow

How a piece of Oblivion.exe gets from "21,240 unnamed functions" to "named,
described, rebuilt and tested". One run of `tools/workflows/re-slice.js`
works one **slice**: a handful to a few dozen functions that belong together
(the frame clock, the skinned stream copy, the palette builder and its
upload, ...). The architect runs the slices one after another and reads each
result before choosing the next.

Why slices, and why this shape:

- The binary has 21,240 functions (OBVR's `tools/oblivion-1.2.0.416-functions.tsv.gz`,
  Ghidra's table). Most are library and boilerplate code. OBNG wants the hot
  paths OBVR has measured, not everything.
- REA's Ghidra bridge serialises requests (one Program request at a time,
  deepwiki morluto/rea). Parallel workers would queue on it. So the slice is
  exported to files first and the workers read files.
- A finding that one agent produced and nobody tried to break is a guess
  with a confident tone. Every function gets three refuters with different
  lenses before anything is built on it.

## Roles

- **Architect** (the main session, Fable 5.1): chooses the slice, seeds the
  names, exports, writes the manifest and the shared layout headers, runs the
  workflow, merges the result, writes the ledger and the knowledge file.
- **Workers, refuters, reviewers**: the stages below, one agent per function
  and stage. They read the export and the repository; they do not call REA or
  Ghidra. Model and effort differ by stage (the `STAGES` table in
  `tools/workflows/re-slice.js`, overridable through `args.stages`): the
  money sits where a mistake costs the most downstream work.

| Stage | Model, effort | Why |
|---|---|---|
| map | Opus 5.5, high | a wrong map costs three refuters and a fix round |
| refute: instruction fidelity | Opus 5.5, high | the gate everything else rests on |
| refute: consistency | Sonnet 5.5, medium | search-shaped; a different model so the skeptics do not share one blind spot |
| refute: behaviour | Opus 5.5, medium | judgement over callers and strings |
| fix | Opus 5.5, high | must read the cited instructions |
| rebuild | Opus 5.5, high | code plus one test flow per branch |
| review: correctness | Opus 5.5, high | must find an input where rebuild and original differ |
| review: tests and code | Sonnet 5.5, medium | coverage and style, runs the test |
| build | Haiku 5.5, low | mechanical |
| build fix | Opus 5.5, high | fixes at the cause |

  These are the starting values (reasoning in the pilot's notes, not
  measured). The pilot's journal gives tokens and refutation rate per stage;
  a stage whose high-effort skeptics find nothing the medium ones miss goes
  down a level.

## Phase 0 - scope (architect, before the run)

1. Pick the slice and its functions. Entry points come from OBVR's measured
   hot spots (`../OBVR/docs/performance-spec.md`, `knowledge/`), xOBSE, or the
   previous slice's open questions.
2. Make sure the Ghidra project carries the seed names
   (`tools/ghidra/apply-symbols.sh`; the CSVs in `re/symbols/`).
3. Write `re/slices/<slice>/functions.txt` (one address a line) and run
   `tools/ghidra/export.sh <slice>` → `re/export/<slice>/<address>/`.
4. Write `re/slices/<slice>/manifest.json`: per function its address, the
   seed name, what is already known (with marks and sources), the questions
   the slice should answer; plus the subsystem name for `src/engine/` and
   the shared layout headers the architect wrote.
5. Run the workflow with the manifest as `args`.

## Stage 1 - map (one worker per function)

Reads `pseudo.c`, `asm.s`, `meta.json`, the manifest and, for
cross-reference, `re/symbols/*.csv`, `knowledge/*.md` and OBVR's
`GameAddresses.h`. Writes `re/functions/<address>.md`:

- identity (address, seed name, proposed name), purpose
- calling convention, `this` type, parameters with evidence
- every struct access as base, offset, size, proposed field, evidence
- the control flow in branches (the rebuild stage turns each into a test flow)
- claims, each marked **read / derived / unverified** with its evidence
- open questions, and layout fields it needs that a shared header lacks

Returns the same as a structured object.

## Stage 2 - refute (three skeptics per function)

Each skeptic gets the map result and the export and is told to break it.
Three lenses, so the three do not fail the same way:

- **instruction fidelity**: does every read-claim match the instructions -
  calling convention and stack cleanup, register use, signed/unsigned, float
  precision, the actual constants? Pseudocode is not evidence here.
- **consistency**: do the offsets and names agree with xOBSE, OBVR's
  `GameAddresses.h`, `knowledge/` and the other functions of the slice?
- **behaviour**: do the callers, callees, strings and constants carry the
  purpose claim, or does it rest on the name alone?

A function is **refuted** when two of three skeptics refute it. Then one
fix round: the worker gets the refutations and rewrites the file; two
skeptics check again. If one still refutes, the function is **disputed** in
the ledger and goes no further in this run. Nothing is dropped silently.

## Stage 3 - rebuild (one worker per confirmed function)

Tests first. The decision logic goes into pure functions over plain values
(`src/engine/<subsystem>/<Name>Logic.h`), each branch of the mapped control
flow is a test flow in `tests/<Name>Test.cpp`, refusal and fallback paths
included. The engine-facing shape (the layout structs, the call through the
original's convention) is kept apart from the logic. Workers never edit the
shared layout headers; missing fields go into the returned `layoutRequests`
for the architect to merge.

## Stage 4 - review (two reviewers per rebuild)

- **correctness**: the rebuild against the decompilate and the mapped claims
  - same branches, same arithmetic, same edge handling.
- **tests and code**: every flow exercised, nothing claimed that is not run,
  Clean Code, the repository's style.

One fix round when a reviewer rejects, then the rejecting lens reviews
again. Still rejected: **review-failed** in the ledger.

## Stage 5 - build (one agent, after all rebuilds)

`cmake -B build-tests tests -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 -G Ninja`,
build, `ctest --output-on-failure`. A failure gets one fix agent and one
more build. The result goes into the ledger as it is.

## Phase 6 - synthesis (architect, after the run)

Merge the layout requests into the shared headers, carry the confirmed names
back into the Ghidra project (a CSV under `re/symbols/`), extend the
knowledge file of the topic, update `re/ledger.md`, commit.

## The gate outside the workflow

A rebuilt function is **reconstructed** when its tests are green. It is
**verified** only after the in-game differential check: a detour runs the
original and the rebuild side by side and compares (OBVR's check mode for
the stream copy did 202,000 copies with 0 mismatches). The workflow builds
the comparison; the harness or Nadi runs it. The ledger keeps the two states
apart.

## Cost

Per function: 1 map + 3 refute (+1 fix +2 refute) + 1 rebuild + 2 review
(+1 fix +1 review), plus one build per run. Forty functions are roughly 260
agent runs. The pilot (the frame clock, about five functions) measures what
a function costs before the next slice is chosen.
