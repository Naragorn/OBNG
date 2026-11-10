#pragma once

// [Probe] HeapCalls=1: the engine allocator's two entry points - the
// allocation 0x00401F00 (cdecl(size) -> MemoryHeap 0x00B02020's 0x00401AA0)
// and the release 0x00401F20 (cdecl(pointer) -> 0x00401D40), read - counted
// and timed on the render thread (the one seen allocating inside an eye pass:
// the culler must be on), split
// by inside and outside the eye pass, with the requested sizes by class.
// A measurement: one RDTSC pair per call.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallHeapProbe(const ShadowSettings& settings, LogFn log);
void LogHeapProbeSummary();

}  // namespace obng::plugin
