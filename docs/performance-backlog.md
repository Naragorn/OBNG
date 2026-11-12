# Performance backlog

The aim (Nadi, 2026-11-10): at least 90 fps at 90 Hz on the Dream Air at
its resolution with every in-game detail option at its maximum, and beyond
that everything that can be had. The large items go first, ranked by their
share of the frame in an ordinary scene; what is put back because it is
small is listed here, with what was measured, so that it is picked up at
the end and not found again. Classes A, B, C: AGENTS.md.

## Where the frame stands (measured 2026-11-10)

Linux, the Beyond at 75 Hz, an eye of 2568x2568, every route of today on
(stereo draw, square root, skeleton update, volatile pack; run
obng-profile-gold-road-single/20261110-111908): every Gold Road phase holds
the 13.33 ms period (median 13.32-13.34; 0-0.2 % of the frames over 20 ms
at the spots and on the walks, 2 % across the loads between the spots).
Of a frame at the second spot the render thread waits 44 % (the poses
wait and a wait in the pass), the engine's own code is 31 % spread thin
(no function above 1.6 %), D3D9 9 %, OBNG's rebuilds 6 %, OBVR 4 %. The
scene pass is 2.4-4.1 ms, the application's GPU time before submit
5.2-6.7 ms.

Windows, the Dream Air at 90 Hz, an eye of 4036x3376 (2.07 times the
Beyond's pixels), the captures left in the game folder on 2026-11-09
(OBVR-performance-1-36 and -37, 129 and 118 s; which scene they are and
which routes were on is not recorded here - unverified):

| frame length | 36: frames | 36: time | 37: frames | 37: time | what fills it |
|---|---|---|---|---|---|
| up to 11.6 ms | 89.5 % | 80.9 % | 92.9 % | 88.7 % | busy 6.0-6.7 ms, the poses wait 4 ms |
| 11.6-13 ms | 9.0 % | 9.1 % | 6.1 % | 6.3 % | no single span long; the poses wait still 3-5 ms |
| 13-60 ms | 1.1 % | 1.6 % | 0.7 % | 1.2 % | busy 10-44 ms, the scene pass 3-14 ms |
| over 60 ms | 0.4 % (38) | 8.4 % | 0.3 % (33) | 3.8 % | 134-286 ms each; in 36 inside present (103 ms), in 37 outside every timed span |

The compositor's GPU numbers there: 3.4-3.8 ms before submit, 4.9-5.2 ms
in all (p95 6.5) - the GPU is not what limits these scenes.

What this says (derived): on the Gold Road the ordinary frame fits 90 Hz
with about 4 ms to spare on the Dream Air; what is lost is lost to the
long frames (a third of a percent of the frames, 4-8 % of the time), to
the frames that miss by a millisecond although the thread waited, and to
the scenes heavier than this one, which no capture here covers.

### Corrections and the evening sessions (measured 2026-11-10, tools/re/capture-frames.py)

- The Beyond at the Dream Air's frame size (an eye of 4036x3376, run
  obng-profile-gold-road-single-dreamair/20261110-114256): the Gold Road
  still holds 13.33 ms in every phase; the application's GPU time before
  submit rises from 5.2-6.7 to 6.5-8.3 ms, the scene pass and the other CPU
  spans stay; 0.3-0.9 % of the frames at the spots are over 20 ms (0-0.2 %
  at 2568x2568).
- The long frames of captures 35-37 are not hitches of play: they come in
  clusters 45 s apart (+12 s, +57 s, +103 s), 70 % of their samples in
  ntdll under the loader's drain loop 0x00434020 (knowledge/
  background-loading.md) - the hand script's teleports. Withdrawn as an
  item.
- The frames of 11.6-13 ms are not misses: the compositor showed 99.5-
  99.7 % of the frames of captures 32, 33, 36, 37 exactly once. They are
  the frame start's jitter around the vsync. Withdrawn as an item.
- What does miss, by the compositor's count of frames shown twice or more
  (Dream Air, 90 Hz): capture 31 (2026-11-07 23:27) 29.5 %, 23 (11-02
  21:11) 13.9 %, 30 (11-07 23:18) 7.0 %, 22 (11-02 20:42) 6.2 %, 26 4.1 %,
  29 0.9 %, 28 0.6 %, 27 / 32 / 33 0.2-0.3 %. Where each was captured is
  not in the captures.
- Capture 31, the frames shown twice or more (2 929): the render thread
  busy 17.1 ms (over 10.5 ms in 88 %), the scene pass 10.3 ms (5.1 ms in
  the frames shown once), the GPU 12.7 ms (over 10 ms in 60 %); the CPU
  alone over in 30 %, both in 58 %, the GPU alone in 2 %. Capture 23 the
  same shape (CPU alone 40 %, both 46 %). Capture 30 is the GPU's: the GPU
  alone over in 57 %, the scene pass 3.0 ms.
- Capture 31's scene pass in those frames, by where the samples fall
  (17 291 samples): Oblivion.exe 43 % spread over the constant and texture
  stage functions (none above 2.6 %), D3D9.DLL 18.8 % (called from
  SetupShaderPrograms 0x0077A1F0, ApplyConstants 0x009A8E30,
  SetVertexShaderConstant 0x007830C0), OBNG.dll 9.2 %, OBVR.dll 8.3 %,
  OblivionFrameFixes.dll 6.7 % (under the stream packing 0x0076F590 /
  0x0076FE20), ntdll 5.6 %, FoliageAA.dll 3.2 %. That capture is older
  than the square root, skeleton and volatile pack routes on Windows.

What this says (derived): the large item is the scene pass of the heavy
places - it doubles there, and it is the draw path (constants and their
D3D9 calls, the stream packing, OBNG's and OBVR's own share in it), with
the GPU over its budget in the same frames.

### Anvil with Better Cities, the Windows mod list on Linux (measured 2026-11-10)

Run obng-profile-anvil-bc/20261110-121028: the Beyond at 75 Hz (its 90 Hz
mode could not be selected from SteamVR on Linux), an eye of 4036x3376,
every route of today on, the Windows installation's mod list linked in for
the run (tools/run-with-windows-mods.py: 11 403 files, 32 plugins, the load
order by file time as on Windows; Data/OBSE and Data/menus stay Linux's).
From the land gate's marker towards the harbour gate (tools/hand-scripts/
obng-profile-anvil-bc.txt; the walks with the collision off). The run
passes; the cell after the move is 0000ABE8, AnvilExterior07 (read from
Oblivion.esm). The harness' screenshots are black in this mode, so the
picture was not looked at.

| per frame | Anvil, Linux | Gold Road, Linux, same size | capture 31, Dream Air | capture 30, Dream Air |
|---|---|---|---|---|
| render thread busy, median / p90 / p95 | 8.4 / 10.5 / 11.5 ms | 5.1 / 6.8 / 7.3 | 9.9 / 19.5 / 23.3 | 7.2 / 9.3 / 10.0 |
| scene pass, median / p90 / p95 | 5.1 / 6.6 / 7.0 ms | 3.4 / 4.0 / 4.3 | 6.0 / 12.2 / 15.4 | 3.9 / 4.8 / 5.2 |
| busy over 10.5 ms (a 90 Hz frame's CPU budget) | 10.0 % | 0.8 % | 41.6 % | 3.0 % |
| compositor's GPU total, median / p90 | 12.0 / 14.0 ms | 8.9 / 10.4 | 6.7 / 14.2 | 6.6 / 9.2 |
| shown twice or more | 4.1 % (of 13.33 ms) | 0.8 % | 29.4 % (of 11.11 ms) | 6.8 % |

- The Linux GPU figure is about twice the Windows one for the same size
  (Gold Road 8.9 against 3.7-5.2 ms), so it says nothing about the Dream
  Air's GPU; only the CPU side carries over.
- The CPU side on Windows is 20-30 % above Linux on the Gold Road (busy
  6.0-6.7 against 5.1 ms; derived from two different days' runs). With
  that factor Anvil's median would be 10-11 ms on Windows - at the 90 Hz
  budget, which is what capture 31 shows (9.9 ms). Capture 31's tail (p90
  19.5 ms, the scene pass 12.2) is not in this walk: its place is heavier
  than the gate-to-gate line, or holds something this run lacks.
- The scene pass of the frames over 13.3 ms here, by where the samples
  fall: Oblivion.exe 51 % (spread, none above 2.7 %), D3D9 19 %, OBNG.dll
  8.4 %, ntdll 6 %, OBVR.dll 4 %, kernel32 3.4 %, OblivionFrameFixes 2.1 %,
  FoliageAA 1.9 % - the shape of capture 31's.

### Anvil with the occlusion culler, and OBNG's own share (measured 2026-11-10)

The run above was without [Route] OcclusionCull, which Nadi's Windows INI
has had on since 2026-11-09 (docs/subsystems/occlusion-cull.md; capture 31
is older than it). With it (run obng-profile-anvil-bc/20261110-122516,
tools/hand-scripts/obng-measure-profile-current.ini as corrected):

| per frame, Anvil on Linux | culler off | culler on |
|---|---|---|
| render thread busy, median / p90 / p95 | 8.4 / 10.5 / 11.5 ms | 6.5 / 8.8 / 9.7 |
| scene pass, median / p90 / p95 | 5.1 / 6.6 / 7.0 ms | 3.6 / 5.4 / 5.8 |
| busy over 10.5 ms | 10.0 % | 3.1 % |
| compositor's GPU total, median / p90 | 12.0 / 14.0 ms | 10.1 / 12.4 |
| reprojected (SteamVR, of the run) | 11.5 % | 6.5 % |

The culler leaves out 30-71 % of an eye pass's 380-755 draws in the town
(16-17 % at the harbour gate, 3 % before the move), 0 % unconfirmed.

What is left of the scene pass with it (31 371 samples): Oblivion.exe 45 %
spread (the pass lists 0x007AD270 3.4 %, the cull walk under 0x0070AB40
about 7 % over six functions, the stream packing with OblivionFrameFixes
about 4.5 %), D3D9 (DXVK here) 22 %, OBNG.dll 15.1 %, OBVR.dll 3.8 %,
kernel32 3.6 %, ntdll 2.6 %.

OBNG.dll's own 15.1 % by function (tools/re/dll-samples.py with the link
map the build now writes, build-plugin/OBNG.map; shares of the pass's
samples): the eye switch TimedEyeSwitch::Switch 2.9 %, the culler's
BoundRect 2.2 %, RouteDrawPrimitive 1.3 %, the culler's Collect 1.2 %,
CullDrawRenderPass 0.8 %, DecideDraw 0.8 %, Table::Find 0.7 %, the ports'
Slot 0.6 %, three unnamed pieces 2.6 % together. The culler is about 6 %
of the pass (0.2 ms a frame), the stereo route about 5.5 %. Outside the
pass OBNG.dll is 5.3 % of the samples: the skeleton route's RoutedDown
3.6 %, its Matches (the order checked against the tree on every call)
0.9 %, SkinBound 0.7 %.

### The culler's bound projection (2026-11-10, class A)

BoundRect (OcclusionProbeLogic.h) with its floor and ceil inline instead of
the C library's and with twelve divisions instead of twenty-four when the
projection's w does not depend on x and y: the same kinds and rectangles as
before in 200 000 random cases (plugin/tests/OcclusionProbeLogicTest.cpp).
A native 32-bit build: 406 -> 42 ns a call, nearly all of it the library's
floor and ceil. In the game (Anvil, run 20261110-124108 against -122516):
the culler's decide step 196 -> 173 ns a draw, BoundRect 2.2 -> 1.3 % of
the scene pass's samples, OBNG.dll 15.1 -> 13.3 % of them - about 0.03 ms
a frame; Wine's floor is not the slow one.

### Three counts in Anvil (measured 2026-11-10, run obng-profile-anvil-bc/20261110-125834)

[Route] OcclusionCullStats (every eighth eye pass's first asks noted,
DrawGroupLogic.h) and [Probe] HeapCalls, tools/hand-scripts/
obng-measure-draw-groups.ini; the 16 summary windows in the town, 2 940
passes noted.

- **Draws that share a mesh.** Per pass 1 068 asks, 624 hidden (58 %), 445
  drawn, 20 of them skinned. The 425 drawn unskinned draws have 285
  distinct model data (the shader object never splits a mesh's group
  further): instancing by mesh could leave out at most a third of them,
  and 88 (21 %) sit in groups of four or more; the largest group seen is
  24. An upper bound - textures and lights still split groups.
- **Hidden draws below hidden nodes.** Of the 624 hidden draws 448 (72 %)
  sit below a parent all of whose noted draws are hidden (72 such nodes of
  340); two levels up 394 (63 %) below 53 nodes, three levels up 377 (60 %)
  below 35 nodes, four levels up 201 (32 %) below 14. A cull at the node
  three levels up would dispose of a third of all asks with 35 decisions
  instead of 377, before the engine collects them.
- **The allocator.** Through its main entry 0x00401F00 the render thread
  makes 212 allocations a frame in 0.09 ms (340-470 ns each), nearly all
  of 64 bytes or less, and none inside the eye pass; the releases cost
  0.007 ms. Other entries reach the heap as well (more releases than
  allocations are seen), so this is a floor; the sampler's share of
  0x00402480 puts the whole at 0.1-0.2 ms a frame in play. It is 17 % of
  the long frames of capture 31: an item for loads, not for the frame.

### What Oblivion Stutter Remover did, against today's numbers (2026-11-10)

OSR (nexusmods.com/oblivion/mods/23208; its INI as quoted in the mod
page's sticky posts, read 2026-11-10) had four parts: the heap replaced
(`bReplaceHeap`, algorithms 1 FastMM4, 2 Microsoft, 3 SimpleHeap1, 5 / 6
its own thread heaps, 8 tcmalloc), critical sections hooked (the default
overrides: "Renderer+0x180", caller 0x00701748, suppressed; the MemoryHeap's
own at 0x00B32B80 made to "stutter"), hash tables resized, and frame rate
management. Nadi's Windows install has no OSR; of its plugins
OblivionPerformanceRuntime sets `LowFragmentationHeap=1` (the process
heap's, not the engine's MemoryHeap), nothing replaces MemoryHeap (the
sampler still finds its slow path 0x00402480; the probe found the entry
0x00401F00 unpatched).

Against the Anvil run: the allocator is 0.1-0.2 ms a frame (above); the
critical sections are visible in the scene pass as kernel32 3.6 % and
ntdll 2.6 % of its samples, the stack words naming the accumulator's
0x007AD270, 0x00405760 and the cull walk's 0x0070E1A0 as callers (stale
words possible) - about 0.2 ms a frame if all of it is lock traffic. Which
locks, taken how often and by which threads, is not counted yet.

The cull at the node, estimated from the same samples: the cull walk and
the accumulator's registration are about 12 % of the pass (0.4 ms a
frame); a third of the asks sit below the 35 hidden nodes, so 0.15 ms,
plus 377 culler decisions at 173 ns (0.07 ms): 0.2-0.25 ms a frame.

