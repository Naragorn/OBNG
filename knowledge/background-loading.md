# Background loading: the IO thread, its queues, and where the main thread waits

Status: knowledge, written 2026-11-03 from the decompile
(`re/export/rea-all/functions/`), the EXE's vtables (read from the file
with a PE parser) and four OBVR captures. Nothing of this is built in OBNG.

Marks: **read** = read in the binary, **measured** = measured in the game,
**derived** = follows from the two, **unverified** = not checked.

## What the engine has (read)

- Oblivion.exe 1.2.416 already loads in the background. The system is
  Bethesda's `BSTaskManager` with `IOManager` on top of it, and xOBSE
  names the same classes (llde/xOBSE `GameTasks.h`: `BSTask`, `IOTask`,
  `QueuedFile`, `QueuedFileEntry`, `QueuedModel`, `QueuedKF`,
  `QueuedReference`, `QueuedHead`, `QueuedHelmet`; `g_ioManager` at
  0x00B33A10, `g_modelLoader` at 0x00B33A1C, `ModelLoader::QueueReference`
  at 0x00438060).
- **One worker thread.** The `IOManager` constructor 0x00434480 calls the
  `BSTaskManager` constructor 0x00434230 with `(2, 1, 0x12)`: the second
  argument is the thread count (the loop in 0x00434230 creates that many
  `BSTaskManagerThread` objects, string "BSTaskManagerThread", through the
  `BSThread` constructor 0x00430E00), the third is the queue count, 18 =
  6 priorities x {textures, meshes, misc} (the status dump 0x00435600
  prints "Priority %i - Textures: %3i Meshes: %3i Misc: %3i" for six rows
  and "Total BSTask Count: %d" from 0x00B33A20).
- `BSThread` (0x00430E00) creates its thread suspended (`CreateThread`
  flag 4) with two semaphores and no `SetThreadPriority`, so the IO thread
  runs at normal priority as far as the constructor shows (where else the
  priority could be set is unverified). The thread with
  "Could not create a thread for background loading." and
  `SetThreadPriority(-1)` (0x0047CFD0) is **not** the IO thread: its
  caller 0x0042FA10 is reached from 0x00682500, the "PathBuilder". It is
  the pathing thread.
- **The worker loop is 0x00433BC0**: `WaitForSingleObject` on the manager's
  semaphore, pop a task with 0x00433760 (walks the 18 queues by priority),
  move the task's state from 1 to 3 with `InterlockedCompareExchange`,
  then call the manager's virtual +0x40 and +0x44 on it (run and finish),
  release the semaphore, drop the task's reference count. The CAS and the
  per-thread TLS in 0x00433760 are written for N threads; whether the task
  code itself is safe for more than one worker is unverified.
- **`QueuedModel`'s vtable 0x00A36B6C** (read from the file): slot 0
  0x004393B0 (destroy), slot 1 0x00439400 (`Run`), slot 2 0x004395D0
  (finish). `Run` looks the path up in the model cache, opens the file or
  the BSA entry (0x0042E8E0 / 0x0042EBC0; the log line "Queued %s with file
  entry offset %i and size %i" is built in 0x00434D40) and parses the NIF
  (0x006F9980, which reaches the Gamebryo header reader 0x00713180). So the
  **NIF parse runs on the IO thread** when the model is queued.
- **The same `Run` runs synchronously on the caller's thread** when the
  `ModelLoader` lookup 0x00439EB0 / 0x00439FF0 misses the cache: it takes
  the manager lock (0x00432860, skipped on the IO thread itself), builds
  the `QueuedModel` (0x00437250), calls 0x00439400 and 0x004395D0 inline,
  and unlocks. 23 functions call 0x00439EB0 (equip, spawn, and the like).
- **The main thread's wait is 0x00434020**: it sums the queued task counts
  of all priorities up to its argument, shows the loading progress
  (0x0057B950 / 0x004523A0 with a percentage), and loops with
  `Sleep(50 ms)` while anything is left. Callers pass priority 5 (the
  loading screen path 0x0066EAF0, 0x0045FDA0, 0x004DF1A0, 0x005C2F20,
  0x0044E720, 0x004433A0, 0x004455E0) or 1 and 4 (0x00445A10, 0x00671620,
  0x00444C70). The 50 ms sleep means a load ends up to 50 ms after the IO
  thread is done.
- The allocator behind everything is `MemoryHeap` (0x00402480, string
  "MemoryHeap::MemoryProblem").

## What was measured (OBVR captures, the main thread sampled about every millisecond)

Script: OBVR job `13865cd6/tmp/longframes2.py`; frames of 20 ms and more,
every sample's eip and eight stack words resolved to engine functions.

| capture | long frames | of which: main thread waiting in 0x00434020 | NIF parse on the main thread (0x00439400, 0x006F9980, 0x00713180) | scripts | Havok lock sites | draw dispatcher 0x007693E0 |
|---|---|---|---|---|---|---|
| house exit, Linux 2026-10-25 (`perf-cell-border`) | 40 frames, 2.8 s (the loading screen) | 38.5 % (~1.1 s) | 0.0 % | 2.4 % | 5.7 % | 1.4 % |
| border walk, Linux 2026-10-25 (`perf-cell-walk`) | 12 frames, 0.39 s | 0.0 % | 0.0 % | 21 % | 31.5 % | 2 % |
| Tamriel outdoors, Windows 2026-11-02 (`OBVR-performance-1-23`, 142 s) | 1343 frames, 41.7 s | 4.7 % (~2.0 s) | 0.0 % | 0.5 % | 0.2 % | 21.5 % |
| interior, Windows 2026-11-02 (`OBVR-performance-1-22`, 111 s) | 465 frames, 10.4 s | 0.0 % | 0.0 % | 0.9 % | 0.2 % | 21.7 % |

- **The main thread does not parse models or textures itself** in any of
  the four captures (0.0 % of the long-frame samples sit in the parse or
  in the synchronous `ModelLoader` miss path). The engine's IO thread does
  that work (measured; that it is the IO thread follows from the vtable,
  derived).
- **Where the main thread waits for the loader** is the drain loop
  0x00434020: the whole loading screen on the house exit (about 1.1 s of
  the 2.8 s of long frames; the rest of the loading screen's time is in
  the progress draw and the SteamVR resync), and about 2 s per 142 s
  outdoors in Tamriel (frames of 225 and 240 ms with 45 % of their samples
  in the drain loop). So a faster loader shortens loading screens and the
  outdoor "loading area" pauses, nothing else.
- **The other long frames are not loading.** In the Tamriel capture the
  three longest frames (651, 540 and 486 ms at +15 s, +34 s and +106 s)
  are saves: 0x00466B70 ("autosave") and 0x00465130 (`SaveGame`, "Could not
  create save buffer, out of memory."), 60-75 % of their samples in an
  ntdll wait. Three saves in 142 s point to a timed autosave mod
  (unverified which). The next three (445, 395 and 286 ms at +35-36 s)
  are the exterior attach after the save load: 0x00680110, 0x0067F830
  ("Failed to find coord for space."), 0x0068AB20, with the `MemoryHeap`
  allocator at the top of the exclusive list. At cell borders the long
  frames are Havok lock waits and mod scripts (see
  `cell-change-hitches.md`). Steady outdoor long frames are the draw
  (the dispatcher 0x007693E0 on the stack in 21.5 %, see
  `culling-and-draw-setup.md`).
- **Not measured:** what the IO thread itself does with its time (CPU in
  the parse, the disk, or waiting for the manager lock against the main
  thread). OBVR's sampler reads only the main thread.

## What this means for a "streaming loader like Skyrim's" (derived)

- The architecture the idea describes - worker threads read BSA, NIF and
  textures, the main thread receives finished objects - is what Oblivion
  has, with one worker. Rebuilding it buys nothing by itself; the
  measurable headroom is the drain-loop time, and that is bounded by the
  single IO thread's throughput and by whatever the main thread does while
  it waits.
- The cheapest experiment is the thread count: the constant 1 in
  0x00434480's call to 0x00434230 is a one-byte patch. Whether `QueuedModel`,
  the Gamebryo streams, `MemoryHeap` and the model cache survive two
  workers is unverified; a crash would say so within a loading screen.
- Before that, sample the IO thread: if it is bound by the disk (BSA
  reads), more threads will not help and a prefetch (queue the next cells'
  references earlier, which `ModelLoader::QueueReference` already allows)
  would; if it is bound by the parse, threads help; if it waits on the
  manager lock, the lock is the fix.
- What a modern engine does instead: a job system with N workers, a
  per-frame budget for the main-thread apply (attach, D3D resource
  creation), lock-free completion queues, and no spin-with-Sleep(50) wait.
  Skyrim's loader was not read for this note; the claim that it works that
  way is the common description, unverified here.

## Addendum 2026-11-03: the long frames partitioned by OBVR's spans, and the allocator (measured, read)

The inclusive columns above overlap (a sample counts for every function on
its eight stack words), so they are not a partition. Partitioned by OBVR's
own spans, the 41.7 s of long frames in the Tamriel capture
(`OBVR-performance-1-23`, script `13865cd6/tmp/spans.py`) are:

| part | share | time |
|---|---|---|
| the two scene passes (draw, 0x007693E0 on the stack in 52 % of them, DXVK's d3d9 16 % of the eips) | 37 % | 15.4 s |
| engine logic outside OBVR's spans | 37 % | 15.3 s |
| waiting for SteamVR's poses (`NtWaitForSingleObject`, the compositor holding a late frame; a consequence, not a cause) | 21 % | 8.8 s |
| present | 5 % | 2.1 s |

Inside the engine-logic part (`engine_rest.py`): the IO drain loop 13 %
(its `Sleep(50)` is `NtDelayExecution`), the allocator about 20 %
(`MemoryHeap::Allocate` 0x00401AA0 and its small-block pool 0x00402480 at
the top of the exclusive list, `__VEC_memzero` 10 % inclusive, plus
critical-section waits), the exterior attach after a load 9 %
(0x00680110, 0x0067F830), the autosave 7.5 % plus `NtWriteFile` 6 %, the
`BSTaskManager` lock wait 0x004322B0 2.5 %, Havok and scripts about 1 %.

- **The saves are vanilla door autosaves, not a timer mod** (derived):
  the capture's OBVR log shows three cell loads during the capture
  ("Refs: a load runs", worlds 382, 498, 613) and the three save frames
  sit at +15 s, +34 s and +106 s; Oblivion.ini has
  `bSaveOnInteriorExteriorSwitch=1` (read). Timestamps are not in the log,
  so the pairing rests on the counts.
- **`MemoryHeap` (read, 0x00401AA0):** one global `CRITICAL_SECTION`
  ("MemoryHeap::Allocate") around every allocation; requests up to 0x200
  bytes go to a small-block pool of 4 KB pages (`VirtualAlloc`, 0x00402480,
  which also calls `GlobalMemoryStatus` when a page is added); larger ones
  walk free lists by size class. The IO thread allocates through the same
  lock while it parses, so main-thread waits in this lock are IO-thread
  contention (derived; the sampler did not read the IO thread).
  `OblivionPerformanceRuntime.ini` only turns on the low-fragmentation
  heap of the CRT heap (`LowFragmentationHeap=1`), which this pool never
  uses except as the `_malloc` fallback; Oblivion Stutter Remover (whose
  `bReplaceHeap` replaces this allocator) is not installed on the Windows
  test system (read, the plugins folder).
- What a modern engine does instead: a thread-local allocator with no
  global lock (mimalloc, tbbmalloc, jemalloc) and no memset of pool pages
  on the hot path.

## Addendum 2026-11-16: the main thread's own loads pause the IO workers (read; not timed apart)

Why the main thread stands in the task manager's pause 0x004322B0 while the player travels (23 % of the long
frames' samples in a cold ride, 4 % in a warm one - docs/performance-backlog.md, "the ride towards Anvil").
R = read in the instructions (tools/re/disasm.py on the EXE of md5 cdd2f0c5), Rp = read in the rea-all pseudocode
only.

- **A cache miss off the manager's thread loads at once, between a pause and a resume** (R): the texture load
  0x00442890 ("TES::CreateTextureImage unable to create image" is its string) looks the path up in the map
  [0x00B35300]; on a miss it compares GetCurrentThreadId with [[0x00B33A10]+0x30] (0x00442960-66), calls the pause
  0x00432860 when they differ (0x0044296D), loads through 0x00701E00 and 0x0075FA90, and resumes with 0x00432890
  (0x0044299C). The model loader's lookup 0x00439FF0 has the same shape (the compare 0x0043A046, the pause
  0x0043A04D, the load 0x004377D0 / 0x00439940 / 0x004378F0, the resume 0x0043A088); 0x00439EB0 is its twin (Rp).
- **The pause waits for every worker's running task** (Rp, 0x004322B0): the first holder sleeps while [+0x1C] is
  non-zero, then for each worker releases its semaphore if it idles and waits on its event without a timeout. So a
  single texture missed on the main thread costs the rest of whatever the IO worker is parsing.
- **Who misses on the ride** (Rp, named by their strings; the sampler's chains of run 20261113-142230):
  - the textures through 0x0052D2C0 - an actor's skin parts ("Textures\%s", "%08XModUpperBodyFemale",
    "%08XModBody", "Meshes\%s"), called from 0x0047AC20 ("skin", "UpperBody", "LowerBody", "Hand", "Foot", "Tail");
  - the models through 0x004E3490 (an actor's animation set up: "Skeleton", "\Idle.KF", "\Death.KF", "Bad
    InitAnimation for NPC"), 0x0041A610 ("Skeleton", "SpecialAnims") and 0x00475D80 (bone names).
  Derived: when an actor gets its 3D, its animation files and its body's textures are not among what the
  background loader queued for it; the main thread fetches them itself and stops the workers for each.
- The pause has ten callers in all (Rp: 0x00439FF0, 0x00439EB0, 0x00442890, 0x0043E0F0, 0x00444FB0 - the grid's
  move -, 0x00465130 / 0x00464AB0 / 0x00465860 - the save game -, 0x004F2630, 0x005AD980); the texture load has
  nineteen callers, the two model lookups twenty-nine files that call them.
- **Measured 2026-11-16 (Linux, two rides, the addendum below)**: the pause by caller in a ride's long frames. Still
  not measured: on Windows, and in play on the ground. Unverified: that [mgr+0x30] is the IO thread's id (the
  sampler shows the main thread taking the pause, so it is not the main thread's - derived).
- Levers (proposals, nothing built): queue an actor's animation files and skin textures with its models, so the
  main thread's lookups hit the cache (class B); or let a main-thread miss wait only for the lock the load needs
  instead of every worker's running task (class B, needs the read of what the pause protects).

## Addendum 2026-11-16 evening: the ride's long frames by their timed parts (measured)

Runs `artifacts/shadow/obng-profile-ride-anvil/20261116-164227` and `-165629`: Linux, the Beyond at 75 Hz, the Dream
Air's frame size, the Windows mod list (51 plugins, no Oblivion Walkers), OBNG.dll ffab3813 (69b0c97), OBVR.dll
2ab65b6c, `tools/hand-scripts/obng-measure-baseline-loads.ini` ([Timed] on the pause 0x004322B0, the texture load
0x00442890, the model lookups 0x00439FF0 / 0x00439EB0, the skin textures 0x0052D2C0, an actor's animation set up
0x004E3490, the skin parts 0x0047AC20, the drain loop 0x00434020, TES's update 0x00444FB0, the grid's update
0x004823D0, the process manager; a spike line for every update step over 20 ms). A flight of 120 s west from the
Gold Road at speed 200 with collision off (`obng-profile-ride-anvil.txt`), the machine quiet (load under 1.6).

| | run -164227 | run -165629 |
|---|---:|---:|
| world frames over 20 ms / over 40 ms (of about 8 200) | 20 / 6 (680 ms) | 22 / 6 (749 ms) |
| update steps over 20 ms | 24 | 28 |
| - TES's update 0x00444FB0 holds them (12-33 ms a call) | 10 (329 ms) | 10 (329 ms) |
| - an actor's 3D made on the main thread (0x004E3490: 24-72 ms a call) | 2 (145 ms) | 5 (322 ms) |
| - the render's frame function (22-44 ms; the wait for poses is in it) | 5 (139 ms) | 2 (75 ms) |
| - none of the timed functions (20-73 ms) | 7 (214 ms) | 11 (258 ms) |
| the pause 0x004322B0 inside those steps | 35 ms | 98 ms |

- **The pause by caller** (the spike lines): 31.2 of a texture load's 39.1 ms, 12.7 of 27.4 ms, 16.6 of 26.3 ms; 68.7
  of a model lookup's (0x00439FF0) 71.1 ms; in three more actor loads 0 to 3.5 ms. So the pause is most of a load
  when a worker is in a long task, and nothing when the workers idle - as the read of 0x004322B0 has it. It is in
  no step that TES's update holds (the grid's move 0x00444FB0 is among its callers; its long calls here were not
  the pause).
- **An actor's 3D without the pause still costs 24 to 53 ms** in six of the seven loads (0x004E3490 less the
  pause; the seventh was a model lookup that was all pause): the skin parts 0x0047AC20 (19-62 ms a step) with the
  skin textures under them (13-41 ms, of it texture loads of 11-39 ms). The lever "queue an actor's files with its models" takes these whole; the lever "let a miss wait only
  for the lock it needs" takes the pause's share alone (35 and 98 ms of 145 and 322 ms).
- **TES's update is the larger part by count** and is not read: ten calls of 12 to 33 ms a ride. The grid's update
  0x004823D0 is not it (under 1.1 ms a call). What 0x00444FB0 does in those calls is the next read.
- A fourth of the long steps hold none of the timed functions (20 to 73 ms): not named.
- **Void**: the four rides of 2026-11-16 before these (12:17, 12:21, 12:27, 15:39) flew a paused world - a mod's
  scroll (LoversHooker.esp) opened after the script's CloseAllMenus and stayed (97.5 % of their frames
  `live_menu` in frames.csv). Their long frames were the first seconds after the placement. The script closes
  menus at the ride's marks now.


