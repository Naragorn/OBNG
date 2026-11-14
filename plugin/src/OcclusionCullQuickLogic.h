#pragma once

// [Route] OcclusionCullQuick: a draw decided without its rectangles.
//
// The culler's decision for a draw read the renderer's matrices, built the
// second eye's view and projected the bound into both eyes for every draw of
// every frame (0.18 us a draw on Nadi's Windows session of 2026-11-14,
// 0.24 us in a horde of 4 000 draws a pass: 0.96 ms a pass for 25 draws left
// out). The rectangles serve two ends only: a test of this frame, and telling
// a bound outside both frustums. A draw whose verdict on record is a visible
// one for this very object, and whose test is not due in this frame
// ([Route] OcclusionCullVisibleEvery, or a far LOD piece that is never
// tested), is drawn whatever its rectangles say - so they are not made.
//
// The one difference to the full decision (class C, on purpose; the test
// OcclusionCullQuickLogicTest states it): a bound outside both eyes'
// frustums is drawn here where the full decision leaves it out. The engine's
// own cull lets such a draw through rarely (0 in every window of that
// session), and drawing it is what the game does without the culler - the
// culler leaves out less, never more. [Route] OcclusionCullQuick=2 takes the
// full decision for every draw and counts where the quick one would differ.

#include "OcclusionCullGuardLogic.h"

namespace obng::plugin::occlusion_cull::quick {

enum class Rule : std::uint8_t {
	kOff = 0,        // every draw's rectangles, as before
	kOn = 1,
	kCheck = 2,      // the full decision taken, the quick one compared with it
	kAlternate = 3,  // on in every second summary window: both under the same load in one run
};

constexpr Rule RuleFromIni(int value) {
	return value == 0 ? Rule::kOff : value == 2 ? Rule::kCheck : value == 3 ? Rule::kAlternate : Rule::kOn;
}

constexpr const char* RuleName(Rule rule) {
	return rule == Rule::kOff     ? "off"
	       : rule == Rule::kOn    ? "on"
	       : rule == Rule::kCheck ? "checked against the full decision"
	                              : "on in every second summary window (a measurement)";
}

struct State {
	Rule rule = Rule::kOn;
	bool on = true;  // this summary window's

	void Set(Rule to) {
		rule = to;
		on = to == Rule::kOn || to == Rule::kAlternate;
	}
	void NextWindow() {
		if (rule == Rule::kAlternate) {
			on = !on;
		}
	}
	bool Checks() const { return rule == Rule::kCheck; }
};

// Whether the draw's decision in this frame needs no rectangles. `v` is the
// verdict on record for the address (null: none), not yet asked about in
// this frame; `every` is [Route] OcclusionCullVisibleEvery as it holds now.
inline bool Applies(const Verdict* v, std::uint32_t frame, std::uint32_t identity, std::uint32_t every, LodRule lodRule) {
	if (v == nullptr || identity == 0 || v->identity != identity || v->hiddenFrame != 0) {
		return false;
	}
	const bool due = TestDue(*v, Decision::kDraw, frame, every) && (!v->lod || guard::LodTested(lodRule));
	return !due;
}

// The decision where Applies() holds: what Decide() (OcclusionCullLogic.h)
// leaves in the verdict and counts for such a draw.
inline Decided Decide(Verdict& v, std::uint32_t frame, Stats* stats) {
	v.seenFrame = frame;
	v.decision = Decision::kDraw;
	if (v.lod) {
		++stats->guard.lodAsked;
	}
	Decided out;
	out.decision = Decision::kDraw;
	out.newThisFrame = true;
	out.verdict = &v;
	return out;
}

struct Counts {
	std::uint64_t quick = 0;    // decided without rectangles
	std::uint64_t full = 0;     // decided with them (the first time in a frame)
	std::uint64_t checked = 0;  // the check: draws the quick decision applied to
	std::uint64_t same = 0;
	std::uint64_t outside = 0;  // the full decision left out a bound outside both frustums
	std::uint64_t other = 0;    // any other difference: a fault
};

// The check: the full decision of a draw the quick one applied to.
inline void Compare(Counts* counts, const Decided& full, TestKind kind) {
	++counts->checked;
	if (full.decision == Decision::kDraw) {
		++counts->same;
	} else if (full.decision == Decision::kSkipFrustum && kind == TestKind::kBothOutside) {
		++counts->outside;
	} else {
		++counts->other;
	}
}

inline int FormatSummary(const Counts& c, const State& state, std::uint64_t passes, char* out, std::size_t capacity) {
	oc::LineWriter line{out, capacity};
	const double p = passes == 0 ? 1.0 : static_cast<double>(passes);
	line.Text("Route: occlusion cull quick decisions (%s, %s in this window) - %.1f a pass without their rectangles, %.1f with",
	          RuleName(state.rule), state.on ? "on" : "off", static_cast<double>(c.quick) / p, static_cast<double>(c.full) / p);
	if (state.Checks()) {
		line.Text("; check: %llu quick decisions compared, %llu the same, %llu outside both frustums (drawn where the full decision leaves out), "
		          "%llu MISMATCH",
		          static_cast<unsigned long long>(c.checked), static_cast<unsigned long long>(c.same), static_cast<unsigned long long>(c.outside),
		          static_cast<unsigned long long>(c.other));
	}
	return line.at;
}

}  // namespace obng::plugin::occlusion_cull::quick
