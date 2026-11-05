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
// The stereo draw's route is four: DrawPrimitive 0x007672F0,
// DrawSkinnedPrimitive 0x00767520 and the renderer's two direct draw entries
// DrawShapeDirect 0x007694F0 and DrawTriStripsDirect 0x00769700
// (RouteStereoDraw.cpp).
constexpr UInt32 kRouteStereoDrawDetours = 4;

// [Route] StereoEyes: 1 is the 1:1 gate (the rebuild's port calls are the
// original's), 2 the double draw (StereoDraw.h). Anything else is unknown
// and read as 1, said in the log.
constexpr UInt8 StereoEyesFromIni(int value, bool* known) {
	*known = value == 1 || value == 2;
	return *known ? static_cast<UInt8>(value) : UInt8{1};
}

// [Route] StereoReapply: what the eye switch re-applies for the second eye.
// slot14 = the shader's slot 14 again (SetupShaderPrograms whole: the
// program bind, both maps, the palettes); view = the rebuilt ApplyConstants
// with the view filter on the vertex map only (ConstantReapply.h); none = no
// constants at all (the rows, the VIEW transform and the viewport only: a
// measurement of the switch's floor, the picture wrong). Anything else is
// unknown and read as view, said in the log.
enum class StereoReapply : UInt8 {
	kSlot14,
	kView,
	kNone,
};

inline StereoReapply StereoReapplyFromIni(const char* value, bool* known) {
	auto equals = [value](const char* word) {
		const char* a = value;
		const char* b = word;
		while (*a != '\0' && *b != '\0' && *a == *b) {
			++a;
			++b;
		}
		return *a == '\0' && *b == '\0';
	};
	if (equals("slot14")) {
		*known = true;
		return StereoReapply::kSlot14;
	}
	if (equals("none")) {
		*known = true;
		return StereoReapply::kNone;
	}
	*known = equals("view");
	return StereoReapply::kView;
}

constexpr const char* StereoReapplyName(StereoReapply r) {
	return r == StereoReapply::kSlot14 ? "slot14" : r == StereoReapply::kNone ? "none" : "view";
}

struct RouteSettings {
	bool rangeWalk = false;   // ShadowSceneLight::CollectGeometryInRange 0x007D59E0 runs BatchedWalk; off in play
	bool stereoDraw = false;  // the two draw functions run OBNG's rebuilds (RouteStereoDraw.cpp); off in play
	UInt8 stereoEyes = 1;     // the eyes the stereo draw may serve (1 or 2); two need eye data (StereoEyesApi.h)
	float stereoTestEyeShift = 0.0f;  // the plugin's own eye data: the viewport split in halves, eye 1 this many
	                                  // game units along the camera's right axis (StereoEyeSwitchLogic.h); 0 = off
	StereoReapply stereoReapply = StereoReapply::kView;  // the second eye's constants re-apply
	bool stereoRestoreDirect = true;  // [Route] StereoRestoreDirect: the direct entries re-apply the first eye's view constants after the second eye's draw; 0 for the comparison
	bool stereoParticles = true;  // [Route] StereoParticles: the particle renderer's device call (0x007667F6) runs through the eye plan; 0 leaves the game's call, particles in the first eye only
	bool stereoFollowers = true;  // [Route] StereoFollowers: geometry below the camera-following roots (the sky) gets the second eye's viewport without the view shift; 0 for the comparison

	UInt32 Asked() const { return (rangeWalk ? kRouteRangeWalkDetours : 0u) + (stereoDraw ? kRouteStereoDrawDetours : 0u); }
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
