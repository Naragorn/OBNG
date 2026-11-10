# Threads in Oblivion.exe: what exists, what the INI switches do, what the frame serialises

Status: knowledge, begun 2026-11-09 (the threading reading slice, first
pass). Marks: **read** = seen in Oblivion.exe 1.2.0.416 (address given),
**measured** = seen in the running game (run given), **derived**,
**unverified**. Tools: tools/re/setting-refs.py (an INI setting's object and
the functions whose code refers to it), tools/re/disasm.py,
tools/re/sampler-phases.py.

## The INI's threading switches: which the exe reads at all (read)

Every setting is a static object (value at +0, name pointer at +4); a
setting whose value address is referenced only by its registration code
(the 0x009Dxxxx initialisers, the list at 0x009A9D44 and the 0x00A1xxxx exit
handlers) is read by nothing.

| Setting | Object | Readers besides registration | Verdict |
|---|---|---|---|
| bUseThreadedMorpher:General | 0x00B02D20 | none | **dead** |
| bUseThreadedTempEffects:General | 0x00B02D28 | none | **dead** |
| bUseThreadedParticleSystem:General | 0x00B02D18 | none | **dead** |
| iThreads:OPENMP, iOpenMPLevel:OPENMP | 0x00B05254, 0x00B0524C | none | **dead** |
| bUseBackgroundFileLoader:BackgroundLoad | 0x00B02DF8 | none | **dead** |
| bLoadBackgroundFaceGen:BackgroundLoad | 0x00B1210C | none | **dead** |
| bDebugFaceGenMultithreading:Debug | 0x00B120FC | none | **dead** |
| bUseThreadedBlood, bUseThreadedAI, bUseMultiThreadedTrees | - | the names are not in the exe | **not this build's** |
| iNumHavokThreads:HAVOK | 0x00B097D8 | 0x004D4A20, 0x004D5000 (copied to 0x00B2E2D4 where a cell's physics world is made), read by 0x0088AEB0 | live, see below |
| bUseMultiThreadedFaceGen:BackgroundLoad | 0x00B120DC | 0x00477ED0 (with the byte 0x00B120B4), 0x005268D0 | live: gates 0x00525A70 in the actor's 3D set-up (meaning unread) |
| bBackgroundCellLoads:BackgroundLoad | 0x00B08960 | 0x00444FB0 (the grid's move with the player) | live |
| bLoadHelmetsInBackground, bBackgroundLoadLipFiles, iAnimationClonePerLoop, iPostProcessMilliseconds* | | 0x004788E0 / 0x0047BC40; the actor update 0x00603CA0 and three more; 0x00475D80..; the IO manager's 0x00433590 | live (the loader side) |
| bUseBackgroundPathing:PathFinding | 0x00B15800 | ten pathing functions (0x0063A020 ... 0x00658DF0) | live |

## The threads the exe creates (read: every code reference to the imports)

CreateThread (IAT 0x00A280F8) is called from four functions:

- 0x0047CFD0 (caller 0x0042FA10): a thread object's start, entry
  0x0047CF30, "Could not create a thread for background loading" - the
  background loader's thread class.
- 0x00430E00 (callers 0x00434230, 0x00439C10): two semaphores and a
  suspended thread, entry 0x00430DE0 - the IO manager's task threads.
- 0x00410C40 (caller 0x005B5F20): the Bink movie's.
- 0x008BAF70 (through 0x008BAFD0 from the physics world's constructor
  0x0088AEB0): Havok's worker threads, entry 0x008BADF0 - a loop that
  waits (0x008F58A0), runs 0x00898C90(world, ...) and signals (0x008F58B0).

No other thread is created by the exe's own code (the CRT's and the
libraries' aside); `_beginthreadex` is not imported. The object at
[0x00B3F940] that RenderScene 0x0040C830 signals (a counter at +0x190, a
semaphore at +0x198; 0x00701AB0 / 0x00701AD0 in the frame update) is fed
through one of these; which is **unread**.

## Havok's threads (read, measured)

- **The switch is `iSimType:HAVOK`, not the thread count** (read): the
  setting object 0x00B11638 (image value 1) is handed to 0x008896F0 by
  0x005350F0, which maps it into the byte 0x00B2E2DC - 0 -> 1, 2 -> 2,
  3 -> 5, **4 -> 9**, anything else (1 included) -> 4. The physics world's
  construction info takes that byte as its simulation type (0x0088A4F0:
  cinfo+0x95 = [0x00B2E2DC]); the hkWorld's constructor 0x0089A230 copies
  it to world+0xB4 and builds the simulation by it (ten cases; unknown
  values fall back to 4). The wrapper's constructor 0x0088AEB0 creates the
  worker threads (0x008BAFD0(world, [0x00B2E2D4] = iNumHavokThreads) ->
  CreateThread in 0x008BAF70, entry 0x008BADF0) **only for type 9**.
  The live Oblivion.ini has iSimType=1 and iNumHavokThreads=1: the default
  simulation, no workers. (The image's own initial byte at 0x00B2E2DC is
  9; the setting overwrites it at start-up.)
- So iNumHavokThreads alone does nothing (measured, run -230119: Havok on
  the render thread 1.26 / 2.50 samples a frame on the road / with 60 NPCs
  against 1.01 / 2.37; weak run, see below).
- **iSimType=4 with iNumHavokThreads=3** (run obng-actors-count/
  20261109-230822, Oblivion.ini changed for the run only): the game runs
  the scenario to its end, no crash. Havok's samples on the render thread
  were **higher**, 1.95 / 2.56 / 3.08 / 3.48 a frame over the four steps
  against 1.01 / 1.35 / 1.82 / 2.37, and the road ran at 50 Hz instead of
  75. Not a result yet: another game was running on the machine in both
  Havok runs, and the crowd was not drawn in either (the scenario's
  flake). What the higher share is - the render thread stepping its part
  and waiting for the workers inside Havok, or the load - needs the
  repeat on a quiet machine, and the workers' own time (they are not the
  sampled thread).
- **The pair on the quiet machine** (2026-11-09 23:27-23:33, runs
  obng-actors-count/20261109-232709 with iSimType=1 and -232937 with
  iSimType=4 and iNumHavokThreads=3; the crowd drawn in both, the sampler
  by phase, samples a frame on the render thread):

  | step | frames in 20 s (1 / 4) | out of the pass (1 / 4) | Havok (1 / 4) |
  |---|---:|---:|---:|
  | road | 1500 / 1499 | 8.44 / 8.56 | 0.96 / 0.59 |
  | +20 NPCs | 878 / 895 | 12.54 / 12.50 | 1.14 / 1.11 |
  | +40 | 746 / 731 | 12.88 / 13.43 | 1.71 / 1.66 |
  | +60 | 319 / 326 | 17.08 / 17.29 | 2.19 / 1.99 |

  The multithreaded type runs without a crash and takes 0.4 of Havok's
  samples off the render thread on the empty road and 0-0.2 with the
  crowd; the frame count and the out-of-pass total do not move. **No
  gain worth a changed simulation**: Havok is 1-2 ms of the frame and its
  step here does not spread. The earlier loaded run's higher numbers were
  the load.
- Unread: what simulation types 1, 2 and 5 are; whether the engine's own
  callbacks (collision listeners, the character controller) are safe under
  type 9 on PC - the Xbox 360 build is the one that would have used it.

## What the frame does in sequence (read: re/functions/0x0040D800.md, 0x0040D4D0.md, 0x0040C830.md)

One thread runs, in order: input and the IO manager's post-processing ->
the world's time and animation -> sound -> the actor process manager
(AI: 0x00678510 and its low/middle/high process updates, each actor's
update with its ApplyAnimData) -> the player -> the grid's move -> the
actor processes' time step 0x0067ACA0 -> sky and weather -> the scene
node update -> the render routine 0x0040D4D0: RenderScene (cell pre-pass,
actor shadow maps, the scene pass, first person, image space) -> the
interface -> present. Havok's step is planned at the top
(PlanHavokSteps 0x00889810) and run from the actors' side.

Measured where the render thread's time goes outside the scene pass
(sampler by phase, run obng-actors-count/20261109-183942, samples a frame,
the empty road -> 60 NPCs): game code (actors, AI, scripts, cells) 1.8 ->
4.8; NiMain (scene graph, animation) 0.6 -> 2.9; Havok 1.0 -> 2.4; ntdll
0.7 -> 1.3; CRT 0.25 -> 0.7. No single function above 0.9; the largest
are the heap's slow path 0x00402480 (+0.8), 0x0070AC00 (+0.66), the
controller walk 0x0047C930 (+0.6).

## What this says so far (derived)

- The engine's own parallelism on this build is the loaders (cells,
  helmets, lip files, FaceGen set-up, pathing requests) and, possibly,
  Havok's workers. Morpher, particles, temp effects and OpenMP are
  switches without readers: setting them does nothing.
