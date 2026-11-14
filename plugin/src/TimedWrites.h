#pragma once
// [Timed] WritesOf=<name>[,<name>...] and WritesEvery=<n> (8): for the named
// timed functions, every n-th call's writes into the EXE's statics - the
// EXE's .data copied when the call begins and when it returns, the places
// that differ counted per function (TimedWritesLogic.h decides which call).
// Every summary writes per function the calls probed and the places written
// most often. For the question which parts of an actor's frame can leave the
// main thread: a part that writes a static shared by all actors cannot as it
// stands. A probed call costs two copies of 0.7 MB and their comparison
// (about 0.2 ms), so the timings of a run with it are not the game's. What
// another thread writes during a probed call is counted too; a place written
// in most probes is the function's own. It sees the statics only.

#include <cstddef>

#include "ShadowTimeInfo.h"
#include "TimedCallLogic.h"
#include "Types.h"

namespace obng::plugin::timed_writes {

// Reads the keys; `specs` are the timed functions installed. Returns how
// many of them are watched.
UInt32 Install(const char* iniPath, const timed::Spec* specs, UInt32 count, LogFn log);

// After Calls::Enter and after Calls::Exit, on the timed thread.
void Entered(UInt32 index, std::size_t depthAfter, bool pushed);
void Exited(std::size_t depthAfter);

void LogSummary();

}  // namespace obng::plugin::timed_writes
