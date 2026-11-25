#pragma once

// [Route] FarGridDefer (src/engine/world/FarGridDeferLogic.h): at a cell crossing the grid's move shifts the far
// grid (0x00445454) and its attach updates it (0x00444396) - 1.4 - 2.4 ms of the move; the route makes both calls at
// the next update step's start instead (StepHooks.h). The far LOD then follows the crossing one frame later, two
// cells from the player. 0 off, 1 on.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallFarGridDefer(const char* iniPath, LogFn log);
void LogFarGridDeferSummary();

}  // namespace obng::plugin
