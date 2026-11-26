#pragma once

// [Route] OcclusionCullBeforeBlend=2, the see-through rectangles. A blended record drawn before the pass's tests is
// see-through, and the depth it writes must not count as cover (the prison bars defect of 2026-11-13,
// docs/subsystems/occlusion-cull.md). Mode 1 sends the tests before the first blended record; in a town that record
// is often a small one near the start of the pass - an NPC's lower teeth, a notice, an easel - and the tests stop
// after a few dozen records (measured 2026-11-26 in the Market District: 35 - 60 tests a pass against about 300, 0 - 1 %
// of the draws left out against 20 %). Mode 2 keeps each blended record's screen rectangles instead, lets the tests
// run to the finish's end, and there drops every test that would read the depth through a kept surface: one whose
// rectangle shares pixels with the surface's in an eye where the surface is not behind the tested bound's nearest
// point. A dropped test gives no verdict, so its geometry is drawn, as a record after mode 1's early end was. A
// blended record without a rectangle that encloses it (it crosses the near plane, has no bound, the viewport is odd)
// or one beyond the list's size ends the tests as in mode 1. Pure logic, the caller is OcclusionCullSeeThrough.cpp.

#include <cstdint>
#include <cstdio>

#include "OcclusionCullLogic.h"
#include "OcclusionProbeLogic.h"

