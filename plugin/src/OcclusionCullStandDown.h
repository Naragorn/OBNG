#pragma once

// [Route] OcclusionCullStandDown, the part beside the culler
// (OcclusionCullStandDownLogic.h has the rule): the culler's counts taken
// pass by pass, and whether it rests. Render thread, called by
// OcclusionCull.cpp.

#include "OcclusionCullLogic.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Whether the culler rests in this pass: every draw is drawn undecided.
extern bool g_occlusionCullRests;

void OcclusionCullStandDownInit(LogFn log, int iniValue);

// A node left out at the cull walk, with the draws noted below it.
void OcclusionCullStandDownNode(std::uint32_t drawsBelow);

// After an eye pass (`live` are the counts since the last summary).
void OcclusionCullStandDownPass(const occlusion_cull::Stats& live);

// At the summary, before the counts are reset.
void OcclusionCullStandDownSummary(LogFn log, const occlusion_cull::Stats& window, double ticksPerMillisecond);

}  // namespace obng::plugin
