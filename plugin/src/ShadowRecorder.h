#pragma once

// The shared bookkeeping of every shadowed function: its counter, the log
// budget, the mismatch lines and the summary. The detours in
// ShadowTimeInfo.cpp, ShadowLoadingMenu.cpp and ShadowRangeWalk.cpp report here.

#include "ShadowLogic.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

enum class Shadowed : int {
	TimeInfoUpdate = 0,
	TimeInfoInit,
	TimeInfoFreeze,
	TimeInfoUnfreeze,
	LoadingMenuTick,
	LoadingMenuCtor,
	RangeWalk,
	FastSqrt,
	ActorUpdate,
	GridUpdate,
	VolatilePack,
	Count,
};

const char* ShadowName(Shadowed which);

void InitRecorder(const ShadowSettings& settings, LogFn log);
LogFn RecorderLog();

// One call of a shadowed function: counted, and when it mismatched and the
// budget allows, written with `detail` (the fields that differ, prepared
// by the caller) and `inputs`.
void RecordCall(Shadowed which, bool mismatch, const char* detail, const char* inputs);

// A call whose comparison could not be made (the original's tick not
// recoverable); counted apart so the summary shows it.
void RecordSkipped(Shadowed which, const char* why);

// The summary line when it is due, plus the planner histogram.
void SummaryIfDue(UInt32 tick, const shadow::PlannerSample* planner, bool resetPlanner);

// A thread that calls SummaryIfDue once a second, for runs in which no
// per-frame detour drives the summary (every shadow off, the route on - the
// measurement runs). It never takes the planner sample. False when the thread
// could not be created. Call after InitRecorder; at most one thread.
bool StartSummaryTimer();
shadow::PlannerSample& Planner();

// The fields of two clock states that differ, as text for a mismatch line.
void DescribeStateDifferences(const shadow::State& before, const shadow::State& original,
                              const shadow::State& rebuild, UInt32 mask, char* out, UInt32 capacity);

}  // namespace obng::plugin