## Addendum 2026-11-18: the grid's move with the player 0x00444FB0, by its callees (read, measured)

What it is (read in the rea-all pseudocode, 0x00444FB0, called once a frame from the update step 0x0040D800 with the
player's position; names are proposals): while the player stays in his cell it only queues the next row of cells
for the background loader as he nears the edge (0x004BE090, read `bBackgroundCellLoads` 0x00B08960). When he
crosses into a new cell it pauses the IO workers (0x00432860), shifts both grids - the near cells' (GridCellArray
slot 0x10, 0x00482090) and the far cells' (GridDistantArray slot 0x10, 0x00483B60; both from their vtables
0x00A3D79C / 0x00A3D7E0, found through RTTI) -, brackets them with Havok calls (0x0088D1D0, 0x0088BD60), attaches
the new cells (0x00444340), purges (0x0043FC20, below), and resumes the workers (0x00432890). When the new cells are
not loaded yet, 0x00440C40 starts a fade (0x0057ACC0 with 0.1) and renders a frame of its own (0x0040D4D0).

Measured: four rides (`artifacts/shadow/obng-profile-ride-anvil/20261118-140656`, `-141027`, `-142518`, `-142849`;
Linux without SteamVR, the stand-in Beyond at 75 Hz, `--exclusive`, the Windows mod list without Oblivion Walkers,
OBNG.dll ffab3813, `tools/hand-scripts/obng-measure-grid-move.ini`: [Timed] on every callee a five-byte jump can
take). Every ride crosses ten cells; every crossing is one update step of 26 to 83 ms, the grid's move 12.3 to 57.5
ms of it:

