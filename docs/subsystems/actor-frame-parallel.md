# The actors' frame on several threads (spec, 2026-11-14)

Nadi, 2026-11-14: "wir wollen ja eig große teile die perf kosten im game decompen und dann parallelisieren und
modernisieren". This is the first of the two large pieces: what the game does for its actors each frame - the
process manager's update 0x00678510 - read whole, mapped by what each part reads and writes, and then run on
several threads where that map allows it. The second piece is docs/subsystems/render-prep-parallel.md.

## Why this piece (measured)

| where | actors | the process manager a call | of a frame |
|---|---:|---:|---:|
| Nadi's Windows session 2026-11-14, a horde in Anvil | 86-127 skeleton updates | 4-7 ms | 34 ms |
| Linux, the gate horde without sound (run obng-profile-gate-guards/20261114-164228, with the timers) | 175 high updates | 21.0 ms | 55 ms |
| Linux, obng-actors-count 2026-11-10 | 117 | 9.2-10.0 ms | - |

The gate horde's call by its parts ([Timed], exclusive, ms a call): the high update 0x00605770 15.6 inclusive, of
it the skeleton updates (ApplyAnimData 0x00471F20) 5.3, the character controllers 0x00896000 4.9, 0x005FAAE0 2.2,
0x00471C00 1.2, 0x005F6540 1.0, 0x0063A210 0.8, the animation step 0x00603500 0.7; the loop over the high actors
0x00677EC0 itself 4.1 outside every timed function. It is a loop over actors: the natural unit of a thread's work.

## What is already known, and what it rules out

- **The skeleton updates cannot go to threads as they are** (docs/subsystems/actor-batch.md, measured 2026-11-12):
  every skeleton carries collision objects whose update writes its node from its Havok body inside the down pass, in
  the order the children depend on; 0 of 8 000-14 000 skeletons a window were safe. What is left there is the down
  passes with the collision updates kept on the loop's thread and a repair after them (0.3-0.6 ms in the Market,
  derived). The batch (`[Route] ActorBatch`, one thread) and the planner (`[Route] ActorThreads`) are built.
- **The character controller is the largest single item** (knowledge/threading.md): one update an actor and frame,
  10-37 us, 55-60 % of the loop without the skeleton updates; its cost is convex tests of the actor's phantom
  against the triangles it stands on (19 of 24 tests an update), in the support check 0x008AE890 and the integrate
  0x008902B0. The contact pass inside the integrate is rebuilt (`[Route] ContactDedup`, verified).
- **The frame's order** (knowledge/threading.md, "What the frame does in sequence") and that Havok's world step is
  not inside the process manager.
- Not known: what the high update, the loop, 0x005FAAE0, 0x00471C00, 0x005F6540, 0x0063A210 and the controller's
  support check read and write outside their own actor - the map this spec's first two steps make.

## The plan

1. **Timers into every part of an actor's frame** (`[Timed]` lines for the callees of 0x00605770 and 0x00677EC0 that
   are not timed yet), in the gate horde without sound and in obng-actors-count: every part's share, and what is left
   untimed. Cheap.
2. **The read-and-write map**: the slice `actor-high-update` through the slice workflow (docs/re-workflow.md; map,
   refuters, no rebuild): per function what it reads and writes of (a) its own actor, (b) other actors, (c) the
   Havok world, (d) the engine's globals and lists, (e) the heap. The result is `knowledge/actor-frame-map.md`:
   which parts are an actor's own, which are shared and through what.
3. **The first parallel part**, chosen by 1 and 2. The candidate by today's numbers: the character controllers'
   casts - they read the Havok world and write their own phantom and collector (derived from the call path, to be
   shown by 2) - run for all actors before the loop on worker threads, the loop then taking each actor's result.
   Class B: bit-identical per actor in a shadow check (one actor, the same world: the same manifold and velocity),
   then the game gates.
4. **The next parts** by the map: the AI's own work per actor (packages, detection), 0x005FAAE0.

