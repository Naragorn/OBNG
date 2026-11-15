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

// [Probe] SkinnedCensus (SkinnedCensus.cpp): the skinned draw entry's note -
// the occlusion culler's draw note (OcclusionCullNoteDraw) and, with the probe
// on, the call counted by actor, part and shader class.
void InstallSkinnedCensus(const char* iniPath, LogFn log);
void SkinnedCensusNote(UInt32 geometry, UInt32 skin);
void SkinnedCensusDrawn(UInt32 geometry, UInt32 skin, unsigned long long startTicks);
unsigned long long SkinnedCensusClock();  // 0 while the probe is off
// Around the skinned draw entry's body: the note at its start, and at its end the call counted with the textures and
// programs the device holds then and the time the entry took.
struct SkinnedCensusScope {
	SkinnedCensusScope(UInt32 geometry, UInt32 skin) : geometry_(geometry), skin_(skin) {
		SkinnedCensusNote(geometry, skin);
		start_ = SkinnedCensusClock();
	}
	~SkinnedCensusScope() {
		if (start_ != 0) {
			SkinnedCensusDrawn(geometry_, skin_, start_);
		}
	}
	SkinnedCensusScope(const SkinnedCensusScope&) = delete;
	SkinnedCensusScope& operator=(const SkinnedCensusScope&) = delete;

private:
	UInt32 geometry_;
	UInt32 skin_;
	unsigned long long start_ = 0;
};
void LogSkinnedCensusSummary();

}  // namespace obng::plugin
