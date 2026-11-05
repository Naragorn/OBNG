// The occlusion probe's decisions (plugin/src/OcclusionProbeLogic.h): the
// screen rectangle of a world bound and when there is none, the quad's
// vertices, which passes are sampled, how one draw's three query results are
// classified, what blocks the probe, and the summary lines.

#include "Check.h"
#include "OcclusionProbeLogic.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace oc = obng::plugin::occlusion;
using oc::Bound;
using oc::DrawKind;
using oc::RectKind;
using oc::Sample;
using oc::ScreenRect;
using oc::Stats;
using oc::Viewport;

namespace {

// The view of 0x00762640 for a camera looking along +y with x to the right
// and z up: the axes are the matrix's columns (right, up, dir), the
// translation row is zero (camera-relative rendering).
void LookAlongY(float view[16]) {
	std::memset(view, 0, 16 * sizeof(float));
	view[0] = 1.0f;   // right = (1, 0, 0) in column 0
	view[9] = 1.0f;   // up = (0, 0, 1) in column 1
	view[6] = 1.0f;   // dir = (0, 1, 0) in column 2
	view[15] = 1.0f;
}

// The perspective projection of 0x00762640 C5 for a symmetric frustum with
// tangents 1 and 1, near 10, far 1000.
void Perspective(float proj[16]) {
	std::memset(proj, 0, 16 * sizeof(float));
	const float n = 10.0f;
	const float f = 1000.0f;
	proj[0] = 1.0f;                // 2 / (r - l) with r = 1, l = -1
	proj[5] = 1.0f;                // 2 / (t - b)
	proj[10] = f / (f - n);        // _33
	proj[11] = 1.0f;               // _34
	proj[14] = -(f * n) / (f - n);  // _43
}

const Viewport kPort{100, 50, 800, 600, 0.0f, 1.0f};
const float kOrigin[3] = {1000.0f, 2000.0f, 3000.0f};

Bound At(float x, float y, float z, float radius) {
	return Bound{{kOrigin[0] + x, kOrigin[1] + y, kOrigin[2] + z}, radius};
}

bool Near(float a, float b, float tolerance = 0.01f) {
	return std::fabs(a - b) <= tolerance;
}

void TestRect() {
	Section("A bound ahead of the camera gives the rectangle of its view-space box, at the box's nearest depth");
	float view[16];
	float proj[16];
	LookAlongY(view);
	Perspective(proj);
	ScreenRect rect{};
	// Centre 100 ahead, radius 10: the box spans x -10..10, z -10..10, depth 90..110.
	// The widest tangent is 10/90 at the near face: +-0.1111 of the half port.
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kTestable, "testable");
	// x: 100 + (0.5 +- 0.05556) * 800 = 455.56 .. 544.44, widened by a pixel after rounding outward.
	Check(Near(rect.left, 454.0f) && Near(rect.right, 546.0f), "the horizontal extent, rounded outward and widened by one pixel");
	// y: 50 + (0.5 -+ 0.05556) * 600 = 316.67 .. 383.33.
	Check(Near(rect.top, 315.0f) && Near(rect.bottom, 385.0f), "the vertical extent, y down");
	// depth at 90: f/(f-n) - f*n/((f-n)*90) = 1.010101 - 10.10101/90 = 0.897868.
	Check(Near(rect.depth, 0.897868f, 0.0001f), "the depth of the box's nearest face");

	Section("The origin is subtracted before the view: the same bound with another origin moves on the screen");
	const float otherOrigin[3] = {kOrigin[0] - 50.0f, kOrigin[1], kOrigin[2]};
	ScreenRect moved{};
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), otherOrigin, view, proj, kPort, &moved) == RectKind::kTestable, "testable");
	Check(moved.left > rect.left + 100.0f, "50 units to the right at depth 100 is a quarter of the port");

	Section("The view's translation row counts (the second eye's shifted view)");
	float shifted[16];
	LookAlongY(shifted);
	shifted[12] = -50.0f;  // _41 = -(right . T): the eye 50 units to the right
	ScreenRect left{};
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), kOrigin, shifted, proj, kPort, &left) == RectKind::kTestable, "testable");
	Check(left.right < rect.left, "the bound lies to the left of where the unshifted eye sees it");

	Section("The rectangle is clipped to the viewport");
	ScreenRect clipped{};
	// Centre 95 to the right at depth 100, radius 10: reaches past the right edge (tangent 1).
	Check(oc::BoundRect(At(95.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &clipped) == RectKind::kTestable, "testable");
	Check(Near(clipped.right, 900.0f) && clipped.left > 800.0f, "the right edge is the port's");
	ScreenRect top{};
	Check(oc::BoundRect(At(0.0f, 100.0f, 95.0f, 10.0f), kOrigin, view, proj, kPort, &top) == RectKind::kTestable, "testable");
	Check(Near(top.top, 50.0f) && top.bottom < 150.0f, "the top edge is the port's (up is the port's top)");
}

