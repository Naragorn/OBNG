#pragma once

// [Probe] HeapCalls=1: MemoryHeap's Allocate 0x00401AA0 and Free 0x00401D40
// (the wrappers 0x00401F00 / 0x00401F20 and 79 direct callers reach them,
// read), counted on every thread; the render thread's frames over 30 ms
// with the heap's time in them; and
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
