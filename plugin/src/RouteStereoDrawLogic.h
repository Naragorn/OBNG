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

// The distinct viewport rectangles seen by draws the gate kept single
// (the interface mode), with their counts: which pass draws there.
struct OutsideViewports {
	static constexpr std::uint32_t kMaxRows = 8;
	struct Row {
		stereo_eye_switch::Viewport rect;
		std::uint64_t count = 0;
	};
	Row rows[kMaxRows] = {};
	std::uint32_t size = 0;
	std::uint64_t overflow = 0;

	void Add(const stereo_eye_switch::Viewport& rect) {
		for (std::uint32_t i = 0; i < size; ++i) {
			if (stereo_eye_switch::SameRect(rows[i].rect, rect)) {
				++rows[i].count;
				return;
			}
		}
		if (size >= kMaxRows) {
			++overflow;
			return;
		}
		rows[size].rect = rect;
		rows[size].count = 1;
		++size;
	}
};

// What the route did since the last summary.
struct Stats {
	std::uint64_t wholeDraws = 0;    // DrawPrimitive 0x007672F0 calls routed
	std::uint64_t skinnedDraws = 0;  // DrawSkinnedPrimitive 0x00767520 calls routed
	std::uint64_t wholeOutcome[3] = {0, 0, 0};    // by whole::Outcome
	std::uint64_t skinnedOutcome[4] = {0, 0, 0, 0};  // by skinned::Outcome
	std::uint64_t directShapeDraws = 0;   // DrawShapeDirect 0x007694F0 calls routed (slot 90)
	std::uint64_t directStripsDraws = 0;  // DrawTriStripsDirect 0x00769700 calls routed (slot 91)
	std::uint64_t particleDraws = 0;      // the particle draw's device call 0x007667F6 routed (slot 92's)
	std::uint64_t eyeSwitches = 0;        // Switch(1) calls: the second eye's draws framed
	std::uint64_t eyeSwitchesApplied = 0; // of them with eye data: the second eye's state set
	std::uint64_t eyeSwitchesViewportOnly = 0;  // of the applied: camera followers (the sky), no view shift
	std::uint64_t carriedRegisters = 0;   // registers given the remembered second-eye value (StereoEyeSwitchLogic.h RegisterPairs)
	std::uint64_t drawsOutsideEye = 0;    // draws with eye data whose viewport was not eye 0's: drawn once
	OutsideViewports outside;             // their viewports
	stereo_eye_switch::ShaderClasses shaderClasses;  // the shader vtables the re-apply saw (applied switches)
	std::uint64_t reappliesType1 = 0;     // re-applies that took the type-1 shader's maps (SpeedTree branches)
	std::uint64_t switchTicks = 0;        // TSC ticks inside Switch(1) and Switch(0), applied switches only
	std::uint64_t reapplyTicks = 0;       // of them inside the constants re-apply
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
		eyeSwitchesViewportOnly += c.viewportOnly;
		carriedRegisters += c.carriedRegisters;
	}
	void AddTicks(std::uint64_t switchT, std::uint64_t reapplyT) {
		switchTicks += switchT;
		reapplyTicks += reapplyT;
	}
	void Reset() {
		const std::uint8_t keep = eyes;
		*this = Stats{};
		eyes = keep;
	}
};

// [Route] StereoDrawTimer: the whole draw's device calls of a summary
// window, timed - what the device's draw itself costs beside the eye switch.
inline int FormatDeviceDraws(std::uint64_t calls, std::uint64_t ticks, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (calls == 0 || ticksPerMillisecond <= 0.0) {
		return std::snprintf(out, capacity, "Route: stereo draw device calls - none timed");
	}
	const double ms = static_cast<double>(ticks) / ticksPerMillisecond;
	return std::snprintf(out, capacity, "Route: stereo draw device calls (the whole draw's) - %llu calls, %.3f ms in all, %.2f us each",
	                     static_cast<unsigned long long>(calls), ms, ms * 1000.0 / static_cast<double>(calls));
}

// [Route] StereoDrawTimer: the whole draw's steps of a summary window - the
// draw as a whole, the shader's slots 10..19 and the render state's two
// calls - so that what is neither the device's draw nor the eye switch has a
// name.
struct StepTimes {
	static constexpr unsigned kFirstSlot = 10;
	static constexpr unsigned kSlots = 10;  // 10..19
	std::uint64_t wholeCalls = 0;
	std::uint64_t wholeTicks = 0;
	std::uint64_t slotCalls[kSlots] = {};
	std::uint64_t slotTicks[kSlots] = {};
	std::uint64_t stateCalls = 0;  // SubObjectSlot1 and SetDrawFlag
	std::uint64_t stateTicks = 0;
	// False: no slot of the draw's ten, nothing added.
	bool AddSlot(unsigned slot, std::uint64_t ticks) {
		if (slot < kFirstSlot || slot >= kFirstSlot + kSlots) {
			return false;
		}
		++slotCalls[slot - kFirstSlot];
		slotTicks[slot - kFirstSlot] += ticks;
		return true;
	}
};

