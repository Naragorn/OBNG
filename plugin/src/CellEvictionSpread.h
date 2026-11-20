#pragma once

// [Route] CellEvictionSpread (src/engine/world/CellEvictionSpreadLogic.h): a background cell task's end that would
// evict a cell from the exterior buffer is held for a few frames while the cell to be evicted has its references' 3D
// let go in slices ([Route] CellEvictionSliceTenthsMs, 20 = 2.0 ms a frame); then the end runs and its eviction is
// cheap. Hooks: the pump's end call 0x00433632, the update step's pump call 0x0040D8D8, the entries of the pump
// 0x00433590, the cell request 0x004BE090, the grid move's task ends 0x004BE4E0 and the grid attach 0x00444340 (not
// with [Timed] lines on those four), and a step hook. 0 off, 1 on.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallCellEvictionSpread(const char* iniPath, LogFn log);
void LogCellEvictionSpreadSummary();

}  // namespace obng::plugin
