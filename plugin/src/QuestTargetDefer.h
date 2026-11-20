#pragma once

// [Route] QuestTargetDefer (src/engine/world/QuestTargetDeferLogic.h): the grid's move calls the player's quest
// targets' rebuild 0x00665260 at 0x004454E6 after every cell crossing, 3 - 5 ms of door pathing; the route makes that
// call at the next update step's start instead (StepHooks.h). 0 off, 1 on.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallQuestTargetDefer(const char* iniPath, LogFn log);
void LogQuestTargetDeferSummary();

}  // namespace obng::plugin
