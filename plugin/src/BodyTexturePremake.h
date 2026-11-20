#pragma once

// [Route] BodyTexturePremake=1 (src/engine/facegen/BodyTexturePremakeLogic.h): a queued head's run on the loader's
// thread (QueuedHead's vtable slot 1, 0x00435300) is followed by the same NPC's body textures 0x0052D2C0, so the
// character's end on the main thread finds them in the texture map. The main thread's own call of 0x0052D2C0
// (0x0047ADFF) and the premake take turns on a lock of OBNG's; the premake only tries it.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallBodyTexturePremake(const char* iniPath, LogFn log);
void LogBodyTexturePremakeSummary();

}  // namespace obng::plugin
