#pragma once

// The actors' skeleton updates of a frame collected and run after the loop
// (src/engine/scene/ActorBatchLogic.h has the reading and the rules).
//
//   [Route] ActorBatch=1: two entry detours - the process manager's loop
//     over the high actors 0x00677EC0 (the batch opens before it and runs
//     after it) and ActorAnimData::ApplyAnimData 0x00471F20 (inside the loop
//     the body is noted instead of run). The noted bodies run through the
//     actors' route (FastSceneUpdate.h: prepare, down, up), which this
//     therefore needs; PluginEntry.cpp skips it without [Route]
//     ActorUpdate.
//
// The down step runs on the loop's thread, one job after the other: what
// this switch changes is the order alone. Spreading it over threads is the
// next step and has its own preconditions (docs/subsystems/actor-batch.md).

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

UInt32 InstallRouteActorBatch(const RouteSettings& settings, LogFn log);
bool ActorBatchInstalled();

// The batch's summary line (loops, bodies deferred and run in place by
// reason, the time in the loop and in the run after it); nothing when off.
void LogActorBatchSummary();

}  // namespace obng::plugin
