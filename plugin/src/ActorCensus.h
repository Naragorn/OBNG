#pragma once

// [Probe] ActorCensus=1: once a summary, the actors the engine updated in
// one frame (Actor::Update 0x00603CA0, the high actors' loop's call per
// actor): how many, how far from the player, whether their scene node is
// hidden from the cull walk, their base forms' types and plugins, and each
// actor as reference id / base id / type / distance (ActorCensusLogic.h).
// One entry thunk; outside a sample it costs a call and a compare an actor.
// Not together with a [Timed] line on 0x00603CA0. A measurement.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallActorCensus(const ShadowSettings& settings, LogFn log);
void LogActorCensusSummary();

}  // namespace obng::plugin
