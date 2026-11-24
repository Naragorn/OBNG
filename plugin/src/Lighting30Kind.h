#pragma once

// [Probe] Lighting30Kinds (src/engine/render/Lighting30KindLogic.h): the geometry the game would light in several
// passes (ShadowLightShader, bit 1; SkinShader, 2; ParallaxShader, 4) is prepared with the game's own one-pass
// Lighting30Shader instead - the shader kind chooser's one call at 0x007B8091 goes through this probe. Counts what
// it moved. A measuring instrument: the picture changes (no actor shadow falls on moved geometry), never on in
// play.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallLighting30Kind(const char* iniPath, LogFn log);
void LogLighting30KindSummary();

}  // namespace obng::plugin
