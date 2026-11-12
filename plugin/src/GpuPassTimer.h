#pragma once
// [Probe] GpuPasses=1 (a measurement, off in play; GpuPassesEvery=n samples
// one frame of n, 4 by default): the GPU time of the frame's render steps from
// Direct3D 9 timestamp queries - the actor shadow maps (0x004073D0), every
// non-empty pass list of every accumulator finish (stamped from the pass list
// hook of DrawInstancing.cpp, which installs that hook for this probe even
// with the instancing off), the image space (0x007B48E0) - read back a few
// frames later without waiting, summed per summary window
// (GpuPassTimerLogic.h). Not beside [Probe] UnroutedDraws (it takes the image
// space's entry). The queries are released at the device's Reset (its vtable
// slot 16 hooked once). Needs [Route] StereoDraw=1 (the pass list hook is
// installed with the route).
#include <cstdint>

#include "ShadowTimeInfo.h"

namespace obng::plugin {

// Reads its keys from the INI at iniPath; installs the two detours when on.
void InstallGpuPassTimer(const char* iniPath, LogFn log);
void LogGpuPassTimerSummary();

namespace gpu_passes {

inline bool g_on = false;

// A pass list of a finish begins and ends (render thread, outermost lists
// only): its accumulator, pass id, records and first geometry.
void ListBegin(std::uint32_t accumulator, std::uint32_t pass, std::uint32_t records, std::uint32_t firstGeometry);
void ListEnd();

}  // namespace gpu_passes

}  // namespace obng::plugin
