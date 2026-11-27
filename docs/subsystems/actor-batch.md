# The actors' batch

What: inside the frame's loop over the high actors (0x00677EC0, called by
the process manager's update 0x00678510 with (0, 0)) each actor's skeleton
update - the body of ActorAnimData::ApplyAnimData 0x00471F20 - is noted
instead of run, and all of them run after the loop in three steps: prepare
(one thread), the subtrees' down passes, the parents' upward passes in the
loop's order. Why: the down passes are the only part of the actors' frame
that touches one actor's subtree alone, so they are what several threads
could share; this is the reordering that has to stand first.

## The shape

- `src/engine/scene/ActorBatchLogic.h`: ApplyAnimData's body as read (both
  controllers out for the update and back), the decision defer / in place,
  the roots known from the frame before, the batch and its flush order.
  `tests/ActorBatchLogicTest.cpp` has every flow.
- `plugin/src/ActorBatch.cpp`: the two entry detours and the engine port;
  the update itself goes through the actors' route (`FastSceneUpdate.h`:
  PrepareActorSubtree, RunActorSubtreeDown, RunActorSubtreeUp).
- `[Route] ActorBatch=1`, needs `[Route] ActorUpdate=1`; off in the shipped
  INI.

## What it changes (derived from the read order)

The order alone. An actor's own update continues behind the call with its
skeleton of the frame before, and later actors' updates see earlier actors'
skeletons of the frame before (in the original they see them moved). What
follows the loop sees every skeleton moved. Not bit-comparable with the
original for that reason; there is no shadow for it.

In place, not deferred: outside the loop, the loop's other callers (waiting,
3600 s steps), the player (not in this loop at all as measured: 0 a loop),
a root the frame before did not see, a root twice in one loop, a full batch.

## Measured (Linux, obng-actors-count, run 20261110-101048)

