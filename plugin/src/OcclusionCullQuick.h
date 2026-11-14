#pragma once

// [Route] OcclusionCullQuick, the part that reads the game
// (OcclusionCullQuickLogic.h has the rule): a draw with a visible verdict
// and no test due is decided without its rectangles; every other draw's
// rectangles are made here from the renderer's matrices. Render thread,
// called by OcclusionCull.cpp's decision.

#include "OcclusionCullLogic.h"
#include "OcclusionProbeLogic.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

void OcclusionCullQuickInit(LogFn log, int iniValue);

// True when the draw was decided without rectangles (`out` is the
// decision). `seen` is the verdict on record for the address or null, not
// asked about in this frame yet. With the check on it answers false for
// every draw and notes whether it would have applied.
bool OcclusionCullQuick(occlusion_cull::Verdict* seen, UInt32 frame, std::uint32_t identity, std::uint32_t every,
                        const occlusion_cull::Settings& settings, occlusion_cull::Stats* stats, occlusion_cull::Decided* out);

// Both eyes' rectangles of a bound, from the renderer's matrices as they are
// at this draw.
occlusion_cull::TestKind OcclusionCullRects(UInt32 renderer, const occlusion::Bound& bound, const occlusion::Viewport ports[2],
                                            const float secondEyeOffset[3], float margin, UInt32 eyes, occlusion_cull::EyeRects* rects,
                                            occlusion::RectKind kinds[2]);

// After the full decision: the check's comparison, when the quick decision
// would have applied to this draw.
void OcclusionCullQuickCompare(const occlusion_cull::Decided& full, occlusion_cull::TestKind kind);

// At the summary: the line, the counts reset, the next window's state.
void OcclusionCullQuickSummary(LogFn log, std::uint64_t passes);

}  // namespace obng::plugin