namespace obng::plugin::occlusion_cull::see_through {

namespace oc = occlusion;

constexpr std::uint32_t kSurfacesAtMost = 256;  // a pass's kept surfaces; one more ends the tests as in mode 1

enum class Mode : std::uint8_t {
	kAtFinishEnd,         // 0: the tests at the finish's end, blended records ignored (before 2026-11-13)
	kBeforeFirstBlended,  // 1: the tests before the first blended record (2026-11-13)
	kSeeThrough,          // 2: the tests at the finish's end less those that read through a blended surface
};

constexpr Mode ModeFromIni(int value) {
	return value == 0 ? Mode::kAtFinishEnd : value == 2 ? Mode::kSeeThrough : Mode::kBeforeFirstBlended;
}

constexpr const char* ModeName(Mode mode) {
	return mode == Mode::kAtFinishEnd ? "at the finish's end"
	       : mode == Mode::kSeeThrough ? "see-through rectangles"
	                                   : "before the first blended record";
}

// [Route] OcclusionCullBeforeBlendAlternate: the mode of the next summary window - modes 1 and 2 take turns, so that
// one run compares them under the same crowd (the Market District's crowd changes a run's draws by half, measured
// 2026-11-26); without it, or from mode 0, the mode stays.
constexpr Mode NextWindowMode(Mode current, bool alternate) {
	return !alternate || current == Mode::kAtFinishEnd ? current
	       : current == Mode::kSeeThrough             ? Mode::kBeforeFirstBlended
	                                                  : Mode::kSeeThrough;
}

// A record blends when its NiAlphaProperty has the blend bit (flags bit 0), as EndsTestsBefore reads it.
constexpr bool Blends(bool hasAlpha, std::uint32_t alphaFlags) { return hasAlpha && (alphaFlags & 1u) != 0; }

struct Surfaces {
	EyeRects rects[kSurfacesAtMost];
	std::uint32_t count = 0;
	void Reset() { count = 0; }
};

enum class Outcome : std::uint8_t {
	kNotBlended,  // the tests stay open (an opaque record, or mode 0)
	kKept,        // a blended record kept as its rectangles, the tests stay open
	kOffScreen,   // a blended record outside both eyes' frustums: no depth on screen, the tests stay open
	kEnds,        // the tests go out before this record
};

// A record about to be drawn while the pass's tests are still open: what it does to them. `rects` and `kinds` are
// the record's own (RectsFor, with the culler's margin), `eyes` the pass's cameras (2 with the eye data, 1 flat).
inline Outcome OnRecord(Mode mode, bool blends, const EyeRects& rects, const oc::RectKind kinds[2], std::uint32_t eyes, Surfaces* s) {
	if (!blends || mode == Mode::kAtFinishEnd) {
		return Outcome::kNotBlended;
	}
	if (mode == Mode::kBeforeFirstBlended || s->count >= kSurfacesAtMost) {
		return Outcome::kEnds;
	}
	EyeRects keep;
	bool any = false;
	for (std::uint32_t eye = 0; eye < eyes && eye < 2; ++eye) {
		if (kinds[eye] == oc::RectKind::kOutsideFrustum) {
			continue;
		}
		if (kinds[eye] != oc::RectKind::kTestable || !rects.has[eye]) {
			return Outcome::kEnds;  // no rectangle encloses what it draws in this eye
		}
		keep.rect[eye] = rects.rect[eye];
		keep.has[eye] = true;
		any = true;
	}
	if (!any) {
		return Outcome::kOffScreen;
	}
	s->rects[s->count++] = keep;
	return Outcome::kKept;
}

// The two rectangles share pixels (the right and bottom edges are exclusive, as the tests' quads cover them).
constexpr bool Overlap(const oc::ScreenRect& a, const oc::ScreenRect& b) {
	return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// Whether a test would read the depth through a kept surface: in an eye where both have a rectangle, the rectangles
// share pixels and the surface's nearest point is not behind the tested bound's nearest point (the test's quad sits
// at that depth; a surface wholly behind it cannot hide it).
inline bool ReadsThrough(const EyeRects& test, const Surfaces& s) {
	for (std::uint32_t i = 0; i < s.count; ++i) {
		for (int eye = 0; eye < 2; ++eye) {
			if (test.has[eye] && s.rects[i].has[eye] && Overlap(test.rect[eye], s.rects[i].rect[eye]) &&
			    s.rects[i].rect[eye].depth <= test.rect[eye].depth) {
				return true;
			}
		}
	}
	return false;
}

// Keeps, in their order, the tests that read through no kept surface and every trace control; a kept test that
// moves down is handed on as move(from, to) so that the trace's names can follow. Returns how many stay.
template <class Move>
std::uint32_t KeepTests(Test* tests, std::uint32_t count, const Surfaces& s, Move&& move) {
	if (s.count == 0) {
		return count;
	}
	std::uint32_t kept = 0;
	for (std::uint32_t i = 0; i < count; ++i) {
		if (!tests[i].control && ReadsThrough(tests[i].rects, s)) {
			continue;
		}
		if (kept != i) {
			tests[kept] = tests[i];
			move(i, kept);
		}
		++kept;
	}
	return kept;
}

struct Counts {
	std::uint64_t kept = 0;         // blended records kept as rectangles
	std::uint64_t offScreen = 0;    // blended records outside both frustums
	std::uint64_t endedNoRect = 0;  // passes whose tests ended at a blended record without a rectangle
	std::uint64_t endedFull = 0;    // passes whose tests ended with the list full
	std::uint64_t tests = 0;        // tests looked at when they went out
	std::uint64_t testsDropped = 0; // of them dropped as reading through
};

inline void FormatSummary(const Counts& c, Mode mode, std::uint64_t passes, char* out, std::size_t capacity) {
	const double p = passes != 0 ? static_cast<double>(passes) : 1.0;
	std::snprintf(out, capacity,
	              "Route: occlusion cull see-through (%s) - %.1f blended records a pass kept as rectangles, %.1f off screen; the tests ended early in %llu "
	              "passes (%llu without a rectangle, %llu with the list full); %.1f of %.1f tests a pass dropped as reading through a kept surface",
	              ModeName(mode), static_cast<double>(c.kept) / p, static_cast<double>(c.offScreen) / p,
	              static_cast<unsigned long long>(c.endedNoRect + c.endedFull), static_cast<unsigned long long>(c.endedNoRect),
	              static_cast<unsigned long long>(c.endedFull), static_cast<double>(c.testsDropped) / p, static_cast<double>(c.tests) / p);
}

}  // namespace obng::plugin::occlusion_cull::see_through