| part (per crossing, from the spike lines) | ms |
|---|---:|
| the cells' attach 0x00444340 | 6.8 - 25.0 |
| - of it its own callees timed: the cell attach 0x004441A0, 0x0043FED0, 0x004D5BD0, the Ni updates 0x00707610 / 0x00707980 | 1 - 4 (once 17) |
| - the rest, untimed: the far grid's update 0x00483D60 (by the render sampler: 4 to 7 of 6 to 13 samples a ride under 0x00444340 have it below, most with eip in 0x007B3A40) | the remainder |
| the purge 0x0043FC20 (in every second to third crossing) | 1 - 42 |
| - of it `MemoryPool::FreeUnusedPagesForAllPools()` 0x00402740 (its string) | 1.2 - 19.4 |
| - of it 0x004A25F0 | 2.2 - 5.5 |
| the two grids' shift (0x00482090, 0x00483B60) | 1 - 3.4 |
| Havok (0x0088D1D0, 0x0088BD60) | under 5 |
| the render frame inside the step (the fade's own frame, 0x00440C40, called once a ride: 0.3 ms) | 3 - 10, once 35 |

- **The far grid's update 0x00483D60** (read): for every far cell it walks, a cell that leaves goes to 0x007B3A40,
  which walks every bucket of a global map (0x00B2C340 the bucket count, 0x00B2C344 the buckets, 0x00B2C348 the
  count) and drops the cell from the lists of the entries that hold it; a cell that comes goes to 0x004BD430.
- **Measured, the third round** (rides `20261118-220557`, `-220929`, the INI's F38-F41): per crossing 0x00483D60 takes
  6.4 to 14.6 ms, of it **0x004BD430 5.4 to 13.3 ms** (156 calls a crossing, 1 712 in eleven; 120 to 160 us a call),
  the map's scan 0x007B3A40 only 0.9 to 2.4 ms (670 calls a crossing, 2 us each) - the sampler's eips in 0x007B3A40
  had pointed the wrong way. 0x00802A60 0.1 ms.
- **0x004BD430, a far cell given its LOD** (read; the name a proposal): allocates a task 0x004BCE00 with two new
  nodes (0x0075FA90 twice), then either (the byte 0x00B09DB0 set; which setting it is, is not read) formats
  "DistantLOD\\%s_%i_%i.lod" with sprintf and asks the file system 0x00B33A04 (vtable +4) whether the file exists
  before it makes the load task 0x004BCAC0, or makes the task 0x004BCA90; then 0x004BCB70 and the IO manager's queue
  ([0x00B33A10] vtable +0x3C). So the main thread allocates, formats and looks a file up for every far cell that
  comes into the grid, and the loading itself goes to the IO thread.
- **Measured, the fourth round** (rides `20261118-221839`, `-222208`; the INI's F32-F40): **the file lookup is all of
  it.** `FileFinder::FindFile` 0x00431020 (FileFinder's vtable 0x00A36414 slot 1, by RTTI; the object at 0x00B33A04)
  takes 4.7 to 11.7 ms of 0x004BD430's 4.8 to 11.9 ms a crossing. Everything else in it is under 0.2 ms a crossing
  (the task 0x004BCE00, the LOD file task 0x004BCAC0, 0x004BCB70, 0x004BD230, 0x004BDE80, 0x004F0DA0; 0x004BCA90 was
  not called - the byte 0x00B09DB0 is set in this install).
- **FindFile is slow everywhere, not only here**: 13 400 and 13 600 calls a run (placement, loads and the ride),
  1.44 and 1.46 s in all - 105 to 107 us a call. Of the render sampler's 771 samples with a return address inside
  0x00431020-0x004311FF on the stack, 752 (97.5 %) have their eip in Wine's ntdll.dll (0x7BEF0000): the time is the
  operating system's file query that FindFile makes through FileFinder's vtable slot 0x0C (`call eax` at
  0x004310BF; which import it reaches is not read - the EXE imports GetFileAttributesA, GetFileAttributesExA,
  FindFirstFileA, CreateFileA). Derived: under Proton a loose-file query costs about 0.1 ms; **on Windows it is not
  measured** and is probably much cheaper (unverified), so the size of this lever on Nadi's Windows game is open.
- Lever (proposal, nothing built, class A): FindFile's answers kept by path - the Data folder does not change while
  the game runs (unverified for a mod that writes files it reads back) - so each path is asked of the OS once. Gate:
  the shadow run, the original's answer against the kept one for every call. Gain on Linux: the 5 to 12 ms of every
  crossing and most of 1.4 s a run in loads; on Windows to be measured first (the same INI and `[Timed]` line in the
  Windows game's OBNG.ini).
- **The purge 0x0043FC20** (read): under the critical section 0x00B35380, with the renderer's lock 0x00977C50, it
  frees the memory pools' unused pages on the main thread at a cell crossing.
- Levers (proposals, nothing built): FindFile's answers kept (above; class A, it is the far cells' LOD queue's whole
  cost); the purge off the crossing's frame (class B; 1 to 19 ms in every second
  or third crossing). The map's scan is not worth one (1 to 2 ms).

## 2026-11-18: FindFile's answers kept (`[Route] FileFindCache`, class A, built, on in both games)

`src/engine/io/FileFindCacheLogic.h` (the decisions and the table, every flow in `tests/FileFindCacheLogicTest.cpp`)
and `plugin/src/FileFindCache.cpp` (the entry detour on 0x00431020, an SRW lock: FindFile is called from more than
one thread). A call's key is the finder, the flags, the mask and the path's bytes; only relative paths of asset
kinds are kept (nif, dds, kf, lod, cmp, spt, wav, mp3, ogg, lip, tga, egm, egt, tri, fnt, tex, dlodsettings) - INIs,
text, logs, paths without an extension, absolute and ".\" paths are asked every time. 1 the route, 2 the check.

Measured (Linux without SteamVR, the stand-in Beyond, `--exclusive`, the Windows mod list, OBNG.dll 3e1ae91e, the ride;
`tools/hand-scripts/obng-measure-file-find-{check,off,on}.ini`):

| run (obng-profile-ride-anvil/) | mode | FindFile calls | asked of the OS | from the cache | time in the OS's answers | mismatches |
|---|---|---:|---:|---:|---:|---:|
| 20261118-223542 | check | 33 867 | 19 210 (every kept kind) | - | 1 993 ms | **0** |
| 20261118-224245 | route | 33 775 | 8 039 | 11 079 (4 750 found, 6 329 not) | 992 ms + 2.9 ms for the hits | 0 |
| 20261118-224944 | route | 33 018 | 7 669 | 10 692 | 935 ms + 2.9 ms | 0 |

- **The gate**: 19 210 kept-kind calls compared with the original's answer, 0 mismatches (the check run's harness
  verdict is FAIL for another reason: a game started beside it after "ride-90", not through the runner's locks -
  its timing is void, its comparison is not).
- **What it saves: about 1 s of the OS's file queries a run** (1 993 against 935 to 992 ms), a hit costing 0.26 us
  against 103 us asked. The calls the cache answers are repeats - the same textures and meshes asked again while
  the save loads and actors get their 3D.
- **What it does not save: the cell crossing.** 0x004BD430's LOD queue at a crossing 6.3 to 6.4 ms with the cache, 6.4
  to 6.5 ms without (two runs each, ten crossings each); the step 35.0 to 36.0 against 34.2 to 34.4 ms. Every
  crossing asks for the new cells' LOD files - paths never asked before -, so the first answer is the OS's. The
  lever for the crossing is a lookup that does not ask the OS per file at all (a listing of the loose files under
  Data made once, the archives' own in-memory lookup 0x0042EBA0 as it is), not built.
- Not measured: Windows (the OS's answer there is likely much cheaper - unverified; the route's own cost, a lock and
  a hash, is the same).

## 2026-11-19: the pools' page release kept off the cell crossing (`[Route] PoolPageRelease`, class B, built)

**What the purge's release is** (read 2026-11-19 in the disassembly, the names proposals):
- 0x00444FB0 calls the purge 0x0043FC20 twice at a crossing: at 0x00445371 when 0x0043FE30 emptied some of the buffer
  of interior cells (it returns how many), and at 0x004454EF every time. 0x004455E0 calls it three times more; the
  loads and the cell changes behind a loading screen are its other callers.
- The purge (under the critical section 0x00B35380; 0x00977C50 only returns 1 in this build) calls 0x0055E390(1)
  twice, 0x007B84E0, 0x004A25F0 when [0x00B35300] is set and its this's byte +0xA9 or its argument is, and
  `MemoryPool::FreeUnusedPagesForAllPools()` 0x00402740.
- 0x00402740 holds the allocator's lock 0x00B32B80 - the lock `MemoryHeap::Allocate` 0x00401AA0 takes on every
  allocation - and calls 0x00402640 for each of the pools in the 129 slots at 0x00B33080 (by element size / 4).
  0x00402640 sorts the pool's free list by address (0x00402190, a merge sort of the list) and cuts every page whose
  use count (+0x108, a 16-bit count a page) is zero out of it with `VirtualFree(page, 0x1000, MEM_DECOMMIT)`.
- **A pool's pages stay reserved**: 0x00402280 reserves the pool's whole range once (`VirtualAlloc(MEM_RESERVE)`), and
  0x00402480 commits a page again when the free list is empty (`VirtualAlloc(MEM_COMMIT)`, +0x118 the committed
  pages, +0x104 their peak, +0x10C the reserved). The release gives back committed memory, never address space.
- 27 pools are made at start (0x0040E3B1 on: element sizes 8 to 0x108 bytes), **174 MB reserved in all** - read, and
  the route's summary reads the same 174.0 MB from the 27 pools at run time.

**The route**: two entry detours (plugin/src/PoolPageRelease.cpp, the decisions in
src/engine/memory/PoolPageReleaseLogic.h, every flow in tests/PoolPageReleaseLogicTest.cpp). The purge's detour
reads its return address; when it is one of the crossing's two (0x00445376, 0x004454F4) it marks its thread, and the
release's detour skips the release on that thread. Every other purge releases as before. 1 skip, 2 measure (both
timed, nothing skipped: the before with the same instrument). Class B: the free list stays unsorted until the next
release, so the pools hand out other addresses than the original's; derived: what stays committed is bounded by the
174 MB the pools have reserved anyway.

Measured (Linux without SteamVR, the stand-in Beyond, `--exclusive`, the Windows mod list, OBNG.dll 40231057, the
ride, ten crossings a ride; `tools/hand-scripts/obng-measure-pool-release-{measure,skip}.ini`, all four PASS):

| run (obng-profile-ride-anvil/) | mode | the release at the crossings | longest crossing purge | the grid's move a crossing (mean / longest) | the crossing's step (mean / longest) | pools committed at the end (peak) |
|---|---|---:|---:|---:|---:|---:|
| 20261119-085648 | measure | 36.4 ms (11.3, 10.2, 9.8, 3.5, the rest < 0.5) | 14.9 ms | 19.4 / 35.2 ms | 35.8 / 57.2 ms | 72.7 MB (81.8) |
| 20261119-090336 | measure | 41.1 ms (12.8, 12.0, 11.8, 1.1, 1.1, ...) | 15.5 ms | 19.8 / 35.4 ms | 35.1 / 59.5 ms | 70.5 MB (82.3) |
| 20261119-090013 | skip | 0 (10 skipped) | 3.7 ms | 16.4 / 23.0 ms | 30.6 / 37.5 ms | 77.5 MB (77.5) |
| 20261119-090704 | skip | 0 (10 skipped) | 4.2 ms | 15.7 / 21.3 ms | 33.5 / 54.0 ms | 80.3 MB (80.3) |

- The release costs 10 to 13 ms in three of the ten crossings of a ride (where the cells left behind emptied whole
  pages), under 1.1 ms in the others. Skipped, **the worst grid's move drops from 35 to 21-23 ms** and the mean by
  3-4 ms a crossing. The 54.0 ms step of 090704 is the fade's own render frame inside the step (25.7 ms), not the move.
- What the purge still costs at a crossing (2.2 to 4.2 ms) is 0x004A25F0 (0.7 to 4.2 ms, 17 calls a ride); 0x0055E390
  and 0x007B84E0 are under 0.2 ms.
- The pools hold 5 to 10 MB more committed at the end of a ride (77.5 / 80.3 against 70.5 / 72.7 MB); the peak is the
  same (80 to 82 MB in all four). The process's commit and address space differ more between two runs of the same
  mode than between the modes (611 to 642 MB commit, 1 695 to 1 942 MB address space).
- Not measured: Windows (VirtualFree there is not Wine's; the free list's sort is the same work).
- **The long flight** (`tools/hand-scripts/obng-stress-ride-long.txt`: seven legs of 120 s back and forth, 15 minutes, 64
  crossings over cells left and entered again; `obng-stress-ride-long/20261119-091127` skip, `-092723` measure, both
  PASS, no crash): the crossing purges 64 against 200 ms in all, the longest 3.9 against 22.0 ms; the release took
  over 5 ms at 6 of the 64 crossings without the route (125 ms in all). The grid's move a crossing: mean 11.1 against
  13.0 ms, over 20 ms 2 against 4 times, longest 26.6 against 37.6 ms. The pools' committed memory with the route:
  83.2, 83.9, 84.2 and 84.8 MB at a quarter, half, three quarters and the end - flat at the peak the run without the
  route also reached (84.5 MB). The process's commit at the end 703 against 702 MB, its address space 1 986 against
  2 034 MB: no growth from the route.
- Shipped 2026-11-19: on in both games (OBNG.dll 25d45b55, `PoolPageRelease=1`); the Windows game not measured.

## 2026-11-19: FindFile's file questions answered from a listing (`[Route] LooseFileList`, class A, verified, on in both games)

**FileFinder's search slot 0x00431800** (read in the disassembly; FileFinder's vtable 0x00A36414 slot 3, called by
FindFile 0x00431020 through `call eax` at 0x004310BF; `__thiscall(char* path, flags, mask)`, RET 0x0C): unless
flags & 1 the archives 0x0042EBA0(path, mask) (true: 2); unless flags & 2 `_access(path, 0)` 0x00982D71 relative to
the working folder (found: 1); unless flags & 4, for each search folder of the char* array [this+8] (count: the word
[this+0x10]) whose name the path does not start with (`_strnicmp` 0x009864D9), `_access(folder + path)` built in a
0xF4-byte local - found: copied over the path, 1; else 0. FindFile turns a drive letter into flags 5 and ".\" into
flags 4 (and drops the ".\"). The archive lookup 0x0042EA60 strips a leading "Data\", hashes the name (0x006FA1B0) and
walks the list of archives 0x00B338E0, the last hit 0x00B338E4 first - all in memory.

**The route** (plugin/src/LooseFileList.cpp, src/engine/io/LooseFileListLogic.h, every flow in
tests/LooseFileListLogicTest.cpp): the slot's entry detoured; every file and folder under the working folder listed
once on a thread of its own at start (FindFirstFileEx, 28 069 entries with the Windows mod list linked in, 491 to 653
ms, 43 calls before it is ready go to the original); the slot's two `_access` steps answered by the listing, the
archives and `_strnicmp` still the EXE's own. Asset kinds only (FileFindCache's list), no path Windows would rewrite,
nothing under a name with a case twin; all else to the original.

**The first check found a fault in the Linux game, not in the route**: one mismatch, "Data\Meshes\Armor\Fur\F\
Cuirass_ba1.NIF" found by the listing, not by the original. The Linux game's Data/Meshes held both `Armor` (the
game's) and `armor` (one mod file, made 2026-11-04); `tools/run-with-windows-mods.py` put its 673 armor links into
`armor`, and Wine showed the game the other one. **Every Linux run with the Windows mod list before 2026-11-19 10:50
was without the Windows list's loose armor meshes.** Fixed at the cause: the one file moved into `Armor` (a copy in
artifacts/backups/linux-Meshes-armor-twin-20261119), `armor` removed, no other twin under Data; the linker now stops
on a case twin; the listing refuses any path under a twin (Wine picks one, unpredictably).

