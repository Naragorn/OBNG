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

## Put back


| item | class | measured or derived gain | cost | state | where |
|---|---|---|---|---|---|
| Skeleton down passes on several threads | B | at best 1.6 ms with 97 actors, 0.3 ms with 34 (derived from 2.15 / 0.37 ms measured) | large; the collision objects must leave the down pass first, the controllers' statics shown by a write watch | the reordering is built (`[Route] ActorBatch`), one thread, not accepted | docs/subsystems/actor-batch.md |
| Character controller: no support check for a standing actor | C | about 10 us a standing actor: 0.4 ms in the crowd scenario, up to 1.3 ms with 134 standing (derived) | medium; a standing actor must still notice the ground moving away | not started | knowledge/threading.md |
| Character controller: far or unseen actors every second frame | C | by their share, not measured | medium | not started | knowledge/threading.md |
| Character controller on several threads | B | at best about 2 ms with 134 actors | large; the state machine and the contact callbacks may write into the world | not started | knowledge/threading.md |
| Volatile pack: the compare once a frame instead of once a pack | A | the compare is about 1.0 sample a frame in a crowd | small; needs a safe frame signal | put back | knowledge/skinned-vertex-streams.md |
| The cells' selected update (`[Route] GridUpdate`) | A | none measured against the original | built and verified | off | docs/subsystems/scene-update.md |
| OBNG's own code in the scene pass: the eye switch, the culler's BoundRect / Collect / Find, the draw route | A | 15 % of the pass in Anvil, about 0.5 ms a frame; half of it looks reachable (not measured) | small to medium, our own code, the link map names the functions | not started | this file |
| The skeleton route's Matches on every call | A | 0.9 % of the samples outside the pass | small | not started | this file |
| The rest of the process manager's update outside the actor updates | ? | 1.5 ms with 117 actors, not looked into | small to look | not started | knowledge/threading.md |
| 0x00471C00 before ApplyAnimData | ? | 0.74-0.78 ms with 117 actors (6-7 us a call) | small to look | not started | knowledge/threading.md |
