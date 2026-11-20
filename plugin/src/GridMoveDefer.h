#pragma once

// [Route] GridMoveDefer (src/engine/world/GridMoveDeferLogic.h): the update step's block with the grid's per-frame
// move 0x00444FB0 (0x0040DD6E - 0x0040DE3E) runs at the start of the next update step (StepHooks.h), before the task
// pump and OBVR's pose wait, as a copy of the game's own code; a jump skips it at its place. 0 off, 1 on.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallGridMoveDefer(const char* iniPath, LogFn log);
void LogGridMoveDeferSummary();

}  // namespace obng::plugin
