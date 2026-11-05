#pragma once

// The unrouted-draw probe ([Probe] UnroutedDraws, UnroutedDrawProbe.cpp):
// counts every device draw of a single-pass frame by where it falls and
// keeps the calling site of each one the stereo route does not double. A
// measurement, off in play; the route pays one test of a flag per draw for
// it.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Whether the probe is on (set once by InstallUnroutedDrawProbe).
extern bool g_unroutedDrawProbeOn;

// Turns the probe on. The hooks on the device's four draw methods are put at
// the first routed draw, when the device is known.
void InstallUnroutedDrawProbe(LogFn log);

// Asks for the probe's summary lines; the render thread writes them at the
// next device draw. Does nothing when the probe is off.
void LogUnroutedDrawSummary();

// Called by the route around each of its detours' bodies while the probe is
// on: the device draws between the two are the routed ones.
void UnroutedDrawRouteEnter(void* renderer);
void UnroutedDrawRouteLeave();

// The route's detours hold one of these for their whole body.
struct RoutedDrawScope {
	bool on;
	explicit RoutedDrawScope(void* renderer) : on(g_unroutedDrawProbeOn) {
		if (on) {
			UnroutedDrawRouteEnter(renderer);
		}
	}
	~RoutedDrawScope() {
		if (on) {
			UnroutedDrawRouteLeave();
		}
	}
	RoutedDrawScope(const RoutedDrawScope&) = delete;
	RoutedDrawScope& operator=(const RoutedDrawScope&) = delete;
};

}  // namespace obng::plugin
