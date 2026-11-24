#pragma once

// [Route] PoolScan (src/engine/memory/PoolScanLogic.h): MemoryHeap's pool allocation 0x00402480 walks the pool's
// whole page table for a page to commit whenever its free list is empty - in vain, at every allocation, once every
// page of the pool is committed. 1 answers "none" from the pool's own count of committed pages and walks only when
// a page is left; 2 walks as the game does and compares the two answers. Class A: the same answer.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallPoolScan(const char* iniPath, LogFn log);
void LogPoolScanSummary();

}  // namespace obng::plugin
