#pragma once

// [Route] OcclusionCullAct: whether the occlusion culler's verdicts are acted
// on. Test only, every draw is tested and decided as ever and drawn all the
// same - the eye pass's own clock (OcclusionCullLogic.h Stats::passTicks)
// then gives the pass with the culler's work and without its skips, which no
// other clock has in the flat game (OBVR's frame capture needs the headset
// renderer; docs/subsystems/occlusion-cull.md). Alternating by summary
// window, one run has the pass without and with the skips under the same
// load. Pure, OcclusionCullActLogicTest.

#include "OcclusionCullLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::plugin::occlusion_cull::act {

enum class Rule : std::uint8_t {
	kTestOnly = 0,
	kAct = 1,
	kAlternate = 2,
};

// The INI's value; anything but 0 and 2 is the culler as it is meant.
constexpr Rule RuleFromIni(int value) {
	return value == 0 ? Rule::kTestOnly : value == 2 ? Rule::kAlternate : Rule::kAct;
}

constexpr const char* RuleName(Rule rule) {
	return rule == Rule::kAct ? "acted on" : rule == Rule::kTestOnly ? "not acted on (test only)" : "acted on in every second summary window (a measurement)";
}

// Whether the next summary window acts on the verdicts, from the rule and
// whether the last one did.
constexpr bool ActsInWindow(Rule rule, bool actedLastWindow) {
	return rule == Rule::kAct || (rule == Rule::kAlternate && !actedLastWindow);
}

struct State {
	Rule rule = Rule::kAct;
	bool acting = true;          // this summary window's
	std::uint64_t heldBack = 0;  // this window's draws decided hidden or outside both frustums, and drawn (once a frame each)

	void Set(Rule to) {
		rule = to;
		acting = ActsInWindow(to, false);
		heldBack = 0;
	}
	void NextWindow() {
		acting = ActsInWindow(rule, acting);
		heldBack = 0;
	}
};

// cull::Decide, its decision carried out or not: test only, a skip is turned
// into a draw - in the table too, for the geometry's later passes of the
// frame - and counted. The verdict itself stays as the tests left it, so the
// draw is tested again every frame and a window that acts finds it warm.
inline Decided Decide(State& state, Table& table, std::uint32_t geometry, std::uint32_t frame, TestKind kind, const Settings& settings,
                      const Cameras& cameras, Stats* stats, std::uint32_t identity = 0, LodFn isLod = nullptr) {
	Decided decided = occlusion_cull::Decide(table, geometry, frame, kind, settings, cameras, stats, identity, isLod);
	if (!state.acting && decided.decision != Decision::kDraw) {
		++state.heldBack;
		decided.verdict->decision = Decision::kDraw;
		decided.decision = Decision::kDraw;
	}
	return decided;
}

// The window's account, written behind the culler's summary line (`at` is
// where that ends): two more totals for tools/cull-summary.py, and what the
// window did. Answers the line's new length.
inline int AppendSummary(const State& state, std::uint64_t eyePasses, char* out, std::size_t capacity, int at) {
	if (at < 0 || static_cast<std::size_t>(at) >= capacity) {
		return at;
	}
	oc::LineWriter line{out, capacity, at};
	line.Text(" heldBack %llu acted %u", static_cast<unsigned long long>(state.heldBack), state.acting ? 1u : 0u);
	if (state.acting) {
		line.Text("; verdicts acted on");
	} else {
		line.Text("; verdicts not acted on (test only): %.0f a pass decided hidden or outside and drawn",
		          static_cast<double>(state.heldBack) / (eyePasses == 0 ? 1.0 : static_cast<double>(eyePasses)));
	}
	return line.at;
}

}  // namespace obng::plugin::occlusion_cull::act