## Gates (AGENTS.md classes)

- A part moved to threads is class B: per unit the same result as the original on the same inputs (a check mode in
  the game that runs both and compares, as `ContactDedup=2` and `OcclusionCullQuick=2` do), then the scenarios, a
  long run without a crash (the stress flight, the gate horde), Nadi's look in the headset, the peer sessions'
  regression runs.
- A write watch over the EXE's data sections during the part on one thread lists the statics it writes
  (`[Probe] ActorThreadsCheck` does this for the down passes); a static written is a lock, a per-thread copy, or the
  part stays on the loop's thread.
- Worker threads take the loop thread's x87 control word and MXCSR (the skeleton update's lesson).
- Every measurement: the gate horde with `bSoundEnabled=0` (under Proton the sound's wait hides the frame,
  knowledge/voice-lines.md), the mean and the worst frame, before and after under the same builds of OBVR and
  Oblivion Walkers in one sitting.

## Risks

- Shared state nobody has listed: Havok's world (phantoms are added and removed, the broad phase moves), the
  process lists, the actors' packages, the heap's lock (0x00401040 / 0x00401AA0 are in every part's samples).
- Order: later actors see earlier actors' results of this frame in the original (as the batch already changes for
  the skeletons). Each such dependence is found by the map or by the check mode's mismatches.
- 32-bit address space: worker stacks and per-thread scratch count against 4 GB (3.1 GB used in the gate horde).

## What it could gain (derived, not measured)

With the controllers alone on four threads: 4.9 ms -> about 1.5 ms a call in the gate horde, about 1 ms in Nadi's
Windows horde. With every per-actor part that the map clears: 15.6 ms -> 4-6 ms in the gate horde. Amdahl's rest is
the loop's own 4.1 ms until it is read.

## Step 2's result (2026-11-14): knowledge/actor-frame-map.md

The slice `actor-high-update` went through the workflow (78 agents, 13 of 14 functions mapped and held, the
controller's update 0x00896000 disputed on a debug branch's label). What the map says for the plan:

- **No per-actor part is clear as it stands.** Each of the candidates writes a shared static or was not followed
  far enough: the controller's integrate reads the iteration cap `[0x00B2EFB8]` that the update writes per actor, and
  every cast goes through 0x008DE950, which counts on the Havok world (+0x88) and updates the broad phase; detection
  0x005F6540 counts on `[0x00B333C0]` without a lock; the package update 0x0063A210 writes into another actor's
  process (0x0063B0F3) and queues background pathing; the animation step 0x00471C00 and Actor::Update write the byte
  `[0x00B3CBD0]`; head tracking 0x00603500 calls detection.
- **The controller's update is not called where the plan assumed**: it is reached through a vtable slot from the
  actor's movement (0x005E0660 under the high process's 0x00643340, measured in the sampler), not from
  Actor::Update. "Run the casts before the loop" needs that path read first.
- Most blockers are small and of one kind - a counter, a cap, a flag in a static - which a per-thread copy or a
  value passed along can take away; the world's counter and broad-phase update in 0x008DE950 is the one that
  decides whether casts can run side by side at all. It is read in pseudocode only.

So step 3 does not start yet. Before it: the slice `actor-movement` (0x005E0660, 0x00643340, 0x00628790, 0x005F1910,
0x006814B0, 0x005232D0, 0x008DE950, 0x008ABA40, 0x008ABAC0, 0x008AD070) with the same questions, and the write watch
over the EXE's data during one actor's controller update (the probe `[Probe] ActorThreadsCheck` has the mechanism).

## Step 1's result (2026-11-14, run 20261114-205959, the gate horde without sound)