// [Route] StereoDrawTimer: how often the pass setup (shader slot 12) meets
// the pass the call before it set - the upper bound of what a check before
// the setup could leave out. Only the whole draw's calls are seen: a skinned
// or direct draw between two of them is not, so the counts are upper bounds.
struct PassRepeats {
	static constexpr unsigned kPasses = 64;
	std::uint64_t calls = 0;
	std::uint64_t samePass = 0;           // the same pass object as the call before
	std::uint64_t samePassAndStates = 0;  // and the same property and effect state
	std::uint32_t passes[kPasses] = {};   // the distinct pass objects of the window
	unsigned passCount = 0;
	bool passesFull = false;
	std::uint32_t lastPass = 0;
	std::uint32_t lastProperty = 0;
	std::uint32_t lastEffect = 0;

	void Note(std::uint32_t pass, std::uint32_t propertyState, std::uint32_t effectState) {
		if (calls != 0 && pass == lastPass) {
			++samePass;
			if (propertyState == lastProperty && effectState == lastEffect) {
				++samePassAndStates;
			}
		}
		++calls;
		lastPass = pass;
		lastProperty = propertyState;
		lastEffect = effectState;
		for (unsigned i = 0; i < passCount; ++i) {
			if (passes[i] == pass) {
				return;
			}
		}
		if (passCount == kPasses) {
			passesFull = true;
			return;
		}
		passes[passCount++] = pass;
	}
};

inline int FormatPassRepeats(const PassRepeats& r, char* out, std::size_t capacity) {
	if (r.calls == 0) {
		return std::snprintf(out, capacity, "Route: stereo draw pass setups - none seen");
	}
	const double percent = 100.0 / static_cast<double>(r.calls);
	return std::snprintf(out, capacity,
	                     "Route: stereo draw pass setups (slot 12, the whole draw's) - %llu calls, %llu (%.1f %%) with the pass object of the call "
	                     "before, %llu (%.1f %%) with its property and effect state too; %u%s distinct pass objects",
	                     static_cast<unsigned long long>(r.calls), static_cast<unsigned long long>(r.samePass),
	                     static_cast<double>(r.samePass) * percent, static_cast<unsigned long long>(r.samePassAndStates),
	                     static_cast<double>(r.samePassAndStates) * percent, r.passCount, r.passesFull ? " or more" : "");
}

// [Route] StereoDrawTimer: the eye switch's device and memory steps of a
// summary window, so that the switch's own cost has names.
struct PortTimes {
	enum Step : unsigned { kReadView, kWriteRows, kSetTransform, kSetViewport, kGetViewport, kReadRegisters, kWriteRegisters, kSteps };
	static constexpr const char* kNames[kSteps] = {"read view",   "write rows",               "SetTransform(VIEW)",      "SetViewport",
	                                               "GetViewport", "GetVertexShaderConstantF", "SetVertexShaderConstantF"};
	std::uint64_t calls[kSteps] = {};
	std::uint64_t ticks[kSteps] = {};
	void Add(Step step, std::uint64_t t) {
		++calls[step];
		ticks[step] += t;
	}
};

inline int FormatPortTimes(const PortTimes& t, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (ticksPerMillisecond <= 0.0) {
		return std::snprintf(out, capacity, "Route: stereo draw switch steps - none timed");
	}
	int at = std::snprintf(out, capacity, "Route: stereo draw switch steps -");
	for (unsigned i = 0; i < PortTimes::kSteps && at >= 0 && static_cast<std::size_t>(at) < capacity; ++i) {
		const double ms = static_cast<double>(t.ticks[i]) / ticksPerMillisecond;
		at += std::snprintf(out + at, capacity - at, "%s %s %llu calls %.3f ms (%.3f us)", i == 0 ? "" : ";", PortTimes::kNames[i],
		                    static_cast<unsigned long long>(t.calls[i]), ms, t.calls[i] != 0 ? ms * 1000.0 / static_cast<double>(t.calls[i]) : 0.0);
	}
	return at;
}

inline int FormatStepTimes(const StepTimes& t, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (t.wholeCalls == 0 || ticksPerMillisecond <= 0.0) {
		return std::snprintf(out, capacity, "Route: stereo draw steps - none timed");
	}
	const auto ms = [ticksPerMillisecond](std::uint64_t ticks) { return static_cast<double>(ticks) / ticksPerMillisecond; };
	int at = std::snprintf(out, capacity, "Route: stereo draw steps (the whole draw's) - %llu draws, %.3f ms in all; render state calls %llu, %.3f ms",
	                       static_cast<unsigned long long>(t.wholeCalls), ms(t.wholeTicks), static_cast<unsigned long long>(t.stateCalls),
	                       ms(t.stateTicks));
	for (unsigned i = 0; i < StepTimes::kSlots && at >= 0 && static_cast<std::size_t>(at) < capacity; ++i) {
		if (t.slotCalls[i] == 0) {
			continue;
		}
		at += std::snprintf(out + at, capacity - at, "; slot %u %llu calls, %.3f ms", StepTimes::kFirstSlot + i,
		                    static_cast<unsigned long long>(t.slotCalls[i]), ms(t.slotTicks[i]));
	}
	return at;
}