Measured (Linux without SteamVR, the stand-in Beyond, `--exclusive`, the Windows mod list, the ride;
`tools/hand-scripts/obng-measure-loose-files-{check,off,on}.ini`, FileFindCache and PoolPageRelease on as shipped):

| run (obng-profile-ride-anvil/) | mode | LOD queue 0x004BD430 a crossing (mean / longest) | grid's move (mean / longest) | step (mean / longest) | FindFile time asked of the original a run |
|---|---|---:|---:|---:|---:|
| 20261119-103619 | off | 6.3 / 9.9 ms | 15.8 / 24.3 ms | 32.1 / 45.9 ms | 911 ms |
| 20261119-104305 | off | 6.3 / 9.6 ms | 15.6 / 22.9 ms | 31.3 / 50.9 ms | 934 ms |
| 20261119-103942 | on | 1.5 / 3.7 ms | 12.5 / 24.0 ms | 30.4 / 57.1 ms | 593 ms |
| 20261119-104628 | on | 1.4 / 3.6 ms | 12.3 / 24.0 ms | 29.1 / 49.7 ms | 575 ms |

- **The gate** (run 20261119-105108, `LooseFileList=2`, FileFindCache off so that every call reaches the slot, after
  the twin's fix): 33 366 calls, 26 875 of them answered by the listing and compared with the original's answer and
  path - 8 810 in an archive, 1 373 loose, 631 under a search folder, 16 061 not found - **0 mismatches**; 6 448
  refused, 43 before the listing was ready. Verified.
- What is left in the LOD queue (1.4 ms a crossing) is the archive lookup: answered calls cost 589 to 606 ms a run for
  about 16 000 calls, 37 us each, all of it the walk of the archive list (no OS call in it) - a lever (one map of
  every archive's files made once), not built.
- The 6 448 refused calls cost 827 to 837 ms a run in the OS: 6 143 of them INIs asked during the save's load in one
  ten-second window (FileFindCache's census by extension) - which INI and who asks, not read yet.
- The 57.1 ms step of 103942 and the longest grid moves (24 ms in all four) are crossings the route does not touch:
  the render frame inside the step and the cells' attach.


## 2026-11-19 midday: what is left of a cell crossing, with the three routes on (measured)

Runs `artifacts/shadow/obng-profile-ride-anvil/20261119-111011` and `-111657` (round A,
`tools/hand-scripts/obng-measure-crossing-attach.ini`: every callee of 0x00444340 and 0x00444FB0 a five-byte jump can
take) and `-111334`, `-112020` (round B, `obng-measure-crossing-step.ini`: every direct callee of the update step
0x0040D800); Linux without SteamVR, the stand-in Beyond at 75 Hz, `--exclusive`, the Windows mod list, OBVR.dll
f4e4b70d (78723f8), OBNG.dll 9394cb4b with FileFindCache, PoolPageRelease and LooseFileList on. All four PASS.
Earlier crossing spikes of `-103942` / `-104628` (the base INI's anchors) add 0x00665260 and 0x007B3A40.

A crossing's update step is 21 to 49 ms (40 crossings); its parts (the spike lines list a part at 3 % of the step
or more):

| part | ms a crossing | note |
|---|---:|---|
| the grid's move 0x00444FB0 | 7.8 - 18 (twice 24) | |
| - the cells' attach 0x00444340 | 2.8 - 11.5 (twice 15 - 19) | |
| -- the far grid's update 0x00483D60: the LOD queue 0x004BD430 (the archive walk) and the map's scan 0x007B3A40 | 0.9 - 5.0 (1.3 - 3.8 and 0.8 - 2.1) | |
| -- 0x004D5BD0 | 0.8 - 3.9 (twice 11 - 17) | per cell; reads like the cell's references attached (unverified) |
| -- the heap's allocation 0x00401F00 (-> 0x00401AA0) | 1.0 - 2.5 | about 5 million calls a run, 0.3 us each with the timer's own cost |
| -- the cell attach 0x004441A0, the Ni updates 0x00707610 | about 1 each | |
| - 0x00665260 | 3.3 - 3.8, every crossing | the player's (this +0x5F4/+0x5F8) list walked with 0x0052B440, 0x004D6D40, 0x00485E00, 0x00487820 - what the list is, is not read |
| - the purge's 0x004A25F0 | 0.7 - 4.6, every second crossing | |
| - the rest (Havok, the shifts, the pause 0x004322B0 under 3 %) | about 1.5 | |
| the render frame 0x0040D4D0 inside the step | 2.8 - 5 as any frame; 12 - 29 in a third of the crossings | the long ones not split yet |
| the rest of the step | about 9 | no direct callee of 0x0040D800 reaches 3 % of a crossing's step |

- **The step's rest is OBVR's wait for the headset, not the engine's work** (derived): no timed callee of 0x0040D800
  holds it; OBVR's `begin_frame` (WaitGetPoses) is 8.95 ms mean in the same runs' capture (measured,
  `OBVR-performance-1-5-report`), and OBVR calls it from its camera hook 0x0066BE6E inside 0x0066B710, which the step
  reaches through an indirect call (which one is not read). The step's mean is 13.8 ms, the stand-in's 75 Hz period.
  So the "about 15 ms of the step outside the grid's move" of the earlier crossings is no lever: it is the frame's
  pacing, and it ends where the engine's work begins.
- What a crossing costs the frame, then: the grid's move (8 - 18 ms) plus, in a third of the crossings, a long render
  frame (12 - 29 ms). At 90 Hz (11.1 ms) the move alone misses a frame every crossing.

## 2026-11-19 afternoon: the archive lookup's cost is the loose file check's _stat; ArchiveLooseCheck (measured)

Runs `20261119-122033` and `-122357` (`tools/hand-scripts/obng-measure-crossing-cheap.ini`: the callees of the archive
lookup and of 0x00665260), same setup as above (OBNG.dll 9394cb4b).

- **The archive lookup 0x0042EA60 is the loose file check's `_stat`** (measured): 171 ms a ride inclusive, of which the
  check 0x0042C1D0 holds 157 ms over 3 055 records (about 51 us each under Proton). The folder search 0x0042CE40 is
  5.8 ms over 119 561 calls, the name hash 0x006FA1B0 30 ms. A folder-hash map over the archives (the idea of the
  morning) would have saved at most the 6 ms of the folder search; it was not built.
- **0x0042C1D0** (read, disassembly; independently SkyBSA's `research/CheckFileIsOverridden.h`,
  github.com/DavidJCobb/oblivion-SkyBSA): `__thiscall(Archive*, BSAEntry* record, char* path) -> bool`, RET 8. Unless
  `bInvalidateOlderFiles` (byte 0x00B04460) is set and the record's +8 bit 31 ("checked") is clear it returns 0 at
  once; otherwise it `_stat64i32`s (0x00983754) "Data\" + path; a loose file newer than the archive's time
  (+0x180/+0x184) zeroes the record's offset (+0xC &= 0x80000000) and returns 1 (the archive does not have it);
  else bit 31 is set and 0 returned. Called by FolderContainsFile 0x0042D000 at a record's first find.
- **SkyBSA** (installed in both games, 1.1.0) patches `0x0042C283 -> 0x0042C295` (a found loose file always wins,
  read in its `LooseFilesAlwaysOverride.cpp`) and `0x0042EAEB` (the lookup no longer tries the last archive that
  answered first); the `_stat` stays. BSA redirection (ArchiveInvalidation Invalidated!, a dummy BSA) does not touch
  the check either. `bInvalidateOlderFiles=0` would drop the `_stat` but stop every loose file from overriding an
  archive, SkyBSA's patch included (derived: the patch sits behind the switch).
- **ArchiveLooseCheck (class A)**: the check's entry answered from LooseFileList's listing of the game folder when no
  loose file or folder of that name exists (the `_stat` would fail: bit 31 set, 0); a listed name, a kind the listing
  does not answer for or a path under a case twin goes to the original (with SkyBSA's patch). Check run
  `20261119-123339`: 4 686 listing answers compared with a `_stat` of its own, 0 mismatches. Off against on
  (`obng-measure-archive-off.ini` / `-on.ini`, runs `-123702`, `-124348` off, `-124025`, `-124712` on; OBNG.dll
  6621bdb6, OBVR.dll unchanged between them):

| | off | on |
|---|---:|---:|
| the archive lookup 0x0042EA60, a ride | 176 / 170 ms | 17 / 18 ms |
| FolderContainsFile 0x0042D000, a ride | 163 / 157 ms | 5.0 / 6.2 ms |
| the far cells' LOD queue 0x004BD430, a ride | 131 / 135 ms | 12 / 11 ms |
| the cells' attach 0x00444340, a ride | 217 / 218 ms | 90 / 103 ms |
| the grid's move at a crossing, mean (10 crossings) | 11.1 / 11.1 ms | 9.6 / 9.6 ms |
| the grid's move at a crossing, longest | 17.5 / 18.7 ms | 13.8 / 12.3 ms |
| the archive lookup at a crossing | 1.3 - 3.7 ms | under 3 % of the step |

  The route answered 4 543 calls of a ride in 2.6 ms; 347 records with a loose file of that name (and 24 refused)
  still went to the original (65 - 71 ms a ride). The crossing's whole step did not fall in these runs (28.3 / 25.9
  -> 29.1 / 27.1 ms mean): the long render frames inside the step (12 - 38 ms in 3 - 4 of 10 crossings in the on runs)
  are the noise, not the route.
- **0x00665260's 3.4 ms are one call of 0x0065D8D0** (measured): 0x00665260 is the player's active quest targets
  update (xOBSE: PlayerCharacter +0x5F4 `activeQuest`, +0x5F8 `activeQuestTargets`), cheap every frame (15 ms
  exclusive over 72 579 calls). At a crossing its rebuild 0x00529A20 calls 0x0065D8D0 per target, and that holds
  the 3.3 - 4.2 ms. 0x0065D8D0 resolves the target ref (0x0052B440) and runs 0x0065D880 -> 0x0068B030 -> 0x0068AB20
  with the pathing's settings saved and restored around it; read as the search for the route through doors to the
  target (what the compass points at; derived from the calls, the name unverified). Not a cheap lever: a frame later
  or on a thread is class B, keeping the answer is class C.

## 2026-11-19 afternoon: the long render frame at a crossing is the render thread waiting on the renderer's lock (measured)

Runs in `artifacts/shadow/obng-profile-ride-anvil/`: `20261119-131842`, `-132209` (`obng-measure-render-frame.ini`,
the render frame's and RenderScene's callees), `-132746`, `-133110` (`obng-measure-render-textures.ini`),
`-133529`, `-133853` (`obng-measure-texture-create.ini`), `-134406` (`obng-measure-lock-waits.ini`, `[Probe]
LockCalls=1` with the wait times added 2026-11-19). All PASS, single pass on. (`-131016` / `-131340` FAIL: a timed
line on RenderScene 0x0040C830 took the entry OBVR's single pass hooks, so single pass never started; do not time it.)

- Render frames over 14 ms in a ride: 8 - 11. In nearly all of them the texture manager's prepare holds 11 - 54 ms
  (NiDX9TextureManager, vtable 0x00A8A9DC by RTTI, slot 2 0x0077AC90; read): in the scene pass NiDrawScene 0x0070C0B0
  (up to 40 ms) and the water pre-pass 0x0049E880 (up to 17 ms). The render sampler: about three quarters of those
  frames' samples under that chain with the eip in ntdll.
- The prepare's time is its own: no texture is created on the render thread during the ride (NiDX9SourceTextureData's
  create 0x00761270 -> 0x00760DA0, which opens the file, reads it whole 0x0042C8E0 and hands it to
  D3DXGetImageInfoFromFileInMemory / D3DXCreateTextureFromFileInMemory: 260 calls a run, all at loads).
- **The wait** (read): the prepare enters the renderer's lock `[[0x00B3F928]+0x180]` at 0x0077ACC6 for every texture,
  before it looks at the texture's renderer data (+0x24); the lock is left at 0x0077AD16, before the data is used.
  The other holder is NiDX9Renderer's vtable 0x00A88EA4 slot 65, 0x00763560 (by RTTI; the slot's only pointer at
  0x00A88FA8; reached from NiSourceTexture's 0x00701C00): under the same lock, when +0x24 is null, it creates the
  texture's data (the file read and D3DX above). It runs on another thread (derived: none of its creates are timed
  on the main thread in the ride). Lock-wait probe, one ride (measured): the lock waited on 372 ms in all; the
  prepare's enter 0x0077ACCC waited 219 times, 326 ms, the longest 12.0 ms; every wait had 0x00763572 (the precache)
  as the holder, the longest 24.2 ms.
- So the render thread waits for the loader's file read and D3DX of *other* textures (derived: the prepare waits
  whether its own texture is ready or not). The D3DX path publishes +0x24 after the texture is complete
  (0x00761270: `*(data[1] + 0x24) = data` after 0x00760DA0 succeeded; read); the pixel-data path's publish is not
  read yet.
- Also seen: the heap's lock 0x00B32B80 waited on 349 ms a ride, in 451 091 short waits (the longest 1.6 ms) from the
  allocation 0x00401AF6 / 0x00401D92 sites.
- The device was created with behaviour flags 0x54 (HARDWARE_VERTEXPROCESSING | PUREDEVICE | MULTITHREADED;
  measured, OBVR.log "device behavior flags").

## 2026-11-19 afternoon: TextureLockSkip - the prepare without the lock for a ready texture (measured)

`[Route] TextureLockSkip` (plugin/src/TextureLockSkip.cpp, src/engine/render/TextureLockSkipLogic.h): a 6-byte jump
at 0x0077ACB8 inside the prepare; a texture with its renderer data (+0x24) goes straight to the data's use at
0x0077AD1C, one without takes the original's way under the lock (0x0077ACBF). All three writers publish +0x24 as
their last step (read: 0x0076134B, 0x007615A8, the end of 0x00774550). Runs `20261119-141855` (check), `-142219`,
`-142906` (off), `-142543`, `-143230` (on); `obng-measure-texture-lock-*.ini`, OBNG.dll 9b8ef08d, all PASS.

- Check: 11 102 335 prepares read the pointer without the lock and then under it, 0 mismatches. No prepare in any of
  the runs met a texture without its data (the render thread never created one through the prepare; derived: the
  creations of the loads go through the precache 0x00763560).
- Off against on, the ride's render frames (measured, the [Timed] spike lines from 10 ms; frames.csv for the frames):

| | off | on |
|---|---:|---:|
| render frames of 14 ms or more | 8 / 8 | 1 / 2 |
| render frames of 20 ms or more | 3 / 4 | 0 / 0 |
| the longest render frame | 52.4 / 26.9 ms | 15.8 / 16.0 ms |
| the prepare a run | 782 / 781 ms | 356 / 358 ms |
| frames over 20 ms (frames.csv) | 37 / 41 | 27 / 29 |
| frames over 30 ms | 12 / 12 | 5 / 7 |
| frame mean / p99 | 13.41 / 14.9 - 15.3 ms | 13.38 / 14.7 ms |

  The longest frames of a ride (81 - 95 ms) are untouched: they are the crossing's update step (the grid's move,
  0x00665260's door search, the attach), not the render.

## 2026-11-19 late afternoon: the ride's longest frames after TextureLockSkip (measured)

Runs `obng-profile-ride-anvil/20261119-145710` and `-150035`: Linux without SteamVR, the stand-in Beyond at 75 Hz,
`--exclusive`, the Windows mod list, OBNG.dll 9b8ef08d with every route as shipped (TextureLockSkip on),
`tools/hand-scripts/obng-measure-long-frames.ini` (the update step's spikes over 20 ms with the suspects timed). The
eye is 4036x3376 on purpose: `obng-profile-ride-anvil.txt` pins `[Render] GameResolutionWidth/Height` itself, so OBVR's
stand-in size fix 2d22f2f does not change this ride and its numbers stay comparable with the earlier rides.

A step's length comes in whole headset periods (13.33 ms: 40.0, 53.3, 66.7, 80.0, 93.4 ms), since OBVR waits for the
next one. Of the ride's 120 s, the steps of 30 ms or more fall on the same places in both runs (seconds after the
`ride` mark):

| s | step, run 1 / 2 | inside it (timed) |
|---:|---:|---|
| 106.9 | 66.7 / 53.3 ms | `0x00442890` (TES::CreateTextureImage, string read in it) 2 calls, 20.0 / 13.3 ms, the longest 17.6 / 8.4; the loader pause 3.6 / 4.6 ms |
| 110.8 | 39.9 / 40.0 ms | one script run `0x005169A0` of 24.9 / 24.8 ms |
| 119.9 | 93.2 / 53.1 ms | the loader pause `0x004322B0` 8 calls, 78.1 / 26.3 ms, the longest 18.5 / 18.8 |
| 53.6 | 49.9 ms (run 1) | `0x00444FB0` 29.3 ms |
| 40.9, 59.0 | 40.0 ms (run 2) | nothing timed (the render 3.4 ms); not explained |

The load before the `ride` mark (placing the player) has frames up to 107 ms; they are a load, not the ride.

Read (the decompiles, the call sites seen): `0x004322B0` is the main thread's pause of the background loader. It
raises the pause count, waits with `Sleep(1)` until no loader thread is pausing already, then wakes each loader thread
through its semaphore and waits for it with `WaitForSingleObject(INFINITE)`. The main thread therefore waits until
every loader thread has finished the job it is on: a texture file read and D3DX, under the renderer's lock, up to 24 ms
(knowledge above). `0x00432860` calls it when the caller is not the loader's thread; `0x00432890` resumes. Callers:
the model load `0x00439EB0` and `0x00439FF0` on a cache miss, CreateTextureImage `0x00442890` on a cache miss (it loads
the texture on the main thread through `0x00701E00` and `0x0075FA90`), `0x00444FB0`, `0x00464AB0`, `0x00465130`,
`0x00465860` and `0x005AD980`. Which of them paused 8 times at 119.9 s, and which texture CreateTextureImage loads at
106.9 s, is not known yet (unverified). An older reading of the sampler blamed the grid move for the 81-95 ms frames
of the TextureLockSkip runs; this split shows that was wrong. Those frames are the pause and the texture load.

### Who makes the main thread wait (measured, `[Probe] LoaderWaits`)

Runs `obng-profile-ride-anvil/20261119-151416` and `-151740` (as above, OBNG.dll 96876a16,
`tools/hand-scripts/obng-measure-loader-waits.ini`): every wait for the loader of 0.5 ms or more with its caller,
thread and tick; thread 432 is the main thread, 580 the loader's.

- **An NPC's body textures, loaded on the main thread.** `0x0052D2C0` (it builds an actor's body: the strings
  `%08XModUpperBodyFemale`, `%08XModBody`, `Textures\%s`; call site 0x0052D4E8) calls CreateTextureImage for the race's
  skin textures: `Textures\Characters\Breton\Female\FootFemale.dds` 13.0 - 14.9 ms, `HandFemale.dds` 4.3 - 9.5 ms,
  `Orc\Male\FootMale.dds` 13.1 ms, `Khajiit\Female\FootFemale.dds` 4.2 ms. Inside each, the loader pause from
  CreateTextureImage (0x00442972) takes 3.5 - 8.7 ms of it: the main thread waits for the loader's current job, then
  reads and creates the texture itself. These are the 53 - 67 ms steps at 106.9 s. The same FootFemale.dds was loaded
  twice in one ride (106.9 s and 120.0 s), so the texture cache let it go in between (derived).
