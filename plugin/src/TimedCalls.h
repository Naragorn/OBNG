#pragma once

// [Timed] in OBNG.ini: any function of the EXE timed at its entry and its
// return (TimedCallLogic.h has how). Up to 48 lines
//
//   F1=00603CA0 6AFF68E00D9D00 Actor::Update
//
// address, the entry's displaced whole instructions as the EXE has them
// (five bytes or more, nothing relative among them), a name. Every summary
// writes per function the calls, the time in all, the time without the
// timed functions called inside, and the longest call. Only calls on one
// thread are timed: the first thread that calls the first listed function
// (so list a main-thread function first); a call
// costs two reads of the time stamp counter and some bookkeeping, about 50
// ns - not for functions called thousands of times a frame. A function
// another detour already holds is refused. A measurement.
//
// WritesOf=<name>[,<name>...] and WritesEvery=<n>: which statics of the EXE
// the named functions' calls write (TimedWrites.h).

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Reads the section from the INI at `iniPath` and installs what it lists.
// Returns the number of functions timed.
UInt32 InstallTimedCalls(const char* iniPath, LogFn log);
void LogTimedCallsSummary();

}  // namespace obng::plugin
