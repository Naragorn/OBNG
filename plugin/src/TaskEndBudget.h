#pragma once

// [Route] TaskEndBudget=N (src/engine/io/TaskEndBudgetLogic.h): the IO pump's per-frame budget for ending finished
// background tasks, in ms, instead of the game's iPostProcessMilliseconds (5). The pump's read of the setting at
// 0x004335D4 is pointed at OBNG's value; the setting itself and Oblivion.ini stay untouched. 0 = the game's own.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallTaskEndBudget(const char* iniPath, LogFn log);

}  // namespace obng::plugin
