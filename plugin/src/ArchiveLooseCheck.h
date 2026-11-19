#pragma once

// [Route] ArchiveLooseCheck: the archives' check for a loose file that overrides a record 0x0042C1D0 (archive
// invalidation) answered from the listing of the game folder (plugin/src/LooseListing.h,
// src/engine/io/ArchiveLooseCheckLogic.h) - the operating system's _stat of "Data\<path>" at a record's first find
// replaced by a lookup in memory when no loose file of that name exists. A name the listing has, a kind it does not
// answer for and every call before the listing is ready go to the original.
// 0 off, 1 the route, 2 the check: every answer from the listing compared with a _stat of its own (class A's gate).

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallArchiveLooseCheck(const char* iniPath, LogFn log);
void LogArchiveLooseCheckSummary();

}  // namespace obng::plugin