- The cheapest candidate for real parallel work is Havok's existing worker
  path, and it is two INI lines ([HAVOK] iSimType=4, iNumHavokThreads=N):
  no code of OBNG's - and measured it gains nothing that shows in the
  frame (the pair above). The engine has no cheap parallelism left to
  switch on.
- Everything else (the actors' AI and animation beside the scene pass) has
  no existing mechanism; it would be OBNG's own, with the scene graph's
  readers and writers to prove apart - large.

## The actors' frame, timed (measured 2026-11-10)

Step timers on the actors' pipeline ([Probe] ActorUpdateTiming, the
original skeleton update, run obng-actors-count/20261110-095916, ms a
frame, 5 -> 60 -> 80 -> 100 -> 117 actor updates a frame):

| step | 5 | 60 | 80 | 100 | 117 |
|---|---|---|---|---|---|
| ProcessManager::Update 0x00678510 | 0.14 | 3.0-3.5 | 4.8-5.1 | 8.2 | 9.2-10.0 |
| of it actor updates 0x00603CA0 | 0.08-0.14 | 2.5-2.9 | 4.1-4.3 | 6.8-6.9 | 7.6-8.4 |
| of them ApplyAnimData 0x00471F20 (original update) | 0.04-0.07 | 1.2-1.3 | 2.1-2.2 | 3.5-3.6 | 3.8-4.3 |
| of them 0x00471C00 | 0.03-0.04 | 0.33-0.39 | 0.37-0.44 | 0.60-0.66 | 0.74-0.78 |
| of them the animation step 0x00603500 | 0.01 | 0.01-0.02 | 0.14-0.16 | 0.46-0.47 | 0.49-0.59 |
| UpdateHighProcesses 0x00673C10 | 0.09-0.13 | 0.10-0.12 | 0.11-0.13 | 0.21-0.23 | 0.17-0.19 |

The process manager's update is the crowd's cost outside the scene pass:
0.14 ms on the empty road, 9-10 ms with 117 actors. The actor updates come
from the loop over the high actors 0x00677EC0 (the manager's fourth call),
not from 0x00673C10. ApplyAnimData is half of an actor's update with the
original skeleton update and about a third with the route (22-24 us an
actor); the other half - AI, movement, 0x00471C00 - is 40-47 us an actor
and has not been looked into. 1.5 ms of the manager's update is outside
the actor updates.

With the actors' batch (docs/subsystems/actor-batch.md, run 20261110-
101048, the route on): 97 skeleton updates a loop are 2.15 ms of down
passes and 0.34 ms of upward passes and bookkeeping; the loop without them
4.4-4.6 ms. The down passes are what threads could share: at four threads
1.6 ms less at best with 97 actors, 0.3 ms with 34 (derived; the collision
objects' share, about 3 us an actor, stays on one thread).
