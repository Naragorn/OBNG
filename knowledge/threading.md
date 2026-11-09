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

0x0088AEB0, the physics world wrapper's constructor: after the hkWorld is
made, **only when the dword at the hkWorld's +0xB4 is 9** it allocates a
0x108-byte object through 0x008BAFD0(world, [0x00B2E2D4]) and keeps it at
wrapper+0x10; 0x008BAFD0 -> 0x008BAF70 creates the worker threads from that
count. What +0xB4 is (the simulation type is the candidate) and what sets
it to 9 is **unread** - the world's construction info is filled in
0x004D4A20 / 0x004D5000 next to the copy of the setting.

Measured (run obng-actors-count/20261109-230119, iNumHavokThreads=3 in
Oblivion.ini for that run only, the crowd scenario; the machine was under
other load and the scene was not drawn in this run, so the numbers are
weak): Havok's samples on the render thread 1.26 a frame on the road and
2.50 with 60 NPCs, against 1.01 and 2.37 with one thread - **no step moved
off the render thread**. Either the world is never made with the
multithreaded type on this build, or the workers run and the render
thread waits for them inside Havok. The read of +0xB4's writer settles it.

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
- The cheapest candidate for real parallel work is therefore Havok's
  existing worker path: if the world can be made with the multithreaded
  type (one field of the construction info) the engine's own code spreads
  the step. That is a read of 0x004D4A20 / 0x0088AEB0 and the step's
  caller, then one run - small.
- Everything else (the actors' AI and animation beside the scene pass) has
  no existing mechanism; it would be OBNG's own, with the scene graph's
  readers and writers to prove apart - large.
