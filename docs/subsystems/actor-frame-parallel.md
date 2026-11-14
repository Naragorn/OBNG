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
