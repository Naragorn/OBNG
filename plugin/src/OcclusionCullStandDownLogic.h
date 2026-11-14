#pragma once

// [Route] OcclusionCullStandDown: the culler rests while it costs more than
// its skips save.
//
// In the open among many actors nearly every draw is seen: the culler
// decided 3 983 draws a pass, left out 25 and cost 1.06 ms a pass (Nadi's
// Windows session of 2026-11-14, a horde at Anvil), and in 29 % of that
// session's passes its skips saved less than its own work. So it judges
// itself by windows of passes: what the skips saved - the draws left out,
// and those below a node left out, at the mean cost of a draw that was drawn
// - against what deciding, the tests and the collect cost. Two windows in a
// row that do not pay, and it rests: every draw is drawn undecided, no test
// goes out. After the rest it works a probe window; a probe that pays ends
// the rest, one that does not doubles it up to a cap.
//
// What it changes (class C, on purpose): while it rests nothing is left out
// - the game as without the culler - and a place where it would pay is found
// one rest late. The saving is an estimate: a skinned draw costs more than
// the mean, so the culler rests a little early where the hidden draws are
// actors. [Route] OcclusionCullStandDown=2 judges and counts without resting.
// Pure, OcclusionCullStandDownLogicTest.

#include <cstddef>
#include <cstdint>

#include "OcclusionProbeLogic.h"

namespace obng::plugin::occlusion_cull::stand_down {

enum class Rule : std::uint8_t {
	kOff = 0,
	kOn = 1,
	kCount = 2,  // judged and counted, the culler works on
};

constexpr Rule RuleFromIni(int value) {
	return value == 1 ? Rule::kOn : value == 2 ? Rule::kCount : Rule::kOff;
}

constexpr const char* RuleName(Rule rule) {
	return rule == Rule::kOn ? "on" : rule == Rule::kCount ? "counted only" : "off";
}

struct Settings {
	std::uint32_t windowPasses = 32;     // the passes a window is judged on
	std::uint32_t warmPasses = 4;        // after a rest: passes before the window, until tests have come back and skips begin
	std::uint32_t restPasses = 256;      // the first rest
	std::uint32_t maxRestPasses = 2048;  // probes that keep failing double the rest up to this
	std::uint32_t failedWindows = 2;     // windows in a row that did not pay before the culler rests
	float resumeFactor = 1.25f;          // a probe must save this much of its cost to end the rest
};

// Counts that only grow, of the passes the culler worked in.
struct Totals {
	std::uint64_t draws = 0;         // draws asked about, the left out among them
	std::uint64_t skippedDraws = 0;  // of them left out
	std::uint64_t nodeDraws = 0;     // draws below nodes left out at the cull walk (never asked about)
	std::uint64_t cullTicks = 0;     // deciding, the tests' end, the collect
	std::uint64_t passTicks = 0;     // the eye pass, the culler's work in it included

