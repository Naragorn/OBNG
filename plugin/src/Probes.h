#pragma once

// Probes that measure, not compare: call counters with return sites on the
// scene pass's hot functions (who calls them, how often), and the process's
// memory. Both report in the shadow summary (ShadowRecorder).

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Puts the counting detours on the probed functions when the settings ask
// for them; returns how many were made. Memory needs no detour.
UInt32 InstallProbes(const ShadowSettings& settings, LogFn log);

// How many counting detours the settings ask for.
UInt32 ProbesAsked(const ShadowSettings& settings);

// Writes the probes' lines: the call counts since the last summary (then
// reset) and the memory sample with its peak. Called by the recorder when
// a summary is due; does nothing when no probe is on.
void LogProbeSummary();

// The stereo route's eye switch marks the second eye's constants re-apply
// (1 while it runs, 0 otherwise) for the view-dependence probe
// ([Probe] ViewConstants); a store, nothing else.
void SetProbeEyePhase(UInt8 phase);

}  // namespace obng::plugin
