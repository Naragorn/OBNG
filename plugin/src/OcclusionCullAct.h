#pragma once

// [Route] OcclusionCullAct, the part OcclusionCull.cpp calls
// (OcclusionCullActLogic.h has the rule): the culler's verdicts acted on, only
// tested, or acted on in every second summary window. Render thread.

#include "OcclusionCullLogic.h"
#include "ShadowTimeInfo.h"

namespace obng::plugin {

// At the settings: the INI's value and where the lines go.
void OcclusionCullActInit(LogFn log, int iniValue);

// cull::Decide with the window's rule (OcclusionCullActLogic.h Decide).
occlusion_cull::Decided OcclusionCullActDecide(occlusion_cull::Table& table, std::uint32_t geometry, std::uint32_t frame, occlusion_cull::TestKind kind,
                                               const occlusion_cull::Settings& settings, const occlusion_cull::Cameras& cameras,
                                               occlusion_cull::Stats* stats, std::uint32_t identity, occlusion_cull::LodFn isLod);

// cull::FormatSummary with the window's account behind it; the next window
// then begins by the rule.
int OcclusionCullActSummary(const occlusion_cull::Stats& window, const occlusion_cull::Settings& settings, double ticksPerMillisecond, std::uint64_t qpc,
                            char* out, std::size_t capacity);

}  // namespace obng::plugin
