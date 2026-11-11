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
