#pragma once

// [Probe] Lighting30SpecularFade (src/engine/render/Lighting30SpecularLogic.h): the one-pass lighting's specular
// faded with distance for the geometry [Probe] Lighting30Kinds moved, as the game's multi-pass path fades it
// (fSpecularLOD1 .. fSpecularLOD2). Lighting30Shader's twenty specular pixel programs (SM3 and SM3LL) get one
// multiply by ToggleADTS.w behind their gloss multiply as the loader makes them (the pixel program maker's package lookup at 0x00801D2D,
// while Lighting30Clip.cpp has the loader's calls), and the shader's setup (its vtable slot at 0x00A930F0) writes
// the fade into ToggleADTS.w for a moved geometry's specular program. Everything else draws with the toggle at 1,
// as before. On with [Probe] Lighting30Kinds only. Counts what it did.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

// kindsOn: whether [Probe] Lighting30Kinds moves anything; without it nothing is installed.
void InstallLighting30Specular(const char* iniPath, LogFn log, bool kindsOn);
void LogLighting30SpecularSummary();
// Lighting30Clip.cpp's maker calls: the loader is making one of Lighting30Shader's pixel programs.
void SetMakingLighting30Program(bool making);

}  // namespace obng::plugin
