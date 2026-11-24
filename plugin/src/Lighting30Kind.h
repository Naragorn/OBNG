#pragma once

// [Probe] Lighting30Kinds (src/engine/render/Lighting30KindLogic.h): the geometry the game would light in several
// passes (ShadowLightShader, bit 1; SkinShader, 2; ParallaxShader, 4) is prepared with the game's own one-pass
// Lighting30Shader instead - the shader kind chooser's one call at 0x007B8091 goes through this probe - and its
// property marked as the probe's. [Probe] Lighting30Shadows (Lighting30ShadowLogic.h, on with the kinds): the
// marked properties' pass lists get the actor shadow records the shader's own builder leaves out (its call at
// 0x00864078). Counts both. A measuring instrument until its picture gate has passed: never on in play.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallLighting30Kind(const char* iniPath, LogFn log);
void LogLighting30KindSummary();

}  // namespace obng::plugin
