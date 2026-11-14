#pragma once
// [Route] SoundCommit=0|1|2|3 (SoundCommitLogic.h has the rules): the sound
// update's two commits of DirectSound's deferred 3D settings, timed (1), one
// a frame (2), one every second frame (3) or the summary windows in turn as
// 1 and 3 (4). The two call sites are patched
// only when both read the game's own bytes.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallSoundCommit(const char* iniPath, LogFn log);
void LogSoundCommitSummary();

}  // namespace obng::plugin
