#pragma once

// [Probe] LockCalls=1: the game's own calls of EnterCriticalSection,
// TryEnterCriticalSection, LeaveCriticalSection, InterlockedIncrement and
// InterlockedDecrement counted through Oblivion.exe's import slots
// (0x00A2806C, 0x00A28070, 0x00A28074, 0x00A28078, 0x00A2807C, read from the
// import table): per critical section how often it is entered inside and
// outside the eye pass, by how many threads, how often it was held by
// another thread, and the enter sites; the interlocked calls by site. Only
// the EXE's calls go through these slots - the DLLs' do not. A measurement.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallLockProbe(const ShadowSettings& settings, LogFn log);
void LogLockProbeSummary();

}  // namespace obng::plugin
