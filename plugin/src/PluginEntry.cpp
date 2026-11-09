#include "PluginEntry.h"

namespace obng::plugin {

namespace {

Settings g_settings;

}  // namespace

bool Query(const ExtenderInterfaceHead& xse, PluginInfo* info, const Host& host) {
	info->infoVersion = kInfoVersion;
	info->name = kPluginName;
	info->version = kPluginVersion;
	host.resetLog();
	host.log("%s %u - xOBSE %u, Oblivion %08X, editor=%u", kPluginName, kPluginVersion, xse.obseVersion,
	         xse.oblivionVersion, xse.isEditor);
	return true;
}

bool Load(const ExtenderInterfaceHead& xse, const Host& host) {
	if (xse.isEditor != 0) {
		host.log("Editor: nothing to do");
		return true;
	}
	if (xse.oblivionVersion != kOblivionVersion_1_2_416) {
		host.log("Oblivion %08X is not 1.2.416 (%08X): the shadowed addresses are for 1.2.416 only, nothing done",
		         xse.oblivionVersion, kOblivionVersion_1_2_416);
		return true;
	}
	g_settings = Settings{};
	if (!host.readSettings(&g_settings)) {
		host.log("No OBNG.ini next to the DLL - defaults: every TimeInfo and LoadingMenu shadow on, the range walk off, nothing routed, "
		         "the memory probe on, 20 mismatch lines a function, a summary every 10 seconds");
	}
	const ShadowSettings& s = g_settings.shadow;
	const RouteSettings& r = g_settings.route;
	host.initRecorder(s);
	if (s.Asked() == 0) {
		host.log("Shadow: every switch is 0, nothing shadowed");
	} else {
		uint32_t made = host.installShadowTimeInfo(s);
		made += host.installShadowLoadingMenu(s);
		made += host.installShadowRangeWalk(s);
		made += host.installShadowSqrt(s);
		// The scene update's shadows (FastSceneUpdate.cpp): the actors' and the
		// cells'. The actors' entry is the SceneUpdate probe's as well.
		ShadowSettings scene = s;
		if (scene.actorUpdate && scene.sceneUpdate) {
			host.log("Shadow: NiAVObject::Update skipped - the SceneUpdate probe takes the same entry");
			scene.actorUpdate = false;
		}
		if (scene.actorUpdate || scene.gridUpdate) {
			made += host.installShadowActorUpdate(scene);
		}
		if (s.volatilePack && s.packCache) {
			host.log("Shadow: the volatile pack skipped - the PackCache probe takes the same entries");
		} else if (s.volatilePack) {
			made += host.installShadowVolatilePack(s);
		}
		host.log("Shadow: %u of %u detours made", made, s.Asked());
	}
	if (r.Asked() == 0) {
		host.log("Route: every switch is 0, nothing routed");
	} else {
		uint32_t made = 0;
		if (r.rangeWalk && RouteSkippedForShadow(s.rangeWalk, r.rangeWalk)) {
			host.log("Route: ShadowSceneLight::CollectGeometryInRange skipped - the RangeWalk shadow takes the same entry "
			         "and wins (a route without a prior clean shadow run makes nothing safer)");
		} else if (r.rangeWalk) {
			made += host.installRouteRangeWalk(r);
		}
		if (r.stereoDraw) {
			made += host.installRouteStereoDraw(r);
		}
		if (r.sqrt && RouteSkippedForShadow(s.sqrt, r.sqrt)) {
			host.log("Route: _CIsqrt skipped - the Sqrt shadow takes the same entry and wins (a route without a prior clean "
			         "shadow run makes nothing safer)");
		} else if (r.sqrt) {
			made += host.installRouteSqrt(r);
		}
		RouteSettings scene = r;
		if (scene.actorUpdate && s.sceneUpdate) {
			host.log("Route: NiAVObject::Update skipped - the SceneUpdate probe takes the same entry");
			scene.actorUpdate = false;
		} else if (scene.actorUpdate && RouteSkippedForShadow(s.actorUpdate, r.actorUpdate)) {
			host.log("Route: NiAVObject::Update skipped - the ActorUpdate shadow takes the same entry and wins (a route without a prior "
			         "clean shadow run makes nothing safer)");
			scene.actorUpdate = false;
		}
		if (scene.gridUpdate && RouteSkippedForShadow(s.gridUpdate, r.gridUpdate)) {
			host.log("Route: the cells' selected update skipped - the GridUpdate shadow takes the same entry and wins");
			scene.gridUpdate = false;
		}
		if (scene.actorUpdate || scene.gridUpdate) {
			made += host.installRouteActorUpdate(scene);
		}
		if (r.volatilePack && s.packCache) {
			host.log("Route: the volatile pack skipped - the PackCache probe takes the same entries");
		} else if (r.volatilePack && RouteSkippedForShadow(s.volatilePack, r.volatilePack)) {
			host.log("Route: the volatile pack skipped - its shadow takes the same entries and wins");
		} else if (r.volatilePack) {
			made += host.installRouteVolatilePack(r);
		}
		host.log("Route: %u of %u detours made", made, r.Asked());
	}
	const FixSettings& f = g_settings.fix;
	if (!f.Asked()) {
		host.log("Fix: every switch is 0, nothing fixed");
	} else if (host.installFixes(f)) {
		host.log("Fix: the grass fades by its distance from the camera, reaching %s (GrassFadePatchLogic.h); the shaders are replaced as the "
		         "game binds them",
		         f.grassFadeIniReach ? "as far as the INI's distances say" : "as far as the game has it straight ahead");
	} else {
		host.log("Fix: could not be set up - the game's grass shaders stay");
	}
	if (s.actorUpdateTiming) {
		if (s.sceneUpdate || s.actorUpdate || r.actorUpdate || s.gridUpdate || r.gridUpdate) {
			host.log("Probe: NiAVObject::Update timing skipped - the SceneUpdate probe, the ActorUpdate shadow or its route takes the same entry");
		} else {
			host.log("Probe: NiAVObject::Update timing - %u of 1 detours made", host.installActorUpdateTiming(s));
		}
	}
	const uint32_t probesAsked = ProbesAskedFor(s);
	if (probesAsked == 0 && !s.memoryProbe) {
		host.log("Probes: off");
	} else {
		const uint32_t probesMade = host.installProbes(s);
		host.log("Probes: %u of %u call counters made, memory %s", probesMade, probesAsked, s.memoryProbe ? "on" : "off");
	}
	// Last, so its first summary finds the probes and the route in place.
	if (s.summaryTimer) {
		if (host.startSummaryTimer(s)) {
			host.log("Log: a summary every %u s from the summary timer", s.summaryEverySeconds);
		} else {
			host.log("Log: the summary timer could not be started - only the detours that drive it write a summary");
		}
	}
	return true;
}

const Settings& LoadedSettings() {
	return g_settings;
}

void ResetForTest() {
	g_settings = Settings{};
}

}  // namespace obng::plugin
