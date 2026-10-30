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