`obng-measure-gate-guards-parts.ini`: the process manager, the loop, the high update and 24 of their direct
callees timed (4 150 process manager calls, 146 high updates a call). Per process manager call: 14.0 ms, the loop
13.65 ms inclusive and **1.30 ms of its own**, the high update 12.30 ms inclusive - and **12.06 ms of that outside
every timed direct callee**. All 24 direct callees together are under 0.4 ms (the largest: 0x0065A2C0 0.09 ms at
2 032 calls, 0x006A2280 0.07 ms). So nothing hides in the loop's or the high update's own helpers: the time is in
Actor::Update 0x00603CA0 (not timeable here - the far actors' route holds its entry) and in the virtual calls into
the process (the slots the run of 16:42 timed: the skeleton updates 5.3 ms, the character controllers 4.9 ms,
0x005FAAE0 2.2 ms, with 175 actors then). The loop's own share is 1.3 ms, not the 4.1 ms the earlier run's
subtraction gave (that run timed fewer of its callees). Two earlier runs of this INI (20261114-202927, -205xxx) are
void: the INI had lost its route sections.

## The second slice's result (2026-11-14, actor-movement: 54 agents, 10 functions mapped and held)

- **The call site named above is withdrawn**: 0x005E0660 and the functions above it are getters; the sampler's stack
  words were stale. Where the controller's update is called from is open again; an entry probe on 0x00896000 that
  records its return address is the next cheap step.
- **The controller's casts cannot simply run side by side**: each cast moves the phantom in the Havok world's broad
  phase (0x008DE950: an unguarded counter, the broad phase, the pair tables), and the support check widens a
  world-wide tolerance around its query. Havok's own way out is in the same function: with the world locked the
  move is queued as a pending operation instead of done. A parallel phase would be: the world locked, every
  actor's casts run against the broad phase as it stands, the queued moves applied afterwards on one thread - if
  the queue (0x00898820), the cast's reads of the broad phase and the collectors are safe for that, which is not
  read. That is the question for step 3, and it is a larger one than the plan assumed: not "a per-thread copy of a
  counter" but "Havok's world used read-only by several threads".
- Unchanged: the smaller blockers of the first slice (the iteration cap, the detection counter, the flag byte).

## Where Havok's time is, and the waits (2026-11-14 evening)

**Linux, the gate horde without sound, timed** (`obng-measure-gate-guards-havok.ini`, run
obng-profile-gate-guards/20261114-211745, 4 773 frames, 158 high updates a frame; ms a frame): the update step
0x0040D800 33.8 inclusive, 7.5 of its own outside the timed functions; the process manager 14.0; the render's frame
function 10.2; the skeleton updates 3.8; **the character controllers 3.57 (the support check 1.48, the integrate
1.82)**; **the Havok world's step 0x004CCCE0 1.41** (under the cells' grid update, 0.8 calls a frame). The render
sampler of the same run has 8.9 ms a frame with the eip in Havok's code (0x0088A000-0x0097FFFF) at 48.3 ms a frame.
So of Havok's 8.9 ms about 3.3 are the controllers' casts, 1.4 the world's step, and **about 4.2 lie elsewhere** -
not timed: the skeletons' collision objects inside ApplyAnimData, ray casts from the AI (0x006814B0 is one), others.
Havok's time is not one place.

**Windows, Nadi's horde, the 6.2 ms a frame with the eip in ntdll** (capture OBVR-performance-3; the nearest export of
Windows' own SysWOW64 ntdll.dll, read):

| | ms a frame |
|---|---:|
| ZwWaitForSingleObject, under vrclient.dll - the wait for SteamVR, idle | 2.64 |
| ZwOpenKeyEx, under the sound update 0x006AE860 (1 122 of 1 452 samples) and the WAV reader 0x006AABD0 | 1.11 |
| ZwWaitForAlertByThreadId / ZwAlertThreadByThreadIdEx, under D3D9.DLL mostly | 0.58 |
| the rest (critical sections, mutants, the performance counter) | about 1.9 |

- 2.6 ms of the "waiting" are the frame's pacing, no work to take away.
- The sound's part: registry keys opened from the sound update, about a millisecond a frame in a horde - with the
  WAV reader's own 0.3 ms a candidate for a cache of sound buffers (derived: that a buffer's making opens the keys
  is not read; DirectSound's code is not in the capture).

