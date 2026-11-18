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
  comes into the grid, and the loading itself goes to the IO thread. Which of those steps holds the 120 to 160 us is
  not measured (the next round: 0x004BCE00, 0x004BCAC0 / 0x004BCA90, 0x004BCB70, the file lookup).
- **The purge 0x0043FC20** (read): under the critical section 0x00B35380, with the renderer's lock 0x00977C50, it
  frees the memory pools' unused pages on the main thread at a cell crossing.
- Levers (proposals, nothing built): the far cells' LOD queued from a worker or spread over the frames after the
  crossing (class B; 5 to 13 ms a crossing); the purge off the crossing's frame (class B; 1 to 19 ms in every second
  or third crossing). The map's scan is not worth one (1 to 2 ms).
