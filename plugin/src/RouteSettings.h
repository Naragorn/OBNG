#pragma once

// The [Route] switches: an entry whose rebuild passed its in-game gate can be
// routed - the game runs OBNG's rebuild there instead of the original - so
// its cost can be measured against the original's. Read in Plugin.cpp,
// decided in PluginEntry.cpp (Load), the routes themselves in
// RouteRangeWalk.cpp. No Windows or engine dependency: PluginEntryTest
// includes this.

#include "Types.h"

namespace obng::plugin {

// The range walk's route is one detour: the walk's entry 0x007D59E0.
// AddGeometry 0x007D5790 stays unpatched; the route calls it directly.
constexpr UInt32 kRouteRangeWalkDetours = 1;

struct RouteSettings {
	bool rangeWalk = false;  // ShadowSceneLight::CollectGeometryInRange 0x007D59E0 runs BatchedWalk; off in play

	UInt32 Asked() const { return rangeWalk ? kRouteRangeWalkDetours : 0u; }
};

// Who takes the entry 0x007D59E0. The shadow and the route patch the same
// entry, so at most one of them can; when both are asked the shadow wins: a
// route taken without a clean shadow run makes nothing safer, a shadow run
// with the route asked still produces the comparison the route rests on.
enum class RangeWalkMode : UInt8 {
	Off,     // the game's own entry
	Shadow,  // ShadowRangeWalk.cpp: the original runs, BatchedWalk compared
	Route,   // RouteRangeWalk.cpp: BatchedWalk runs instead of the original
};

constexpr RangeWalkMode SelectRangeWalkMode(bool shadowAsked, bool routeAsked) {
	if (shadowAsked) {
		return RangeWalkMode::Shadow;
	}
	return routeAsked ? RangeWalkMode::Route : RangeWalkMode::Off;
}

// The route was asked but the shadow took the entry: Load logs it as skipped.
constexpr bool RouteSkippedForShadow(bool shadowAsked, bool routeAsked) {
	return routeAsked && SelectRangeWalkMode(shadowAsked, routeAsked) != RangeWalkMode::Route;
}

}  // namespace obng::plugin