// One device draw under the eye plan: the call as the game made it; with a
// plan of two eyes the second eye's state, the same call again, the first
// eye's state back. The result is the first draw's (what the game's call
// returned). For the draw paths that are routed at their device call
// instead of being rebuilt - the particle renderer, vtable slot 92
// 0x00765990: 1 050 instructions of vertex building around one
// DrawIndexedPrimitive at 0x007667F6..0x007667FD.
template <class Device, class Switch>
std::int32_t DrawWithPlan(engine::render::stereo_draw::EyePlan plan, Device& device, Switch& eyeSwitch) {
	const std::int32_t result = device.Draw();
	if (engine::render::stereo_draw::DrawsTwice(plan)) {
		eyeSwitch.Switch(1);
		device.Draw();
		eyeSwitch.Switch(0);
	}
	return result;
}

// "Route: stereo draw (eyes N) - W whole draws (drawn D, refused R, null record Z), S skinned draws
// (closed C, no skin K, refused R, record missing M), N eye switches (applied A), N draws outside eye 0's viewport, eye data set S, refused R;
// switch X.XX us, re-apply Y.YY us per applied switch" - the last part only with a clock
// (ticksPerMillisecond > 0 and applied switches).
inline int FormatStats(const Stats& s, double ticksPerMillisecond, char* out, std::size_t capacity) {
	using WO = whole::Outcome;
	using SO = skinned::Outcome;
	int at = std::snprintf(out, capacity,
		"Route: stereo draw (eyes %u) - %llu whole draws (drawn %llu, refused %llu, null record %llu), %llu skinned draws "
		"(closed %llu, no skin %llu, refused %llu, record missing %llu), %llu direct shape draws, %llu direct strips draws, %llu particle draws, "
		"%llu eye switches (applied %llu, %llu of them without a view shift, %llu registers carried), %llu draws outside eye 0's viewport, "
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
		static_cast<unsigned long long>(s.directShapeDraws), static_cast<unsigned long long>(s.directStripsDraws),
		static_cast<unsigned long long>(s.particleDraws),
		static_cast<unsigned long long>(s.eyeSwitches), static_cast<unsigned long long>(s.eyeSwitchesApplied),
		static_cast<unsigned long long>(s.eyeSwitchesViewportOnly), static_cast<unsigned long long>(s.carriedRegisters),
		static_cast<unsigned long long>(s.drawsOutsideEye), static_cast<unsigned long long>(s.eyeDataSets),
		static_cast<unsigned long long>(s.eyeDataRefused));
	if (at >= 0 && static_cast<std::size_t>(at) < capacity && ticksPerMillisecond > 0.0 && s.eyeSwitchesApplied != 0 && s.switchTicks != 0) {
		const double perSwitch = 1000.0 / ticksPerMillisecond / static_cast<double>(s.eyeSwitchesApplied);
		at += std::snprintf(out + at, capacity - at, "; switch %.2f us, re-apply %.2f us per applied switch",
		                    static_cast<double>(s.switchTicks) * perSwitch, static_cast<double>(s.reapplyTicks) * perSwitch);
	}
	if (at >= 0 && static_cast<std::size_t>(at) < capacity && s.outside.size != 0) {
		at += std::snprintf(out + at, capacity - at, "; outside viewports:");
		for (std::uint32_t i = 0; i < s.outside.size && at >= 0 && static_cast<std::size_t>(at) < capacity; ++i) {
			const OutsideViewports::Row& r = s.outside.rows[i];
			at += std::snprintf(out + at, capacity - at, " %u,%u %ux%u x%llu", r.rect.x, r.rect.y, r.rect.width, r.rect.height,
			                    static_cast<unsigned long long>(r.count));
		}
		if (s.outside.overflow != 0 && at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += std::snprintf(out + at, capacity - at, " (+%llu in other rects)", static_cast<unsigned long long>(s.outside.overflow));
		}
	}
	if (at >= 0 && static_cast<std::size_t>(at) < capacity && s.shaderClasses.size != 0) {
		at += std::snprintf(out + at, capacity - at, "; shader classes (vtable x switches):");
		for (std::uint32_t i = 0; i < s.shaderClasses.size && at >= 0 && static_cast<std::size_t>(at) < capacity; ++i) {
			at += std::snprintf(out + at, capacity - at, " %08X x%llu", s.shaderClasses.rows[i].vtable,
			                    static_cast<unsigned long long>(s.shaderClasses.rows[i].count));
		}
		if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += std::snprintf(out + at, capacity - at, "; %llu re-applies through the type-1 shader's maps",
			                    static_cast<unsigned long long>(s.reappliesType1));
		}
	}
	return at;
}

}  // namespace obng::plugin::stereo_draw_route
