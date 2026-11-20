#pragma once

// [Route] FaceGenCacheEgmMB / FaceGenCacheEgtMB (src/engine/facegen/FaceGenCacheBudgetLogic.h): the limits of FaceGen's
// model cache raised to OBNG's own, at the first update step (after the game has read Oblivion.ini): the two settings'
// values 0x00B120EC / 0x00B120F4, and the cache's own copies when it is made already. 0 = the game's setting.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallFaceGenCacheBudget(const char* iniPath, LogFn log);
void LogFaceGenCacheSummary();

}  // namespace obng::plugin
