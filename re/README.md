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
- `export/rea-all/` - the whole EXE decompiled once: every function's
  Ghidra pseudocode as `functions/0x<address>.c` (21,240 files, 205 MB),
  `decompile-index.json` with each function's caveats, `decompile-journal.jsonl`.
  Made on 2026-10-28 with REA 6.8.0 and Ghidra 12.1.4 in 27 minutes:
  `rea decompile-all <Oblivion.exe> --output re/export/rea-all --provider ghidra`
  (`--resume` continues a cut run). Ignored by Git, so it exists only on the
  shared Modding disk: `D:\Modding\OBNG\re\export\rea-all` on Windows,
  `/run/media/nadi/Games/Modding/OBNG/re/export/rea-all` on Linux.
  **Grep it first** for any question about the EXE (`grep -l "magicNode"
  functions/*.c`, or open `functions/0x69a060.c` by address); start a Ghidra
  session (REA MCP `open_binary`, about 3 minutes of analysis) only for what
  it does not answer - assembly, cross-references, data. Names are Ghidra's
  (`FUN_...`), not the seed symbols; a REA snapshot (`close_binary
  snapshot_path`) saves no analysis time (measured: 0 reusable entries).
