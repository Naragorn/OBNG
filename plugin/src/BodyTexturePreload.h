#pragma once

// [Route] BodyTexturePreload (src/engine/render/BodyTexturePreloadLogic.h): at the first loading screen with the races
// loaded, every race's body skin textures are loaded through TES::CreateTextureImage 0x00442890 and held, so that an
// NPC's body build 0x0052D2C0 finds them in the cache in the ride. The hook is the loading screen's own loader pause:
// the call of 0x00432860 at 0x005ADADC inside LoadingMenu::Tick 0x005AD980 (its entry stays free for the shadow). A body
// mod's script gives races new paths in the first frames after a load (measured 2026-11-19), so the main loop's call of
// the update step 0x0040F270 compares the races' path pointers every step and preloads again when they changed.
// 0 off, 1 on (the default), 2 async - not built yet, runs as 1.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallBodyTexturePreload(const char* iniPath, LogFn log);
void LogBodyTexturePreloadSummary();

}  // namespace obng::plugin