## The rest of Havok's time: not the collision objects, not the AI's casts (measured 2026-11-14 21:24)

`obng-measure-gate-guards-havok-rest.ini`, run obng-profile-gate-guards/20261114-212444 (4 216 frames, 122 high
updates a frame, ms a frame): the skeletons' collision objects together 0.34 (0x0088EDC0 0.14 at 1 982 calls,
0x0088E9F0 0.11, 0x0088F3D0 0.07, 0x00897510 0.02); the AI's casts 0.56 (line of sight 0x005F2820 0.36 at 157 calls,
the ray ahead 0x006814B0 0.20 at 476 calls). Neither is where the unassigned 4 ms of Havok are; what is left to hold
them is the loop's own 3.5 ms and the update step's own 7.8 ms, both outside every timed function.

The per-actor parts in the same run: the skeleton updates 4.26, the controllers 3.76, **vitals 0x005FAAE0 1.97 with
a longest call of 50.8 ms**, 0x00471C00 0.94, detection 0.83, the package update 0.61, head tracking 0.58. The
vitals' longest call is a hitch of its own (one actor, 50 ms): not read why.

## Step 3, first part: the statics the per-actor parts write (2026-11-14, measured, [Timed] WritesOf)

Run `artifacts/shadow/obng-profile-gate-guards/20261114-214301` (the gate horde, Linux, the Windows mod list, sound
off, `tools/hand-scripts/obng-measure-gate-guards-writes.ini`): every 8th call of six functions with the EXE's
.data (0x00B02000, 0xA9C1C bytes) copied at its entry and its return (plugin/src/TimedWrites.h). The probe sees the
statics only, not what a call writes into heap objects; what another thread wrote during a probed call is counted
too; the tally keeps 1024 places a function, and three functions filled it (the counts of their rarer places are
low by that). "Share" is the probed calls in which the place differed.

| function | probed calls | written most often (share) |
|---|---|---|
| vitals 0x005FAAE0 | 63 151 | 0x00BA7924 34 %; 0x00B3BAAC 6 %; Havok's stack area 2 % a place |
| detection 0x005F6540 | 88 608 | 0x00B333C0 81 %; 0x00B333BC, 0x00BA7924, 0x00BA792C 6.6 % each; 0x00B47904 6 % |
| package 0x0063A210 | 54 530 | 0x00B3BAAC 8 %; 0x00BA7924 2.4 %; 0x00B3BAB8 2.1 %; 0x00B02048 2 % |
| head tracking 0x00603500 | 48 724 | 0x00B02048 0.6 %; all others under 0.2 % (196 places, 859 runs in all) |
| 0x00471C00 | 52 449 | nothing above 0.1 % (32 places, 280 runs in all) |
| character controller 0x00896000 | 48 608 | 0x00B47900 96 %, 0x00B47934 74 %, 0x00B48910 62 % and 750 more places of that area |

What the places are (read, re/export/rea-all/functions):

- **0x00B47900 .. 0x00BA7900: Havok's stack area of the main thread.** 0x0088B070 (Havok's start) hands
  `(&DAT_00B47900, 0x60000)` to the thread memory found in the thread's TLS slot +0x19C (or the global one at
  0x00BA7D9C). Every Havok query writes there: the controller in 96 % of its calls, detection, vitals and the package
  through their casts. A thread that runs a Havok query needs a thread memory and a stack area of its own first -
  with the world used read-only, the second known condition of the controllers' casts on threads.
- **0x00BA7924, 0x00BA7928, 0x00BA792C, 0x00BA7930, 0x00BA7934: the frame's cast counters** in the cast wrapper
  0x0088B850: every cast counts 0x00BA7924 up, by the caster's layer the others; with the count above 0x00B2E2F4
  (set at 0x00441970 from 0x00B05224) the cast is not done and returns "nothing hit" (+0x50 = 0, +0x44 = 1.0). This
  is a budget of casts a frame shared by all actors: who gets a cast depends on the order of the actors. On threads
  the counter needs a rule that does not depend on timing (a share an actor, or the budget handed out before the
  threads start); a plain atomic counter would make which actor loses its casts a race.
