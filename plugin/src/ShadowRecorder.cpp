#include "ShadowRecorder.h"

#include <windows.h>

#include "Probes.h"
#include "RouteRangeWalk.h"
#include "OcclusionCull.h"
#include "FastSceneUpdate.h"
#include "RouteStereoDraw.h"
#include "ShadowRangeWalk.h"

#include <cstdio>

namespace obng::plugin {
namespace {

const char* const kNames[] = {"TimeInfo::Update", "TimeInfo::Init", "TimeInfo::Freeze", "TimeInfo::Unfreeze",
                              "LoadingMenu::Tick", "LoadingMenu::LoadingMenu",
                              "ShadowSceneLight::CollectGeometryInRange", "_CIsqrt",
                              "NiAVObject::Update (ApplyAnimData)"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(Shadowed::Count), "one name per shadowed function");

SRWLOCK g_lock = SRWLOCK_INIT;
shadow::Counter g_counters[static_cast<int>(Shadowed::Count)];
UInt64 g_skipped[static_cast<int>(Shadowed::Count)];
shadow::SummaryClock g_summary;
shadow::PlannerSample g_planner;
ShadowSettings g_settings;
LogFn g_log = nullptr;

}  // namespace

const char* ShadowName(Shadowed which) {
	return kNames[static_cast<int>(which)];
}

void InitRecorder(const ShadowSettings& settings, LogFn log) {
	g_settings = settings;
	g_log = log;
}

LogFn RecorderLog() {
	return g_log;
}

void RecordCall(Shadowed which, bool mismatch, const char* detail, const char* inputs) {
	const int i = static_cast<int>(which);
	AcquireSRWLockExclusive(&g_lock);
	const bool write = g_counters[i].Record(mismatch, g_settings.mismatchLines);
	ReleaseSRWLockExclusive(&g_lock);
	if (write) {
		g_log("Shadow MISMATCH %s (%s):%s", kNames[i], inputs, detail);
	}
}

void RecordSkipped(Shadowed which, const char* why) {
	const int i = static_cast<int>(which);
	AcquireSRWLockExclusive(&g_lock);
	++g_counters[i].calls;
	const UInt64 skipped = ++g_skipped[i];
	ReleaseSRWLockExclusive(&g_lock);
	if (skipped <= 3) {
		g_log("Shadow %s: comparison skipped - %s", kNames[i], why);
	}
}

shadow::PlannerSample& Planner() {
	return g_planner;
}

void SummaryIfDue(UInt32 tick, const shadow::PlannerSample* planner, bool resetPlanner) {
	AcquireSRWLockExclusive(&g_lock);
	const bool due = g_summary.Due(tick, g_settings.summaryEverySeconds * 1000);
	shadow::Counter counters[static_cast<int>(Shadowed::Count)];
	UInt64 skipped[static_cast<int>(Shadowed::Count)];
	shadow::PlannerSample sample;
	if (due) {
		for (int i = 0; i < static_cast<int>(Shadowed::Count); ++i) {
			counters[i] = g_counters[i];
			skipped[i] = g_skipped[i];
		}
		if (planner != nullptr) {
			sample = *planner;
			if (resetPlanner) {
				g_planner.Reset();
			}
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
	if (!due) {
		return;
	}
	char line[640];
	int at = std::snprintf(line, sizeof(line), "Shadow:");
	for (int i = 0; i < static_cast<int>(Shadowed::Count) && at < 600; ++i) {
		if (counters[i].calls == 0) {
			continue;
		}
		if (skipped[i] != 0) {
			at += std::snprintf(line + at, sizeof(line) - at, " %s %llu calls %llu mismatches %llu skipped;", kNames[i],
			                    static_cast<unsigned long long>(counters[i].calls),
			                    static_cast<unsigned long long>(counters[i].mismatches),
			                    static_cast<unsigned long long>(skipped[i]));
		} else {
			at += std::snprintf(line + at, sizeof(line) - at, " %s %llu calls %llu mismatches;", kNames[i],
			                    static_cast<unsigned long long>(counters[i].calls),
			                    static_cast<unsigned long long>(counters[i].mismatches));
		}
	}
	g_log("%s", line);
	LogProbeSummary();
	LogRangeWalkSummary();
	LogRouteSummary();
	LogStereoDrawSummary();
	LogActorUpdateSummary();
	LogOcclusionCullSummary();
	if (planner != nullptr && g_settings.havokPlanner && sample.frames != 0) {
		const float stepSeconds = *reinterpret_cast<volatile float*>(0x00BA790C);
		g_log("Probe: Havok planner over %u frames - update type %u, steps per frame 0:%u 1:%u 2:%u 3+:%u, last step %.6f s",
		      sample.frames, sample.lastType, sample.stepCounts[0], sample.stepCounts[1], sample.stepCounts[2],
		      sample.stepCounts[3], stepSeconds);
	}
}

namespace {

DWORD WINAPI SummaryTimer(LPVOID) {
	for (;;) {
		Sleep(1000);
		SummaryIfDue(GetTickCount(), nullptr, false);
	}
}

}  // namespace

bool StartSummaryTimer() {
	static bool s_started = false;
	if (s_started) {
		return true;
	}
	HANDLE thread = CreateThread(nullptr, 0, &SummaryTimer, nullptr, 0, nullptr);
	if (thread == nullptr) {
		return false;
	}
	CloseHandle(thread);
	s_started = true;
	return true;
}

void DescribeStateDifferences(const shadow::State& before, const shadow::State& original,
                              const shadow::State& rebuild, UInt32 mask, char* out, UInt32 capacity) {
	int at = 0;
	out[0] = '\0';
	for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(shadow::Field::Count) && at + 100 < static_cast<int>(capacity); ++i) {
		if ((mask & (1u << i)) == 0) {
			continue;
		}
		const auto f = static_cast<shadow::Field>(i);
		at += std::snprintf(out + at, capacity - at, " %s before %08X original %08X rebuild %08X;", shadow::FieldName(f),
		                    shadow::Bits(before, f), shadow::Bits(original, f), shadow::Bits(rebuild, f));
	}
}

}  // namespace obng::plugin
