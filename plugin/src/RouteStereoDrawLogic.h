#pragma once

// The stereo draw route's decisions over plain values, so every flow runs in
// a test without the game (plugin/tests/RouteStereoDrawLogicTest.cpp): the
// eye plan from the settings, the counters per draw function and outcome,
// the eye switch's counters, and the summary line. The eye switch itself is
// StereoEyeSwitchLogic.h; RouteStereoDraw.cpp supplies the engine ports and
// the two detours.

#include "RouteSettings.h"
#include "StereoEyeSwitchLogic.h"
#include "engine/render/NiDX9Renderer_DrawPrimitiveLogic.h"
#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitiveLogic.h"
#include "engine/render/StereoDraw.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::stereo_draw_route {

namespace stereo = engine::render::stereo_draw;
namespace whole = engine::render::draw_primitive;
namespace skinned = engine::render::draw_skinned_primitive;

// The plan the route hands both rebuilds for one draw: [Route] StereoEyes
// allows two, the eye data (the interface's or the test mode's) makes it so
// (StereoEyeSwitchLogic.h EffectivePlan).
constexpr stereo::EyePlan PlanFor(const RouteSettings& settings, bool hasEyeData) {
	return stereo_eye_switch::EffectivePlan(settings.stereoEyes, hasEyeData);
}

// What the route did since the last summary.
struct Stats {
	std::uint64_t wholeDraws = 0;    // DrawPrimitive 0x007672F0 calls routed
	std::uint64_t skinnedDraws = 0;  // DrawSkinnedPrimitive 0x00767520 calls routed
	std::uint64_t wholeOutcome[3] = {0, 0, 0};    // by whole::Outcome
	std::uint64_t skinnedOutcome[4] = {0, 0, 0, 0};  // by skinned::Outcome
	std::uint64_t eyeSwitches = 0;        // Switch(1) calls: the second eye's draws framed
	std::uint64_t eyeSwitchesApplied = 0; // of them with eye data: the second eye's state set
	std::uint64_t drawsOutsideEye = 0;    // draws with eye data whose viewport was not eye 0's: drawn once
	std::uint64_t eyeDataSets = 0;        // ObngStereoSetEyes accepted
	std::uint64_t eyeDataRefused = 0;     // ObngStereoSetEyes refused
	std::uint8_t eyes = 1;                // the eyes allowed

	void AddWhole(whole::Outcome outcome) {
		++wholeDraws;
		++wholeOutcome[static_cast<unsigned>(outcome)];
	}
	void AddSkinned(skinned::Outcome outcome) {
		++skinnedDraws;
		++skinnedOutcome[static_cast<unsigned>(outcome)];
	}
	void AddSwitches(const stereo_eye_switch::SwitchCounters& c) {
		eyeSwitches += c.applied + c.noData;
		eyeSwitchesApplied += c.applied;
	}
	void Reset() {
		const std::uint8_t keep = eyes;
		*this = Stats{};
		eyes = keep;
	}
};

// "Route: stereo draw (eyes N) - W whole draws (drawn D, refused R, null record Z), S skinned draws
// (closed C, no skin K, refused R, record missing M), N eye switches (applied A), N draws outside eye 0's viewport, eye data set S, refused R"
inline int FormatStats(const Stats& s, char* out, std::size_t capacity) {
	using WO = whole::Outcome;
	using SO = skinned::Outcome;
	return std::snprintf(out, capacity,
		"Route: stereo draw (eyes %u) - %llu whole draws (drawn %llu, refused %llu, null record %llu), %llu skinned draws "
		"(closed %llu, no skin %llu, refused %llu, record missing %llu), %llu eye switches (applied %llu), %llu draws outside eye 0's viewport, "
		"eye data set %llu, refused %llu",
		static_cast<unsigned>(s.eyes), static_cast<unsigned long long>(s.wholeDraws),
		static_cast<unsigned long long>(s.wholeOutcome[static_cast<unsigned>(WO::Drawn)]),
		static_cast<unsigned long long>(s.wholeOutcome[static_cast<unsigned>(WO::PipelineRefused)]),
		static_cast<unsigned long long>(s.wholeOutcome[static_cast<unsigned>(WO::NullRecord)]),
		static_cast<unsigned long long>(s.skinnedDraws),
		static_cast<unsigned long long>(s.skinnedOutcome[static_cast<unsigned>(SO::kClosed)]),
		static_cast<unsigned long long>(s.skinnedOutcome[static_cast<unsigned>(SO::kNoSkin)]),
		static_cast<unsigned long long>(s.skinnedOutcome[static_cast<unsigned>(SO::kPipelineRefused)]),
		static_cast<unsigned long long>(s.skinnedOutcome[static_cast<unsigned>(SO::kRecordMissing)]),
		static_cast<unsigned long long>(s.eyeSwitches), static_cast<unsigned long long>(s.eyeSwitchesApplied),
		static_cast<unsigned long long>(s.drawsOutsideEye), static_cast<unsigned long long>(s.eyeDataSets),
		static_cast<unsigned long long>(s.eyeDataRefused));
}

}  // namespace obng::plugin::stereo_draw_route