### The locks and interlocked calls counted, and the constant map's arranging (2026-11-10)

[Probe] LockCalls (the EXE's import slots, LockProbe.cpp), Anvil, run
obng-profile-anvil-bc/20261110-132036, one town window of 1 374 frames, a
frame:

- Critical sections: about 4 600 enters inside the eye pass and 5 000
  outside, over 45 sections; none was ever held by another thread when
  asked for, except the MemoryHeap's 0x00B32B80 (4 times in the window).
  The largest: 0x00B3FCA0 2 395 outside the pass from the one site
  0x00715629 and one thread; a heap-allocated one (0x18ADC560 in this run)
  2 181 inside the pass from 0x0077ACCC, two threads; 0x00B3F600 1 788 +
  456 from 0x006FF9DE, four threads; 0x00B33F00 601 + 1 296. At the 20 ns
  a pair the sampler's ntdll share implies, 0.1 ms inside the pass and
  0.1 ms outside.
- Interlocked increments and decrements through the import slots: 21 961
  inside the pass, 6 830 outside. Two sites in one function, 0x009A9825
  and 0x009A97FA, are 12 750 of them; the next are 0x0075FB28 1 600,
  0x00405784 620, 0x007F6CB4 590. The sampler's "kernel32 3.6 % of the
  pass" is these calls, not locks.

0x009A97B0 is the constant map's entry arranging (slot +0x48 of the three
map vtables): it walks the entries with a counted reference each and swaps
an entry of kind 1 that lies behind another kind. Rebuilt as a test
(src/engine/render/ConstantMapArrangeLogic.h, class A): arranged maps
return without the walk. **Verified** in the game (run 20261110-132658,
[Shadow] ConstantArrange): 8 575 300 calls, 7 625 100 taken as arranged
with 0 mismatches, the other 950 200 all changed by the original. Routed
(run 20261110-133045): 7.58 M of 8.50 M calls left out, the interlocked
calls inside the pass 21 961 -> 10 582 a frame, the scene pass's median
3.84 -> 3.66 ms and its p90 6.33 -> 5.55 (one pair of runs, both with the
lock probe on; the walks differ a little between runs).

### The allocator, measured at its own methods (2026-11-10, run obng-profile-anvil-bc/20261110-160830)

[Probe] HeapCalls on MemoryHeap's Allocate 0x00401AA0 and Free 0x00401D40
(every wrapper and 79 direct callers reach them; the earlier count at
0x00401F00 saw a part): the render thread makes 200-400 allocations and as
many releases a frame in play, 0.06-0.14 ms for the allocations (290-350 ns
each) and 0.007-0.012 ms for the releases, none inside the eye pass. In the
frames over 30 ms the heap is 0.1-6.6 % of the frame: the 4 048 ms load
into Anvil has 148 ms in it over 852 500 calls (3.6 %), a 426 ms frame
6.2 ms (1.4 %), the 100 ms frames 2.2-2.4 ms. The loader's threads made
6.65 M allocations in 662 ms during that load, and the heap's lock
0x00B32B80 was found held by another thread 54 203 times in that window
(50 times in a window of play). So on this install a faster heap is worth
at most 0.1 ms a frame in play and a few percent of a load; the 17 % of
capture 31's long frames (Windows, 2026-11-07) is not seen here and would
need a Windows capture with this probe to stand.

### The weather confounds the runs; the GPU side in Anvil (measured 2026-11-10)

- **The save rolls the weather and the hour anew each run.** Two Anvil runs
  with the same DLLs and INIs (20261110-165228 and -170016) differ by 2.4 ms
  a frame in the render thread's time outside the scene pass and 2 ms in the
  compositor's GPU figure; one run (-170427) held 17-34 ms a frame for 90 s
  with the GPU figure at 12-27 ms, and the node cull's second on run had 15
  such seconds. What weather or hour those were is not recorded; a forced
  thunderstorm (obng-profile-anvil-bc-storm, run 20261110-172041) is not
  it: 4.6 ms busy, the scene pass 2.7 ms, as light as clear weather. The
  Anvil scenario now sets the hour to 12, the timescale to 0 and clear
  weather (fw 00038EEE); two runs with that agree to 0.01 ms in the busy
  median (5.17 and 5.16). Every pair measured before 17:08 today carries
  this spread.
- **Multisampling**, the weather fixed, an eye of 4036x3376, the node cull
  on (runs -170851 4x, -171242 2x, -171637 none): the application's GPU
  time before submit 7.00 / 5.83 / 5.63 ms in the median (p95 9.17 / 7.89 /
  7.67); the scene pass 2.68 / 2.71 / 3.05 ms (without multisampling the
  engine takes its image-space texture route). Linux has 4x in its
  Oblivion.ini and Windows 2x, which is a part of the "Linux GPU twice
  Windows" noted above. The compositor's total stays at 8.8-9.3 ms in all
  three and is not a measure of the application's work here.
- **The pixels are not what the GPU time follows**: twice the pixels cost a
  quarter more on the Gold Road (above), and 4x to no multisampling is a
  fifth. The Dream Air's driver reports no hidden area (SteamVR's log of
  2026-11-09: HiddenArea(0.00%)), so there is no ready mask of pixels the
  lenses do not show either.
- Not run: FoliageAA's four-pass leaf supersampling off (it is on on both
  installs).

### The Windows evening of 2026-11-10 (measured: captures 39-41, six OBVR session logs, two crash logs)

With the occlusion culler, the node cull, ConstantArrange and VolatilePack
on (the Windows OBNG.ini of 15:49). Frames over 13 ms by capture
(tools/re/capture-frames.py; each about two minutes from the session's
second minute on, cut at the event capacity; GPU timing off):

