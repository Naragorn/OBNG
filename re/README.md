# Reverse-engineering work in progress

`knowledge/` holds what is settled; this directory holds the work that gets it
there. `docs/re-workflow.md` describes the process; `AGENTS.md` the evidence
rules.

- `symbols/` - seed names tied to addresses, one CSV per source
  (`xobse.csv`, `obvr.csv`). Applied to the Ghidra project by
  `tools/ghidra/apply-symbols.sh` so the export reads `TESObjectREFR::...`
  instead of `FUN_...`. Columns: `address,name,kind,confidence,conflict,source,...`.
- `slices/<name>/manifest.json` - the functions of one slice, what is already
  known about each, and the questions the slice should answer. Written by the
  architect before a run.
- `functions/<address>.md` - one file per analysed function: purpose, calling
  convention, parameters, every struct access as offset and proposed field,
  each claim marked read / derived / unverified with its evidence. Written by
  the map stage, corrected by the fix stage, confirmed or disputed by the
  refutation stage.
- `ledger.md` - every function touched, its state (mapped, refuted, disputed,
  reconstructed, verified) and the run that set it.
- `export/` - the Ghidra output per function (`pseudo.c`, `asm.s`,
  `meta.json`). Ignored by Git; regenerate with `tools/ghidra/export.sh`.
