#pragma once

// [Probe] CodePatches=1: where the running game's code differs from
// Oblivion.exe on the disk - every patch another plugin (or OBNG) made, a
// line a patch with where its jump or call leads, and the counts by module
// (CodePatchLogic.h). Read at the first summary and again at the sixth (what
// was patched later). [Probe] CodePatchesLines (default 400) caps the lines,
// CodePatchesFrom / CodePatchesTo (hex) narrow the lines to a range; the
// counts are of everything. Costs one read of the executable's file and one
// pass over its code at each of the two summaries, off the main thread.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallCodePatchProbe(const char* iniPath, LogFn log);
void LogCodePatchSummary();

}  // namespace obng::plugin
