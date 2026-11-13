#pragma once

// [Route] VoiceDecode: a voice line's MP3 decoded off the main thread. The game's voice player 0x006A9E00 decodes a
// line's whole file into its DirectSound buffer before it returns - 3-28 ms on the main thread for every line an
// actor starts (measured 2026-11-13, docs/performance-backlog.md). The route makes the same buffer and returns the
// same duration, decodes the line's first [Route] VoiceDecodeLeadMs (default 150) at the call and leaves the rest to
// one worker thread, which writes it into the buffer while the line plays.
//   0  off - the game's function
//   1  the game's own decoder, moved (class B: the same samples)
//   2  minimp3 (class C: other samples, within a decoder's rounding) - the default
// [Route] VoiceDecodeCheck=1 (mode 2): every line is decoded once more by both decoders on the worker and the
// difference written. Rules and addresses: knowledge/voice-lines.md.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallVoiceDecode(const char* iniPath, LogFn log);
void LogVoiceDecodeSummary();

}  // namespace obng::plugin
