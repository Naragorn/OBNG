#pragma once
// [Route] PassListKeep: a geometry's render passes kept through a relight
// that ended with its light list exactly as before (engine/render/
// PassListKeepLogic.h). 2 keeps them (the cache key at +0x24 given back), 1
// checks: the key is set to a sentinel, the pass getter rebuilds and the
// rebuilt passes are compared with the cached ones (a mismatch would mean a
// kept list differs from a rebuild); 0 off. Class A. PassListKeepAlternate=1
// keeps only in every second frame and times the pass getter by frame side
// (a measurement).
//
// Patches: the two calls of RelightGeometry 0x007C6100 in RelightSubtree
// (0x007C70F0, 0x007C716E; not the entry, which OBVR's relight clock takes)
// and the pass getter's vtable slot +0x5C of the three classes whose getter is
// 0x007D85D0 (0x00A913E0, 0x00A92AF0, 0x00A92BD0). Every site is checked
// against the game's bytes first; none or all are written.
#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallPassListKeep(const char* iniPath, LogFn log);
void LogPassListKeepSummary();

}  // namespace obng::plugin
