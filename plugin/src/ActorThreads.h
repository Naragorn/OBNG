#pragma once
// The actors' down passes in two parts, inside the actors' batch ([Route]
// ActorBatch=1, which needs [Route] ActorUpdate=1; engine/scene/
// ActorDownSplitLogic.h has the two parts and what they keep).
//
//   [Route] ActorThreads=n (1..7, 0 = off): n worker threads beside the
//     loop's thread run every skeleton's first part - the controllers, the
//     world transforms, the plain geometry bounds. The collision objects'
//     updates, the subtrees the engine runs itself (a FaceGen head, a light,
//     particles) and the nodes' bounds follow on the loop's thread before
//     each skeleton's upward pass, in the batch's order; a node a collision
//     update wrote gets its subtree composed again there. A skeleton whose
//     root hangs below another skeleton of the batch runs whole on the loop's
//     thread. The workers get the loop thread's x87 control word and MXCSR.
//     ActorThreadsAlternate=1 runs every second batch flat on one thread (the
//     down phase's time by side).
//   [Probe] ActorThreadsCheck=1: no threads; both parts on the loop's thread,
//     with the classes of the controllers the first part runs counted, in one
//     batch of 16 the EXE's .data copied before and after the first parts
//     (the statics they write), and every world held against its parent's
//     world times its local after the sweep.
//
// Class B: a skeleton ends as the one-thread batch leaves it, bit for bit,
// except where something reads a later entry's world (the logic header's
// note); what changes beyond that is when a collision update runs against
// the other skeletons' first parts.
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
// thread): its sweep and its nodes' bounds, or its whole down pass when it
// was held back.
void BeforeUp(const obng::engine::scene::actor_batch::Job& job, const void* order);

}  // namespace actor_threads

}  // namespace obng::plugin
