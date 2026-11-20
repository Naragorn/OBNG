#pragma once

// [Probe] HangWatch=1 (src/engine/diagnostics/HangWatchLogic.h): a thread of OBNG's looks once a second whether the
// main thread's update steps still come; after ten seconds without one it writes, once a stall, every thread's
// instruction pointer with the code addresses on its stack, and the holder of each lock made known to it.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallHangWatch(const char* iniPath, LogFn log);

// A critical section whose holder a dump should name (the engine's by address, OBNG's own). Up to 48; before or
// after the install.
void HangWatchLock(const char* name, const void* criticalSection);

}  // namespace obng::plugin