void TestNoRect() {
	float view[16];
	float proj[16];
	LookAlongY(view);
	Perspective(proj);
	ScreenRect rect{};

	Section("No bound: a radius of zero, a negative one, a value that is not finite");
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 0.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kNoBound, "radius 0");
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, -1.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kNoBound, "radius below 0");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, nan), kOrigin, view, proj, kPort, &rect) == RectKind::kNoBound, "radius NaN");
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, inf), kOrigin, view, proj, kPort, &rect) == RectKind::kNoBound, "radius infinite");
	Check(oc::BoundRect(At(nan, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kNoBound, "centre NaN");
	Check(oc::BoundRect(At(0.0f, inf, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kNoBound, "centre infinite");

	Section("A viewport the test cannot stand on: a depth range other than 0..1, no area");
	Viewport range = kPort;
	range.minZ = 0.1f;
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, range, &rect) == RectKind::kViewport, "minZ not 0");
	range = kPort;
	range.maxZ = 0.9f;
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, range, &rect) == RectKind::kViewport, "maxZ not 1");
	range = kPort;
	range.width = 0;
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, range, &rect) == RectKind::kViewport, "no width");
	range = kPort;
	range.height = 0;
	Check(oc::BoundRect(At(0.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, range, &rect) == RectKind::kViewport, "no height");

	Section("A bound that reaches the near plane or the camera cannot be tested");
	Check(oc::BoundRect(At(0.0f, 15.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kCrossesNear, "its nearest face before the near plane");
	Check(oc::BoundRect(At(0.0f, 5.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kCrossesNear, "around the camera");
	Check(oc::BoundRect(At(0.0f, -100.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kCrossesNear, "behind the camera");
	Check(oc::BoundRect(At(0.0f, 20.5f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kTestable, "just behind the near plane is testable");

	Section("A bound outside the frustum has no rectangle in the port");
	Check(oc::BoundRect(At(300.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kOutsideFrustum, "to the right");
	Check(oc::BoundRect(At(-300.0f, 100.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kOutsideFrustum, "to the left");
	Check(oc::BoundRect(At(0.0f, 100.0f, 300.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kOutsideFrustum, "above");
	Check(oc::BoundRect(At(0.0f, 100.0f, -300.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kOutsideFrustum, "below");
	Check(oc::BoundRect(At(0.0f, 2000.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kOutsideFrustum, "beyond the far plane");

	Section("A bound across the far plane is tested at its nearest face");
	Check(oc::BoundRect(At(0.0f, 1000.0f, 0.0f, 10.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kTestable, "testable");
	Check(rect.depth < 1.0f && rect.depth > 0.99f, "the depth of the face at 990");
}

void TestOrthographic() {
	Section("An orthographic projection (0x00762640 C6): w is 1, the extent does not shrink with depth");
	float view[16];
	LookAlongY(view);
	float proj[16] = {};
	proj[0] = 2.0f / 256.0f;   // r - l = 256
	proj[5] = 2.0f / 256.0f;   // t - b = 256
	proj[10] = 1.0f / 990.0f;  // 1 / (f - n)
	proj[14] = -(10.0f / 990.0f);
	proj[15] = 1.0f;
	ScreenRect rect{};
	Check(oc::BoundRect(At(0.0f, 500.0f, 0.0f, 16.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kTestable, "testable");
	// x: +-16 of +-128 is +-0.125 of the half port: 100 + (0.5 +- 0.0625) * 800 = 450 .. 550.
	Check(Near(rect.left, 449.0f) && Near(rect.right, 551.0f), "the extent of the box");
	Check(Near(rect.depth, (484.0f - 10.0f) / 990.0f, 0.0001f), "the depth of the nearest face");
	Check(oc::BoundRect(At(0.0f, 15.0f, 0.0f, 16.0f), kOrigin, view, proj, kPort, &rect) == RectKind::kCrossesNear, "before the near plane");
}

void TestQuad() {
	Section("The quad's vertices: a strip of four pre-transformed vertices, half a pixel up and left");
	const ScreenRect rect{10.0f, 20.0f, 30.0f, 50.0f, 0.25f};
	float v[16] = {};
	oc::QuadVertices(rect, v);
	Check(v[0] == 9.5f && v[1] == 19.5f && v[2] == 0.25f && v[3] == 1.0f, "left top");
	Check(v[4] == 29.5f && v[5] == 19.5f && v[6] == 0.25f && v[7] == 1.0f, "right top");
	Check(v[8] == 9.5f && v[9] == 49.5f && v[10] == 0.25f && v[11] == 1.0f, "left bottom");
	Check(v[12] == 29.5f && v[13] == 49.5f && v[14] == 0.25f && v[15] == 1.0f, "right bottom");
}

void TestSameTarget() {
	Section("The end test of a draw runs only on the depth buffer and the port the draw had");
	const oc::Target at{0x1000u, kPort};
	Check(oc::SameTarget(at, oc::Target{0x1000u, kPort}), "the same");
	Check(!oc::SameTarget(at, oc::Target{0x2000u, kPort}), "another depth buffer");
	Viewport other = kPort;
	other.x = 0;
	Check(!oc::SameTarget(at, oc::Target{0x1000u, other}), "another x");
	other = kPort;
	other.y = 0;
	Check(!oc::SameTarget(at, oc::Target{0x1000u, other}), "another y");
	other = kPort;
	other.width = 1;
	Check(!oc::SameTarget(at, oc::Target{0x1000u, other}), "another width");
	other = kPort;
	other.height = 1;
	Check(!oc::SameTarget(at, oc::Target{0x1000u, other}), "another height");
	Check(!oc::SameTarget(oc::Target{0u, kPort}, oc::Target{0u, kPort}), "no depth buffer is no target");
}

void TestSampler() {
	Section("Every Nth eye pass is sampled; none while a batch waits for its results");
	oc::Sampler s{3};
	Check(!s.Take(false) && !s.Take(false) && s.Take(false), "the third");
	Check(!s.Take(false) && !s.Take(false) && s.Take(false), "the sixth");
	Check(!s.Take(true) && !s.Take(true) && !s.Take(true) && !s.Take(true), "none while pending");
	Check(s.Take(false), "the first free pass after a due one");
	Check(!s.Take(false), "then the count starts again");
	oc::Sampler every{1};
	Check(every.Take(false) && every.Take(false), "every pass");
	oc::Sampler zero{0};
	Check(zero.Take(false) && zero.Take(false), "0 reads as every pass");

	Section("A batch whose results do not come is given up");
	Check(!oc::GiveUp(0) && !oc::GiveUp(oc::kPassesToWait), "within the wait");
	Check(oc::GiveUp(oc::kPassesToWait + 1), "after it");
}

Sample Of(DrawKind kind, RectKind rect, std::uint32_t drawPixels, std::uint32_t atDraw, bool endTested, std::uint32_t atEnd,
          std::uint64_t ticks = 100, std::uint32_t control = 1) {
	Sample s;
	s.kind = kind;
	s.rect = rect;
	s.drawPixels = drawPixels;
	s.boundAtDraw = atDraw;
	s.endTested = endTested;
	s.boundAtEnd = atEnd;
	s.controlAtEnd = control;
	s.ticks = ticks;
	return s;
}

void TestClassify() {
	Section("One draw's results: visible, hidden behind what was drawn before, hidden at the pass's end");
	Stats stats;
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 500, 800, true, 700, 10));
	const oc::KindStats& shape = stats.kinds[static_cast<int>(DrawKind::kShape)];
	Check(shape.draws == 1 && shape.testable == 1 && shape.endTested == 1, "counted, testable, tested at the end");
	Check(shape.drewNothing == 0 && shape.hiddenAtDraw == 0 && shape.hiddenAtEnd == 0 && shape.contradictions == 0, "visible everywhere");
	Check(shape.ticks == 10 && shape.ticksHiddenAtEnd == 0 && shape.ticksDrewNothing == 0, "its time is in the total only");

	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 0, 0, true, 0, 20));
	Check(shape.draws == 2 && shape.drewNothing == 1 && shape.hiddenAtDraw == 1 && shape.hiddenAtEnd == 1 && shape.hiddenAtEndDrewNothing == 1,
	      "hidden at the draw and at the end");
	Check(shape.ticks == 30 && shape.ticksHiddenAtEnd == 20 && shape.ticksDrewNothing == 20, "its time counts as hidden");

	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 300, 400, true, 0, 30));
	Check(shape.hiddenAtEnd == 2 && shape.hiddenAtEndDrewNothing == 1 && shape.hiddenAtDraw == 1 && shape.drewNothing == 1,
	      "drawn, then covered by a later draw: hidden at the end only");
	Check(shape.ticksHiddenAtEnd == 50, "its time counts as hidden at the end");

	Section("The validity gate: a bound hidden at the draw whose geometry drew pixels is a contradiction");
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 5, 0, true, 0));
	Check(shape.contradictions == 1 && shape.hiddenAtDraw == 2, "counted");
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 0, 9, true, 9));
	Check(shape.contradictions == 1 && shape.drewNothing == 2, "a visible bound whose geometry drew nothing is none (the bound is the larger)");

	Section("A draw whose target changed before the pass's end is not tested there");
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 5, 5, false, 0));
	Check(shape.testable == 6 && shape.endTested == 5 && shape.hiddenAtEnd == 3, "testable, untested at the end, not hidden");

	Section("The control: the end rectangle again at depth 0 must put pixels down, or the end test cannot be read");
	Check(shape.controlFailed == 0, "none so far");
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 5, 5, true, 0, 0, 0));
	Check(shape.controlFailed == 1 && shape.hiddenAtEnd == 3 && shape.endTested == 5 && shape.testable == 7,
	      "a failed control is counted and its end result left out");
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 5, 5, false, 0, 0, 0));
	Check(shape.controlFailed == 1, "a draw that was not tested at the end has no control");

	Section("Draws without a rectangle are counted by the reason and by their pixels only");
	stats.Add(Of(DrawKind::kSkinned, RectKind::kNoBound, 0, 0, false, 0, 7));
	stats.Add(Of(DrawKind::kSkinned, RectKind::kCrossesNear, 9, 0, false, 0, 7));
	stats.Add(Of(DrawKind::kSkinned, RectKind::kOutsideFrustum, 0, 0, false, 0, 7));
	stats.Add(Of(DrawKind::kSkinned, RectKind::kViewport, 9, 0, false, 0, 7));
	const oc::KindStats& skinned = stats.kinds[static_cast<int>(DrawKind::kSkinned)];
	Check(skinned.draws == 4 && skinned.testable == 0 && skinned.noBound == 1 && skinned.crossesNear == 1 && skinned.outsideFrustum == 1 &&
	          skinned.viewport == 1,
	      "one of each");
	Check(skinned.drewNothing == 2 && skinned.hiddenAtDraw == 0 && skinned.hiddenAtEnd == 0 && skinned.contradictions == 0,
	      "their pixels count, no bound result does");
	Check(skinned.ticks == 28 && skinned.ticksDrewNothing == 14 && skinned.ticksHiddenAtEnd == 0, "their time");

	Section("The kinds are kept apart; a kind out of range is dropped");
	stats.Add(Of(DrawKind::kDirectShape, RectKind::kTestable, 1, 1, true, 1));
	stats.Add(Of(DrawKind::kDirectStrips, RectKind::kTestable, 1, 1, true, 0));
	Check(stats.kinds[static_cast<int>(DrawKind::kDirectShape)].draws == 1 && stats.kinds[static_cast<int>(DrawKind::kDirectStrips)].hiddenAtEnd == 1,
	      "the direct entries");
	stats.Add(Of(DrawKind::kCount, RectKind::kTestable, 1, 1, true, 0));
	Check(shape.draws == 8 && skinned.draws == 4, "nothing added");

	Section("Reset clears everything");
	stats.passesSampled = 3;
	stats.Reset();
	Check(stats.passesSampled == 0 && shape.draws == 0 && skinned.ticks == 0, "zero");
}

void TestBlocked() {
	Section("The probe shares entries with other probes and the stereo route: it runs only without them");
	Check(oc::BlockedBy(false, false, false, false) == nullptr, "alone: allowed");
	Check(std::string(oc::BlockedBy(true, false, false, false)) == "[Probe] PassSteps", "the pass steps hold the finish and Do_RenderShape");
	Check(std::string(oc::BlockedBy(false, true, false, false)) == "[Probe] DrawEntries", "the draw entries hold slots 90 and 91");
	Check(std::string(oc::BlockedBy(false, false, true, false)) == "[Probe] ConstantUploads", "the uploads hold the finish");
	Check(std::string(oc::BlockedBy(false, false, false, true)) == "[Route] StereoDraw", "the route holds slots 90 and 91");
	Check(std::string(oc::BlockedBy(true, true, true, true)) == "[Probe] PassSteps", "the first one named");
}

void TestFormat() {
	Section("The summary: a head line and one line per kind that drew");
	Stats stats;
	stats.passesSampled = 2;
	stats.drawsUnsampled = 5000;
	stats.drawsBeyondCapacity = 3;
	stats.drawsInInnerPasses = 4;
	stats.batchesLost = 1;
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 500, 800, true, 700, 2000));
	stats.Add(Of(DrawKind::kShape, RectKind::kTestable, 0, 0, true, 0, 1000));
	stats.Add(Of(DrawKind::kShape, RectKind::kCrossesNear, 9, 0, false, 0, 1000));
	char line[700];
	const int written = oc::FormatHead(stats, 31, 123456789ull, line, sizeof(line));
	Check(written > 0 && static_cast<std::size_t>(written) < sizeof(line), "fits");
	Check(std::string(line) ==
	          "Probe: occlusion - 2 eye passes sampled (1 in 31), 3 draws in them, 3 beyond the 4096 a pass holds, 4 in passes inside "
	          "them, 1 batches lost, 5000 draws unsampled; qpc 123456789",
	      "the head line");
	Check(oc::FormatKind(stats, DrawKind::kShape, 1000.0, line, sizeof(line)) > 0, "the shapes drew");
	Check(std::string(line) ==
	          "Probe: occlusion shapes - 3 draws: 2 testable (near plane 1, outside the frustum 0, no bound 0, viewport 0); no pixel drawn 1; "
	          "bound hidden at the draw 1, at the pass's end 1 of 2 tested (1 of them drew no pixel); contradictions 0, failed controls 0; draw time 4.000 ms, "
	          "1.000 ms hidden at the end, 1.000 ms without a pixel",
	      "the kind's line with the calibration");
	Check(oc::FormatKind(stats, DrawKind::kShape, 0.0, line, sizeof(line)) > 0, "without a calibration");
	Check(std::string(line).find("draw time 4000 ticks, 1000 ticks hidden at the end, 1000 ticks without a pixel") != std::string::npos,
	      "the ticks as they are");
	Check(oc::FormatKind(stats, DrawKind::kSkinned, 1000.0, line, sizeof(line)) == 0, "a kind without a draw writes nothing");
	Check(oc::FormatKind(stats, DrawKind::kCount, 1000.0, line, sizeof(line)) == 0, "a kind out of range writes nothing");
	Check(std::string(oc::KindName(DrawKind::kShape)) == "shapes" && std::string(oc::KindName(DrawKind::kSkinned)) == "skinned shapes" &&
	          std::string(oc::KindName(DrawKind::kDirectShape)) == "direct shapes" &&
	          std::string(oc::KindName(DrawKind::kDirectStrips)) == "direct strips" && std::string(oc::KindName(DrawKind::kCount)) == "?",
	      "the kinds' names");
}

}  // namespace

int main() {
	TestRect();
	TestNoRect();
	TestOrthographic();
	TestQuad();
	TestSameTarget();
	TestSampler();
	TestClassify();
	TestBlocked();
	TestFormat();
	return Failures();
}
