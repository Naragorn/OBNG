#pragma once

// [Probe] PassMaskAlternate (src/engine/render/PassMaskAlternateLogic.h): at every summary the given bits of the
// game's lighting pass mask 0x00B42F40 are flipped, so one run holds phases with and without a lighting pass under
// the same load. 8 is the specular pass (bDoSpecularPass), 0x40 the control (every list built anew, none changed).
// Read with tools/re/alternate-phases.py. A measuring instrument: it changes the picture, never on in play.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallPassMaskAlternate(const char* iniPath, LogFn log);
void LogPassMaskAlternateSummary();

}  // namespace obng::plugin
