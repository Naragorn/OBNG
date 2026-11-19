#pragma once

// [Route] PoolPageRelease: the memory pools' page release `MemoryPool::FreeUnusedPagesForAllPools()` 0x00402740
// skipped when the purge 0x0043FC20 comes from the grid's move at a cell crossing (src/engine/memory/
// PoolPageReleaseLogic.h); every other purge releases as before. 0 off, 1 skip, 2 measure: both entries detoured,
// the purge and the release timed, nothing skipped (the before with the same instrument). Two entry detours, so
// not together with a [Timed] line on 0x0043FC20 or 0x00402740.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallPoolPageRelease(const char* iniPath, LogFn log);
void LogPoolPageReleaseSummary();

}  // namespace obng::plugin
