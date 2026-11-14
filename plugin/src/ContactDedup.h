#pragma once

// [Route] ContactDedup: the character controller's contact pass 0x008CE770
// (bhkCharacterPointCollector, called after every triangle hit a cast
// collects) run by the rebuild of src/engine/havok/ContactDedupLogic.h - the
// same removals in the same order, without asking again what an earlier pass
// answered. 0 off, 1 the rebuild, 2 the check: the original runs on the
// game's array, the rebuild on a copy, both compared bit for bit (class A's
// gate). One entry detour.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallContactDedup(const char* iniPath, LogFn log);
void LogContactDedupSummary();

}  // namespace obng::plugin
