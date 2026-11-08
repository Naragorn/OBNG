#pragma once

// The occlusion culler ([Route] OcclusionCull, OcclusionCull.cpp): in the
// single-pass eye pass, geometries whose world bound an occlusion query of
// the last frames found wholly behind the drawn depth, in both eyes, are not
// drawn. The decisions are OcclusionCullLogic.h's; this is the part that
// touches the game and Direct3D. The direct draw entries ask it through
// OcclusionCullDraws. A prototype for the measurement: its
// queries and state block are released at the device's Reset (its vtable
// slot hooked at the first eye pass) and made anew afterwards.

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Puts the culler's three detours (the accumulator's finish 0x007AE070 for
// the eye pass, DrawRenderPass 0x007A9820 for the pass records, Do_RenderShape
// 0x007693E0 for the shape draws) when settings.occlusionCull is on; returns
// how many were made. The direct draw
// entries (slots 90/91) belong to the stereo route, which asks the culler
// through OcclusionCullDraws below.
UInt32 InstallOcclusionCull(const RouteSettings& settings, LogFn log);

// For a direct draw entry about to draw `geometry`: true = draw it, false =
// leave it out. Render thread.
bool OcclusionCullDraws(UInt32 geometry, bool strips);

// Asks for the culler's summary line; the render thread writes it at its
// next eye pass (the counts since the last line, then reset). Nothing when
// the culler is off.
void LogOcclusionCullSummary();

}  // namespace obng::plugin