The scenario passes with the batch on, no crash. With 97 bodies deferred a
loop: the loop without the skeleton updates 4.4-4.6 ms, the run after it
2.5 ms, of it the down passes 2.15 ms (22 us an actor); 34 deferred: 0.76-
0.98 ms and 0.42-0.45 ms (down 0.36-0.38). Two bodies a frame run outside
the loop (the player's among them). The picture was not compared.

## Before the down passes may go to threads (read, open)

- The collision objects: 16-18 a skeleton, each updated inside the down
  pass through bhkNiCollisionObject::UpdateWorldData 0x0088EDC0 -> vfunc
  +0x64 0x0088F3D0. That writes into the Havok world (the keyframe towards
  the node 0x008A34C0, the hard set 0x0089EA70, a motion type change
  0x0089ED20) and uses the global list 0x00BA7A40; with the blend at 1.0 it
  goes the other way (vfunc +0x68: the node from the body), which the
  children's transforms then depend on. They cannot run on several threads;
  they have to be split out of the down pass (after it, in order) for the
  skeletons where the node is not written, and skeletons where it is stay
  whole on one thread.
- The controllers (20 a plain NPC, 320 an armoured one): per actor as far
  as the structure says, not shown. A write watch over the EXE's data
  sections during a one-thread run would list every static they write.
- The upward pass is already apart; the orders' map is held for the run.
- Worker threads need the loop thread's x87 control word.

## The collision object's update, read (2026-11-10)

bhkNiCollisionObject::UpdateWorldData 0x0088EDC0 (the whole function): with
the global mode 0x00BA7A8C at 3 nothing. Otherwise by the flags word +0x0C:
bit 6 set -> 0x0089EAE0 and the bit cleared (nothing in mode 2); bit 0
clear -> nothing; else the virtual +0x64 only when (the body +0x10 is there
and 0x00607840 says so) or the blend +0x14 is below 1.0 or the motion type
+0x1C is 1.

The blend object's +0x64, 0x0088F3D0: by the blend +0x14 - at 0.0 the hard
set 0x0089EA70 (corrected 2026-11-12: the node written from the Havok body; skipped in mode 1), at 1.0 the
virtual +0x68 (the node from the body; skipped in mode 2), between them the
blended transform, written to the node through the virtual +0x78 in mode 0
and to the body through the keyframe 0x008A34C0. Before that, when the
motion type +0x1C is not the wanted one (1, or 6 at blend 1.0): +0x68 if it
was 6, else the global list 0x00BA7A40, then the change 0x0089ED20.

So an update writes its node at blend 1.0, at a blend between 0 and 1 in
mode 0, and when it leaves motion type 6; at blend 0.0 it writes the Havok
world alone. Which of these the game's actors are in, and how often, is what
`[Probe] CollisionWrites=1` counts (the node's local and world transform
compared around each update; ProbeLogic.h CollisionWrites): the share of
skeletons with no node written is the share whose collision updates could
move behind the down pass.

`[Probe] ProcessManager=1` times the whole of 0x00678510 a frame - the size
of the actors' frame in real play, which the scenario's 97 actors do not
give.

## The collision object's classes and the threads (read and built 2026-11-12)

Read in the instructions (re/export/actor-threads/0x0088EDC0, 0x0088F3D0, 0x00607840): 0x0088EDC0 returns at once in
mode 3 ([0x00BA7A8C]), with flags +0x0C bit 6 runs 0x0089EAE0 (unread; nothing in mode 2), with bit 0 clear returns,
and calls slot +0x64 when the body +0x10 is in the world (0x00607840: body, body+8, (body+8)+8 all set) or the blend
+0x14 is below 1 or the motion type +0x1C is 1. The blend object's 0x0088F3D0: blend 0 -> nothing in mode 1, else case 1
(the hard set 0x0089EA70: the node written from the body) when the previous blend +0x18 is 0 or +0x0D bit 0 is set, else case 0; blend 1
-> nothing in mode 2, else case 2 (+0x68: the node from the body) wanting motion type 6; between (NaN included) -> case
1 in mode 2, else case 0, wanting motion type 1. A motion type other than the wanted one changes first - from 6 through
+0x68 (the node written), else through the global list 0x00BA7A40 and 0x0089ED20. Case 0 writes the node through +0x78
in mode 0, and keys the body. (The first reading here had a bone at rest take a hard set that leaves its node alone;
the hard set's own instructions say otherwise, see below.)

Built (`src/engine/scene/ActorThreadsLogic.h` with `tests/ActorThreadsLogicTest.cpp`; `plugin/src/ActorThreads.{h,cpp}`,
two hooks in ActorBatch.cpp): every split job of the batch is planned on the loop's thread - every collision object put
in a class (nothing, hard set, keyframe only, blended write, node from body, motion from 6, motion change, bit 6, other
class); the job is safe when all are in the first three and no subtree the engine runs itself (Kind::Other) holds a
collision object. `[Route] ActorThreads=n` runs the safe jobs' down passes on n workers and the loop's thread, the
collision updates noted instead of run; before each split job's upward pass, in the batch's order, the loop's thread
runs its noted collision updates (or, for an unsafe job, its whole down pass). The workers take the loop thread's x87
control word and MXCSR; the loop's thread returns when every worker has left its drain. `[Probe] ActorThreadsCheck=1`
runs no threads: it classifies, compares every collision update's node before and after (the classes that write, and
"mispredicted" for a safe job's write) and copies the EXE's .data before and after the down passes in one batch of 16
(the statics they write, by how often). Class B. Not run in the game yet.

## The check in the game: no skeleton can go to a thread as built (measured 2026-11-12)

Run obng-profile-market/20261112-221344 (`tools/hand-scripts/obng-measure-market-threads-check.ini`: the routes as
played, ActorBatch=1, [Probe] ActorThreadsCheck=1; harness passed). Steady windows, 600-750 batches and 8 000-14 000
skeletons in 10 s:

- **0 safe skeletons.** Every skeleton carries about three collision objects of another class: bhkBlendCollisionObject
  AddRotation (vtable 0x00A96384, its UpdateWorldData 0x0088E9F0 not 0x0088EDC0; RTTI read in the EXE), 24 000-42 000
  updates a window, 2-10 % of them changing their node.
- Per skeleton about 15 blend objects do nothing (110 000-210 000 updates, none changed a node).
- **The hard set writes the node**: 0x0089EA70 reads the body's transform (0x004D6950, 0x004D6900) and hands it to the
  node through slot +0x78 (read). In the game 3 400-11 200 hard sets a window, 13-45 % of them changed their node.
  The 2026-11-10 reading ("the body from the node") was wrong.
- The other collision classes of the EXE (RTTI): bhkCollisionObject, bhkNiCollisionObject, bhkPCollisionObject and
  bhkSPCollisionObject share UpdateWorldData 0x00897510.
- .data written during the down passes (one batch of 16 probed): 0x00B3FC88/8C most often - the time controller's scaled
  time memo of 0x007155A0 (NiTimeController's), held under its own critical section 0x00B3FCA0 (pseudocode), so safe
  for threads; 0x00B33EAC (referenced by 0x0044D770/D810/D880, 0x00730D30, 0x007A9C30), 0x00B02048, 0x00B0203C and a few
  process manager fields (no direct code reference: written through pointers; whether by the down passes or by another
  thread in the same window is open).
- The down phase on one thread with the check: 0.57-1.56 ms a frame.

Derived: as built the threads would run nothing. A skeleton's node writes come from its collision objects reading their
bodies, inside the down pass, in the order the children depend on. What could still run in parallel: the down passes
with every collision update left for the loop's thread, and a repair there - after a collision update changed its node,
that node's subtree's world transforms recomputed (the controllers have run), the skeleton's geometry bounds redone.
Its gain is what remains of 0.6-1.6 ms after the loop's thread keeps every collision update and the repairs: about
0.3-0.6 ms in the Market (derived, rough). Not built.


## The down pass in two parts (built 2026-11-27, class B, not accepted yet)

The repair named above, built: `src/engine/scene/ActorDownSplitLogic.h` (the two parts, every flow in
`tests/ActorDownSplitLogicTest.cpp`, among them 1 200 random trees against the flat run: every world transform, bound,
flags word, Havok-side value and view the same bit for bit) and `plugin/src/ActorThreads.cpp` (the pool, the engine
port, the check). `[Route] ActorThreads=n` now sends every skeleton of the batch to the workers.

- **The workers' part** per entry: the controllers a worker may run, the world transform, a plain geometry's bound
  (its bound and bit 7 of its flags kept from before).
- **The sweep** on the loop's thread, before each skeleton's upward pass, in the batch's order: the entries the
  worker noted - held controllers, then the world composed again, then the collision object's update; the Other
  entries (the engine's own subtrees); a geometry whose bound the engine computes. While none of that changes its
  node's world nothing else is touched. From the first change on every entry is walked: a node below a changed one is
  composed again before its own updates, a plain geometry gets its bound again from the kept bound and flag. Then
  the nodes' bounds.
- **What a worker runs of the game's code** (an allow-list by vtable; everything else is held for the loop's
  thread): NiTransformController (0x00A79634; update 0x006C3C40: its target's local transform from its own
  NiTransformInterpolator 0x00A7A83C, update 0x006D60B0, which reads the shared key data and writes its own last time
  and key indices) and bhkBlendController (0x00A97AC4; update 0x008AAD60) on a node without a collision object, where
  it only stores the time (read: 0x00497420 finds no blend collision object). On a node with a collision object the
  blend controller sets that object's blend and at a blend's end calls into the Havok state (0x008AA7F0, 0x008AA3E0,
  0x008AA420) - held. Held too: NiGeomMorpherController (0x00A7A23C, about one in 2 000 controllers), every property's
  controllers (none met in the Market), any other class. The Havok-side steps - held controllers, collision updates,
  Other entries - keep the flat run's order among themselves.
- **A skeleton whose root hangs below another root of the batch** runs whole on the loop's thread (none met).
- **What differs from the one-thread batch** (the logic header has it): something that reads a later entry's world
  sees this frame's (an Other entry, a collision update, a skinned geometry's bound taken again after a change when a
  bone comes after it), and a held controller runs after the controllers a worker ran.

The check in the Market District (`[Probe] ActorThreadsCheck=1`: both parts on the loop's thread; runs
obng-profile-market/20261127-101836, -102150, -102538, -103614, -104118, all passed, measured):

- **The workers' part writes one static**: 0x00B3FC88, the memo of the controllers' scaled time (below). 28 batches a
  run probed, the EXE's .data copied before and after.
- The controllers it runs: NiTransformController 70 %, bhkBlendController 30 %; about 37 % of the transform
  controllers have an interpolator, all NiTransformInterpolator. No controller or interpolator was met twice in one
  batch (none shared between two skeletons).
- Held: bhkBlendController on nodes with a collision object (about 4.5 a skeleton), NiGeomMorpherController.
- Other entries: BSFaceGenNiNode (0x00A64F5C; its downward pass 0x0055D4C0 calls into the process manager, the
  interface and the head's animation - not read for threads), two a skeleton.
- A skeleton: 20 - 21 stops; 10 - 25 % of the skeletons had a node changed and were walked, with 150 - 280 worlds
  composed again each (the change sits near the skeleton's root). No world was found inconsistent after the sweep.
- The loop's thread's part by time: the FaceGen heads 56 - 60 %, the nodes' bounds 12 - 14 %, the collision updates
  8 - 12 %, the held controllers 3 %, the walk with its worlds and bounds 15 - 19 %.

**Crashes and hangs after the arrival, with and without the batch (measured 2026-11-27, cause not shown).** Some
runs end a fraction of a second after the world starts moving again in the Market District - the scenario's console
command CloseAllMenus at "s1" closes a message box that held the world: an access violation in the container changes'
remove 0x0048FBC0 (0x0048FC86: the virtual destructor of an extra data list whose first dword is 0 or 1 - freed
already), or in BaseExtraList::GetExtraData 0x0041E298 (a node pointer 0xBA), or the main thread turning for ever in
the script variable lookup 0x004FA110; each time under OBSE's script commands (0x004D8760 <- obse_1_2_416.dll), so a
mod's script working on NPCs' inventories as they come to life. Counted today: with `ActorBatch=1` 7 of about 35
runs (the check, the batch alone, the batch with its alternation), without it 1 of 12; before today the same crash
address came once in 53 market runs, without the batch (obng-profile-market/20261126-114713). So the defect is not
the batch's own; whether the batch makes it more frequent is not settled - the control soak was stopped after 4 of
10 runs (`artifacts/shadow/_measure/market-soak.sh`, scenario `tools/hand-scripts/obng-soak-market-arrival.txt`,
logs market-soak-1..3.log).

One hazard of the batch was found on the way and closed: high processes are destroyed inside the batched loop while
bodies are noted (12 - 13 in the first second after the arrival, the loop's end 0x00677500 demotes actors); in the
original an actor's skeleton update runs inside its own turn, while its process exists. `[Route]
ActorBatchProcessGone` (on by default with the batch): a detour on HighProcess's destructor 0x00634A30 runs
everything noted before the process goes. It does not end the crashes above (1 of 6 runs with it, 2 of 6 without).

**Before the batch or the threads can be accepted:** the crash's cause (which script, which list, who frees it
first), or at least equal rates with and without the batch over enough runs; then the gate horde
(`artifacts/shadow/_measure/gate-split.sh`, no menu command in it) as the long run and for the timing with a hundred
skeletons.

### The controllers' scaled time without its lock (class A, verified 2026-11-27)

NiTimeController's slot +0x64, 0x007155A0, takes the critical section 0x00B3FCA0 around a one-entry memo in statics
(0x00B3FC88 result, 0x00B3FC8C time, 0x00B3FC90 low, 0x00B3FC94 high, 0x00B27130 cycle type) - on every controller
update of the game, about 3 500 calls a frame in the Market. `src/engine/animation/ScaledTimeLogic.h` is the same
computation from the instructions (`tests/ScaledTimeLogicTest.cpp`); `plugin/src/ScaledTime.cpp` routes it
(`[Route] ScaledTime=1`): the main thread with a memo of its own, other threads without one. Two things read on the
way: the reverse cycle type wraps the time itself, not its offset from the low key (0x007156FA), and a memo hit
leaves the reverse flag (bit 4) out of its key.

- `[Shadow] ScaledTime=1` (run -102538): 30 466 285 calls on the main thread compared - the result, the three fields
  written and the memo - 0 mismatches; no call under another precision control, none on another thread.
- On one thread it gains nothing that counts: timed on one controller 8.0 against 10.6 ns a call with the memo
  hitting, 139.8 against 136.9 ns with the time moving (run -104432). It exists for the workers.

### Measured so far

- Three workers, every second batch flat on one thread (`ActorThreadsAlternate=1`, run -103259, the standing stretch,
  24.9 skeletons a batch; another game and a Ghidra ran beside it): the down phase with its sweeps **0.513 ms a batch
  in two parts against 0.744 ms flat** (the workers' part 0.145 ms of it).
- The frame's whole actor update, the batch with three workers and the scaled time route against the loop as it is,
  alternating by summary window (`ActorBatchAlternate=1`, [Timed] on the process manager's update 0x00678510, run
  -104953, from "s1-y-90" to "done", the other game closed; `tools/re/batch-alternate.py`): **1.49 ms a frame on
  against 1.70 ms off** at 27 high updates a frame (54.8 against 61.5 us a high update, 6 and 5 windows). One run.
- What is left on the loop's thread is mostly the FaceGen heads (56 - 60 % of the sweep's time).
