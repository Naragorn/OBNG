#pragma once

// [Route] OcclusionCullBeforeBlend: how a blended record drawn before the eye pass's tests are out keeps its
// see-through depth from counting as cover - mode 1 sends the tests before it, mode 2 keeps its rectangles and drops
// the tests that would read through them (OcclusionCullSeeThroughLogic.h has the decisions). Render thread, called
// by OcclusionCull.cpp.

#include "OcclusionCullLogic.h"
#include "OcclusionCullSeeThroughLogic.h"
#include "OcclusionProbeLogic.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// The mode from the INI's value (0, 1 or 2; anything else is 1), at install; `alternate`: modes 1 and 2 take turns
// window by window ([Route] OcclusionCullBeforeBlendAlternate).
void OcclusionCullSeeThroughInstall(int iniValue, bool alternate);
occlusion_cull::see_through::Mode OcclusionCullSeeThroughMode();

// The eye pass's begin: no surface kept yet.
void OcclusionCullSeeThroughBegin();

// A record (`direct` false) or a direct shape or strips draw (`direct` true) about to be drawn while the pass's tests
// are open: true when they must go out before it. Mode 1 looks at records only, as on 2026-11-13.
bool OcclusionCullSeeThroughEnds(UInt32 geometry, bool direct, UInt32 renderer, const occlusion::Viewport ports[2], const float secondEyeOffset[3],
                                 float margin, UInt32 eyes);

// The tests going out: in mode 2 those that would read through a kept surface are taken out of the batch; a traced
// batch's names, parents and bounds follow their tests.
void OcclusionCullSeeThroughKeepTests(occlusion_cull::Batch* batch);

void OcclusionCullSeeThroughSummary(LogFn log, std::uint64_t passes);

}  // namespace obng::plugin
