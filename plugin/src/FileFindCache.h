#pragma once

// [Route] FileFindCache: FileFinder::FindFile 0x00431020 answered from what it answered before for the same call -
// the finder, the flags, the mask and the path's bytes (src/engine/io/FileFindCacheLogic.h). Only relative paths of
// asset kinds are kept; INIs, text, logs, absolute and ".\" paths always go to the original. 0 off, 1 the route, 2 the
// check: the original runs on every call and its answer is compared with the kept one (class A's gate). One entry
// detour, so not together with a [Timed] line on 0x00431020.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallFileFindCache(const char* iniPath, LogFn log);
void LogFileFindCacheSummary();

}  // namespace obng::plugin
