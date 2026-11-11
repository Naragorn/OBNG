# Memory and the 32-bit address space

Status: knowledge, written 2026-10-30 from one measured run and the two
installs' PE headers. Marks: **read** (file), **measured** (the running
game), **derived**.

- **The two Oblivion.exe differ** (read, md5 2026-10-30): the Linux install's
  is the reference `cdd2f0c5eff198d4f26b7b5b54ce4930` and is **not**
  large-address-aware (PE characteristics without 0x20); the Windows
  install's is `66a6c2ef97ddea75f43d916aca8d164d`, large-address-aware (the
  4 GB patch). Every address OBNG uses is the same in both (the patch only
  flips the header flag - derived from the flag being the only known
  difference; a byte diff of the two files would settle it).
- **Under Proton the game has 4 GB anyway** (measured, OBNG.dll's memory
  probe, `artifacts/shadow/obng-scene-pass/20261030-101556`):
  `GlobalMemoryStatusEx` reports 4095 MB of user address space for the
  non-LAA image, and the process used **2 136 MB at its peak** (52 %) with
  the prison, eighteen Walkers and this install's mod load - above the 2 GB
  a non-LAA process gets on Windows. So on Linux the large-address flag is
  not what bounds the game; Wine grants the space regardless (observed,
  not read in Wine's source).
- Commit charge stayed at 360-470 MB while the working set was 1.5-2.1 GB:
  under Wine `K32GetProcessMemoryInfo`'s PagefileUsage does not count the
  mapped files (BSAs, textures) that make up most of the working set; the
  address-space number is the one to watch for the "4 GB" question.
- What this means for the 64-bit plan (derived): with this mod load the
  Linux game sits at half of its address space; the wall is a Windows,
  non-LAA problem first, and a texture-mod problem second. Before any
  64-bit host work, measure the same probe with Nadi's full Windows mod
  list (the Windows OBNG.dll writes the same `Probe: memory` line).

## The game heap in a crowd: a fill to a ceiling, not a leak (2026-11-11)

Runs: obng-actors-count with the weather pinned (`set timescale to 0`,
`set gamehour to 12`, `fw 00038EEE`), every OBNG route off
(`tools/hand-scripts/obng-measure-heap-plain.ini`), OBNG.dll `521afa48`,
OBVR.dll `f21ff702`, LINK++ (`LINK.esp` `bb847376`), MoreHeap
DefaultHeapSize 1024 MB on Linux. The heap is MemoryHeap 0x00B02020
(+0x10 top, +0x1C blocks), one `Probe: game heap` line every 10 s.

| Run (artifacts/shadow/...) | Variant | At the full count (about 100 placed NPCs) |
| --- | --- | --- |
| heap-a-baseline/20261111-113811 | all on, 60 s at the full count | still +93 000 blocks, +8-9 MB per 10 s at the end (1.79 M blocks, 450 MB) |
| heap-a-long/20261111-114459 | all on, 180 s at the full count | grows on at that rate, then flat from about 2.10 M blocks, 488 MB |
| heap-c-walkers-off/20261111-114130 | Walkers DLL and esp out | faster (up to +286 000 blocks per 10 s), flat from about 2.09 M blocks, 481 MB |
| heap-b2-long/20261111-115020 | OBVR minimal: Hands off, Body off, Stereo none | flat from about 2.05 M blocks, 469 MB |

- **Measured:** in every variant the heap fills to a ceiling and stays
  there: about 2.04-2.10 M blocks and 469-488 MB with about 100 placed
  NPCs. Nothing grows without bound. Yesterday's "95 000 blocks every
  10 s at a constant count" (obng-actors-count/20261110-225506, -225751)
  is the middle of that fill: those runs ended 20 s after the last NPCs
  came, before the ceiling.
- **Derived:** the fill is the engine's, for the crowd it was given. Its
  speed follows the frame: without Walkers the frame is lighter and the
  fill finishes sooner, with all of OBVR it is slowest. OBVR's share of
  the ceiling is about 19 MB and 60 000 blocks (488 against 469 MB, one
  run each, so the size of that difference is not settled). Walkers
  changes the speed, not the ceiling (481 against 488 MB).
- **Derived, for the Windows crashes of 2026-11-10** (heap 91 % and 99 %
  of 512 MB): a ceiling near 470-490 MB in a crowd of about 100 leaves
  almost nothing of a 512 MB heap. 1024 MB has room for it. Whether the
  Windows crowds were that size was not measured.
- Not run: OBVR with only Full VR off ([Hands] Enabled=0 alone); the
  engine without OBVR (OBVR's harness drives the scenario, and
  `[Camera] HookEnabled=0` stops the harness too); which allocation sites
  make up the fill (`[Probe] HeapCalls`).
