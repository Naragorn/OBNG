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