- **Model loads, 0x00439FF0** (call site 0x0043A052): five pauses of 7 - 16 ms (61 ms) in one step at 119.9 s in run
  -151740, the 53 - 93 ms step of the earlier runs; which model is not logged yet.
- On the loader's own thread (no wait for the frame): the faces of LivingOblivion.esp (0x00524100, 7.3 - 7.4 ms each).
- Small ones on the main thread: 0x00555A80 (heads, 3.6 - 3.9 ms), 0x004BA780, 0x006A16F0 (magic, 5.2 ms once).

### BodyTexturePreload: the race skins loaded at the loading screen and held (measured)

`[Route] BodyTexturePreload` (plugin/src/BodyTexturePreload.cpp, src/engine/render/BodyTexturePreloadLogic.h), 0 off,
1 on (the default), 2 async (the body build's loads on the loader's thread; not built yet, runs as 1). At a loading
screen (the call of the loader pause at 0x005ADADC in LoadingMenu::Tick, the loader resting) every race's ten body
textures (TESRace +0x224, read in the body build 0x0052D2C0 at 0x0052D4AB and xOBSE's unk11) are loaded through
CreateTextureImage and their references kept. A body mod's script gives races new paths after the load (measured: 41
paths, Breton 000224FC, HighElf 00019204 and others, one with the mod's own typo `Bretonl`), so the races' path pointers
are compared again at every loading screen tick and every update step (the main loop's call at 0x0040F270; 2 400 TSC
ticks a step, under 1 us) and the new paths loaded. In the ride the third pass came at the loading screen still.

Runs (as above, `obng-measure-loader-waits.ini`): off `20261119-151416` / `-151740` (OBNG.dll 96876a16, before the
route), on `-155056` / `-155419` (e0839551):

| | off | on |
|---|---:|---:|
| body textures loaded on the main thread in the ride | 4 / 4 (41.6 / 31.6 ms) | 0 / 0 |
| the main thread's waits for the loader >= 0.5 ms in the ride | 13 / 12 (134 / 102 ms) | 0 / 0 |
| frames over 50 ms in the ride | 3 / 1 | 0 / 0 |
| the longest frame in the ride | 867 / 67.6 ms | 40.6 / 41.2 ms |
| frames over 30 / 20 ms | 7 / 3, 33 / 25 | 4 / 6, 27 / 28 |
| frame mean, p99 | 13.49 / 13.37, 14.8 ms | 13.37 / 13.37, 14.7 ms |
| the passes' time at the loading screen | - | 183 + 0.3 + 148 / 232 + 0.3 + 149 ms |
| address space at the end (peak) | 1892 / 1666 MB (2007 / 1846) | 2106 / 2238 MB (2307 / 2418) |

108 textures are held. The cost is memory: the address space ends 200 - 570 MB higher (the two off runs alone differ by
226 MB, so the textures' share is derived, not measured on its own; PrivateUsage did not move, so the textures' memory
is not counted there). The model loads of 0x00439FF0 at 119.9 s did not wait either in these two runs (derived from
the probe: no main-thread wait at all; why is not known). The 40 ms frames (three headset periods) with nothing timed
inside stay; they are not this.

### The 40 ms frames: the skins' normal maps, 4096x4096 for feet (measured)

`[Probe] SlowCalls` (every texture made from a path 0x00701E00 of 3 ms or more, its path, caller and thread;
`obng-measure-slow-calls.ini`), run `20261119-211929` (skins preloaded): the ride's main-thread texture loads were the
body's normal maps through the shader's texture cache 0x007B8200 (call site 0x007B82C3): `Breton\Female\
FootFemale_n.dds` 21.0 and 18.8 ms (twice a ride), `Breton\Male\FootMale_n.dds` 5.6 ms; and three menu textures
(`Menus\GenericBackground\edge_*`, 4 - 5 ms each) at 110.8 s, when a script opens a menu. The others of the 40 ms
frames: a mod script's 66 ms step at 95.3 s, the crossing at 53.6 s (the quest target's door route 0x0065D8D0, the far
map scan 0x007B3A40), and the model loads of 0x00439FF0 at 119.9 s in some runs (5 pauses, 20 - 65 ms).

