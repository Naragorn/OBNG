#pragma once

// [Probe] TaskEnds=1 (src/engine/io/TaskEndsLogic.h): the pump's call of a background task's end at 0x00433632 timed,
// summed by the task's vtable, the ends of 2 ms or more listed with the performance counter's tick they ended at.
// Not with [Route] CellEvictionSpread (the same call site).

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallTaskEndProbe(const char* iniPath, LogFn log);
void LogTaskEndSummary();

}  // namespace obng::plugin
