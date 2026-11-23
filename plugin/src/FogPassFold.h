#pragma once

// [Route] FogPassFold (src/engine/render/FogPassFoldLogic.h): the pass builder's call of 0x00854190 at 0x0085BA25 -
// the pass without lights that ends a lit geometry's list when all four lighting passes are on - is left out for a
// list without a specular light pass, and the list's last record is marked as the builder marks it with the
// specular pass off. 1 folds the lists of one record, 2 counts the lists by shape and changes nothing, 3 folds
// every list without a specular light pass. Class C: the picture is the gate.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallFogPassFold(const char* iniPath, LogFn log);
void LogFogPassFoldSummary();

}  // namespace obng::plugin
