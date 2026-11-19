#pragma once

// [Route] TextureLockSkip: the texture manager's prepare 0x0077AC90 goes straight to a texture's renderer data when it
// is there, without the renderer's lock, which the loader's precache holds while it reads a texture file and runs
// D3DX (src/engine/render/TextureLockSkipLogic.h). A texture without data takes the lock as before.
// 0 off, 1 the route, 2 the check: every prepare takes the lock as the original does, and a pointer read without it
// first is compared with the one read under it.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallTextureLockSkip(const char* iniPath, LogFn log);
void LogTextureLockSkipSummary();

}  // namespace obng::plugin
