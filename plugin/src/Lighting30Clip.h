#pragma once

// [Probe] Lighting30Clip (src/engine/render/Lighting30ClipLogic.h): the one-pass lighting's light sum clipped at
// 1, as the game's multi-pass path clips it before the texture - the saturate modifier set on one add of each of
// Lighting30Shader's lighting pixel programs, in the shader package's bytecode in memory, as the shader's loader
// 0x007FD560 makes the programs (its three calls of the maker 0x00801C90). On with [Probe] Lighting30Kinds only;
// it changes all geometry the shader draws, the game's own hair and heads too. Counts what it changed.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

// kindsOn: whether [Probe] Lighting30Kinds moves anything; without it nothing is installed.
void InstallLighting30Clip(const char* iniPath, LogFn log, bool kindsOn);
void LogLighting30ClipSummary();

}  // namespace obng::plugin
