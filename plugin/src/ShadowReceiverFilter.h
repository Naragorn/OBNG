#pragma once
// [Route] ActorShadowReceiverMin=r and ActorShadowReceiverMaxGap=g (units; 0
// = that rule off): an actor shadow light does not take a geometry as a
// receiver whose world bound radius is under r, or whose bound sphere lies
// more than g units from the caster's bound centre - it gets no actor-shadow
// receiver pass (engine/render/ShadowReceiverFilterLogic.h). Class C.
// ActorShadowReceiverAlternate=1 switches it by summary window for a
// measurement. Counts the shadow lights' receivers by radius either way.
//
// Takes the entry of ShadowSceneLight::AddGeometry 0x007D5790 (thiscall(light;
// geometry), RET 4): not together with [Shadow] RangeWalk=1, whose recording
// detour takes the same entry; with [Route] RangeWalk=1 its walk calls the
// entry too, so the filter holds there as well.
#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallShadowReceiverFilter(const char* iniPath, LogFn log);
void LogShadowReceiverFilterSummary();

}  // namespace obng::plugin
