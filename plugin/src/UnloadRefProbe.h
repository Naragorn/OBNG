#pragma once

// [Probe] UnloadRefs=1: what a cell's unload spends on which references - the loop 0x004CB4D0 (every reference of the
// cell but the player gets Set3D(null) under the object owner lock) run by OBNG with each call timed, summed by the
// reference's form type and by whether it had 3D. Not with a [Timed] line on 0x004CB4D0.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallUnloadRefProbe(const char* iniPath, LogFn log);
void LogUnloadRefSummary();

}  // namespace obng::plugin
