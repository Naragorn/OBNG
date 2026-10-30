#pragma once

// The range-walk shadow (ShadowRangeWalk.cpp): per outermost call of
// ShadowSceneLight::CollectGeometryInRange 0x007D59E0, the original runs
// with its AddGeometry 0x007D5790 calls recorded, then BatchedWalk runs
// over the same scene recording instead of performing AddGeometry, and the
// two are compared. The decisions are in ShadowRangeWalkLogic.h.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Puts the two detours (AddGeometry first, then the walk's entry, so the
// walk is never compared without its recording) when settings.rangeWalk is
// on. Returns how many were made. Call after InstallShadowTimeInfo, which
// sets up the recorder.
UInt32 InstallShadowRangeWalk(const ShadowSettings& settings, LogFn log);

// The coverage line (walks by source path, AddGeometry calls compared)
// since the last summary; called by the recorder when a summary is due.
// Does nothing when the shadow is not on.
void LogRangeWalkSummary();

}  // namespace obng::plugin