Read in the game folder: 38 of the characters' textures are 4096x4096 (554 MB on disk), the female feet's skins and
normal maps of every race (`FootFemale_n.dds` 4096x4096 DXT5, 22 MB each; a body mod's); the rest are 512 - 2048.

BodyTexturePreload now also holds each skin's normal map (`NormalPathOf`: `Data\` + the skin's path with `_n` before
`.dds`, loaded through 0x007B8200 as the body's shader does). Runs `-212452` / `-212816` against `-211929`:

| | skins only | skins and normal maps |
|---|---:|---:|
| main-thread texture loads in the ride | 7 (64.7 ms) | 3 / 3 (15.0 / 14.4 ms, the menu's three) |
| frames over 40 / 50 ms | 2 / 1 | 0 / 0, 1 / 0 |
| the longest frame | 66.4 ms | 39.9 / 42.3 ms |
| the passes at the loading screen | (as above) | 1227 + 0.3 + 265 / 1242 + 0.4 + 239 ms |
| address space at the end (peak) | 2113 (2271) MB | 2969 (3026) / 2857 (2909) MB |

The normal maps cost about 700 MB of address space more (derived from these runs) and a second at the loading screen,
because of the 4096x4096 feet; at 1024x1024 a DXT5 map is 1.4 MB instead of 22 (derived).

### The horse at 119.9 s: four KF loads, four waits for the loader; AnimationPauseBatch (measured)

`[Probe] SlowCalls` now also keeps the KF loads of 0x00439FF0 (the model loader's animation cache, `__thiscall(loader,
path)`, RET 4) of 3 ms or more. Run `20261119-214333`, 119.9 s: `Creatures\Horse\backward.kf` 18.2 ms,
`jumploop.kf` 14.1, `swimturnright.kf` 15.1, `handtohandidle.kf` 20.8 - on the main thread, from an actor's animation
setup 0x00475D80 (call site 0x00475EC3); each one's loader pause took all but 0.2 ms of it. A horse comes into view.

`[Route] AnimationPauseBatch` (plugin/src/AnimationPauseBatch.cpp, src/engine/io/AnimationPauseBatchLogic.h; class B,
template 0, on in both games): the setup's entry is detoured and the KF load's resume call at 0x0043A088 patched; inside
a setup the resumes are held back and given at its end, so only the first miss waits (the pause counts, read in
0x004322B0 / 0x00432350). Runs `-214844`, `-215208`, `-215532` (OBNG.dll 25358a92, `obng-measure-animation-batch.ini`):
at 119.9 s one wait of 19.2 / 16.4 / 11.8 ms (`backward.kf`) and no other, against four to five (64.5 - 67.3 ms) before;
no freeze, the harness passed three times. Frames over 40 ms in the ride 2 / 0 / 1, the longest 40.5 / 39.9 / 53.9 ms;
the 53.9 ms at 104.0 s is in Havok (0x00893B90, 0x008E4590, 0x008ABAC0, 0x008AD7D0; also 31 ms at 53.6 s before), not
this.

### The address space over a long session, with the preloads held (measured)

Run `obng-long-memory/20261119-220418` (`tools/hand-scripts/obng-long-memory.txt`, 15.5 minutes: the ride towards Anvil,
then a jump into Kvatch, Skingrad, Chorrol, the Imperial City's market, Bravil, Leyawiin, Cheydinhal, Bruma and Anvil
with 80 s of flight from each; OBNG.dll 25358a92, every route as on in the games, `obng-measure-animation-batch.ini`;
Linux, the Windows mod list): the address space rose from 2241 MB at the first summary to 2906 MB in the first two
minutes (the preload's passes and the first ride), then stayed at 2902 - 2907 MB through all nine jumps; the peak was
2932 MB of 4095 (72 %). It does not grow with the session here: about 1.16 GB stay free. Not covered: fights, interiors
with many actors, the menus, a session of hours, the Windows game.

### The ride's last 50 ms steps are not Havok's work (measured)

Runs `20261119-222228` / `-222552` (`obng-measure-havok.ini`: the broadphase batch 0x008E4590, 0x008DE950, the proxy's
set position 0x008ABAC0 and integrate 0x008AD7D0, 0x00893B90, 0x008DFFB0 timed on the main thread): all of them cheap,
the longest call 0.66 ms, 0x008E4590 0.92 ms in 6 551 calls a window. The steps of 53 ms at 48.1, 104.1 and 106.9 s
have nothing timed inside but the render (2.4 - 3.3 ms); OBVR's capture puts 39 - 48 ms between the present and the
next pose wait. Read by the sampler's eip (not its stack words, which can be stale):
- **104.0 s:** 11 samples in winegstreamer.dll (a sound decoded on the main thread through Wine's GStreamer; which
  sound is not known), then 10 in ntdll with Havok's 0x008AE08F / 0x008E46D6 / 0x008DEAA3 on the stack - the broadphase
  waiting on a lock (as knowledge/cell-change-hitches.md saw at crossings; its holder not known).
- **106.9 s:** the main thread's loader pause from the model load 0x00439EB0 (call site 0x00439F11), twice: 8.9 and 22.2
  ms ([Probe] LoaderWaits), a Breton NPC's meshes coming into view (derived from the textures loaded around it).
- **48.1 s:** a mod's script (0x005169A0 / 0x004FAB20 / 0x004FA110 under the eip).
The earlier reading of these frames as Havok's work came from stale stack words.

### The ride's remaining 40 - 53 ms frames, all three suspects (measured)

Runs `20261119-223333` / `-223657` (`obng-measure-last-steps.ini`: [Probe] SlowCalls with the model files 0x00439EB0
and the sounds by path 0x006AC610, LockCalls, LoaderWaits) and the two before. New tool
`tools/re/frame-gap-samples.py`: the sampler's eip (not its stale stack words) between a long frame's present and its
next pose wait, Wine's ntdll by export name.

- **Music, through DirectShow on the main thread (the recurring one).** The frames at 37.3, 40.8, 60.7, 68.3 and 104.0 s
  (40 - 53 ms; 2 - 3 a ride, at varying places) are 25 - 35 ms of `winegstreamer.dll` and registry calls
  (ZwOpenKey(Ex), ZwQueryValueKey, ZwEnumerateKey, ZwCreateKey, ZwSetValueKey, ZwClose) between the present and the pose
  wait. Read: the music player 0x006AD030 (the strings "death", "success"; three such functions, 0x006AB160 and
  0x006ACD10 too) makes a new filter graph for a track, `CoCreateInstance(CLSID_FilterGraph at 0x00A78FB4, ...)` at
  0x006AD123, and calls IGraphBuilder::RenderFile (vtable +0x34) on the MP3: COM's registry lookups and Wine's GStreamer
  graph build, on the main thread. How long this takes on Windows is not measured.
- **The "Havok lock" was a misreading.** The ntdll samples at 104.0 s were these registry calls and, inside the pose
  wait, the stand-in runtime's Sleep (openvr_mock's WaitGetPoses, called from OBVR.dll); the Havok return addresses on
  the stack were stale. The Havok functions timed are cheap (above).
- **Model loads** (0x00439EB0, kept from 3 ms): equipment meshes when an NPC comes into view, `MOO\Crafting\Small\
  SkinningKnife.nif` 6.0 ms (caller 0x004798FA, the weapon equip 0x00479780), `Armor\Steel\Shield.NIF` 4.6 ms (caller
  0x0047A086); each one's loader pause varies with the job the loader is on (2.4 ms here, 8.9 + 22.2 ms in run -222228).
- **Sounds by path** (0x006AC610): only an ambient wind loop over 3 ms (3.5 ms). Not the music.
- **Lock waits** ([Probe] LockCalls) in the ride: the FaceGen cache's lock 0x00B39C00 once, 14.4 ms at 119.9 s - the
  main thread's insert 0x005515B0 waiting while 0x005506B0 (the cache's purge to its budget, under the lock) runs on
  another thread. Before the ride: the archive's lock (0x0042C3E0 waiting on 0x0042DB50, 13.3 ms) and the renderer's
  precache lock (0x00763572, 8.5 ms).
- **110.7 s** (40 ms): d3dx9 making the three menu textures a mod's script opens (above).

### MusicAsync: the music's track start off the main thread (measured)

`[Probe] MusicGraph` (plugin/src/MusicGraphProbe.cpp; the seven CoCreateInstance sites patched, RenderFile timed in
quartz's vtable), run `20261119-224946`: CoCreateInstance 0.4 - 1.7 ms, **RenderFile 27 - 28 ms** a track
(`Explore\atmosphere_03.mp3` 28.3, `Battle\battle_07.mp3` 28.4, `Explore\atmosphere_06.mp3` 26.7; the title
`Special\TES4Title.mp3` 102.0 ms at the main menu) - all on the main thread (thread 432).

`[Route] MusicAsync` (plugin/src/MusicAsync.cpp, src/engine/sound/MusicAsyncLogic.h; class B, template 0, on in both
games): the seven sites hand the game a stand-in filter graph (IGraphBuilder, IBasicAudio, IMediaControl,
IMediaPosition, IMediaEvent in one COM object, the DirectShow headers of the SDK); its RenderFile queues the track for
a worker thread (COM's multithreaded apartment) that makes the real graph and renders the file; the volume, the
position and Run/Pause/Stop asked before it is there are kept and given to the real graph in the game's order at the
first call on the main thread after it is ready (the update's GetEvent every frame); GetEvent has no event until then
and answers one EC_ERRORABORT for a graph that could not be built. The calls the game never makes (read) wait for the
worker and pass on (counted: none in the runs).

Runs with it on, `-225818`, `-230150`, `-230602`, `-230926` (OBNG.dll 2bf854b0 / 402b7a8e,
`obng-measure-music-async.ini`): every track built on the worker (the title 105 - 113 ms; explore and battle tracks 26
- 31 ms), none failed, no call waited; given to the game's graph 37 - 75 ms after its RenderFile (2 - 5 frames; the
title 202 - 205 ms at the main menu), the graph running (state 2) after the handover. In `-230150` two tracks started
during the ride (`Battle\battle_04.mp3`, `Explore\atmosphere_04.mp3`, the second during a fade: the game runs it after
the handover) without a long frame. Frames over 30 ms with Wine's GStreamer and the registry between present and pose
wait: 2 / 2 / 2 in the three last rides without the route (and 0 - 1 in the rides before them: a track starts in the
ride only when the music changes, a fight's for one), 0 in all four with it. Not measured: Windows' cost of RenderFile;
how the music sounds (Nadi's ears).

## 2026-11-20: a cell crossing with every route of 2026-11-19 on, and the cell buffer's eviction (measured)

Runs `obng-profile-ride-anvil/20261120-090130`, `-091623`, `-091951`, `-092621` (Linux, the stand-in Beyond at 75 Hz,
`--exclusive`, the Windows mod list, the Linux game's OBNG.ini of the day, OBNG.dll 402b7a8e;
`obng-measure-crossing-now.ini`, `obng-measure-cell-unload.ini`). A note on those INIs: the game's OBNG.ini has
`SummaryTimer=0`, so these runs wrote no summaries and the spike watch stopped after 60 lines; the later rides' INIs
set it to 1. The crossings (every 11.8 s of the ride, ten a ride) and the first evictions are in the 60 lines.

- **The crossing:** the grid's move 0x00444FB0 is 6.9 - 12.0 ms. Its largest part is the quest targets' rebuild
  0x00665260: **3.1 - 4.6 ms at every crossing**, all of it one call of the door pathing 0x0065D8D0 (the pathing
  0x0068B030 / 0x0068AB20 from the player to each target over the doors; the first door kept at target +0x10 for the
  compass; read). Then the attach 0x00444340 1.9 - 6.1 ms, 0x004D5BD0 0.7 - 2.9, the map scan 0x007B3A40 and the far
  grid's update 0x00483D60 about 1 each.
- **The cell buffer's eviction, a second stall between the crossings:** about 6 s after a crossing (when the loader
  has the new cells) the buffer's add 0x00442740 / push 0x0043FED0 drops its oldest cell and unloads it,
  0x00447BA0: **6.2 - 22.9 ms in one frame**, one to three cells at once, four to six times a ride. Nearly all of it is
  0x004CB4D0, the loop that calls every reference's vtable +0x150 with 0 (its 3D let go; read) between 0x00496EA0 and
  0x00496F50: 2.4 - 21.5 ms. The rest of the unload (0x004CB010, 0x004E7690, 0x004F1840, 0x004D1570) is 1 - 2 ms.
- **The main thread mostly waits in it:** in those frames the sampler's eip is in ntdll's
  ZwWaitForAlertByThreadId / RtlpWaitForCriticalSection (11 of 13 samples at 51.5 s in `-091951`), and with
  [Probe] LockCalls on (`-092621`) the waits are reached from OBNG's critical section replacement - the game's own
  critical sections through its imports. Which one and who holds it the next runs (with summaries) tell.

### QuestTargetDefer measured, and what the eviction waits on (measured, with summaries)

Runs `20261120-094923` / `-095252` (off; the first started while OBVR Helper's last slot batch ran out and has 196
frames over 20 ms in the ride, so it is left out), `-095617` / `-095942` (`[Route] QuestTargetDefer=1`,
OBNG.dll f0398de7), `-100307` / `-100632` (`obng-measure-cell-unload-locks.ini`: LockCalls and the owner lock
0x00496EA0 timed). Summaries on this time.

- **QuestTargetDefer:** every crossing's rebuild ran at the next step's start, 3.37 - 5.15 ms each, none dropped,
  none merged. The longest frame within 0.15 s of a crossing (OBVR's capture, the eight crossings that fall in the
  ride's first 100 s): off 24.8 / 23.5 / 24.1 / 22.5 / 33.6 / 23.7 / 21.7 / 21.8 ms (mean 24.5), on 20.8 / 21.9 / 23.4 /
  24.6 / 25.5 / 21.5 / 21.4 / 19.3 (22.3) and 20.0 / 21.6 / 23.6 / 19.6 / 31.4 / 19.6 / 21.2 / 19.5 (22.1); frames over
  14 ms around the crossings 22 off, 23 and 17 on. So 2 - 2.5 ms off the crossing's frame; at 75 Hz the crossing still
  takes two periods (derived: 13.3 ms a period), at 90 Hz (11.1 ms) a 22 ms frame is two periods where 24.5 was three
  (derived, not measured at 90 Hz). It does not end the crossing's hitch on its own.
- **The eviction does not wait on the owner lock:** 0x00496EA0 (timed on the main thread) shows in one of the
  eviction steps only (69 calls, 10.1 ms, at 51.5 s in `-100307`); in the other nine it is under the spike line's
  floor. The critical sections LockCalls sees: the game's heap lock 0x00B32B80 holds nearly all waits (20 - 65 ms of
  waits a 10 s window over all threads, the longest 0.1 - 4.3 ms), at its allocation 0x00401AF6 and its free
  0x00401D92; the free's waits rise in the windows with an eviction (9 531 - 22 125 waits, 11 - 30 ms) and nearly
  vanish without one. Derived: the eviction's thousands of frees (every reference's 3D let go) meet the loader
  thread's allocations on the one heap lock - the main thread's ntdll waits in those frames are these critical
  section waits. Not proven per frame (the probe counts per window and all threads). The renderer's lock
  (18B05F0C / 18B0600C, the precache 0x00763F71 / 0x00763572) waits 1 - 7 ms a window, the light lock 0x00B3FC00 once
  1.2 - 3.5 ms.

### The eviction's waits per frame: the heap lock is up to half of it, not all (measured)

[Probe] LockCalls now keeps the main thread's critical section waits per update step
(src/engine/memory/StepLockWaitsLogic.h; a line for a step with 2 ms of waits or more, joined with the spike lines by
`tools/re/step-lock-waits.py`). Runs `20261120-102908` / `-103233` (OBNG.dll 39c568a9,
`obng-measure-cell-unload-locks.ini`), the 14 steps with an eviction in the ride:

- In 9 of them the main thread waited 2.1 - 8.3 ms on critical sections, nearly all on the heap lock 0x00B32B80 at the
  free 0x00401D92: 1 924 - 7 453 contended frees a step, about 1 us each (the alloc's holder is another thread, the
  loader by the window counts above). Beside the unload's 6.5 - 19.2 ms that is a third to a half (7.3 of 15.1, 7.8 of
  19.2, 6.1 of 11.8, 5.4 of 11.0, 4.3 of 6.5 ms).
- In 5 of them the waits were under 2 ms while the unload still took 9.1 - 12.3 ms (2.3 once): the unload's own work
  (0x004CB4D0: every reference's 3D let go).
- The owner lock's section 0x00B35D00 (0x00496ECC): 0.00 - 0.02 ms. The scene locks 0x00B3F600 / 0x00B3FA00: under
  0.7 ms.
So an eviction is about 5 - 12 ms of its own work plus 0 - 8 ms of heap lock contention with the loader. A heap with
less contention would take off up to half; only spreading the unload over frames takes the frame under a period.

### The exterior cell buffer and its eviction, read for a spread over frames (read 2026-11-20)

- **The buffer** is one most-recently-used array, TES (`*0x00B333A0`) +0x3C, `uExterior Cell Buffer` entries
  (0x00B051DC); its first grids² (0x00B06A2C squared) slots are the grid's cells, the rest the cells kept beyond it.
  0x0043FED0 (`__thiscall(TES, cell)`) pushes a cell: at slot 0 when its state byte +0x26 is 5 or 6, behind the
  grid's part otherwise; a cell already in the array moves up; the cell shifted out of the last slot is unloaded,
  `0x00447BA0(DataHandler *0x00B33A98, cell)` at 0x0043FF68. 0x0043FEA0 says whether a cell is in the array.
- **Who evicts in the ride:** the background loader's cell task ends on the main thread in its method 0x004BDDF0
  (no direct caller: a vtable slot), which pushes the loaded cell through 0x00442740 -> 0x0043FED0. Several tasks
  ending in one frame are the two to four evictions of one step.
- **The unload 0x00447BA0**, for a cell without flag bit 5: 0x0067A8C0; state = 1; 0x004CB4D0 (under the object owner
  lock `0x00496EA0(0x00B35C80, cell)` / 0x00496F50: every reference of the list at cell +0x48 but the player gets
  `Set3D(null)`, vtable +0x150; xOBSE's TESObjectREFR slot 0x54); 0x004CB010; the land 0x004E7690; an exterior's path
  grid 0x004C6280; the cell's node let go 0x004CE320 (state 0); then for an exterior 0x004F1840, which takes the cell
  out of the worldspace's map and **destroys the cell form** (vtable +0x10 with 1) unless 0x0046B680 / 0x0044FAA0
  keep it.
- **Who looks a cell up meanwhile:** by coordinate through the worldspace's map 0x004EFE80: the grid's attach
  0x004441A0 (0x004F1630 gets or loads the cell at once), 0x00447740, and the background load request 0x004BE090,
  which for a cell that exists and is in the buffer only refreshes it and otherwise queues a load task for it.
- **A cell re-entering the grid from the buffer** takes 0x004D5BD0's short way (0x0043FEA0 true: 0x004CB790, no walk
  of the references); only a cell not in the buffer has each reference's 3D queued (0x00438060) and attached. So a
  buffered cell's references are expected to have their 3D.
- **What follows for a spread** (derived):
  - Unloading one cell a frame instead of several makes it worse at a fixed rate: three evictions in one 26.7 ms step
    miss one period, three steps of one each would miss three (measured: `buf:Add ... x3` in one two-period step).
    Only slices that fit the frame's slack help.
  - Putting the evicted cell aside and unloading it over frames breaks "out of the buffer means unloaded": the load
    request 0x004BE090 would queue a load for a cell that is half unloaded and about to be destroyed.
  - Stripping the next victims' 3D ahead while they stay in the buffer breaks the short way's expectation if one of
    them re-enters the grid.
  - The way with the fewest new states: hold the cell task's end 0x004BDDF0 (a task done on the loader's thread whose
    main-thread end has not run yet is a state the engine has every frame), strip the last slot's cell in slices of
    a few ms while it is still in the buffer, then let the task end: its push evicts a cell whose 3D is gone already.
    Before a grid attach 0x00444340 everything held must be finished at once, so that no stripped cell is attached.
    Not read yet: who calls the task's end and what keeps the task alive after it (its reference count at +8).

### CellEvictionSpread measured: no gain as built; where a frame has slack (measured 2026-11-20)

`[Route] CellEvictionSpread` (plugin/src/CellEvictionSpread.cpp, src/engine/world/CellEvictionSpreadLogic.h; off
everywhere). Rides with `obng-measure-eviction-on.ini` / `-off.ini`, frames over 20 ms in the ride's 120 s by where
they fall (within 1.5 s of a half-cell line = the evictions, at a crossing, the first 6 s, elsewhere):

| run | variant | all | evictions | crossings | first 6 s | elsewhere |
|---|---|---:|---:|---:|---:|---:|
| 114318, 115008, 121723, 123100, 123756 | off | 37, 36, 45, 46, 29 | 12, 12, 15, 12, 7 | 13, 13, 13, 14, 13 | 3, 3, 7, 11, 2 | 9, 8, 10, 9, 7 |
| 120653, 121352 | on, placed references stripped ahead | 58, 58 | 15, 18 | 18, 15 | 14, 7 | 11, 18 |
| 122731, 123428 | on, actors too (one a slice) | 66, 244 | 7, 15 | 12, 16 | 4, 7 | 43, 206 |

- The route does what it was built to do: all 54 ends of a ride held and run later, 4 090 references stripped ahead in
  slices of at most 2.2 ms, the unload's reference loop 0x004CB4D0 down to 1 ms a ride with the actors stripped too;
  no flush but one by the cell request. (The first build held nothing: it took the thread that installs the hooks for
  the main thread; the game's loop runs on another. The main thread is the one that runs the update step.)
- **Why no gain with the placed references only:** half of an unload is its actors ([Probe] UnloadRefs, run
  `20261120-122256`, a ride's 54 unloads: type 0x31, placed references, 4 305 calls, 62 ms, 14 us each; 0x32,
  characters, 51 calls, 34 ms, 0.66 ms each, the longest 4.7 ms; 0x33, creatures, 50 calls, 29 ms, the longest 5.3
  ms). The ends, one a frame now, still cost up to 13.9 - 17.6 ms each.
- **Why it got worse with the actors:** an actor in a buffered cell that loses its 3D gets it made again by the engine
  (98 actors stripped for 67 with 3D; five seconds of 27 - 44 ms frames at 35 - 40 s and 65 - 70 s with file reads on
  the main thread in run 123428). An actor's 3D can only go once the unload has taken the cell's actors out of the
  process lists (0x0067A8C0, the unload's first step).
- **Where a frame has slack** (OBVR's capture against the step's spike lines, runs 121723, 123100, 123756): inside an
  update step the order is the update's early part with the IO pump - OBVR's pose wait - the grid's move - the render -
  the present. In a normal frame the pose wait starts 0.9 ms after the present and lasts 8.8 ms (median; 8.0 at the
  5th percentile): work before it is free up to about 8 ms, work after it has none. An eviction step has 10 - 19 ms
  (once 27) of work before the wait: over the slack. **A crossing step's pose wait starts at +0.6 - 4.5 ms: the grid's
  move runs after the wait, where every millisecond delays the present** - that is why every crossing misses a period
  whatever it costs (13 of 13 in every run), and why QuestTargetDefer's rebuild at the next step's start (before the
  wait) is free.
- The pump's budget is Oblivion.ini `[BackgroundLoad] iPostProcessMilliseconds` (0x00B048E4, 5 in Nadi's INI): the pump
  ends finished tasks until 5 ms have passed, so two or three evictions land in one frame. At 1 it would end one a
  frame (3 ms on average, under the slack) - to be measured (`tools/hand-scripts/obng-profile-ride-anvil-pp1.txt`).

## iPostProcessMilliseconds: the main thread's per-frame budget for ending background tasks (2026-11-20)

**What it is (read).** The IO manager's pump 0x00433590 runs once a frame from the update step (call at
0x0040D8D8) and ends the background tasks that are done: it calls a task's slot 5 on the main thread (0x00433632),
then looks at the clock, and goes on with the next finished task while its budget lasts. A task's end is never cut
short, so the first costly end always runs whole. The budget in milliseconds is read at 0x004335D4:

| setting (Oblivion.ini `[BackgroundLoad]`) | value's address | shipped | used when |
|---|---|---:|---|
| `iPostProcessMilliseconds` | 0x00B048E4 | 5 | the manager's +0x38 is 6 (the game running) |
| `iPostProcessMillisecondsLoadingQueuedPriority` | 0x00B048EC | 20 | otherwise (derived: the waits of a loading screen, 0x00434020 sets +0x38) |
| `iPostProcessMillisecondsEditor` | 0x00B048F4 | 50 | not read by the pump (unverified: the editor) |

The names are read from the setting objects' name pointers in the EXE's data (0x00B048E8, 0x00B048F0, 0x00B048F8).
The three settings came with patch 1.2 (TweakGuides); 5 / 20 / 50 are the defaults (Step Mods' default INI), and
Nadi's Oblivion.ini has them unchanged. Nothing to do with image post-processing, as TweakGuides guessed.

**What a task's end costs.** Mostly microseconds (200 - 800 ends a 10 s window in the ride). A background cell task's
end (slot 5 of vtable 0x00A45BE8, 0x004BDDF0) pushes the loaded cell into the exterior buffer and so evicts and
unloads the oldest one: 3 ms on average, up to 17 ms (54 a ride). With a budget of 5 ms the pump ends cell tasks until
5 ms have passed: two to four evictions in one frame, 10 - 19 ms.

**Why that matters in VR (measured).** Inside the update step the pump runs before OBVR's pose wait. In a normal
frame the wait lasts 8.8 ms (median): work before it is free up to about 8 ms, more than that misses the headset's
period. So one eviction a frame is mostly free, two or three are a missed period.

**Measured, the setting at 1 through the console** (`SetNumericINISetting "iPostProcessMilliseconds:BackgroundLoad" 1`,
scenario `tools/hand-scripts/obng-profile-ride-anvil-pp1.txt` against `obng-profile-ride-anvil.txt`, the same block,
alternating; Linux, the stand-in Beyond at 75 Hz, `--exclusive`, the Windows mod list, OBNG.dll 136b7cc6 with every
route as shipped, `obng-measure-eviction-off.ini`; frames over 20 ms in the ride's 120 s):

| run | budget | at the evictions | at the crossings | all | evictions in one long step | the ride's unloads |
|---|---:|---:|---:|---:|---|---:|
| obng-profile-ride-anvil-pp1/20261120-124732 | 1 | 6 | 14 | 33 | 1 each | 54, 131 ms |
| obng-profile-ride-anvil-pp1/20261120-125424 | 1 | 5 | 11 | 26 | 1 each | 54, 119 ms |
| obng-profile-ride-anvil/20261120-125058 | 5 | 11 | 12 | 38 | up to 4 | 54, 166 ms |
| obng-profile-ride-anvil/20261120-125751 | 5 | 15 | 11 | 36 | up to 4 | 54, 197 ms |

Five earlier rides of the day at 5 had 12, 12, 15, 12, 7 long frames at the evictions. All 54 unloads still happen
within the ride at 1 (nothing piles up); the frame's mean (13.37 - 13.38 ms) and p99 (14.8 - 14.9 ms) are the same.
The unloads' total time is lower at 1 (derived: fewer frees meet the loader's allocations on the heap lock when
they are spread). What is left at 1 are the cells whose unload alone is over the slack (many actors). Not measured:
whether objects appear later when much is loading at once (a city), and Windows.

**What guides say** (looked up 2026-11-20):
- TweakGuides (tweakguides.dmegaming.com/Oblivion_9.html): added with patch 1.2; "not certain what they do", guesses
  post-process effects; found the loading one at 5 instead of 20 most noticeable.
- Oblivion Reloaded's recommendations (github.com/mcstfuerson/TES-Reloaded releases, repeated on Reddit and Nexus):
  `iPostProcessMilliseconds=25`, `iPostProcessMillisecondsLoadingQueuedPriority=100`, no reason given.
- A Nexus tweaking guide (nexusmods.com/oblivion/articles/44297): "contradictory information as to what these do".
- A guru3d forum post (forums.guru3d.com, thread 301288): 16 recommended; "decreasing the value increases stuttering
  during loading ... at 1 stuttering is much worse".
- Step Mods' INI guide: the default 5, no description.
The guru3d observation is the opposite direction. It fits a flat screen at 30 - 60 fps (derived): there a few big
hitches read better than a backlog worked off over many frames, and nothing absorbs a small one. In VR the pose wait
absorbs up to 8 ms a frame, so small pieces are free and big ones cost a whole period. The measurement above is VR's.

**The route.** `[Route] TaskEndBudget=N` (plugin/src/TaskEndBudget.cpp, src/engine/io/TaskEndBudgetLogic.h; class B):
the pump's read at 0x004335D4 is pointed at OBNG's own value, so the game runs with N ms while the setting and
Oblivion.ini stay as they are; 0 leaves the game's setting. Built 2026-11-20 (commit 14830e9); its own rides and the
switch in the games' OBNG.ini follow.
