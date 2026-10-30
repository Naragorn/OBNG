#pragma once

// The range walk's route (RouteRangeWalk.cpp): the game runs BatchedWalk at
// the entry of ShadowSceneLight::CollectGeometryInRange 0x007D59E0 instead of
// the original, performing AddGeometry 0x007D5790 itself. The decisions are
// in RouteRangeWalkLogic.h; who may take the entry (the shadow wins) is
// decided by Load (RouteSettings.h).

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Puts the route's detour on 0x007D59E0 when settings.rangeWalk is on.
// Returns how many detours were made (0 or 1). Load calls it only when the
// RangeWalk shadow is off: both cannot hold the same entry.
UInt32 InstallRouteRangeWalk(const RouteSettings& settings, LogFn log);

// The route's line (walks routed, AddGeometry calls performed, the calls left
// to the original) since the last summary; called by the recorder when a
// summary is due. Does nothing when the route is not on.
void LogRouteSummary();

}  // namespace obng::plugin
