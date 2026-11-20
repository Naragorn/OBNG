#pragma once

// [Route] SoundFileCache: a WAV file the game plays is read from its file once and from memory after that, and a
// sound folder's files are listed once (src/engine/sound/SoundFileCacheLogic.h). The WAV reader 0x006AABD0 keeps its
// own code; the mmio procedure it hands to mmioOpen (0x006AAAA0, its address at 0x006AAC4A) is replaced by one that
// serves kept bytes, and the folder pick 0x006A9CD0 by one that keeps the listing and draws the game's random number
// as the game does. 0 off, 1 the route, 2 the check: the game's file stays open beside the kept bytes and every answer
// of the procedure is compared, and every folder is listed again and compared (class A's gate). Files over 1 MiB are
// read from their file every time; the kept files have a budget of 24 MiB, the least recently played dropped first.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallSoundFileCache(const char* iniPath, LogFn log);
void LogSoundFileCacheSummary();

}  // namespace obng::plugin