| capture | place (derived from the sessions' cells) | frames over 13 ms | scene pass, in-time / slow |
|---|---|---|---|
| 31 (2026-11-07, before the culler) | Anvil | 32.9 % | 4.8 / 10.1 ms |
| 39 | Anvil with a Walkers siege | 26.1 % | 3.6 / 5.6 ms |
| 40 | Anvil, calm | 0.3 % | 2.6 ms |
| 41 | Cheydinhal | 10.3 % | 4.1 / 4.5 ms |

- Capture 39 is two halves: a minute at 0-11 % and a minute at 41-89 %.
- In 39's slow frames the render thread is the limit: 6.27 sampler hits a
  frame not waiting against 3.78 in the in-time frames (the sampler hits
  about every 2 ms here: 67 107 samples in 136 s), about 12.7 against 7.7
  ms. The difference is spread: the actor update 0x00603CA0 and its
  skeleton 0x00471F20, 0x00519D90 / 0x0051E560 / 0x004D66B0 (not read),
  the cull and draw (0x007AD270, 0x0053D7A0), Havok's Gsk. The inclusive
  counts come from eight stack words a sample and are lower bounds.
- A correction of how to read these captures: the ntdll share (41-49 % of
  the slow frames' samples) is the thread waiting in a system call - every
  such sample sits at an address ending in C, the return of a syscall stub
  (0x7773837C alone has 16 872 of capture 39's samples) - and the stack
  words behind it (0x008F0C90, 0x00908A40) are stale. It is not Havok.
- SteamVR's half rate over whole sessions (OBVR's "Compositor:" lines over
  the playtime the crash logs give): 8.7 % of the compositor's frames in
  the 10-minute session, 4.3 % in the 23-minute one. The 2026-11-07
  session of 23:45 had 35 713 frames at half rate in at most 27 minutes
  (file times), about a quarter - another course of play.
- Both crashes are at 0x0064F67C (0x0064F650 reads the flags of the
  reference a package's +0x28 record points to: the pointer was dangling),
  in packages of OblivionWalkers.esp; fixed there (794322a). OBNG.log of
  the crash sessions did not exist any more (overwritten at each start;
  kept per session since).
- The engine's heap was 91 % and 99 % carved at the two crashes (512 MB);
  33-65 % in every older crash log. Not explained. MoreHeap is at 1024 MB
  since (Nadi), the fill is logged every summary since ([Probe] Memory).
- Rare missing meshes and blue textures (Nadi, headset): not explained by
  the logs. The culler's own count of skips that its test in the same pass
  did not confirm is 0-1 % of the skips, 8 % in one window.

### The actors' frame and the heap in the crowd scenario (measured 2026-11-10, Linux, obng-actors-count)

Runs 20261110-225506 (the routes on, `obng-measure-actors-probes.ini`) and
20261110-225751 (every route off, `obng-measure-heap-plain.ini`).

- `[Probe] ProcessManager`: 0x00678510 takes 0.27 ms a frame with the
  player alone, 2.0-2.9 ms with about 54 actors, 7.2-9.5 ms with about 100
  (every frame over 3 ms); the same with the routes on and off within the
  spread.
- `[Probe] CollisionWrites`: a collision object's update wrote its own node
  in 1-4 % of the skeletons at about 54 actors and 8-15 % at about 100
  (2 % of the updates). The other 85-99 % of the skeletons touch the Havok
  world alone in their collision updates.
- The engine's heap grows at a constant actor count: flat at 239-250 MB and
  about 618 000 blocks with about 54 actors, then with about 100 actors
  about 95 000 blocks and 8-9 MB more every 10 s (1.05 -> 1.15 -> 1.25 ->
  1.34 M blocks with the routes on; 0.86 -> 0.96 -> 1.05 -> 1.15 M with
  every route off). Not OBNG's routes, then. Whether the engine alone
  (the actors fight: AI, combat, pathing), OBVR or Walkers - not separated.
  At that rate a 512 MB heap is full in a few minutes of such a crowd;
  the Windows crash logs' 91 % and 99 % fit that, without proving it.

### The actors' frame split: controller, skeletons, the rest (measured 2026-11-11, Linux, run obng-actors-count/20261111-080227)

`[Probe] ProcessManager` with the character controller and its parts, the
routes on, per frame (calls times mean over the window's frames):

| actors a frame | process manager | character controller | of it integrate / support check | skeletons (route) | the rest |
|---|---|---|---|---|---|
| 55-58 | 3.0-3.1 ms | 0.9-1.0 ms | 0.4 / 0.4 | about 1.0-1.3 | about 0.8-1.2 |
| 72-75 | 5.1-5.6 | 1.3 | 0.55 / 0.55 | about 2.0-2.7 | about 1.6-1.9 |
| 89-91 | 7.3-7.9 | 1.7-1.8 | 0.75-0.80 / 0.71-0.73 | about 2.8-3.1 | about 2.8-3.0 |
| 108 | 8.9-11.0 | 1.8-2.3 | 0.7-1.0 / 0.9-1.0 | about 3.3-4.0 | about 3.7-4.7 |

(The skeleton line and the probe's lines are written at slightly different
moments of a summary; the skeleton and rest columns are good to a few
tenths.)

- The character controller is 20-23 % of the process manager's update, 17-
  21 us a call - not the 4-5 ms derived on 2026-11-10 from "55-60 % of the
  loop" (that share was of the loop without the skeleton updates, in the
  batch's run). Withdrawn.
- 59-78 % of the controller updates have the moving bit in this scenario
  (the placed NPCs fight): skipping the support check for standing actors
  would reach a third of 0.7-1.0 ms.
- The skeletons are the largest named part (30-37 %), the rest - AI, the
  animation step, 0x00471C00, everything unread in 0x00603CA0 and
  0x00678510 - is as large or larger (37-43 %) and grows faster than the
  actor count.
- The last window (177 frames, 14.4 ms, longest 43 ms) is the scenario's
  end with long frames; not used.

### The actors' frame function by function (measured 2026-11-11, Linux, run obng-actors-count/20261111-090312)

`[Timed]` (plugin/src/TimedCalls.h, `obng-measure-actor-frame-timed.ini`):
41 functions timed at entry and return, the routes on. Per frame, the time
without the timed functions inside:

| function | 55-58 actors (751 frames) | 114-119 actors (249 frames) |
|---|---|---|
| ProcessManager::Update 0x00678510, in all | 2.71 ms | 9.78 ms |
| ApplyAnimData 0x00471F20 (the skeleton update, routed) | 1.01 (17 us a call) | 3.78 (32 us a call) |
| CharacterController::Update 0x00896000 | 0.89 | 2.37 |
| the animation step 0x00603500 | 0.02 | 1.10 |
| the high actors' loop 0x00677EC0 itself (and its untimed calls) | 0.39 | 1.06 |
| 0x005F6540 (10 and 190 calls a frame) | 0.05 | 0.71 |
| 0x00471C00 | 0.08 | 0.36 |
| Actor::Update 0x00603CA0 itself (and its 103 virtual calls) | 0.11 | 0.34 |
| UpdateHighProcesses 0x00673C10 | 0.31 | 0.12 |
| every other timed function | under 0.02 each | under 0.02 each |

- The rows add up to the whole (9.72 of 9.78 ms): the "rest" of the earlier
  split is the animation step, the loop's own part and 0x005F6540 - all
  three small with 55 actors and large with 119, where the placed NPCs
  fight. The actor update's own part with its virtual calls is small.
- The skeleton update is 39 % of the actors' frame with 119 actors, the
  character controller 24 %; a skeleton costs twice as much a call as with
  55 actors (not explained: the fight's animations, or the cache).
- The actor loop's slots +0x1D4 / +0x1DC (0x00610540, 0x00610EB0) were not
  called in this scenario.
- The first run (20261111-085559) timed nothing: the plugin is not loaded
  on the game's main thread under the Steam loader. The timed thread is now
  the first that calls the first listed function.

### Anvil's docks in front of The Flowing Bowl, the Windows mod list (measured 2026-11-11, Linux, run obng-profile-anvil-docks/20261111-092728)

`obng-profile-anvil-docks.txt` with `obng-measure-docks-all.ini`: the cell
AnvilDockGate 00005699 (-48, -8), noon, fixed weather, an eye of 4036x3376,
the Beyond at 75 Hz, every route as the Windows INI has it (the culler with
its node cull). The head through eight directions, then forty seconds
standing.

The frames (OBVR's capture, 5214 frames in 90 s): 37 % in time, 21 % near,
42 % over 15.2 ms. Busy (the frame without the wait for poses and the
pacing wait): median 11.0 ms, p75 12.7, p95 15.6 - over the Dream Air's
11.1 ms in 49 % of the frames, over 13.3 ms in 18 %. The scene pass: median
4.9 ms, p95 6.4. The GPU intervals of the capture are all 0.000 ms here
(GpuTiming=1 gives nothing on this path).

Where the update goes while standing ([Timed], two windows of about 600
frames, per frame, own time):

| part | ms a frame |
|---|---|
| the actors: ProcessManager::Update 0x00678510, in all (58 skeletons a frame) | 5.05-5.20 |
| - the skeleton updates 0x00471F20 (33 us a call) | 1.91-1.93 |
| - CharacterController::Update | 1.06-1.10 |
| - the animation step 0x00603500 | 0.57-0.62 |
| - the high actors' loop itself | 0.57-0.68 |
| - UpdateHighProcesses, 0x005F6540, 0x00471C00, Actor::Update itself | 0.34-0.36, 0.27-0.32, 0.16-0.26, 0.14 |
| the cells: GridCellArray::Update with Havok's step 0x004CCCE0 (0.28-0.63) and the cells' selected nodes (0.50-0.52) | 0.78-1.15 |
| 0x00440400 on g_TES (not read) | 0.71 |
| sound, input, IO, time globals, the rest of the timed callees | under 0.05 each |
| the render routine 0x0040D4D0 (the scene pass, the interface, present, and OBVR's waits inside it) | 6.4 |
| the update step itself (untimed callees: the player's update, the world's update 0x00707370, OBVR's hooks and waits) | 2.9-3.1 |

- The actors are the largest part of the update here too: 5 ms with 58
  actors, more per actor than in the crowd scenario (2.7 ms with 55).
- The culler: 701 draws a pass, 95 skipped (13 %); 13.6 of 15 candidate
  nodes left out a pass.
- A collision object's update wrote its node in 24 % of the skeletons
  (15 % of the updates) - more than in the crowd scenario (8-15 %).
- The engine's heap: 451 of 512 MB carved (88 %) after two minutes with
  this mod list, 1.96 M blocks. The address space: 3284 of 4095 MB used
  (80 %, the Linux process).

### The installed skeleton is four times the game's (read 2026-11-11)

Both installs carry a loose `Meshes/Characters/_male/skeleton.nif` (and the
same file as `skeletonbeast.nif`); block types counted from the NIF headers:

| file | bytes | NiNode | NiTransformController | bhkBlendController | bhkBlendCollisionObject |
|---|---|---|---|---|---|
| the game's (Oblivion - Meshes.bsa) | 28 856 | 71 | 70 | 18 | 18 |
| Linux, loose (2026-11-05) | 95 298 | 289 | 286 | 108 | 18 |
| Windows, loose (2013-05-06) | 96 302 | 295 | 292 | 114 | 18 |

The actors' route splits its calls by subtree size (the "actor update by
subtree size" line). At the docks (run 20261111-092728, a window): 19 609
calls with 201-400 entries at 39.0 us (244 controller walks firing 321
updates each) and 12 254 with 51-100 entries at 16.1 us (34 walks, 20
updates). The large class is 76 % of the skeleton time. That the large
class is the loose skeleton and the small one a tree of the game's size is
derived from the counts (289-295 nodes against 71), not shown per actor;
which actors still have the small tree is not known.

Derived: were the large class as cheap as the small one, the docks'
skeleton updates would be about 0.75 ms a frame less (19 609 x 23 us over
about 600 frames). What else scales with the bones (the animation step,
the blend controllers in the Havok world) is not measured.

The Linux game's MoreHeap is at 1024 MB since 2026-11-11 (Nadi), as the
Windows one since 2026-11-10.

### Who the docks' actors are, and the unused bones left out (measured 2026-11-11, Linux, runs obng-profile-anvil-docks/20261111-101335 and -101643)

`[Probe] ActorCensus` (ActorCensus.cpp), one frame's Actor::Update calls
every ten seconds, standing in front of The Flowing Bowl with the Windows
mod list (the run's load order: 02 Better Cities Resources.esm, 20
LivingOblivion.esp, 38 OblivionWalkers.esp, 3B Better Cities Full.esp, 3F
Maskar's Oblivion Overhaul.esp):

- 48-50 actors a frame: 33-34 NPCs (type 0x23), 15-16 creatures (0x24).
- By distance from the player (game units, about 70 a metre): 3-4 within
  1000, 5 to 2000, 10-12 to 4000, 5-6 to 8000, 23-25 beyond 8000.
- The far half is one cluster at 10 000-11 700 units: 14-15 creatures with
  base forms of OblivionWalkers.esp (0x38) and Maskar's Oblivion Overhaul
  (0x3F), and 8-9 NPCs with base forms made at run time (0xFF).
- No actor's scene node had the hidden bit (flags bit 0) set; whether the
  engine has another mark for "not in view" that this census misses is not
  known.

`[Probe] UnusedBones` / `[Route] ActorUnusedBones` (SceneUpdateLogic.h
UnusedBones; 189 names in the Linux game's OBNG-unused-bones.txt): a large
skeleton's order has 325 entries, 148 of them unused, with 230 controllers
in their chains - NiTransformController (vtable 0x00A79634; its update
0x006C3C40 writes its target's local transform from its interpolator, read)
and bhkBlendController (0x00A97AC4; its update 0x008AAD60 only stores the
time when the target has no blend collision object, read with 0x00497420).

Left out against not (the first docks run of 09:27 against the run of
10:16; two separate runs): a large tree's update 39.0 -> 31.8-33.4 us (321
-> 176-201 controller updates fired) - but the small trees, which have
nothing left out, went 16.1 -> 13.0-13.6 us in the same pair, the same 17
%. The gain is therefore not shown: between nothing and about 7 us a large
tree. The census run between them (same code, nothing left out) had large
trees at 55-67 us; the machine's state differs between runs by more than
the effect. An alternation inside one run would settle it.

### Far actors' skeletons at a quarter rate, alternating inside one run (measured 2026-11-11, Linux, run obng-profile-anvil-docks/20261111-104859)

`[Route] ActorFarEvery=4`, `ActorFarDistance=6000`, `ActorFarAlternate=1`
(`obng-measure-docks-far.ini`): the lower rate on in every second summary
window, the docks with the Windows mod list as before. In this run the
engine updated 96-100 actors a frame (the Walkers hordes had gathered:
twice the 50 of the runs an hour earlier), 73-79 of them beyond 6000 units.

Per frame, the windows' means (five on, four off):

| | off | on (75 % of the far actors' calls left out) |
|---|---|---|
| ProcessManager::Update, in all | 8.77 ms | 7.45 ms |
| ApplyAnimData (the skeleton updates) | 2.67 | 1.52 |
| the flat run alone | 2.31 | 1.19 |
| CharacterController::Update | 2.20 | 2.17 |
| the animation step | 0.67 | 0.66 |
| the high actors' loop itself | 1.42 | 1.34 |

The skeleton part drops by 1.15 ms (43 %), the actors' frame by 1.3 ms;
the character controller and the animation step do not move, as expected.
The scenario passed. What it looks like - a far actor in the open moves its
figure at a quarter of the frame rate - nobody has looked at.

A lesson for reading these logs: a summary writes the route's lines before
the [Timed] lines; pairing them by the first timed line shifts them by one
window (it made "on" look slower at first).

## Put back


| item | class | measured or derived gain | cost | state | where |
|---|---|---|---|---|---|
| Skeleton down passes on several threads | B | at best 1.6 ms with 97 actors, 0.3 ms with 34 (derived from 2.15 / 0.37 ms measured); about 1 ms of a 12.7 ms slow frame in the Windows siege (sampler, a lower bound) | large; the collision objects must leave the down pass first (85-99 % of the skeletons allow it, measured), the controllers' statics shown by a write watch | the reordering is built (`[Route] ActorBatch`), one thread, not accepted | docs/subsystems/actor-batch.md |
| Character controller: no support check for a standing actor | C | about 10 us a standing actor: 0.4 ms in the crowd scenario, up to 1.3 ms with 134 standing (derived) | medium; a standing actor must still notice the ground moving away | not started | knowledge/threading.md |
| Character controller: far or unseen actors every second frame | C | by their share, not measured | medium | not started | knowledge/threading.md |
| Character controller on several threads | B | at best about 2 ms with 134 actors | large; the state machine and the contact callbacks may write into the world | not started | knowledge/threading.md |
| Volatile pack: the compare once a frame instead of once a pack | A | the compare is about 1.0 sample a frame in a crowd | small; needs a safe frame signal | put back | knowledge/skinned-vertex-streams.md |
| The cells' selected update (`[Route] GridUpdate`) | A | none measured against the original | built and verified | off | docs/subsystems/scene-update.md |
| A cull at the scene node (`[Route] OcclusionCullNodes`) | C | built 2026-11-10: the scene pass's median 3.33 -> 3.07 ms in Anvil in one pair, the tail unclear | built; a second pair, a long run and the headset look are open | off | docs/subsystems/occlusion-cull.md |
| The render path's critical sections | C | counted: 4 600 enters a frame inside the pass and 5 000 outside, never contended, 0.1 ms each side; each lock is its own case | small to medium a lock | not started | this file |
| Instancing of draws that share a mesh | C | at most a third of the drawn unskinned draws in Anvil, 21 % in groups of four or more | large | not started | this file |
| The engine's allocator replaced | C | measured: at most 0.1 ms a frame in play and 1.4-6.6 % of the long frames on Linux; 17 % of capture 31's long frames on Windows unconfirmed | medium, and the riskiest item here (errors show late) | put back; a Windows run with [Probe] HeapCalls would reopen it | this file |
| OBNG's own code in the scene pass: the eye switch, the culler's BoundRect / Collect / Find, the draw route | A | 15 % of the pass in Anvil, about 0.5 ms a frame; half of it looks reachable (not measured) | small to medium, our own code, the link map names the functions | not started | this file |
| The skeleton route's Matches on every call | A | 0.9 % of the samples outside the pass | small | not started | this file |
| The animation step 0x00603500, the high actors' loop itself, 0x005F6540 | ? | 1.1 + 1.1 + 0.7 ms with 119 fighting actors, under 0.5 ms together with 55 (measured 2026-11-11) | small to look: [Timed] one level down | not started | this file |
| The engine heap's growth in a crowd | ? | no frame time; it ends sessions (full heap) | small to look: [Probe] HeapCalls by call site while it grows, the scenario with OBVR and Walkers off | found 2026-11-10, not explained | this file |
| The loose skeleton's unused bones left out (`[Route] ActorUnusedBones`) | C | not shown: between nothing and about 7 us a large tree (0.3 ms a frame at the docks at best); the 0.75 ms derived before was wrong - the bones left out are the cheap ones | built 2026-11-11, off; an alternation inside one run would measure it | built, not accepted | this file |
| Far actors' skeletons at a lower rate (`[Route] ActorFarEvery`) | C | measured at the docks with 98 actors, 75 of them beyond 6000 units, every 4th call: the skeleton updates 2.67 -> 1.52 ms a frame, the actors' frame 8.77 -> 7.45 | built 2026-11-11, off; the headset look (a far figure at a quarter rate), and what reads a far actor's bones between two updates, are open | built, not accepted | this file |
| 0x00471C00 before ApplyAnimData | ? | 0.74-0.78 ms with 117 actors (6-7 us a call) | small to look | not started | knowledge/threading.md |
| Actor shadows at rest (`[Route] ActorShadowEvery`) | C | the step 0x004073D0 is at most 1.1-2.3 ms a frame in the Market District (G1, an upper bound); what resting saves of it is not measured | built 2026-11-12, off; the alternating run, a long run and the headset look are open | built, not accepted | this file |

## Far actors: the engine's own process levels and where the frame time comes from (2026-11-11, read)

Read in the decompiled functions (pseudocode, not yet checked against the
instructions), for the question of throttling a far actor's whole update:

- ProcessManager::Update 0x00678510 calls all four loops with a time
  argument of 0: 0x00673C10 (level 3), 0x00673E90 (level 2), 0x00674200
  (level 1), 0x00677EC0 (level 0, high). The first three run only on frames
  a random draw (0x009828C0 against 50 and 10) and the accumulator at +0x24
  let through.
- Levels 1 and 2 call the same Actor::Update vfunc +0x1C0 as the high loop,
  but only when 0.15 s (level 1, 0x00674200) or 0.3 s (level 2, 0x00673E90)
  have passed since the actor's last update (0x006599B0 against 0x00402BD0).
- Actor::Update 0x00603CA0 does not take the frame time as its argument: it
  reads frameSeconds 0x00B33E9C directly in seven places and hands it to the
  animation step 0x00603500 (which reads it in six more). Its argument is
  only tested against 900.
- Derived: a throttle of our own (skip n-1 calls at the high loop's call of
  +0x1C0, run the n-th) must put the summed time into 0x00B33E9C around that
  one call and restore it after, or far actors run n times slower. Main
  thread only, so the swap is safe there; unverified which callees keep
  their own clocks.
- Derived: moving far actors to level 1 is a change of the process object
  (0x0067B260), not a flag; what a level-1 actor shows (animation, 3D) is
  unverified.

## Anvil's docks after Walkers 4.11c (2026-11-11, measured, one run)

Run artifacts/shadow/obng-profile-anvil-docks/20261111-115649 (Windows mod
list, obng-measure-docks-census.ini, Walkers DLL 774206fe, LINK++):

- The census at the docks: 30 to 51 actors updated in a frame over nine
  summaries, 12 to 20 of them beyond 8000 units (the run before 4.11c: 96 to
  100 actors, 75 beyond). Whether the drop is 4.11c or where the hordes
  happened to be is not separated by one run.
- ProcessManager::Update 0x00678510: 4.2 to 4.6 ms a frame at about 44
  skeleton updates a frame (before: 8.77 ms at 98). About 90 to 100 us an
  actor in both runs, so the cost follows the actor count (derived from the
  two runs).
- Of the last summary's 30 actors, 11 are creatures with FF references of
  base forms in plugins 3F (MOO) and 38 (OblivionWalkers.esp) standing
  10 600 to 11 800 units away: actors that far are still updated as high
  process. Walkers' reading: they stand in cells within two of the player's
  cell (-48,-8), so inside the 5x5 loaded grid; an actor outside the grid is
  still unmeasured. The Walkers log of the run shows one horde of four and no
  wait or hold line of 4.11c, so the drop from 96-100 actors is not shown to
  be 4.11c's.

## Far actors' whole update in one frame of 16 (2026-11-11, measured, one run)

[Route] ActorFarWhole=1 with ActorFarEvery=16 beyond 6000 units, alternating
by summary window, run artifacts/shadow/obng-profile-anvil-docks/20261111-131630
(Windows mod list, OBNG.dll 8ef8cf3e, about 37 of 57 actors far):

- ProcessManager::Update 0x00678510 a frame: off 5.80 / 5.07 / 4.39 / 3.97 ms,
  on 3.10 / 2.98 / 2.78 / 2.53 / 2.58 ms - about 2 ms less. 94 % of the far
  actors' calls left out.
- The harness passed, no crash. Not looked at: whether far actors move at
  their speed with the summed time, and what the long steps do to their
  physics. Class C; the gate is Nadi's look in the headset.
- The run before (20261111-130350) is void for the picture: its clock ticked
  on the player's update, which runs more than once a frame, so every call
  got summed time.
- On Windows for the evening of 2026-11-11: ActorFarWhole=1, ActorFarEvery=16.

## Windows session 2026-11-11 13:49-14:27 (Dream Air, full mod list, ActorFarWhole n = 16)

Source: the game folder's `OBNG.log` (227 summary windows of 10 s), `OBVR.log`,
`OBVR-performance-1-42/` (OBVR's capture: the first 105 s after its start delay,
the tutorial dungeon only). All **measured** in that one session unless marked.

- Nadi's look in the headset: far actors moved fine with `ActorFarWhole=1`,
  `ActorFarEvery=16`, `ActorFarDistance=6000`; smooth but for dips at Anvil's
  market, Chorrol's south gate and a Walkers horde at Anvil's gate. No crash.
- Far actors: 94 % of the far calls left out in every window; 12 to 36 of 70 to
  105 actors were far at Anvil, so the ProcessManager still took 3.5 to 5.6 ms a
  frame there (the near actors), 0.1 to 1.2 ms elsewhere.
- The dips at Anvil (windows 95 to 105) are the render walk on the CPU, not the
  actors: 1400 to 1700 draws a pass, two passes a frame, the cull driver (walk
  and finish) 5 to 8 ms a pass - 10 to 16 ms a frame against a period of
  11.1 ms, plus the ProcessManager's 4 to 5.6 ms. SteamVR went to half rate in
  186 episodes, 16591 frames (about 184 s of 38 min), most of them there.
- GPU: 3.5 ms median, 5.4 ms at the 99th percentile of total render GPU time -
  in the tutorial dungeon only. The capture ended (capacity) before the first
  exterior; the GPU at the dips is **unmeasured**.
- Memory: game heap at most 449 of 1024 MB, address space at most 2527 of
  4095 MB.
- Defect seen by Nadi in the tutorial dungeon: behind the cell's bars the floor
  stayed blue for seconds and NPCs lacked arms or the chest, fine once the door
  opened. **Unverified** suspicion: the occlusion cull takes the bars' door for
  an occluder (a see-through surface that writes depth) and leaves out what
  stands behind it. The log shows skips there (up to 104 of 626 draws a pass,
  593 unconfirmed in a window) but not which draws. To settle: the same place
  with `[Route] OcclusionCull=0` and `=1`, eye dumps compared.

### The prison cell looked at on Linux: the defect does not reproduce in a still look

Runs 20261111-151757 (culler off), -152235 (culler on, no nodes), -152713
(culler with nodes), `tools/hand-scripts/obng-cull-prison-bars.txt`, the
Windows mod list linked in, `coc ImperialDungeon01` from OBVRHarnessMOO.
**Measured:** no pixel of the clear colour in any left-eye dump at any of the
ten marks in all three runs, the mean brightness equal within 1 of 100, both
first-person hands drawn; with the nodes the culler left out 30 of 34
candidate nodes and 29 of 500 draws a pass there. So neither the bars nor the
spot under the window make the culler drop something visible while the head
stands. What the runs lack against Nadi's session: a new game with the
tutorial's scene (NPCs behind the bars), the first seconds after the load,
real head motion. The defect's cause stays **unverified**.

### A new game's prison, standing: no defect, but the scene did not begin

Runs 20261111-155710 (culler with nodes), -160744 (off), -161816 (on, no
nodes), `tools/hand-scripts/obng-cull-newgame-bars.txt` (the way in from
OBVR's newgame-hands.txt), the Windows mod list. **Measured:** the world ran
from the first mark on (the tutorial's box closed), 150 s of looking at the
bars and back: no pixel of the clear colour, the brightness equal within 1 of
100 between the runs at each of 31 marks. But nobody came to the bars in any
run - 11 actors updated a frame, none in the picture: the tutorial's scene
does not begin while the player stands at the spot the game puts them. The
defect with the NPCs is therefore still not looked at. The script now steps
back to the door first (taken to start Valen Dreth's taunt; unverified).

### The new game's prison with the scene running: the guard at the bars is whole

Run 20261111-165457 (culler with nodes, as on Windows), the script stepping
back to the door and `setstage charactergen 6` from the console (the stage
read by OBVR's newgame fork from Oblivion.esm). **Measured:** the scene plays
(skinned draws rise from 67 thousand to 243 thousand a window; from the mark
at 50 s a Blade stands in the corridor before the door), no pixel of the
clear colour at any of 32 marks, the Blade drawn with head, chest, arms and
legs where the door's bars do not cover him. The camera stood within a step
of the door, so a bar and the frame cover the picture's left half. One run,
the head moved in jumps. The Windows defect is not reproduced on Linux in a
still cell, a new game standing, or the running scene; what is left untested
is Windows itself and real head motion.

### 2026-11-11 evening: the node log in the new game's prison, and the far actors' travel at the docks

Two runs on Linux with the Windows mod list linked, OBNG.dll of 7a64685.

**The node log** (`[Route] OcclusionCullNodeLog=400`, run
`artifacts/shadow/obng-cull-newgame-bars/20261111-181303`, measured): 50 nodes
were left out for the first time in the whole run, all of them before the
`prison` mark - during the race menu, which already renders the cell. They are
other rooms of the cell (`IDWall01`, `IDCeiling01`, `IDColumnShort..`, the
other cells' gates) and the tutorial's NPCs waiting off stage near
(-505, 480, 241): the whole nodes of Glenroy, Baurus and the Emperor, and
parts of them (`UpperBody` of Glenroy and of Captain Renault, `Bip01 Pelvis`,
`SideWeapon`, `Bip01 Neck1`, `BSFaceGenNiNodeBiped`, the Emperor's `Amulet`);
2 to 5 skinned draws a pass were skipped as well. From the `prison` mark to the
end (the scene running, the guards at the bars) exactly 1.0 node a pass was
left out, 0 skinned draws were skipped and 1496 to 1502 particle draws a
window were counted. So: the culler does take actors' part nodes and skinned
draws out by their bounds (the mechanism Nadi's missing arms and chest would
need), but in this run it did so only while they stood behind walls. Whether
it misfires on Windows is not shown; which node the constant one is is not
known (the log names a node's first leaving out only). The particle loss of
the standing run did not repeat here.

**The far actors' travel** (run
`artifacts/shadow/obng-profile-anvil-docks/20261111-182322`,
`obng-measure-docks-far-whole.ini`, the route alternating by 10 s window,
measured): the far actors that move made

| window | lower rate off | lower rate on (every 16th) |
|---|---|---|
| moving ones, units/s | 79.2, 70.4, 81.5, 66.3, 74.5 | 34.5, 30.1, 30.7, 33.8, 38.0 |
| all far ones, units/s | 59.6, 51.5, 63.4, 51.1, 56.7 | 21.1, 23.4, 23.6, 25.3, 26.6 |

With the whole update on every 16th frame a far actor walks at about 45 % of
its speed: the summed time in frameSeconds 0x00B33E9C does not carry the
movement in full. Which part reads another time (the character controller's
step is the first suspect - unverified) is the next thing to read. Until then
`ActorFarWhole` is not accepted (class C: the gate was this measurement).

### 2026-11-11 evening: why the far actors were slow, and the fix

Three probe runs and one run of the fix at the docks (`[Route] ActorFarProbe`,
`obng-measure-docks-far-probe.ini`, the route alternating by window).

- **The controller gets the summed time** (measured, run 20261111-190646):
  every update of the character controller 0x00896000 inside an update run
  with summed time was handed exactly the summed seconds (100 % in every
  window, 0.22 s an update on average). The guess of a step capped by Havok's
  fMaxTime is refuted.
- **The way it is asked to go is one frame's** (measured, run
  20261111-191058): 62-73 units/s asked in updates with the frame's own time,
  4-5 units/s in updates run with summed time - about 1 unit an update. Read
  in 0x00896000: the way (the argument's floats 4..6) is multiplied by the
  step's reciprocal into the velocity.
- **The animation runs past the lower rate** (measured, run
  20261111-191517): the animation update 0x00476D10 (__thiscall of the
  animation data: the actor, the step, a time or -1; proposal for the name) of
  far actors was called about 32 000 times a window, with the lower rate and
  without, never inside Actor::Update 0x00603CA0. Read in 0x00476D10: it
  zeroes the step at +0x0C..+0x14 at its entry, adds its step argument to the
  time at +0x94 and writes the frame's root motion to +0x0C..+0x14 at its end;
  0x004723A0 reads that as the way to go. So an update on every 16th frame
  found one frame's way. (Correction: +0x94 of the animation data is its time,
  a float - 0x00471F20 hands it to NiAVObject::Update as the time; the text in
  knowledge/render-thread-outside-the-pass.md calls it a node.) It also means
  the lower rate does not save the far actors' animation.
- **The fix** (`[Route] ActorFarRootMotion`, on by default, fb124b7): the
  steps of the frames between are summed per actor
  (ActorFarUpdateLogic.h AddRootMotion / TakeRootMotion) and written to
  +0x0C..+0x14 before the update that runs. Measured, run 20261111-192034:

  | moving far actors, units/s | windows |
  |---|---|
  | lower rate off | 94.6, 106.1, 118.2, 102.4 (the first window 76.4, warming up) |
  | lower rate on, steps summed | 97.6, 104.2, 102.0, 93.6, 92.8 |

  The way asked of the controller is 76-79 units/s with the lower rate against
  85-94 without. So the speed is within about 7 % of the original's on
  average, inside the windows' spread; whether the rest is a real loss (the
  clamp in 0x004723A0, a loop wrap of the animation) is not shown by one run.
  812 to 1075 updates a window got the summed step, 0 to 44 had none.

Open: the far actors' animation could be put on the lower rate too (the time
it costs is not measured; their limbs would move at the lower rate, visibly).

### 2026-11-11 evening: what the far actors' lower rate gains, and what their animation costs

The docks runs alternate the route by 10 s window; the timed entries
([Timed], logged after the route's lines of the same window) give ms a frame.

| run | far actors a frame | actor updates a frame off -> on | ProcessManager::Update 0x00678510 off -> on | UpdateStep 0x0040D800 off -> on |
|---|---|---|---|---|
| 20261111-182322 (before the root motion fix) | 64-74 | 82-93 -> 23-26 | 7.98-8.34 -> 3.34-3.90 ms | 20.4-22.2 -> 13.6-16.4 ms |
| 20261111-192034 (with the fix) | 18-24 | 40-45 -> 23-24 | 4.04-4.48 -> 2.72-2.98 ms | 13.5-15.8 -> 13.4-13.8 ms |
| 20261111-193818 (with the fix) | 21-29 | - | 4.12-4.59 -> 2.74-2.93 ms | 13.3-15.7 -> 13.3-13.8 ms |

(measured; why the first run had three times the far actors is not looked
into - Oblivion Walkers deployed a new build between the runs.) About 70-80 us
a left-out update. With 20 far actors the whole update step stays at about
13.4 ms either way; whether a frame cap hides the gain there is not checked.

The far actors' animation update 0x00476D10, which the lower rate does not
reach (run 20261111-193818, [Route] ActorFarProbe, measured): 37-46 calls a
frame for 21-29 far actors (more than one call an actor), 0.12-0.16 ms a frame
in the steady windows (3.2-3.8 us a call; 0.24-0.96 ms in the first three
windows, while the head turns and the scene settles). Putting it on the lower
rate too could gain at most that, about 0.15 ms a frame here, for limbs that
move at the lower rate: not worth it. The caller's other work a frame is not
in this number.

### 2026-11-11 night: the second driver pass a frame (read, not yet measured apart)

Where the Anvil profile stopped. In the docks runs the culler's driver
(ShadowSceneNode::OnVisible 0x007C78D0) begins two passes a frame (1476
passes in 738 frames, run 20261111-193818), in the prison one; on Windows the
windows with 1500-1800 passes in 10 s are the ones whose draws also go to a
1024x1024 viewport (OBNG.log of 2026-11-11). Derived: the second pass is the
water's reflection (WaterManager::RenderReflections 0x0049BEF0, a mirrored
camera, OBVR's map size) - unverified. Read in OcclusionCull.cpp: with eye
data given BeginEyePass does not ask whether the target is the frame's, so
that pass counts as an eye pass (the frame counter, the batch, the node
candidates' ages all step twice a frame); its draws are let through by the
viewport test in DecideDraw. The log's "ms a pass" is therefore the mean of
two different passes. Next: the driver's time and visits by the culling
process's vtable in one docks run (what the second pass is and costs), then
an eye pass only for the frame's own pass.

## 2026-11-12: the second driver pass is the water's reflection (measured), and the culler no longer takes it for an eye pass

`[Route] OcclusionCullDriverLog=1` (a measurement, off by default; `tools/hand-scripts/obng-measure-docks-driver.ini`)
splits the driver's outermost calls of a summary window by caller, culling process and render target. Anvil docks,
Linux harness, Windows mod list, run `artifacts/shadow/obng-profile-anvil-docks/20261112-085440` (measured):

| call | process vtable | target | calls a frame | ms a call | objects visited | nodes left out |
|---|---|---|---|---|---|---|
| the frame's | 0x00A7E610 NiCullingProcess | 8072x3376 | 1 | 3.7-4.5 (walk and finish) | 2700-3100 | 25-27 |
| the reflection's | 0x00A3F380 TESWaterCullingProcess | 1024x1024 | 1 | 0.30-0.46 | 220-430 | 0 |

Both arrive through NiCullingProcess::Cull's call of the object's slot +0x7C (return address 0x0070E02F). So the
"ms a pass" of the node summary was a mean of these two (about 2.2 ms); the frame's own driver call is about 4.2 ms
here and the reflection's walk about 0.3 ms. The 10-16 ms a pass of Nadi's Windows session are therefore a mean as
well; what the frame's pass costs there is not measured apart yet (the driver log is in the Windows build only after
the next deploy).

The defect (read, then measured): with the route's eye data `BeginEyePass` did not look at the render target, so the
reflection's pass was begun as an eye pass too - the frame counter went up twice a frame (a verdict's hold of two
"frames" was one), the results were collected twice, and the reflection's draws went through the eyes' decision (77
a pass "outside eye 0's viewport"). The flat game had the check already (`FlatTargetIsFrame`). Fix:
`cull::EyeTargetIsFrame` - a pass is an eye pass only when its target holds both eyes' ports (`[Route]
OcclusionCullEyeTarget`, on; 0 is the earlier behaviour). Run `20261112-085817` with it (measured): 747-751 eye
passes a window for as many frames (before: 1416-1462), "1494-1502 into other targets" (the reflection is asked
twice, at the driver and at the finish), 0 draws outside eye 0's viewport, per frame 1310 draws asked and 176
skipped (13 %, as before: 2 x 88), 25.0 nodes left out (before 25.8). The driver's time is unchanged (4.1-4.5 ms);
the frame time cannot show a gain here because the run sits at the 75 Hz cap (UpdateStep 13.2-13.6 ms in both).
Not checked: whether the reflection looked wrong before (nodes hidden from the eyes left out of the reflection's
walk were 0.0 a call in the windows measured, so probably not at this place).

Also answered (Oblivion Walkers, 2026-11-12): run 182322 of 2026-11-11 had three times the far actors of the later
runs because a Walkers deploy at 18:53:54 (`[Horde] GateHordesMost=1`) sends all but one horde a gate out of the
loaded grid (the deploy is measured by its file time; the effect on the count is derived).

### The frame's pass apart: walk against finish (derived from run 20261112-085817's own lines, no further run)

Per frame at the docks: the driver's call 4.06-4.47 ms (measured, driver log); the eye pass's finish with the culler
3.70-3.74 ms (measured, the cull summary's "eye pass", which also holds the 0.17 ms of collecting before the
driver's timer starts). So the cull walk over 2995 objects is about 0.5-0.9 ms and the finish about 3.5 ms, of which
the culler's own work is 0.68 ms (decide 0.26, end tests 0.25, collect 0.17) and the drawing of about 1130 draws
about 2.9 ms (derived: about 2.5 us a draw). A rebuild of the frame's pass has to start at the drawing, not at the
walk. Linux (DXVK, headless) only; the Windows build with `OcclusionCullDriverLog=1` was put into the Windows game
on 2026-11-12 so that Nadi's next Windows session gives the same split there.

### The draw side at the docks under the single-pass route (measured 2026-11-12, run obng-profile-anvil-docks/20261112-100131)

`[Probe] PassSteps=1` with the route as played but the occlusion culler off (they share entries;
`tools/hand-scripts/obng-measure-docks-steps.ini`), two steady windows of 750 and 747 frames, per frame:

| step | calls a frame | ms a frame | us a call |
|---|---:|---:|---:|
| FinishAccumulating 0x007AE070, all sites | 8-9 | 4.52-4.56 | |
| - at the eye-pass site 0x007C7D56 (the frame's and the reflection's) | 2 | 4.35-4.45 | |
| - at 0x0070E152 (the shadow maps) | 4-6 | 0.22-0.42 | |
| Do_RenderShape 0x007693E0 | 1224-1251 | 3.44-3.48 | 2.75-2.84 |
| slot 13 SetupTransformations 0x007C9230 | 1207-1234 | 0.18 | 0.15 |
| slot 15 PrepareGeometry 0x0077A310 | 1357-1385 | 0.43-0.44 | 0.31 |
| slot 14 SetupShaderPrograms 0x0077A1F0 | 1247-1275 | 0.59 | 0.47 |
| ApplyConstantMap 0x009A8E30 (inside slot 14) | 3069-3125 | 0.49-0.50 | 0.16 |
| SetVertexShaderConstant 0x007830C0 | 2622-2679 | 0.10 | 0.04 |
| SetVertexShaderConstantArray 0x00783180 | 45-52 | 0.02 | 0.35 |

(DrawSkinnedPrimitive 0x00767520 has no line in this run: the route holds its entry.)

Derived: the three shader slots are 1.20 ms; Do_RenderShape without them is 2.25-2.28 ms a frame, 1.8 us a shape -
two thirds of the draw side. That rest holds, per shape, the route's eye switch (the second eye's viewport and
constants) and two device draws; what each of them costs is not measured apart. Against the dual pass of
2026-10-30 (0.9 us a shape for one eye's rest, the three slots 1.25 us a shape and eye) the single pass pays the
slots once for both eyes (0.97 us a shape here) and the rest twice. The finish outside Do_RenderShape is about
1.1 ms (the list walk, DrawRenderPass, the shader states' begin and end, the skinned draws, the probe's own
timers). Linux and DXVK; the culler off, so about 13 % more draws than in play.

### The whole draw by step (measured 2026-11-12, runs obng-profile-anvil-docks/20261112-101334 and -101722)

`[Route] StereoDrawTimer=1` (a measurement, off by default) times, inside the route's whole draw, the device's
draw calls, every shader slot 10..19 and the render state's two calls
(`tools/hand-scripts/obng-measure-docks-draw-timer.ini`: the culler and the pass-steps probe off). Three steady
windows of 747-750 frames, about 1 190-1 240 whole draws a frame, per frame:

| step | ms a frame | us a draw | what it is |
|---|---:|---:|---|
| the whole draw (RouteDrawPrimitive's rebuild of 0x007672F0) | 3.06-3.17 | 2.57-2.58 | |
| slot 12 | 0.61-0.63 | 0.51 | 0x0077A1B0 for the three commonest shader classes (read in their vtables 0x00A90634, 0x00A9405C, 0x00A92844): a call of slot 2 of the object at shader+0x3C with a trailing 0 (pseudocode; Gamebryo's SetupRenderingPass, a proposal) |
| slot 14 SetupShaderPrograms 0x0077A1F0 | 0.47-0.50 | 0.40 | the constant maps |
| the device's draws (two eyes) | 0.48-0.50 | 0.19 a call, about 2 550-2 700 calls | DXVK |
| the eye switch | about 0.4 (derived: 0.35-0.37 us a switch) | | OBNG's own |
| slot 15 PrepareGeometry 0x0077A310 | 0.34-0.38 | 0.29 | the packing |
| slot 11 | 0.33-0.34 | 0.28 | 0x007C9F30 for the commonest class (vtable 0x00A90634, read), 0x008067F0 and 0x007F13B0 for the next two; Gamebryo's UpdatePipeline, a proposal |
| slot 13 SetupTransformations | 0.13-0.14 | 0.11 | |
| slot 10 | 0.06 | 0.05 | |
| slots 16-19 together | 0.05 | | |
| the render state's two calls | 0.02 | | |
| not named (the logic, the timers) | about 0.15 | | derived |

What that settles: the device's draw is cheap here (0.19 us a call), so one draw for both eyes is worth at most
0.25 ms at this place; the eye switch is 0.4 ms; and no single step holds the draw - six steps of 0.3-0.6 ms each.
Slots 11 and 12 (about 1 ms together) had no timer before. The draw side scales with the number of shapes at about
2.6 us each, so fewer shapes (the culler, instancing) reach every step at once, while a faster single step reaches
0.3-0.6 ms at best. Not measured: how often slot 12 sets a pass the device already holds from the shape before.
Linux and DXVK only.

### Slot 12 mostly repeats the pass object, hardly ever the states (measured 2026-11-12, run obng-profile-anvil-docks/20261112-104249)

The same switch counts, at every slot-12 call of the whole draw, whether the pass object (shader+0x3C) is the one
of the call before, and whether the property and effect state handed in (the renderer's +0x0C and +0x10) are the
same too. Eight windows at the docks: 90-91 % of the calls meet the pass object of the call before, 1.6-3.4 % also
its two states; 30-34 distinct pass objects a window. Upper bounds - skinned and direct draws between two whole
draws are not seen.

So a plain "the same as before, leave it out" in front of slot 12 reaches 2-3 % of its calls (about 0.015 ms a
frame): not worth building. What stays open is whether the pass setup has a part that depends on the pass object
alone (its render states) beside a part that depends on the shape's states (its textures) - then nine in ten calls
could leave the first part out. That needs slot 2 of the pass object read (not done), not another count.

### What slot 12's pass setup does (read 2026-11-12; no run)

Slot 12 0x0077A1B0 calls slot 2 of the object at shader+0x3C. For an NiD3DPass (vtable 0x00A882DC by RTTI
`.?AVNiD3DPass@@`, slot 2 = 0x0075FD90, read in the image; that the object is always of this class is unverified -
RTTI names no class derived from it) the body is 0x0075FD90..0x0075FE20 (read in the instructions; the export has
no file for it, the vtable is its only reference):

- It ends in `ret 20h` and reads none of its eight arguments: the setup depends on the pass object and on global
  state alone, not on the geometry or the property and effect state handed in. So the 2-3 % of "the same states"
  measured above was the wrong question - the arguments do not matter.
- Step 1: the pass's shader program at +0x58 (its slot +0x50 gives a handle) or the byte at +0x5C goes to slot +0xEC
  of the object at [0x00B42040].
- Step 2: when +0x30 is set, 0x007726C0 walks two linked lists of (state, value) pairs and hands each to slot +0x64
  of the object at [0x00B427A4] (pseudocode): the pass's render states and sampler states.
- Step 3: for each of the +0x2A (word) stages in the array at +0x24, 0x00772400 configures the stage: 0x007721E0
  sets the stage's texture (stage+4, prepared through the renderer's object at +0x8B8) with slot +0xE0 of
  [0x00B42758] and then the stage's states (pseudocode).
- Step 4: 0x00771790 switches the stages after the last one off, up to the limits at 0x00B28CB0 / 0x00B28CB8.

Derived: steps 1, 2 and 4 depend on the pass object's own fields; step 3 depends on the stages' textures, which
change from shape to shape while the pass object stays (the shader writes them before slot 12; where, is
unverified). With 90 % of the calls on the pass object of the call before, steps 1, 2 and 4 are repeats in nine of
ten calls - if the objects behind 0x00B42040 / 0x00B427A4 / 0x00B42758 do not already drop a state the device
holds (unverified; then the repeats cost only their loops). What share of the 0.6 ms a frame steps 1, 2 and 4
hold is not measured. A skip would be class C, and its risk is a render state left over from a draw this count
does not see (the skinned and direct draws, the eye switch).

### The pass setup's steps timed (measured 2026-11-12, run obng-profile-anvil-docks/20261112-105825) - put back

Four `[Timed]` entries beside the step timers (`tools/hand-scripts/obng-measure-docks-draw-timer.ini`, F40-F43).
The timers inflate everything: 6-7 million timed calls a window took the frame rate from 75 to 52-72 a second, and
slot 12 itself read 0.81 us a draw against 0.51 without them (about 30 ns a timed call, derived). Three windows:

| step | calls a setup | share of the setup's time | us a call (with the timer) |
|---|---:|---:|---:|
| the setup 0x0075FD90 as a whole | 1 | 100 % | 0.78-0.85 |
| step 3, the stages' configure 0x00772400 | 6.8 | 65-66 % | 0.075-0.082 |
| step 2, the render state lists 0x007726C0 | 1.05 | 6.6-6.8 % | 0.051-0.054 |
| step 4, the stage tail 0x00771790 | 1.3 (it has other callers) | 5.9-6.1 % | 0.038-0.040 |
| its own body (step 1 and the loop; holds the children's timer cost) | | 24-25 % | |

Derived: the part that depends on the shape (step 3) is two thirds of the setup; the parts that depend on the pass
object alone (steps 1, 2 and 4) are a third at most, with the timers' cost taken out about 0.2 us a draw at most -
under 0.25 ms a frame, of which nine in ten calls repeat. A stage's configure costs about 45 ns without the timer,
so the engine's state objects do drop most of what the device already holds (derived from the cost, not read).
Verdict: a skip of the pass-only part is worth 0.2 ms a frame at the very best against the risk of a left-over
render state - put back. The draw side's lever stays the number of shapes.

## 2026-11-12: the actors one level down at the docks, without the far throttle (measured)

`tools/hand-scripts/obng-measure-docks-actors-down.ini`: the far actors' routes off (they hold 0x00603CA0's and
0x00476D10's entries), `[Timed]` on the per-actor chain. Read first: the high actors' loop 0x00677EC0 calls the
actor's slot +0x1C0, which is 0x00605770 for Character (vtable 0x00A6FC9C), Creature (0x00A710F4) and
PlayerCharacter (0x00A73A0C) (read in the image by RTTI); 0x00605770 calls Actor::Update 0x00603CA0 and, on the
actor's process (+0x58), the HighProcess's (vtable 0x00A71814) slots 3 / 4 (0x0063BA30 / 0x0063A210) and 8
(0x00643640), and the actor's slot +0x368 (0x005FAAE0) (pseudocode).

Run `artifacts/shadow/obng-profile-anvil-docks/20261112-111142`, a window of 589 frames with 73 actors a frame;
run `-111523`, a window of 730 frames with 47-49 actors a frame (the timers took the first run to 48-60 frames a
second; the ranking holds, single numbers are inflated):

| item | ms a frame (73 actors) | us an actor (run 1 / run 2) |
|---|---:|---:|
| ProcessManager::Update 0x00678510 | 6.66 | |
| the skeleton update, ApplyAnimData 0x00471F20 (the flat route inside) | 2.27 | 31 / 34 |
| the character controller 0x00896000 | 1.34 | 20.2 / 20.1 |
| the animation step 0x00603500 | 0.72 | 10.8 / 12.8 |
| the animation update 0x00476D10 | 0.56 | 6.2 / 3.1 |
| 0x00471C00 | 0.55 | 7.6 / 1.6 |
| the high process's slot 4 0x0063A210 (run 2 only) | 0.40 at 47 actors | - / 8.5 |
| UpdateHighProcesses 0x00673C10 | 0.34 | |
| 0x005F6540 | 0.33 | 5.0 / 5.3 |
| Actor::Update 0x00603CA0 itself | 0.17 | 2.3 / 2.5 |
| the actor's slot +0x368 0x005FAAE0 (run 2 only) | 0.10 at 47 actors | - / 2.1 |

- The 0.90 ms a frame that 0x00605770 held without a timed callee in run 1 (12.7 us an actor) are the high
  process's slot 4 (8.5 us), the actor's slot +0x368 (2.1 us) and the timers of slot 8's 38 calls an actor (run 2:
  0x00605770's own part is 0.02 ms a frame).
- The character controller is the largest item nobody has worked on: 20 us an actor in both runs, 1.34 ms a frame
  with 73 actors - more than the animation step and 0x005F6540 together. The two entries in "Put back" (no support
  check for a standing actor, far or unseen actors on a lower rate) now have a measured base; the far throttle
  (`ActorFarWhole`) already leaves the controller out for far actors with the rest of the update.
- 0x00471C00 and the animation update differ by a factor between the runs (1.6-7.6 and 3.1-6.2 us): they depend on
  what the actors do, not only on their number.

### The character controller's parts at the docks (measured 2026-11-12, run obng-profile-anvil-docks/20261112-112300)

The existing `[Probe] ProcessManager=1` (`tools/hand-scripts/obng-measure-actors-probes.ini`: no far throttle, no
culler) with the docks script; six windows with 56-67 actors a frame:

| | per update | share of the updates |
|---|---:|---:|
| the controller's update 0x00896000 | 20-22 us | 100 % |
| the support check 0x008AE890 | 10 us | 100 % |
| the integrate 0x008902B0 | 13-14 us | 60-71 % |
| the moving bit set | | 50-67 % |

Per frame with 64 actors: the controller 1.41 ms, of it the support check 0.64 ms and the integrate about 0.59 ms
(derived from the calls and means; the means are printed to a microsecond). The same split as in the crowd scenario
of 2026-11-10/11 (knowledge/threading.md).

What that says about "no support check for a standing actor" (Put back): the actors whose integrate the engine
already leaves out are 29-40 % of the updates here, so the skip reaches 0.19-0.26 ms a frame at the docks without
the far throttle (derived), and less with it. Medium cost, and a standing actor must still notice the ground
leaving - it stays put back. The support check of the moving two thirds (about 0.4 ms) is the ground under a
walking actor and is not a repeat.

## 2026-11-12: near actors by where the camera looks (measured, a count only)

`[Route] ActorViewCount=1` (off by default; `tools/hand-scripts/obng-measure-docks-view-count.ini`) sorts every
update call of a near actor (within `ActorFarDistance`, so not throttled) by where it stands for the camera:
inside a cone of 75 degrees half angle around the eye pass's direction, in front but outside it, or behind; and by
its distance from the player in three bands. The place is the player's (with a head's height), the direction the
last eye pass's: the renderer's camera position (+0xA00+0x30) is 0 0 0 at that moment - the render is
camera-relative (measured in the run's samples; knowledge/culling-and-draw-setup.md has the origin global
0x00B3F92C) - while column 2 of the view is the world-space forward (measured: -0.616 -0.788 0 at the script's
heading of 218 degrees). The first run (20261112-112847) used the renderer's position and is void.

Run `artifacts/shadow/obng-profile-anvil-docks/20261112-113230`, the nine windows with actors after the first
(5 796 frames; the script turns the head through eight directions, so this is a mean over directions, not one
view), the far throttle on:

| band (units from the player) | near actors a frame | inside the cone | in front, outside | behind |
|---|---:|---:|---:|---:|
| under 1000 | 1.0 | 86 % | 14 % | 0 % |
| 1000-3000 | 9.7 | 60 % | 22 % | 18 % |
| beyond 3000 (to 6000) | 8.3 | 52 % | 29 % | 20 % |

19.0 near actors a frame (and 88 far ones, throttled). Outside the cone and beyond 1000 units: 7.9 a frame, 42 %
of the near actors; behind and beyond 1000 units: 3.4 a frame.

Derived: an update of a near actor is about 80 us without the throttle (Actor::Update 71 us and the high process's
slot 4 8.5 us, measured above). The far rule's rate (every 16th call) on the 7.9 would take about 0.6 ms a frame
at the docks; on the 3.4 behind only, about 0.25 ms. Occlusion is not in the count (an actor behind a wall inside
the cone counts as inside). Open before anything is built: an actor outside the cone can come into view within a
few frames of a head turn (the cone's 20 degrees of margin are five frames at 300 degrees a second), so it must
run at once when it enters the cone; and an actor in combat behind the player must not fight at a sixteenth of the
rate.

## 2026-11-12: the view rule for near actors (`[Route] ActorViewRule`, built, off by default; class C)

A near actor that stands outside a cone around the main camera's direction (`ActorViewConeDegrees`, 75; 90 = only
behind), at least `ActorViewMinDistance` units away (1000) and not in combat gets the far actors' rate (`ActorFarEvery`,
with the summed time and the summed animation steps); when it enters the cone its next call runs, as for a far
actor that comes near. It rides on `ActorFarWhole=1` (the same detour of Actor::Update 0x00603CA0).
`ActorViewAlternate=1` switches it by summary window for a measurement.

- The direction's source is the world scene graph's camera ([0x00B333CC]+0xDC; its world rotation at +0x64, first
  column, and its world translate at +0x88), not a render pass - so the rule does not depend on the culler, the
  stereo route or the pass mode. Measured in three docks runs with the head turned through eight directions:
  single pass (`obng-profile-anvil-docks/20261112-114811`: equal to the last eye pass's direction in every
  sample), dual pass (`obng-profile-anvil-docks-dual/20261112-115118`) and the flat game with `[Render] Enabled=0`
  (`obng-profile-anvil-docks-flat/20261112-115350`): the same sequence of directions in all three, the place the
  player's plus about 110 units. OBVR writes the head into that node (read by the OBVR session in its
  src/camera/CameraHook.cpp). Not run: the game without OBVR.dll at all.
- Combat: the actor's slot +0x334 (xOBSE: Actor::IsInCombat(bool), slot 0xCD; 0x005E6110 for Character and
  Creature, read: the process's package of type 0x0C or 0x0D), asked only for an actor the rule would take. No
  actor fought in the runs (0 kept for combat): the exception is covered by the unit flows and the read, not by a
  game run.
- Gain, single pass, alternating by window (measured): ProcessManager::Update 2.70-3.19 ms a frame with the rule
  against 3.35-3.69 without, about 0.6 ms; it took 6.4-10.9 of about 20 near actors a frame (and 41-47 far ones
  were throttled already).
- Open: the look in the headset on a head turn (an actor that enters the cone has stood still for up to sixteen
  frames and then moves that time's way at once); a run with a fight behind the player; occlusion (an actor behind
  a wall inside the cone is not taken).

### The view rule with a fight behind the player (measured 2026-11-12, run obng-view-rule-combat/20261112-120131)

`tools/hand-scripts/obng-view-rule-combat.txt` on the Gold Road (the Linux mod list, Walkers' hordes about): the
rule always on. The ten of the opening's cast placed 1500 units behind the player did not fight - from their
placement on, 7 440-7 510 calls a window stood behind in the 1000-3000 band and the calls at the lower rate rose by
about as many: the rule took them, as it should take actors that stand about. But a fight was already going on
behind the player beyond 3000 units (not placed by the script; which actors is not read - the hordes of this mod
list are the likely ones): for five windows 5 483-9 012 calls a window were kept at the full rate for combat (7 to
12 actors a frame), falling to 343 and 0 as that fight ended, while the calls at the lower rate rose by the same
amount. So the exception works in the game: an actor the engine counts as in combat is not taken, and it is taken
again when its combat ends. Not shown: that the fight went as it does without the rule (no comparison run); an
actor that flees or is attacked without a combat package (not read).

Put into the Windows game on 2026-11-12 with `ActorViewRule=1` (Nadi's go), cone 75 degrees, from 1000 units.

### The view rule's hidden part (`[Route] ActorViewHidden`, built 2026-11-12, off by default; class C)

An actor inside the cone whose scene node (the reference's +0x3C) the occlusion culler left out of the walk in one
of the last two eye passes - its bound drew no pixel against the frame's depth - gets every
`ActorViewHiddenEvery`-th call (4; gentler than the far rate, because it can come round a corner with no margin of
angle), from the same distance on and not in combat. Only where the culler's node cull runs (the single pass, or
the flat game with `OcclusionCullFlat=1`); elsewhere the cone alone decides. `ActorViewAlternate=2` switches only
this part by summary window. The answer comes from the depth test, so what shows through a grating's gaps is not
hidden (Nadi's condition, 2026-11-12: "not at gratings one can see through").

- The docks, single pass, the part alternating (run `obng-profile-anvil-docks/20261112-120931`, measured): of
  13-17 near actors a frame inside the cone the culler left out 0.3-2.7; ProcessManager::Update in the standing
  windows 2.31 / 2.32 ms with the part against 2.51 / 2.30 without - no gain that shows (two actors at three
  quarters of 80 us would be 0.12 ms). The docks are an open place; a walled town or an interior is not measured.
- The new game's prison (run `obng-cull-newgame-bars/20261112-121203`, measured, the distance set to 0 for the
  run): the part took one actor a frame throughout - Baurus (00032A17), waiting behind the walls, which is also the
  "one node a pass" the node log of 2026-11-11 left open - and for a window each Glenroy (00032A16) and the Emperor
  (00032A18) at the same place. Nobody else: the count of calls taken equals one actor a frame, so the prisoner
  in the cell opposite, behind the bars, was never taken as hidden. Not shown: the Blades standing at the bars
  (the run does not say that they came that far), and Windows, where the bars defect of 2026-11-11 is still open.

### The hidden part in a walled town: the Market District (measured 2026-11-12, run obng-profile-market/20261112-124619) - put back

`tools/hand-scripts/obng-profile-market.txt` (the docks script's head turns at `coc ICMarketDistrict01`, the
Windows mod list, single pass), the cone rule on, the hidden part alternating. The first run (20261112-124259)
stood still after thirteen seconds: a mod's "Quest added" box pauses the world; the script closes menus at every
mark since. Ten windows:

- 20-34 near actors a frame and none far (the district is its own world space); 8-18 of them inside the cone, and
  2-8 of those nearer than 1000 units, where the rule takes nobody.
- Inside the cone and left out by the culler: 0.05-1.9 a frame. The part took 0.2-1.8 a frame in its windows.
- ProcessManager::Update with the part 3.10-4.06 ms a frame, without 2.61-5.46: the spread between windows (a fight
  began in the second half: up to 5 911 calls a window kept for combat) is larger than anything the part could
  gain - at one to two actors and three quarters of 80 us that is about 0.1 ms.

With the docks (0.3-2.7 a frame) that is two places where the culler's node cull leaves out one or two actors of
the ten to eighteen inside the cone. The part works and holds Nadi's condition at the bars on Linux, and gains
about 0.1 ms: it stays off and is put back. What would change the picture is a count of actors hidden as a whole
that the node cull does not reach (it asks only nodes whose draws were all hidden before) - not looked at.

## 2026-11-12: the references' animation update (`[Route] ReferenceAnimEvery`, built, off by default; class C)

What 0x00440400 on g_TES is (read; the backlog had it as "not read", 0.71-1.0 ms a frame): it clears
[0x00B33C28] and jumps - in an interior into TESObjectCELL::UpdateReferenceAnimations 0x004CCC50 (proposal) for
the one cell ([tes+0x34]), outside into 0x00482470, which calls 0x004CCC50 for every cell of the grid ([tes+0x08]:
its size at +0x0C, the cells at +0x10, eight bytes each). That is the caller knowledge/render-thread-outside-the-
pass.md left unverified. 0x004CCC50 calls slot +0x160 of each reference of the cell that is no actor (or an actor
with +0x78 set, or 0x0045A500), 0x004E3F30 for a plain reference.

Measured at Anvil's docks with the Windows mod list (`tools/hand-scripts/obng-measure-docks-ref-animations.ini`,
run `obng-profile-anvil-docks/20261112-130655`): 25 cells and about 4 760 calls of 0x004E3F30 a frame; 0x00482470
0.89-1.68 ms a frame with a timer on every call, the cells' own walk 0.13-0.14 ms of it.

The route (`plugin/src/ReferenceAnimRate.cpp`, `src/engine/scene/ReferenceAnimRateLogic.h`): a reference found
with nothing to do (0x004E3F30's own gates, read again after its call) is called again only in one frame of n from
the cell's loop; one with something to do every frame; any other caller gets the original. Lights and references
with an NPC's or a creature's base (types 0x1A, 0x23, 0x24, for which 0x004E3F30 calls slot +0xFC first) are never
left out. `ReferenceAnimAlternate=1` switches it by window. n = 4 in the runs.

Run `obng-profile-anvil-docks/20261112-133246` (the route timing the calls that run, by kind; per frame, the
steady windows):

| kind of reference | calls a frame | ms a frame without the route | with it |
|---|---:|---:|---:|
| nothing to do | 4 709 (1 182 with the route) | 0.30-0.32 (0.06-0.07 us each) | 0.05 |
| an NPC's or creature's base | 71-76 | 0.18-0.36 (2.6-4.8 us each; 1.0-1.1 while the head turns) | the same |
| a controller manager with sequences | 10 | 0.003 | the same |
| lights | 1 | 0.003 | the same |
| animation data on a plain reference | 0 | | |

- The gain is the idle references' 0.26 ms a frame (0x00482470: 0.51-0.70 ms with the route against 0.62-0.77
  without). Not the 0.6-0.7 ms hoped for: an idle reference's gate costs 60-70 ns here, not the cold loads the
  sampler showed on 2026-11-08; n = 16 would add 0.03 ms.
- What stays is the 71-76 actors' call - the animation update 0x00476D10 outside Actor::Update that the far
  actors' fix sums (2026-11-11: 0.12-0.16 ms a frame for the far ones; here all of them, and three times dearer
  while the head turns).
- Noticed late: one reference in ten windows (found with something to do on its turn); 0 in the run before
  (`...-132903`). No call ran without a slot in the table.
- Open: the look at doors, levers and flames in the headset (a start up to three frames late); interiors.

### What makes the actors' animation call dear: the arrival, not the head's turning (measured 2026-11-12, run obng-profile-anvil-docks-still-first/20261112-134643)

`tools/hand-scripts/obng-profile-anvil-docks-still-first.txt` stands forty seconds before the eight head turns
(`obng-measure-docks-anim-phases.ini`: the references' route on with its time by kind, a summary every 5 s). The
windows mapped onto the marks by the clock both logs share (the HandScript marks' "tick" and the culler's "totals
qpc"). The cells' loop's call for the 44-185 actors a frame (an NPC's or creature's base - 0x004E3F30 steps their
animation through 0x00476D10):

| phase | windows | us a call | ms a frame | ProcessManager::Update | frames in 5 s |
|---|---|---:|---:|---:|---:|
| the forty seconds after the arrival, standing | w09-w16 | 17-77 | 1.8-6.4 | 3.7-11.2 ms | 88-260 |
| standing on | w17-w18 | 5.5-9 | 0.7-1.2 | 3.5-4.0 | 195-274 |
| the eight head turns | w19-w24 | 5.5-8.6 | 0.49-0.80 | 3.2-4.1 | 242-373 |
| standing after the turns | w25-w33 | 6.8-28 | 0.6-1.6 | 3.7-7.2 | 138-302 |

So the head's turning costs nothing extra; the earlier reading "three times dearer while the head turns" was the
arrival, which the docks script's turns used to follow at once. For about thirty seconds after arriving the
actors' animation calls cost ten times their later price and the frame rate falls to 18-52 a second (Linux,
harness); later in the run it rises again (w29-w31, standing) without a turn. Not known: what the call does then
(first sequences, blends, loads - 0x00476D10's callees were not timed), and whether Windows shows the same.

### The three routes together (measured 2026-11-12, run obng-profile-anvil-docks/20261112-134958)

`tools/hand-scripts/obng-measure-docks-three-routes.ini`: the far actors' whole update (every 16th call beyond
6000 units), the view rule (75 degrees, from 1000 units) and the references' rate (every 4th frame) switched on
and off together by 10 s window, the culler on in both. The steady windows after the turns, per frame:

| | ProcessManager::Update | 0x00482470 (references) | the two | UpdateStep |
|---|---:|---:|---:|---:|
| all three on (w11, w13) | 2.69 / 2.72 ms | 0.64 / 0.83 | 3.33 / 3.55 | 16.3 / 18.2 |
| all three off (w10, w12, w14) | 4.93 / 4.97 / 4.47 | 0.81 / 1.00 / 0.84 | 5.74 / 5.97 / 5.31 | 18.4 / 22.6 / 17.5 |

The three together: about 2.0-2.6 ms a frame on the measured parts at the docks (about 40 far actors left out a
frame, 6-8 near ones taken by the view rule, 3 530 references left out). The whole run was slower than the
morning's docks runs (27-61 frames a second against 75; UpdateStep 16-30 ms against 13.2) - not explained; the
OBVR.dll in the game changed between them (a59e316b) and nothing here separates that from other causes. This is
what the Windows game now has on (the three keys are set there).

### The arrival: first-time work, and how much depends on what is cached (measured 2026-11-12, three runs)

The same still-first docks run three times in a row (Linux, Windows mod list, single pass):

| run | after | the thirty seconds after the arrival | the actors' animation call | frames in 5 s |
|---|---|---|---:|---:|
| `...-still-first/20261112-134643` | OBVR's own runs with a new OBVR.dll | UpdateStep 41 ms, RenderFrame 20 ms; Input 0.18 ms and Sound 0.24 ms (0.03 later) | 1.8-6.4 ms a frame | 88-260 |
| `...-still-first/20261112-140045` | the run above | UpdateStep 18-31 ms for about 25 s | 1.4-2.4 | 161-297 |
| `...-still-first/20261112-140513` | the run above | none after the load's own window | 0.2-1.1 for 10 s | 341-376 (75 a second) |

- Run 3 timed the animation update's callees (`obng-measure-docks-arrival.ini`): in the load's window and the ten
  seconds after it the time is in 0x00474070 (22.5 ms a frame in the load's 26 frames, about 1 ms a frame after;
  5 calls a frame) - the function that adds an animation to the actor's keyframe manager (its error string
  "Unable to add '%s' to keyframe manager on '%s'", read in re/export/rea-all 0x474070.c): the actors' first
  animations being set up on the main thread. The other callees stay under 0.1 ms.
- Run 2 had `tools/thread-sampler.py` beside it (each thread's CPU time from /proc twice a second): in the
  arrival the main thread ran 0.74 of each second against 0.81-0.87 later, the DXVK thread and the sound mixer
  no more than later, and the machine was busier (35 % of its sixteen threads against 19-23 %) - the main thread
  waited, on something outside the game's own threads. Input and Sound six times dearer in run 1 point the same
  way (both are system calls under Wine). Derived, not measured: files not yet in the page cache (the Windows mod
  list lives on an ntfs3 NVMe partition) and, for RenderFrame, first-time shader work.
- So: the first arrival after other work is slow for up to half a minute, a second arrival at the same place
  hardly. What a modern engine does instead: animations (and meshes, textures) streamed in on worker threads
  before they are needed, not set up on the main thread on first use. Not measured on Windows; not yet measured
  with a cold page cache on purpose (needs `drop_caches`, root).

### The arrival with a cold page cache: not the disk - the number of actors (measured 2026-11-12, run obng-profile-anvil-docks-still-first/20261112-141835)

Nadi dropped the page cache (`drop_caches`, fincore: the archives 0 bytes resident), then the still-first run with
`tools/thread-sampler.py` now also counting each thread's reads (/proc/<pid>/task/<tid>/io):

- The load itself (ready to placed, 12 s): the main thread read 45 MB a second from the disk. The thirty seconds
  after it: 1.3 MB a second from the disk (17 MB a second through read calls, served from the cache), 0.2 and 0.0
  later. The disk is not what the arrival waits on; the derivation above ("files not yet in the page cache") is
  refuted.
- The arrival was moderate again: UpdateStep 17-26 ms for about 35 s, the keyframe manager's adds (0x00474070)
  0.6-4.5 ms a frame for 25 s.
- What differs between the four still-first runs is the number of actors: skeleton updates and far actors' calls
  a frame in the windows after the arrival - run 134643 (severe) 21-25 / 31-113, run 140045 (moderate) 21-28 /
  42-77, run 140513 (none) 21-25 / 16-27, the cold run (moderate) 21-31 / 80-226. The run without an arrival cost
  had a quarter of the far actors. Derived: the half minute after an arrival is the actors who come into the
  loaded area getting their first animations set up on the main thread, and it grows with how many come - where
  that number comes from (the town's NPCs, Oblivion Walkers' hordes, both) is not separated.

### Who the actors at the docks are (measured 2026-11-12, run obng-profile-anvil-docks-still-first/20261112-142459)

`tools/hand-scripts/obng-measure-docks-arrival-census.ini`: the actor census on (it takes Actor::Update's entry,
so the far actors' route is off in this run), the keyframe manager's adds 0x00474070 timed. The plugin indices
from the run's load order as `tools/run-with-windows-mods.py` builds it (the Windows list, then what only Linux
has, masters first - reconstructed, not read from the game): 00 Oblivion.esm, 02 Better Cities Resources.esm,
38 OblivionWalkers.esp, 3F Maskar's Oblivion Overhaul.esp; FF = made in the game.

- After the arrival 52-63 actors are updated a frame, 18-23 of them within 6000 units. Half have a reference
  made in the game (FF): 23 in the first window after the load, growing to 35 over the next thirty seconds while
  the other prefixes stay. By base: 15-17 from OblivionWalkers.esp (the hordes), 8-21 Better Cities, 10-17
  Oblivion.esm, 7-9 bases made in the game, 3-5 Maskar's Oblivion Overhaul.
- The keyframe manager's adds: 659 a frame for the load's 14 frames (41.7 ms a frame - the hitch of the load
  itself), then 5-23 a frame at 0.9-1.8 ms a frame for thirty seconds - while the actors made in the game go
  from 23 to 35 - and none after. So the arrival's tail is mostly actors that are made or come in after the load,
  each setting up its first animations on the main thread; Walkers' hordes are about a third of the actors, not
  most of them, and which of the twelve that came later were Walkers' is not separated.
- Without the far route the actors' frame stays at 4.3-8.3 ms (ProcessManager::Update) and UpdateStep at 15-33
  ms through the whole run - the route (on in play) is what keeps the docks near 75 frames a second.

### How far the docks are from 11.1 ms (derived 2026-11-12 from run obng-profile-anvil-docks-still-first/20261112-140513)

The thread sampler beside the warm run (the far actors' route and the references' rate on, the culler on, no view
rule; 74.6 frames a second, the harness's 75 Hz cap): the main thread used 0.78 / 0.73 / 0.80 s of CPU a second
standing / turning / standing - 10.5 / 9.8 / 10.7 ms a frame against the 11.1 ms of 90 Hz. Unverified: whether a
part of that is a spin-wait (OBVR's frame clock, a driver) rather than work; on Windows nothing of this is
measured.

### The main thread's 10.5 ms are work, not a spin (derived 2026-11-12, no further run)

OBVR's capture of the same warm run (`.../20261112-140513/OBVR-performance-1-2-report/report.md`) has the frame's
waits: `poses_wait` (WaitGetPoses) 2.50 ms mean, `present` 1.01 ms, `pacing_vsync` 3.09 ms - the last is not a
wait but an interval OBVR marks from the poses to the predicted vsync (read in OBVR's src/render/
HeadsetRenderer.cpp, the Mark call with posesTick and m_vsyncTick). At 74.6 frames a second the frame is 13.4 ms;
the main thread was off the CPU 2.7-3.6 ms of it (the sampler: 0.73-0.80 s of CPU a second), which is the poses
wait and a part of the present - so the waits block, they do not spin, and the 9.8-10.7 ms a frame are the
game's, OBNG's and OBVR's work. At the docks on Linux that leaves 0.4-1.3 ms to 11.1 ms. Also in the capture:
`present` p99 12.6 ms and `scene_pass` max 50 ms - single long frames, not looked at.

## 2026-11-12: OBNG's own code in the scene pass, measured by step (R1)

The eye switch's port steps timed (`[Route] StereoDrawTimer`, `tools/hand-scripts/obng-measure-docks-switch-steps.ini`,
run `obng-profile-anvil-docks/20261112-151057`, the docks, the culler off; three windows of 633-737 frames, about
1 410 applied switches a frame). Per applied switch (a switch to the second eye and back):

| step | calls a switch | ns a call | ms a frame |
|---|---:|---:|---:|
| GetVertexShaderConstantF (c0..c47, before and after the re-apply) | 2 | 34-35 | 0.095 |
| the constants' re-apply (the engine's own map) | 1 | 90 | 0.13 |
| SetTransform(VIEW) (there and back) | 2 | 20-22 | 0.057 |
| SetVertexShaderConstantF (the carry and the restore) | 1.17 | 31-32 | 0.051 |
| SetViewport (there and back) | 2 | 11-12 | 0.031 |
| write rows / read view / GetViewport (the draw's eye check) | 2 / 1 / 1.1 | 8-14 | 0.055 |
| the rest: the switch's own logic (axes, rows, the carry's compares) and the timers | | | about 0.2 |

The switch is 0.44-0.46 us with these timers and 0.37 without (this morning): about 0.6 ms a frame timed, 0.5
untimed. Each device call is cheap (11-35 ns); the re-apply is the engine's and needed. What is reachable in the
same computation (class A): the switch's own logic (a part of about 0.1 ms) and possibly the two SetTransform
calls for draws with a vertex shader (0.06 ms; whether any routed draw uses the fixed-function transform is
unverified). The culler's own work at the docks was 0.68 ms a frame (decide 0.26, the tests' issue 0.25, the
results' read 0.17; run 20261112-085817): ~1 000 tests a pass, 82 % of them confirming a draw that is visible.

## 2026-11-12: S3 - the culler tests a visible draw in one frame of four; the eye switch's clock reads only for a measurement

- **`[Route] OcclusionCullVisibleEvery`** (class C, built, 1 = as before): a draw found visible is tested again in
  one frame of n (its turn from its handle, and anyway after n frames); a draw skipped on a hidden verdict, one
  with a hidden verdict on record and one never tested are tested every frame as before
  (`OcclusionCullLogic.h` TestDue, its flows in `plugin/tests/OcclusionCullLogicTest.cpp`). The culler can only
  skip less, never more: a draw that has just become hidden is drawn up to n - 1 frames longer, as without the
  culler. `OcclusionCullVisibleAlternate=1` switches it by window.
- Run `obng-profile-anvil-docks/20261112-152535` (`tools/hand-scripts/obng-measure-docks-cull-visible.ini`, n = 4,
  alternating; the culler's own lines per eye pass, one eye pass a frame):

| per eye pass, the steady windows | rule off (w06, w10, w12) | rule on (w05, w09, w11, w13) |
|---|---:|---:|
| tests | 995-1 019 | 369-393 (613-628 visible ones left out) |
| the tests' issue (end tests) | 0.238-0.246 ms | 0.101-0.106 |
| the results' read (collect) | 0.182-0.187 ms | 0.077-0.088 |
| draws skipped | 13-14 % | 13-14 % |
| skips unconfirmed (the popping's bound) | 0-40 | 0-108 (0 in the last three) |
| the eye pass with the culler (pass ms) | 3.67 (paused windows), 3.89 | 3.37-3.58 |

  So about 0.24 ms a frame of the culler's own CPU time, and the eye pass 0.1-0.3 ms shorter; the GPU's share
  (the test rectangles rasterized) is not measured. In the windows w10-w13 the world stood (a message box after
  the "still" mark: ProcessManager::Update 0 calls) - the scene was drawn the same; the docks script now closes
  menus every ten seconds of its last forty.
- **The eye switch** (class A): the switch reads the clock only with `StereoDrawTimer` (four reads a switch
  before), and the view probe's eye phase is a plain store (two locked exchanges a switch before; writer and
  readers are the render thread's). Derived from the instruction counts: 0.05-0.1 ms a frame at 1 410 switches.
  Not shown by measurement: the baseline run with the previous DLL (`...-152303`) had an eye pass of 3.69-3.85 ms
  in its steady windows against 3.67 in the new run's paused ones - within what two runs differ by.

## 2026-11-12: U1 - how many of an eye pass's draws could be one instanced draw (measured)

`[Route] StereoDrawTimer`'s instance count (`plugin/src/InstanceGroupLogic.h`, `StereoDrawTimer.h`): every eighth
frame, every pass setup (slot 12) of the whole draw for the eyes, keyed by the geometry's model data (+0xB4) and the
pass object (shader+0x3C), then also by the property state's ten slots and the effect state's five dwords. The culler
off (`obng-measure-docks-switch-steps.ini`). Runs `obng-profile-anvil-docks/20261112-153638`,
`obng-profile-market/20261112-153910` and `-154248`:

| place | pass setups a pass | groups by mesh and pass | draws in groups of 4+ | largest | and the same property pointers |
|---|---:|---:|---:|---:|---:|
| Anvil docks | 1 078-1 113 | 655-718 (34-39 % fewer) | 226-345 | 12 | 0-1 % fewer |
| Market District | 758-979 | 462-550 (39-44 % fewer) | 172-351 | 25-30 | 4-8 % fewer |

Which property differs between two draws of one mesh and pass (Market District, 23 495 and 30 299 pairs): the
material property (slot 2) in 86-93 %, the shader property (slot 4) in 84-91 % - but its class (vtable) in 0 % -, the
alpha property in 22-23 %, vertex colour 18-20 %, stencil 13-15 %, the texturing property (slot 6) in 2 %. So the
copies of a mesh share their textures and their shader class; each has its own material and shader property object
(whether their contents differ is not read). The effect state adds nothing beyond the properties.

Derived: instancing could merge about a third to two fifths of the eye pass's whole draws (groups up to 12 at the
docks, 30 in the town), about 0.9-1.1 ms a frame at the measured 2.6 us a draw - if the merged draw runs its own
vertex and pixel shaders that take per instance what the engine uploads per draw now: the world transform, the
lights in model space (0x007C9140), the material and the shader property's values. That shader work is what makes it
huge; the textures do not stand in the way.

## 2026-11-12: V1 - what an instanced path would have to carry per instance (measured)

The instance count extended (`plugin/src/StereoDrawTimer.cpp`, read under SEH; `InstanceGroupLogic.h`): within a
group of one mesh and pass, besides the ten property slots, the material's values (NiMaterialProperty +0x1C..+0x53,
xOBSE NiProperties.h), the shader property's flags (+0x1C) and alpha (+0x20), its lights (BSShaderLightingProperty's
list, head +0x70, count +0x78; walked with Gamebryo's node layout next +0 / element +8 - unverified), and the pass's
two programs (pass +0x44 / +0x58). Runs `obng-profile-market/20261112-155012` and `obng-profile-anvil-docks/20261112-155207`:

| differs within a group of one mesh and pass | Market District | Anvil docks |
|---|---:|---:|
| the material object / its values | 79-90 % / 20-35 % | 91-92 % / 16-17 % |
| the shader property object / its class / its flags / its alpha | 77-90 % / 0 / 0 / 19-35 % | 96-97 % / 0 / 0 / 16-17 % |
| the lights (set / count) | 0-2 % / 0-1 % | 7-11 % / 7-11 % |
| the textures (texturing property) | 0-2 % | 0 % |
| the programs | 0 % | 0 % |

The grouped draws' pass objects: up to 8-17 a pass; 2-4 of them cover 80 % and 3-6 cover 90 % of the grouped
draws, the largest alone 56-81 %.

Derived: the copies of a mesh mostly share textures, programs and lights; what differs per instance is the world
transform, the lights' model-space positions that follow from it (0x007C9140), and - for a sixth to a third of them
- the alpha (material and shader property together: fading objects). An instanced path would need instanced
versions of the programs of two to six pass objects, taking the world transform and an alpha per instance and the
lights in world space once. Not yet read: which programs those are (their names and constant maps).

A crash in the Market District run (the first of its kind in the artifacts): an access violation reading 0x5A4 at
0x0048E811 in 0x0048E740, called from TESObjectREFR::RunScripts 0x004D7190 (xOBSE) - a list walk on a reference's
script data hitting a bad node, after the scripted head turns. Not attributed: OBNG's code in that run only read
(the count under SEH, the timers); the far actors' route was on; OBVR.dll was its new build a59e316b (the "Yield"
and "Stagger watch" lines just before), Walkers and the Windows mod list's quests were loaded.

## 2026-11-12: W1 - the programs of the groupable draws (measured, read)

The instance count names the largest pass objects with the device's programs (runs
`obng-profile-anvil-docks/20261112-160123`, `obng-profile-market/20261112-160355`); the bytecode matched against the
shader packages and disassembled - the table and what it means in `knowledge/draw-instancing.md`. The largest pass
(56-81 % of the grouped draws at both places) draws with SLS2003.vso (33 instructions) and SLS2003.pso (22): a
directional light with fog and a projected map. Per instance: the world transform and an alpha. The instanced path
sized (derived): instanced versions of 1 (most of the gain) to 4-5 program pairs of 22-43 vertex and 22-88 pixel
instructions (SM3LL011.pso with 185 is the exception, 4 % in the town), plus the collecting draw path - large, no
longer huge.

## 2026-11-12: Y1 M2/M3 - the instanced draw of SLS2003 groups (`[Route] DrawInstancing`, built, off by default; class C)

What is built and what it must share: `knowledge/draw-instancing.md`, "The instanced path as built". Runs (Linux,
DXVK, the Windows mod list, single pass, the routes as played):

| run | what | result |
|---|---|---|
| `obng-profile-anvil-docks/20261112-170441` | mode 1, every light in the key | 631 of 915 records a frame waited; ModelViewProj, light, pixel constants, textures, programs equal for every copy; ShadowProj derived apart for 22 % |
| `-171014` | the first light only | nothing more apart; splits: vertex colour 75 032, stencil 3 720, alpha 2 232 records split off their mesh by property pointer |
| `-171607`, `-171912` | small properties by value, both shadow forms | groups at the mesh's limit (280 732 against 279 995 by pass and mesh); ShadowProj = the copy's own world rows, error 0; c7.y (fog) apart for 734 |
| `-172316` | the record's dword in the key | every check clean; the one ModelViewProj case over 1e-4 in the run 2.0e-4 |
| `-173641` | mode 2, on in every second frame | 109 instanced groups a frame for 360 copies, 0 fallbacks; the pass lists' draws 0.25-0.27 ms a frame less in the frames with it on (2.85-3.76 against 3.10-4.01 ms) |
| `obng-anvil-bc-eyedump/20261112-173950` | the eye frames, alternating | the barrels' iron bands apart (their highlight pass) |
| `obng-anvil-bc-eyedump/20261112-174831` | geometries with more passes drawn at once | no difference beyond frame-to-frame noise |
| `obng-profile-anvil-docks/20261112-175454` | mode 2 as it stands | 0.15-0.26 ms a frame less (3.62-4.29 against 3.84-4.49 ms), 4-6 %; 81 records a frame left out as multi-pass |
| `obng-profile-market/20261112-175728` | the same in the Market District | no gain (-0.07 to +0.07 ms): of 341 records a frame 108 multi-pass and 79 skinned, 87 waited |

Costs inside (docks): the record step 0.10-0.18 ms a frame, an instanced device call 0.8 us with everything it sets
and puts back (two a group, one an eye). The earlier window-by-window comparison (`-172611`, `-173157`) showed the
eye pass 0.3 ms slower in the on windows - the windows differ in what the camera looks at; the frame-by-frame split
of one window is the comparison that holds.

Derived: as it stands the path is worth about 0.2 ms a frame where copies of single-pass meshes dominate and nothing
in a town; the barrier is the multi-pass geometry. Next lever: clip positions bit-identical to the engine's
(ModelViewProj per copy and eye computed with the engine's own matrix calls), so that multi-pass geometries may wait
too - not built, not measured.

### A0: what multi-pass geometry would add in the Market District (measured 2026-11-12, run obng-profile-market/20261112-180719)

Mode 1 with `DrawInstancingMultiPass=1` (`tools/hand-scripts/obng-measure-instancing-multipass.ini`): every record
that could wait if bit-identical positions were built. Steady windows: 640-830 records a frame in the pass lists,
212-279 waited, in groups that leave 56 records a frame out at most (last window: 182 474 records in 140 526 groups,
748 frames); the rest at once - other programs than SLS2003 ("pass not predicted") 270-310 a frame, skinned 64-194,
first of list 33. Every constant check clean for the multi-pass copies too (ModelViewProj largest 8e-5, ShadowProj as
the copy's world rows exact). Derived: the bit-identical positions (option A) would reach about 56 draws a frame
here, at about 2-3 us each 0.1-0.15 ms before their own overhead - not worth building; the town's draws are other
programs and skinned bodies. Draw instancing stays off by default, worth about 0.2 ms at the docks; put back.

### P2: draw instancing on in the Windows game (2026-11-12)

OBNG.dll of 1bdaca1 (md5 990d8837...) into the Windows game, `[Route] DrawInstancing=2` in its OBNG.ini (backups
of the DLL of a014c88 and the INI before the change in the session's scratchpad). Untested on Windows and in the
headset until Nadi says otherwise. Whether the Windows build of the stack answers the device's Get calls (the
instanced draw reads the vertex registers back) is unverified there; where it does not, every group falls back to
single draws (the summary's "fell back: device").

## 2026-11-12: P1 - the Market District's frame as played (measured, run obng-profile-market/20261112-182234)

The routes as played on Windows after P2 (`tools/hand-scripts/obng-measure-market-play.ini`), the Dream Air's frame
size (an eye of 4036x3376), Linux and the Beyond at 75 Hz, `tools/thread-sampler.py` beside the run, OBVR's capture
and render sampler.

- **Frame rate**: 41-72 frames a second by window (411-723 frames in 10 s) - the town does not hold 75 Hz here; the
  compositor showed 40 % of the frames more than once.
- **GPU** (the compositor's frame timing, 4 765 frames): the application's GPU time before submit 9.75 ms median
  (p75 11.2, p95 14.0), after submit 4.2 ms, in all 14.6 ms median (p95 20.8) - over the 13.3 ms of 75 Hz and far
  over the 11.1 ms of 90 Hz. OBVR's own GPU timing (`GpuTiming=1`) wrote zero durations in this run (for OBVR).
- **Main thread** (thread sampler, steady phase): 0.68-0.78 s of CPU a second at 41-57 frames a second - 14-18 ms of
  work a frame (the docks: 10.5 ms).
- **Where the main thread's frame goes** (the [Timed] lines, a steady window of 418 frames, ms a frame): UpdateStep
  23.8 in all, of it RenderFrame 10.9 (the driver - cull walk and finish - 7.9, of that the eye pass's finish 6.0
  with 1 364 draws a pass and 291 left out by the culler; the rest of the render 3.0), the actors' process manager 4.5
  (the high actors' loop 4.25, ApplyAnimData 1.48 for 25 actors), the cells 1.8 (grid update 0.67, the references'
  animation 0.59, the selected nodes 0.55), 0x004424D0 0.68; the waits (the poses 3.9 ms mean) fill the rest.
- **The render sampler** (in-pass samples of the steady phase, 14 194): the engine's own code 45 %, D3D9 (DXVK)
  18 %, OBNG.dll 16 %, Oblivion Frame Fixes' copier 4 %, OBVR 4 %.

Derived: in a town at the Dream Air's size the GPU is the first wall - 14.6 ms of the application's GPU work a frame
against 11.1 ms at 90 Hz, the same hardware on Windows (unverified there) - and the main thread the second, 14-18 ms
against 11.1. Neither is one item: the GPU needs about a quarter off, the main thread about a third. What fills the
GPU's 9.75 ms is not measured (OBVR's timer gave zeros).

## 2026-11-12: G1, G0, C1, C2 - the Market District's GPU by step, two frame sizes, the actors, the eye pass's draws

### G1: the GPU time by step (`[Probe] GpuPasses`, built; measured, run obng-profile-market/20261112-183740)

`plugin/src/GpuPassTimer.{h,cpp}`, `GpuPassTimerLogic.h`: Direct3D 9 timestamp queries around the actor shadows step
0x004073D0, every non-empty pass list of every accumulator finish (by finish, pass id, records, the first record's
shader class by RTTI) and the image space step 0x007B48E0, one frame of four, read back without waiting
(`tools/hand-scripts/obng-measure-market-gpu.ini`). 0 frames lost, 0 disjoint. Per frame, the routes as played, the
Dream Air's frame size:

| window | frames in 10 s | actor shadows | eye pass's lists | all lists | image space | first stamp to last |
|---|---:|---:|---:|---:|---:|---:|
| heavy (w03-w05) | 236-449 | 1.67-2.26 | 2.81-4.38 | 5.82-7.76 | 0.39-0.42 | the frame's length |
| steady (w10-w12) | 749-750 | 1.08-1.22 | 1.15-2.30 | 3.23-3.50 | 0.35 | 13.3 |

The span from the first stamp to the last is the frame's length in every window: the GPU's timeline has long gaps
between the stamped steps, and a step's time is an upper bound of the GPU's work in it (a stamp is written when
the GPU gets there; inside a step it can wait for the CPU's next commands). Derived: the GPU waits for the CPU in
this town - **P1's "the GPU is the first wall" is withdrawn**: the compositor's 9.75 ms before submit hold those
waits too.

### G0: the Beyond's frame size against the Dream Air's (measured, run obng-profile-market-beyond/20261112-184108)

The same INI with an eye of 2568x2568 (`tools/hand-scripts/obng-profile-market-beyond.txt`): the image space 0.21
ms against 0.35-0.36 (it goes with the pixels, 1.7 times for 2.07 times the pixels); the eye pass's lists 0.6-3.5
ms against 0.9-4.4 - not with the pixels; the heavy windows drop to 437-579 frames in 10 s at the small size as at
the large one (the AI makes each run's crowd its own). Derived: the town is bound by the main thread at both sizes.

### C1: the actors (measured, run obng-profile-market/20261112-184435, the timers inflate)

`tools/hand-scripts/obng-measure-market-actors.ini`. The heaviest window (655 frames, 31.4 high updates and 25.9
skeleton updates a frame), ms a frame: the process manager 3.70, the high actors' loop 3.19, the high update
0x00605770 2.88 (92 us a call); inside it the skeleton update ApplyAnimData 1.35 (52 us an actor - 31-34 at the
docks), the character controller 0.48 (19.5 us), the animation step 0.47 (18.9 us), 0x005F6540 0.32, the high
process's slot 4 0.32 (10.5 us), slot 8 0.14 (1 614 calls), 0x00471C00 0.12; once a frame UpdateHighProcesses
0.50; the cells 1.4 (grid update 0.49, the references' animation 0.50, the selected nodes 0.44).

### C2: the eye pass's draws (measured, the culler's lines and G1's lists, heavy windows)

Whole shapes 804-896 drawn a pass (206-217 left out by the culler), skinned 97-217, direct strips 99-101 (49-50
left out), direct shapes 16. By pass id (records a frame): 132 (ShadowLightShader, SLS2003) 338-392, **375 =
0x177 (ShadowLightShader) 126-201**, 139 29-70, 141 (SkinShader) 28-63, 185 35-42, 367 0-59, 287 0-68, 14
(SpeedTreeLeafShader) 13-16. Passes 0x177..0x17A are the ones SetupTransformations gives six clip planes from the
shadow light (knowledge/culling-and-draw-setup.md, step 8): the receivers of the actors' projected shadows, drawn
again in their own pass - 126-201 draws a frame beside the shadow maps themselves (the actor shadows step, finishes
#0-#1, 256x256 targets).

Derived: the town's frame is the main thread's - the actors (3.7 ms), the eye pass (about 1 300 draws with the
culler) and the actor shadows (their step and the receiver pass, together the largest item not looked at yet).

## 2026-11-12: S1 - the actor shadows at rest (`[Route] ActorShadowEvery`, built, off by default; class C)

Built without a measurement first (Nadi: "Ja dann Bau das"); the measurement is built in.

What the step does with its lights, read 2026-11-12 in the instructions (re/export/perf-levers/0x004073D0,
re/export/actor-shadow-rest/; knowledge/actor-shadows.md "A light's frame"): the reset 0x007C5BE0 gives every
light's map (+0x114) back to the pool [0x00B42F50] and hides the light; the registration shows again the lights of
the casters it accepts; the walk places, fades and collects the geometry of each shown light; 0x007C6770 draws the
map of each shown light, 0x007D46C0 taking a map from the pool and storing the light's projection at +0x10. So
the map, the projection and the geometry list of a light last from its draw to the next step's reset - a light can
keep them through more steps if the reset leaves its map with it and nothing places or draws it.

The route (`plugin/src/ActorShadowRest.{h,cpp}`, decisions in `src/engine/render/ActorShadowRestLogic.h`, every flow
in `tests/ActorShadowRestLogicTest.cpp`): with `ActorShadowEvery=n` a light rests - keeps its map through the reset,
is counted by the walk without being placed, its map render left out - when it was drawn or at rest in the step
before, its caster is the same and its bound centre within `ActorShadowStill` units (4 by default) of where it
stood at the light's last draw, it holds its map, was in view and faded in (+0xDC 1, +0xE0 past the fade time,
+0xD8 at least 0.05), it is a casters' light (+0xF5 0) and it is not its turn (one step of n, the turns given in
order so the lights spread over the steps). A step after a gap (over 100 ms) or with another map count than the
step before keeps nothing: the pool is resized right after the reset (0x007C2710 pops a shrinking pool's free list
without a check). A light kept but not at rest (its caster not registered, the count reached) gives its map back
before the maps' render; one the walk shows again to fade out (target 0) is drawn into the map it holds.

Tolerance (class C), held by construction: a resting shadow is at most n - 1 steps old and its caster within
`still` units of the drawn place; the camera's fade and view of a resting light are as old. Five patches, all
checked against the game's bytes before any is written, the step's call written last: the scene render's call of
the step 0x0040C91B (numbers and times the step), the reset's release 0x007C5C61, the walk's draw block 0x004078DA,
the maps' render's call 0x00407A4B and its call of a light's map 0x007C6943. Not the step's entry (OBVR's shadow
share and `[Probe] GpuPasses` take it).

Measured: nothing yet. Unit tests: 61 checks over the flows of the logic. Not run in the game. Open: the run with
`tools/hand-scripts/obng-measure-market-shadows.ini` (ActorShadowEvery=4, alternating in blocks of four steps: the
step's own time on and off in one run, the lights drawn and at rest a step, the reasons a light was not kept); a
device Reset while lights rest (their maps' content after it is unverified); the headset look. What resting cannot
touch: the receivers' pass (0x177, 126-201 draws a frame in the Market) draws every frame.

