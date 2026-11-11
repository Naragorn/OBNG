#pragma once

// [Probe] ProcessManager=1: the process manager's update 0x00678510 timed -
// the frame's whole actor work (the AI and every actor's update in the low,
// middle and high process lists; the loop over the high actors 0x00677EC0 is
// called from it). Per summary: the calls, the mean, the longest, and how
// many took more than 3 ms. With it the character controller's update
// 0x00896000 (calls, mean, longest, how many with the moving bit) and its
// five parts. Seven entry detours and two clock readings a call; cheap
// enough to stay on while playing. A measurement.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void InstallProcessProbe(const ShadowSettings& settings, LogFn log);
void LogProcessProbeSummary();

}  // namespace obng::plugin
