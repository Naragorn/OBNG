// [Route] OcclusionCullBeforeBlend=2, the see-through rectangles (plugin/src/OcclusionCullSeeThroughLogic.h): a
// blended record drawn before the pass's tests is kept as its screen rectangles instead of ending the tests, and the
// tests that would read the depth through one of them are dropped when the tests go out. The mode from the INI, what
// a record does to the open tests under each mode, the overlap and depth rule, the compaction of a batch, and the
// summary line.

#include "Check.h"
#include "OcclusionCullSeeThroughLogic.h"

#include <cstring>
#include <string>

namespace cull = obng::plugin::occlusion_cull;
namespace st = obng::plugin::occlusion_cull::see_through;
namespace oc = obng::plugin::occlusion;

namespace {

oc::ScreenRect Rect(float left, float top, float right, float bottom, float depth) {
	oc::ScreenRect r{};
	r.left = left;
	r.top = top;
	r.right = right;
	r.bottom = bottom;
	r.depth = depth;
	return r;
}

// The same rectangle in both eyes, or in the left one only.
cull::EyeRects Both(oc::ScreenRect r) {
	cull::EyeRects e;
	e.rect[0] = r;
	e.rect[1] = r;
	e.has[0] = true;
	e.has[1] = true;
	return e;
}

cull::EyeRects LeftOnly(oc::ScreenRect r) {
	cull::EyeRects e;
	e.rect[0] = r;
	e.has[0] = true;
	return e;
}

const oc::RectKind kTestable[2] = {oc::RectKind::kTestable, oc::RectKind::kTestable};

void TestMode() {
	Section("The mode from the INI");
	Check(st::ModeFromIni(0) == st::Mode::kAtFinishEnd, "0: the tests at the finish's end (before 2026-11-13)");
	Check(st::ModeFromIni(1) == st::Mode::kBeforeFirstBlended, "1: before the first blended record (2026-11-13)");
	Check(st::ModeFromIni(2) == st::Mode::kSeeThrough, "2: the see-through rectangles");
	Check(st::ModeFromIni(7) == st::Mode::kBeforeFirstBlended && st::ModeFromIni(-1) == st::Mode::kBeforeFirstBlended,
	      "any other value: the rule of 2026-11-13, the safe one");
	Check(std::string(st::ModeName(st::Mode::kAtFinishEnd)) != st::ModeName(st::Mode::kBeforeFirstBlended) &&
	          std::string(st::ModeName(st::Mode::kBeforeFirstBlended)) != st::ModeName(st::Mode::kSeeThrough),
	      "every mode has its name");
}

void TestAlternate() {
	Section("Modes 1 and 2 take turns window by window when asked");
	Check(st::NextWindowMode(st::Mode::kSeeThrough, true) == st::Mode::kBeforeFirstBlended, "2 -> 1");
	Check(st::NextWindowMode(st::Mode::kBeforeFirstBlended, true) == st::Mode::kSeeThrough, "1 -> 2");
	Check(st::NextWindowMode(st::Mode::kAtFinishEnd, true) == st::Mode::kAtFinishEnd, "mode 0 stays: it has no turn");
	Check(st::NextWindowMode(st::Mode::kSeeThrough, false) == st::Mode::kSeeThrough && st::NextWindowMode(st::Mode::kBeforeFirstBlended, false) ==
	                                                                                    st::Mode::kBeforeFirstBlended,
	      "not asked: the mode stays");
}

void TestBlends() {
	Section("A record blends when its alpha property has the blend bit");
	Check(st::Blends(true, 0x00ED), "a notice (Broadsheet01, 00ED): blends");
	Check(st::Blends(true, 0x12ED), "an NPC's teeth and the far trees (12ED, blend and test): blend");
	Check(!st::Blends(true, 0x12EC), "alpha test alone: no");
	Check(!st::Blends(false, 0x00ED), "no alpha property: no");
}

void TestOnRecordOtherModes() {
	Section("What a record does to the open tests: modes 0 and 1");
	st::Surfaces s;
	const cull::EyeRects small = Both(Rect(10, 10, 20, 20, 0.5f));
	Check(st::OnRecord(st::Mode::kBeforeFirstBlended, true, small, kTestable, 2, &s) == st::Outcome::kEnds,
	      "mode 1: a blended record ends the tests, however small");
	Check(st::OnRecord(st::Mode::kBeforeFirstBlended, false, small, kTestable, 2, &s) == st::Outcome::kNotBlended, "mode 1: an opaque one does not");
	Check(st::OnRecord(st::Mode::kAtFinishEnd, true, small, kTestable, 2, &s) == st::Outcome::kNotBlended,
	      "mode 0: nothing ends them, a blended record is not looked at");
	Check(s.count == 0, "neither mode keeps a rectangle");
}

void TestOnRecordSeeThrough() {
	Section("What a record does to the open tests: mode 2");
	st::Surfaces s;
	const cull::EyeRects teeth = Both(Rect(100, 100, 104, 103, 0.7f));
	Check(st::OnRecord(st::Mode::kSeeThrough, false, teeth, kTestable, 2, &s) == st::Outcome::kNotBlended && s.count == 0,
	      "an opaque record: nothing kept, the tests stay open");
	Check(st::OnRecord(st::Mode::kSeeThrough, true, teeth, kTestable, 2, &s) == st::Outcome::kKept && s.count == 1,
	      "a blended record with its rectangles: kept, the tests stay open");
	Check(s.rects[0].has[0] && s.rects[0].has[1] && s.rects[0].rect[1].right == 104.0f, "both eyes' rectangles kept as they came");

	const oc::RectKind oneOut[2] = {oc::RectKind::kTestable, oc::RectKind::kOutsideFrustum};
	cull::EyeRects half = Both(Rect(0, 0, 50, 50, 0.4f));
	Check(st::OnRecord(st::Mode::kSeeThrough, true, half, oneOut, 2, &s) == st::Outcome::kKept && s.count == 2 && s.rects[1].has[0] &&
	          !s.rects[1].has[1],
	      "seen by one eye only: kept for that eye alone");

	const oc::RectKind bothOut[2] = {oc::RectKind::kOutsideFrustum, oc::RectKind::kOutsideFrustum};
	Check(st::OnRecord(st::Mode::kSeeThrough, true, half, bothOut, 2, &s) == st::Outcome::kOffScreen && s.count == 2,
	      "outside both eyes' frustums: it writes no depth on screen, nothing kept, the tests stay open");

	const oc::RectKind near[2] = {oc::RectKind::kTestable, oc::RectKind::kCrossesNear};
	Check(st::OnRecord(st::Mode::kSeeThrough, true, half, near, 2, &s) == st::Outcome::kEnds && s.count == 2,
	      "crossing the near plane in an eye: no rectangle encloses it, the tests end as in mode 1");
	const oc::RectKind noBound[2] = {oc::RectKind::kNoBound, oc::RectKind::kNoBound};
	Check(st::OnRecord(st::Mode::kSeeThrough, true, half, noBound, 2, &s) == st::Outcome::kEnds, "no bound: the tests end");
	const oc::RectKind port[2] = {oc::RectKind::kTestable, oc::RectKind::kViewport};
	Check(st::OnRecord(st::Mode::kSeeThrough, true, half, port, 2, &s) == st::Outcome::kEnds, "an odd viewport: the tests end");

	const oc::RectKind flatSecond[2] = {oc::RectKind::kTestable, oc::RectKind::kCrossesNear};
	Check(st::OnRecord(st::Mode::kSeeThrough, true, LeftOnly(Rect(0, 0, 5, 5, 0.3f)), flatSecond, 1, &s) == st::Outcome::kKept && s.count == 3 &&
	          !s.rects[2].has[1],
	      "one eye (the flat game): the second eye's kind is not looked at");

	st::Surfaces full;
	for (std::uint32_t i = 0; i < st::kSurfacesAtMost; ++i) {
		st::OnRecord(st::Mode::kSeeThrough, true, teeth, kTestable, 2, &full);
	}
	Check(full.count == st::kSurfacesAtMost, "the list fills to its size");
	Check(st::OnRecord(st::Mode::kSeeThrough, true, teeth, kTestable, 2, &full) == st::Outcome::kEnds && full.count == st::kSurfacesAtMost,
	      "one more blended record with the list full: the tests end, nothing overwritten");
	Check(st::OnRecord(st::Mode::kSeeThrough, false, teeth, kTestable, 2, &full) == st::Outcome::kNotBlended, "an opaque record with the list full: open");
	full.Reset();
	Check(full.count == 0, "the next pass starts empty");
}

void TestReadsThrough() {
	Section("A test reads through a kept surface where the rectangles meet and the surface is not behind it");
	st::Surfaces s;
	st::OnRecord(st::Mode::kSeeThrough, true, Both(Rect(100, 100, 200, 200, 0.5f)), kTestable, 2, &s);
	Check(st::ReadsThrough(Both(Rect(150, 150, 250, 250, 0.8f)), s), "overlapping, the surface in front of the bound's nearest point: dropped");
	Check(st::ReadsThrough(Both(Rect(150, 150, 250, 250, 0.5f)), s), "the surface at the same depth: dropped (it could hide the nearest point)");
	Check(!st::ReadsThrough(Both(Rect(150, 150, 250, 250, 0.3f)), s), "the surface behind the bound's nearest point: kept, it hides nothing");
	Check(!st::ReadsThrough(Both(Rect(200, 100, 300, 200, 0.8f)), s), "touching at the edge (200): no shared pixel, kept");
	Check(!st::ReadsThrough(Both(Rect(300, 300, 400, 400, 0.8f)), s), "apart: kept");
	Check(st::ReadsThrough(Both(Rect(120, 120, 130, 130, 0.9f)), s), "inside the surface: dropped");
	Check(st::ReadsThrough(Both(Rect(50, 50, 400, 400, 0.9f)), s), "around the surface: dropped");

	st::Surfaces leftOnly;
	const oc::RectKind rightOut[2] = {oc::RectKind::kTestable, oc::RectKind::kOutsideFrustum};
	st::OnRecord(st::Mode::kSeeThrough, true, Both(Rect(0, 0, 100, 100, 0.2f)), rightOut, 2, &leftOnly);
	cull::EyeRects rightTest;
	rightTest.rect[1] = Rect(10, 10, 20, 20, 0.9f);
	rightTest.has[1] = true;
	Check(!st::ReadsThrough(rightTest, leftOnly), "the surface in the left eye only, the test in the right eye only: kept");
	Check(st::ReadsThrough(LeftOnly(Rect(10, 10, 20, 20, 0.9f)), leftOnly), "the same rectangle in the left eye: dropped");
	Check(!st::ReadsThrough(Both(Rect(10, 10, 20, 20, 0.9f)), st::Surfaces{}), "no surface kept: nothing is dropped");
}

void TestKeepTests() {
	Section("The batch less the tests that read through, in order, the trace's names following");
	st::Surfaces s;
	st::OnRecord(st::Mode::kSeeThrough, true, Both(Rect(100, 100, 200, 200, 0.5f)), kTestable, 2, &s);
	cull::Test tests[5];
	tests[0].geometry = 1;
	tests[0].rects = Both(Rect(0, 0, 50, 50, 0.9f));      // apart: kept
	tests[1].geometry = 2;
	tests[1].rects = Both(Rect(150, 150, 160, 160, 0.9f));  // behind the surface: dropped
	tests[2].geometry = 3;
	tests[2].rects = Both(Rect(150, 150, 160, 160, 0.1f));  // in front of it: kept
	tests[3].control = true;
	tests[3].rects = Both(Rect(0, 0, 4036, 3376, 0.99f));  // a trace control over everything: kept
	tests[4].geometry = 5;
	tests[4].rects = Both(Rect(190, 190, 210, 210, 0.6f));  // behind, overlapping: dropped
	std::uint32_t moves[5][2] = {};
	std::uint32_t moved = 0;
	const std::uint32_t kept = st::KeepTests(tests, 5, s, [&](std::uint32_t from, std::uint32_t to) {
		moves[moved][0] = from;
		moves[moved][1] = to;
		++moved;
	});
	Check(kept == 3, "three of five stay");
	Check(tests[0].geometry == 1 && tests[1].geometry == 3 && tests[2].control, "in their order: the first, the third and the control");
	Check(moved == 2 && moves[0][0] == 2 && moves[0][1] == 1 && moves[1][0] == 3 && moves[1][1] == 2,
	      "only the tests that move are handed on (from 2 to 1, from 3 to 2), so the trace's names can follow");
	cull::Test one[1];
	one[0].geometry = 9;
	one[0].rects = Both(Rect(0, 0, 10, 10, 0.9f));
	std::uint32_t none = 0;
	Check(st::KeepTests(one, 1, st::Surfaces{}, [&](std::uint32_t, std::uint32_t) { ++none; }) == 1 && none == 0,
	      "no surface kept: the batch stays as it is, nothing moved");
	Check(st::KeepTests(one, 0, s, [&](std::uint32_t, std::uint32_t) { ++none; }) == 0 && none == 0, "an empty batch: nothing to do");
}

void TestSummary() {
	Section("The summary line");
	st::Counts c;
	c.kept = 4000;
	c.offScreen = 100;
	c.endedNoRect = 3;
	c.endedFull = 1;
	c.tests = 30000;
	c.testsDropped = 600;
	char line[400];
	st::FormatSummary(c, st::Mode::kSeeThrough, 100, line, sizeof(line));
	const std::string text(line);
	Check(text.find("see-through") != std::string::npos, "the mode named");
	Check(text.find("40.0 blended records a pass kept") != std::string::npos, "kept surfaces a pass");
	Check(text.find("1.0 off screen") != std::string::npos, "off-screen ones a pass");
	Check(text.find("ended early in 4 passes (3 without a rectangle, 1 with the list full)") != std::string::npos, "the early ends by cause");
	Check(text.find("6.0 of 300.0 tests a pass dropped") != std::string::npos, "the tests dropped against those sent");
	st::FormatSummary(st::Counts{}, st::Mode::kBeforeFirstBlended, 0, line, sizeof(line));
	Check(std::string(line).find("before the first blended record") != std::string::npos, "mode 1: named, no division by zero passes");
}

}  // namespace

int main() {
	TestMode();
	TestAlternate();
	TestBlends();
	TestOnRecordOtherModes();
	TestOnRecordSeeThrough();
	TestReadsThrough();
	TestKeepTests();
	TestSummary();
	return Failures();
}
