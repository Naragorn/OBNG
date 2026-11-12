#pragma once
// The actors' down passes on threads, inside the actors' batch ([Route]
// ActorBatch=1, which needs [Route] ActorUpdate=1; engine/scene/
// ActorThreadsLogic.h has the reading).
//
//   [Route] ActorThreads=n (1..7, 0 = off): n worker threads beside the
//     loop's thread run the down passes of the skeletons whose collision
//     objects are all in a safe class (the nodes left to the down pass);
//     their collision updates wait and run on the loop's thread before each
//     skeleton's upward pass, in the batch's order. A skeleton with any other
//     collision object, or with collision objects under a subtree the engine
//     runs itself, runs its down pass there too. The workers get the loop
//     thread's x87 control word and MXCSR. ActorThreadsAlternate=1 runs the
//     threads in every second batch only (the down phase's time by side).
//   [Probe] ActorThreadsCheck=1: no threads; every collision update of the
//     batch's down passes classified and its node's transforms compared
//     before and after (which classes write their node, and whether a
//     skeleton called safe ever did), and in one batch of 16 the EXE's .data
//     copied before and after the down passes (which statics they write).
//
// Class B: the down passes are bit for bit the one-thread batch's per
// skeleton; what changes is when a collision update runs against the others'
// down passes.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ShadowTimeInfo.h"
#include "engine/scene/ActorBatchLogic.h"

namespace obng::plugin {

void InstallActorThreads(const char* iniPath, LogFn log);
void LogActorThreadsSummary();

namespace actor_threads {

// Whether the batch's down phase goes through RunDowns.
bool Active();

using OrderOf = const void* (*)(std::uint32_t root);

// The batch's down phase for the split jobs (the loop's thread).
void RunDowns(const std::vector<obng::engine::scene::actor_batch::Job>& jobs, const std::vector<std::size_t>& split, OrderOf orderOf);

// Before a split job's upward pass, in the batch's order (the loop's
// thread): its waiting collision updates, or its down pass when the threads
// did not run it.
void BeforeUp(const obng::engine::scene::actor_batch::Job& job, const void* order);

}  // namespace actor_threads

}  // namespace obng::plugin
