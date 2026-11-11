#pragma once
// [Route] ActorFarWhole: an actor farther from the player than
// ActorFarDistance gets its whole update - Actor::Update 0x00603CA0: the AI,
// the animation step, the character controller, the skeleton - on every
// ActorFarEvery-th call only, and that call with the time of the frames it
// stands for (engine/scene/ActorFarUpdateLogic.h). Class C: a different
// computation on purpose. Takes Actor::Update's entry, so not together with
// [Probe] ActorCensus or a [Timed] line on it (whichever comes second is
// refused by the bytes check).
#include "ShadowTimeInfo.h"

namespace obng::plugin {

// Reads its keys from the INI at iniPath; installs nothing when off.
void InstallActorFarUpdate(const char* iniPath, LogFn log);
void LogActorFarUpdateSummary();

}  // namespace obng::plugin
