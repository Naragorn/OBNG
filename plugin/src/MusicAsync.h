#pragma once

// [Route] MusicAsync (src/engine/sound/MusicAsyncLogic.h): the music player's track start hands the game a stand-in
// filter graph at once and builds the real one (CoCreateInstance and RenderFile, 27 - 28 ms a track under Proton) on a
// worker thread; what the game asks before it is there is given to it at the first call after. The seven
// CoCreateInstance calls of the music player patched. 0 off, 1 on.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallMusicAsync(const char* iniPath, LogFn log);
void LogMusicAsyncSummary();

}  // namespace obng::plugin
