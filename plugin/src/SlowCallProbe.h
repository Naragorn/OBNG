#pragma once

// [Probe] SlowCalls=1: every texture made from a path 0x00701E00, animation file 0x00439FF0, model file 0x00439EB0 and
// sound made from a path 0x006AC610 of 3 ms or more, with its path, caller, thread and tick, written at the next
// summary. Not with [Timed] lines on those entries. (A detour on the script line 0x005169A0 froze the game during the
// save's load twice, 2026-11-19, cause not found; it was taken out.)

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallSlowCallProbe(const char* iniPath, LogFn log);
void LogSlowCallSummary();

}  // namespace obng::plugin