- **0x00B333C0, 0x00B333BC: detection's counters** (0x005F2820 the line of sight: 0x00B333BC counted up per cast and
  compared with a limit from 0x00B148FC) - the same kind of budget, already on the map.
- **0x00B3BAAC..B4 and 0x00B3BAB8..C0: scratch vectors** of 0x0065ADD0 and 0x0065AE80 (a bound's centre less and
  plus its extent, computed into the static and then copied to the caller's result). Harmless in one thread, a race
  on threads: two callers at once can copy each other's vector. Each needs a local instead (two small rebuilds).
- 0x00B02048, 0x00B0203C, 0x00B02030 and neighbours: written in under 2 % of the calls of every function, also seen
  in the actors' down passes (docs/subsystems/actor-batch.md); not read. Unverified whether the probed functions
  write them or another thread did during the probe.

What follows (derived):

- **Head tracking 0x00603500 and 0x00471C00 write no static of their own** in this horde. They are the first
  candidates for the thread scaffold; what they write into heap objects other than their own actor still has to
  come from the code (knowledge/actor-frame-map.md), the probe cannot show it.
- **Vitals, detection and the package cast into the Havok world** and so share the stack area and the cast budget;
  they are in the same class as the controller, not "light parts". They go onto threads only together with the
  Havok conditions (a thread memory a worker, the world read-only, the budgets by rule).

## The controller's time by its Havok calls (2026-11-14, measured, run obng-profile-gate-guards/20261114-215120)

`tools/hand-scripts/obng-measure-gate-guards-controller.ini`, the last window: 171 frames of 58.2 ms, 185 controller
updates a frame, sound off.

| | calls a frame | ms a frame | of the controller |
|---|---:|---:|---:|
| controller update 0x00896000, all | 185 | 4.57 | 100 % |
| 0x008AD070 (under the support check 0x008AE100), exclusive | 186 | 1.83 | 40 % |
| 0x008AD7D0 (the proxy's integrate), exclusive | 128 | 1.85 | 41 % |
| the phantom's broad-phase update 0x008DE950 (all callers) | 655 | 0.32 | 7 % |
| the phantom's transform 0x008ABA40, with its broad-phase update | 185 | 0.19 | |
| 0x008EC790 | 315 | 0.10 | |
| the controller's own code | | 0.31 | 7 % |
| pending world operations queued 0x008D8830 / run 0x00899210 | 2.1 / 1.3 | 0.002 | |

- **Measured**: four fifths of a controller's time lie in two functions' own code and their untimed callees
  (0x008AD070: 0x008A6E40, 0x008A75D0, 0x008AC3C0 ...; 0x008AD7D0: 0x008AC640, 0x008AF890, 0x008ABD40 ...), not in
  the broad phase. The one part that changes the world for everyone - the broad phase's update - is 0.3 ms a frame.
- **Derived**: a split in two is possible by the cost: the broad-phase updates on the main thread (0.3 ms), the
  two heavy functions beside each other. What stands against it is what they write (knowledge/actor-frame-map.md):
  0x008AD070 writes the world's tolerance `[world+0x74]+0x08` and puts it back, 0x008AD7D0 ends in
  0x008ABAC0 -> 0x008DE950 (the broad phase again) and writes the cap `[0x00B2EFB8]`; both use Havok's one stack
  area. The slice havok-thread-conditions reads what is under them.
- The cast wrapper 0x0088B850 is called 579 times a frame and costs 0.25 ms in all: the AI's casts are cheap here,
  the frame budget is what matters about them, not their time. 0x008B22F0 is never called in this scene.
- The world is rarely locked while a phantom moves: two operations a frame are queued.
