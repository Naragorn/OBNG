#pragma once

// [Probe] LoaderWaits=1: who makes the main thread wait for the background loader, and for how long - the pause of
// every loader thread 0x004322B0, the wait for the IO thread 0x00431F50 (both reached through 0x00432860) and the
// texture loaded on the caller's thread 0x00442890 (TES::CreateTextureImage, its path kept). Every wait of 0.5 ms or
// more is kept with its caller, its thread and its tick, and written at the next summary. Not with [Timed] lines on
// these three entries.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallLoaderWaitProbe(const char* iniPath, LogFn log);
void LogLoaderWaitSummary();

}  // namespace obng::plugin
