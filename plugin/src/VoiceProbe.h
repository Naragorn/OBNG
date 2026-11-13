#pragma once

// [Probe] VoiceLines=1: the voice player 0x006A9E00 timed - the function that takes a voice file's path, opens the
// MP3 and decodes it on the calling thread before the line plays (its strings: "CreateSoundBuffer failed while
// playing voice %s", "MP3 file '%s' not found"; docs/performance-backlog.md, 2026-11-13). Every call at or over
// [Probe] VoiceLinesMs (default 1.0) is written with its path, at most [Probe] VoiceLinesAtMost (default 40) a
// window; per summary the calls, the mean, the longest. One entry detour and two clock readings a call.
// A measurement.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallVoiceProbe(const char* iniPath, LogFn log);
void LogVoiceProbeSummary();

}  // namespace obng::plugin
