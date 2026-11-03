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