	Totals Since(const Totals& then) const {
		Totals d;
		d.draws = draws - then.draws;
		d.skippedDraws = skippedDraws - then.skippedDraws;
		d.nodeDraws = nodeDraws - then.nodeDraws;
		d.cullTicks = cullTicks - then.cullTicks;
		d.passTicks = passTicks - then.passTicks;
		return d;
	}
};

// What a window's skips saved, in ticks: the draws left out at the mean cost
// of a draw that was drawn. With every draw left out there is no mean: twice
// the culler's cost, so that it counts as paying.
inline double Saving(const Totals& w) {
	const std::uint64_t leftOut = w.skippedDraws + w.nodeDraws;
	if (leftOut == 0) {
		return 0.0;
	}
	if (w.draws <= w.skippedDraws) {
		return 2.0 * static_cast<double>(w.cullTicks) + 1.0;
	}
	if (w.passTicks <= w.cullTicks) {
		return 0.0;
	}
	return static_cast<double>(leftOut) * static_cast<double>(w.passTicks - w.cullTicks) / static_cast<double>(w.draws - w.skippedDraws);
}

struct State {
	bool resting = false;
	bool probing = false;        // working the window after a rest
	std::uint32_t phase = 0;     // passes into the rest, or into the window
	std::uint32_t warmLeft = 0;  // passes of a probe's warm-up still to go
	std::uint32_t rest = 0;      // this rest's length; 0 when the last probe paid
	std::uint32_t failed = 0;    // windows in a row that did not pay
	Totals begin;                // at the window's begin
	// The account.
	std::uint64_t workedPasses = 0;
	std::uint64_t restedPasses = 0;
	std::uint64_t rests = 0;
	std::uint64_t resumes = 0;
	std::uint64_t windows = 0;
	std::uint64_t windowsPaid = 0;
	double lastSaving = 0.0;  // the last judged window's, in ticks
	double lastCost = 0.0;
};

// After an eye pass; `now` are the totals up to and including it. Answers
// whether the culler rests in the next pass.
inline bool Pass(State& s, const Settings& set, Rule rule, const Totals& now) {
	if (rule == Rule::kOff) {
		return false;
	}
	if (s.resting) {
		++s.restedPasses;
		if (++s.phase >= s.rest) {
			s.resting = false;
			s.probing = true;
			s.phase = 0;
			s.warmLeft = set.warmPasses;
			s.begin = now;
		}
		return s.resting;
	}
	++s.workedPasses;
	if (s.warmLeft > 0) {
		--s.warmLeft;
		s.begin = now;
		return false;
	}
	if (++s.phase < set.windowPasses) {
		return false;
	}
	const Totals window = now.Since(s.begin);
	s.lastSaving = Saving(window);
	s.lastCost = static_cast<double>(window.cullTicks);
	const bool pays = s.lastSaving >= static_cast<double>(s.probing ? set.resumeFactor : 1.0f) * s.lastCost;
	++s.windows;
	s.windowsPaid += pays ? 1u : 0u;
	s.phase = 0;
	s.begin = now;
	if (pays) {
		s.failed = 0;
		if (s.probing) {
			++s.resumes;
			s.rest = 0;
		}
		s.probing = false;
		return false;
	}
	if (!s.probing && ++s.failed < set.failedWindows) {
		return false;
	}
	s.failed = 0;
	s.probing = false;
	++s.rests;
	if (rule == Rule::kCount) {
		return false;
	}
	s.rest = s.rest == 0 ? set.restPasses : s.rest * 2 > set.maxRestPasses ? set.maxRestPasses : s.rest * 2;
	s.resting = true;
	return true;
}

inline int FormatSummary(const State& s, const Settings& set, Rule rule, double ticksPerMillisecond, char* out, std::size_t capacity) {
	occlusion::LineWriter line{out, capacity};
	line.Text("Route: occlusion cull stand-down (%s) - worked in %llu passes, rested in %llu; %llu rests begun, %llu ended by a probe that paid; "
	          "%llu of %llu windows paid; the last window: its skips saved ",
	          RuleName(rule), static_cast<unsigned long long>(s.workedPasses), static_cast<unsigned long long>(s.restedPasses),
	          static_cast<unsigned long long>(s.rests), static_cast<unsigned long long>(s.resumes), static_cast<unsigned long long>(s.windowsPaid),
	          static_cast<unsigned long long>(s.windows));
	const double passes = static_cast<double>(set.windowPasses == 0 ? 1u : set.windowPasses);
	line.Time(static_cast<std::uint64_t>(s.lastSaving / passes), ticksPerMillisecond);
	line.Text(" a pass, the culler cost ");
	line.Time(static_cast<std::uint64_t>(s.lastCost / passes), ticksPerMillisecond);
	line.Text(" a pass");
	return line.at;
}

}  // namespace obng::plugin::occlusion_cull::stand_down
