#pragma once

// [Route] FaceGenLoadOutside=1 and [Probe] FaceGenLoads=1 (src/engine/facegen/FaceGenLoadLogic.h): the four calls by
// which a find of FaceGen's model cache reads a model's .egm or .egt file under the cache's lock 0x00B39C00. The probe
// times every load that reads a file, by kind and thread; the route gives the cache's lock up for the read.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallFaceGenLoad(const char* iniPath, LogFn log);
void LogFaceGenLoadSummary();

}  // namespace obng::plugin
