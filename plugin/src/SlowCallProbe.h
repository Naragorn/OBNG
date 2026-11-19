#pragma once

// [Probe] SlowCalls=1: every texture made from a path, 0x00701E00 (its path, caller and thread kept), of 3 ms or more
// with its tick, written at the next summary; and every animation file (KF) 0x00439FF0 load of 3 ms or more the same way. Not with a [Timed] line on that entry. (A detour on the script line
// 0x005169A0 froze the game during the save's load twice, 2026-11-19, cause not found; it was taken out.)

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallSlowCallProbe(const char* iniPath, LogFn log);
void LogSlowCallSummary();

}  // namespace obng::plugin
