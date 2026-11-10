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

Sample Classified(DrawKind kind, RectKind rect, std::uint32_t drawPixels, std::uint32_t atDraw, std::uint32_t pass, float radius,
                  oc::Owner owner, const char* name, std::uint64_t ticks) {
	Sample s = Of(kind, rect, drawPixels, atDraw, true, atDraw, ticks);
	s.passId = pass;
	s.radius = radius;
	s.owner = owner;
	oc::CopyName(name, s.name);
	return s;
}

void TestOwner() {
	Section("Whose geometry it is: the player's third-person or first-person node among its ancestors, itself included");
	// 10 -> 20 -> 30 (third) -> 40 -> 0;  50 -> 60 (first) -> 0;  70 -> 80 -> 0
	const auto parentOf = [](std::uint32_t object) -> std::uint32_t {
		switch (object) {
			case 10: return 20;
			case 20: return 30;
			case 30: return 40;
			case 50: return 60;
			case 70: return 80;
			default: return 0;
		}
	};
	Check(oc::OwnerOf(10, 30, 60, parentOf) == oc::Owner::kPlayerThird, "two levels below the third-person node");
	Check(oc::OwnerOf(30, 30, 60, parentOf) == oc::Owner::kPlayerThird, "the node itself");
	Check(oc::OwnerOf(50, 30, 60, parentOf) == oc::Owner::kPlayerFirst, "below the first-person node");
	Check(oc::OwnerOf(70, 30, 60, parentOf) == oc::Owner::kOther, "neither");
	Check(oc::OwnerOf(40, 30, 60, parentOf) == oc::Owner::kOther, "above the third-person node is not below it");
	Check(oc::OwnerOf(0, 30, 60, parentOf) == oc::Owner::kOther, "no object");
	Check(oc::OwnerOf(10, 0, 0, parentOf) == oc::Owner::kOther, "no player nodes: nothing matches a null");
	std::uint32_t steps = 0;
	const auto loop = [&steps](std::uint32_t) -> std::uint32_t {
		++steps;
		return 5;
	};
	Check(oc::OwnerOf(5, 30, 60, loop) == oc::Owner::kOther && steps == oc::kMaxAncestors, "a cycle ends after the depth limit");
	Check(std::string(oc::OwnerName(oc::Owner::kOther)) == "others" && std::string(oc::OwnerName(oc::Owner::kPlayerThird)) == "player third person" &&
	          std::string(oc::OwnerName(oc::Owner::kPlayerFirst)) == "player first person" && std::string(oc::OwnerName(oc::Owner::kCount)) == "?",
	      "the owners' names");
}

void TestNames() {
	Section("A name is copied up to the row's width, made printable, and null reads as empty");
	char name[oc::kNameWidth];
	oc::CopyName("Tri UpperBody 0", name);
	Check(std::string(name) == "Tri UpperBody 0", "a short name");
	oc::CopyName("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop", name);
	Check(std::string(name) == "ABCDEFGHIJKLMNOPQRSTUVWXYZabcde" && std::strlen(name) == oc::kNameWidth - 1, "a long one is cut");
	oc::CopyName(nullptr, name);
	Check(name[0] == '\0', "null");
	oc::CopyName("a\tb\x01" "c\"d", name);
	Check(std::string(name) == "a?b?c'd", "control characters and the quote are replaced");
}

void TestClasses() {
	Section("A draw without a pixel falls into one class: why the probe's bound could or could not show it");
	Check(oc::ClassOf(Of(DrawKind::kShape, RectKind::kTestable, 0, 0, true, 0)) == oc::NoPixelClass::kBoundHidden, "the bound hidden at the draw");
	Check(oc::ClassOf(Of(DrawKind::kShape, RectKind::kTestable, 0, 7, true, 0)) == oc::NoPixelClass::kBoundSeen, "the bound seen at the draw");
	Check(oc::ClassOf(Of(DrawKind::kShape, RectKind::kCrossesNear, 0, 0, false, 0)) == oc::NoPixelClass::kNearPlane, "the bound at the near plane");
	Check(oc::ClassOf(Of(DrawKind::kShape, RectKind::kNoBound, 0, 0, false, 0)) == oc::NoPixelClass::kNoBound, "no bound");
	Check(oc::ClassOf(Of(DrawKind::kShape, RectKind::kOutsideFrustum, 0, 0, false, 0)) == oc::NoPixelClass::kOutsideFrustum, "outside the frustum");
	Check(oc::ClassOf(Of(DrawKind::kShape, RectKind::kViewport, 0, 0, false, 0)) == oc::NoPixelClass::kViewport, "no usable viewport");

	Section("The classes are counted per kind, with the draws' time, for draws without a pixel only");
	Stats stats;
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x101, 10.0f, oc::Owner::kOther, "a", 5));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 9, 0x101, 10.0f, oc::Owner::kOther, "a", 7));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 3, 9, 0x101, 10.0f, oc::Owner::kOther, "a", 100));
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 0, 0, 0x102, 10.0f, oc::Owner::kPlayerThird, "b", 11));
	const auto& shapeClasses = stats.classes[static_cast<int>(DrawKind::kShape)];
	Check(shapeClasses[static_cast<int>(oc::NoPixelClass::kBoundHidden)].draws == 1 &&
	          shapeClasses[static_cast<int>(oc::NoPixelClass::kBoundHidden)].ticks == 5,
	      "bound hidden");
	Check(shapeClasses[static_cast<int>(oc::NoPixelClass::kBoundSeen)].draws == 1 && shapeClasses[static_cast<int>(oc::NoPixelClass::kBoundSeen)].ticks == 7,
	      "bound seen; the draw with pixels is in no class");
	Check(stats.classes[static_cast<int>(DrawKind::kSkinned)][static_cast<int>(oc::NoPixelClass::kNearPlane)].draws == 1, "the skinned one at the near plane");
	char line[900];
	Check(oc::FormatClasses(stats, DrawKind::kShape, 1.0, line, sizeof(line)) > 0, "a line for the shapes");
	Check(std::string(line) ==
	          "Probe: occlusion no-pixel shapes - bound hidden 1 (5.000 ms), bound seen 1 (7.000 ms), near plane 0 (0.000 ms), no bound 0 (0.000 ms), "
	          "outside the frustum 0 (0.000 ms), viewport 0 (0.000 ms)",
	      "the classes' line");
	Check(oc::FormatClasses(stats, DrawKind::kShape, 0.0, line, sizeof(line)) > 0 &&
	          std::string(line).find("bound hidden 1 (5 ticks), bound seen 1 (7 ticks)") != std::string::npos,
	      "without a calibration the ticks");
	Check(oc::FormatClasses(stats, DrawKind::kDirectShape, 1.0, line, sizeof(line)) == 0, "no line for a kind without such a draw");
	Check(oc::FormatClasses(stats, DrawKind::kCount, 1.0, line, sizeof(line)) == 0, "a kind out of range writes nothing");
}

void TestPasses() {
	Section("The draws by the shader pass they ran under");
	Stats stats;
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x101, 10.0f, oc::Owner::kOther, "a", 5));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 9, 0x101, 10.0f, oc::Owner::kOther, "a", 7));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 3, 9, 0x101, 10.0f, oc::Owner::kOther, "a", 100));
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 0, 0, 0x102, 10.0f, oc::Owner::kPlayerThird, "b", 11));
	Check(stats.passes.used == 2, "two passes");
	const oc::PassRow* first = stats.passes.Find(0x101);
	Check(first != nullptr && first->tally.draws == 3 && first->tally.noPixel == 2 && first->tally.ticks == 112 && first->tally.ticksNoPixel == 12 &&
	          first->noPixelBoundSeen == 1 && first->hiddenAtEnd == 1,
	      "pass 0x101: three draws, two without a pixel, one of those with the bound seen, one bound hidden at the end");
	Check(stats.passes.Find(0x999) == nullptr, "an unseen pass has no row");
	char line[900];
	Check(oc::FormatPass(stats, 0, 1.0, line, sizeof(line)) > 0, "the first row");
	Check(std::string(line) ==
	          "Probe: occlusion pass 0x101 - 3 draws, 2 without a pixel (1 with the bound seen), 1 hidden at the end, time 112.000 ms, 12.000 ms "
	          "without a pixel",
	      "the pass line");
	Check(oc::FormatPass(stats, 2, 1.0, line, sizeof(line)) == 0, "no row past the used ones");

	Section("More passes than rows: the rest is counted in one row");
	Stats full;
	for (std::uint32_t pass = 0; pass < oc::kPassRows + 3; ++pass) {
		full.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x200 + pass, 10.0f, oc::Owner::kOther, "a", 1));
	}
	Check(full.passes.used == oc::kPassRows && full.passes.beyond.draws == 3 && full.passes.beyond.noPixel == 3, "three beyond the table");
	Check(oc::FormatPassesBeyond(full, 1.0, line, sizeof(line)) > 0 &&
	          std::string(line) == "Probe: occlusion passes beyond the table - 3 draws, 3 without a pixel, time 3.000 ms, 3.000 ms without a pixel",
	      "their line");
	Check(oc::FormatPassesBeyond(stats, 1.0, line, sizeof(line)) == 0, "no line when the table held all");
}

void TestRadius() {
	Section("The draws by the bound's radius");
	Check(oc::RadiusBucket(0.0f) == 0 && oc::RadiusBucket(63.9f) == 0 && oc::RadiusBucket(64.0f) == 1 && oc::RadiusBucket(255.0f) == 1 &&
	          oc::RadiusBucket(256.0f) == 2 && oc::RadiusBucket(1023.0f) == 2 && oc::RadiusBucket(1024.0f) == 3 && oc::RadiusBucket(4095.0f) == 3 &&
	          oc::RadiusBucket(4096.0f) == 4 && oc::RadiusBucket(1.0e9f) == 4,
	      "the buckets' edges");
	Check(oc::RadiusBucket(std::numeric_limits<float>::quiet_NaN()) == 0 && oc::RadiusBucket(-5.0f) == 0, "a radius that is none goes to the first");
	Stats stats;
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x101, 10.0f, oc::Owner::kOther, "a", 5));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 4, 9, 0x101, 5000.0f, oc::Owner::kOther, "a", 7));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 9, 0x101, 5000.0f, oc::Owner::kOther, "a", 9));
	char line[900];
	Check(oc::FormatRadius(stats, 1.0, line, sizeof(line)) > 0, "a line");
	Check(std::string(line) ==
	          "Probe: occlusion radius - below 64: 1 draws, 1 without a pixel (5.000 ms of 5.000 ms); below 256: 0 draws, 0 without a pixel (0.000 ms "
	          "of 0.000 ms); below 1024: 0 draws, 0 without a pixel (0.000 ms of 0.000 ms); below 4096: 0 draws, 0 without a pixel (0.000 ms of 0.000 "
	          "ms); 4096 and above: 2 draws, 1 without a pixel (9.000 ms of 16.000 ms)",
	      "the radius line");
	Stats none;
	Check(oc::FormatRadius(none, 1.0, line, sizeof(line)) == 0, "no line without a draw");
}

void TestOwners() {
	Section("The draws by kind and owner");
	Stats stats;
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 0, 0, 0x102, 10.0f, oc::Owner::kPlayerThird, "b", 11));
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 8, 0, 0x102, 10.0f, oc::Owner::kPlayerThird, "b", 20));
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kTestable, 0, 0, 0x102, 10.0f, oc::Owner::kOther, "c", 30));
	char line[900];
	Check(oc::FormatOwner(stats, DrawKind::kSkinned, oc::Owner::kPlayerThird, 1.0, line, sizeof(line)) > 0, "the player's skinned draws");
	Check(std::string(line) ==
	          "Probe: occlusion owner player third person, skinned shapes - 2 draws, 1 without a pixel, time 31.000 ms, 11.000 ms without a pixel",
	      "the owner line");
	Check(oc::FormatOwner(stats, DrawKind::kSkinned, oc::Owner::kOther, 1.0, line, sizeof(line)) > 0 &&
	          std::string(line).find("owner others, skinned shapes - 1 draws, 1 without a pixel") != std::string::npos,
	      "the others'");
	Check(oc::FormatOwner(stats, DrawKind::kSkinned, oc::Owner::kPlayerFirst, 1.0, line, sizeof(line)) == 0, "no line for an owner without a draw");
	Check(oc::FormatOwner(stats, DrawKind::kCount, oc::Owner::kOther, 1.0, line, sizeof(line)) == 0 &&
	          oc::FormatOwner(stats, DrawKind::kShape, oc::Owner::kCount, 1.0, line, sizeof(line)) == 0,
	      "out of range writes nothing");
	Stats bad;
	Sample odd = Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x101, 10.0f, oc::Owner::kCount, "a", 5);
	bad.Add(odd);
	Check(bad.owners[static_cast<int>(DrawKind::kShape)][static_cast<int>(oc::Owner::kOther)].draws == 1, "an owner out of range counts with the others");
}

void TestNameTable() {
	Section("The geometries' names: one row per name, kind and owner; the rows with the most time without a pixel first");
	Stats stats;
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 0, 0, 0x102, 10.0f, oc::Owner::kPlayerThird, "UpperBody", 11));
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 0, 0, 0x102, 10.0f, oc::Owner::kPlayerThird, "UpperBody", 12));
	stats.Add(Classified(DrawKind::kSkinned, RectKind::kCrossesNear, 0, 0, 0x102, 10.0f, oc::Owner::kOther, "UpperBody", 50));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x101, 10.0f, oc::Owner::kOther, "UpperBody", 1));
	stats.Add(Classified(DrawKind::kShape, RectKind::kTestable, 9, 9, 0x101, 10.0f, oc::Owner::kOther, "Wall", 500));
	Check(stats.names.used == 4, "four rows: the name alone is not the key");
	char line[900];
	Check(oc::FormatName(stats, 0, 1.0, line, sizeof(line)) > 0, "the first");
	Check(std::string(line) ==
	          "Probe: occlusion name \"UpperBody\" (skinned shapes, others) - 1 draws, 1 without a pixel, time 50.000 ms, 50.000 ms without a pixel",
	      "the row with the most time without a pixel");
	Check(oc::FormatName(stats, 1, 1.0, line, sizeof(line)) > 0 &&
	          std::string(line).find("\"UpperBody\" (skinned shapes, player third person) - 2 draws, 2 without a pixel, time 23.000 ms, 23.000 ms") !=
	              std::string::npos,
	      "the second");
	Check(oc::FormatName(stats, 2, 1.0, line, sizeof(line)) > 0 && std::string(line).find("\"UpperBody\" (shapes, others)") != std::string::npos, "the third");
	Check(oc::FormatName(stats, 3, 1.0, line, sizeof(line)) == 0, "a row that always drew pixels is not listed");
	Check(oc::FormatName(stats, 9, 1.0, line, sizeof(line)) == 0, "no rank past the rows");

	Section("More names than rows: the rest is counted");
	Stats full;
	for (std::uint32_t i = 0; i < oc::kNameRows + 2; ++i) {
		char name[oc::kNameWidth];
		std::snprintf(name, sizeof(name), "n%u", i);
		full.Add(Classified(DrawKind::kShape, RectKind::kTestable, 0, 0, 0x101, 10.0f, oc::Owner::kOther, name, 1));
	}
	Check(full.names.used == oc::kNameRows && full.names.beyond.draws == 2, "two beyond the table");
	Check(oc::FormatNamesBeyond(full, 1.0, line, sizeof(line)) > 0 &&
	          std::string(line) == "Probe: occlusion names beyond the table - 2 draws, 2 without a pixel, time 2.000 ms, 2.000 ms without a pixel",
	      "their line");
	Check(oc::FormatNamesBeyond(stats, 1.0, line, sizeof(line)) == 0, "no line when the table held all");
}

}  // namespace

void TestFloorCeil() {
	Section("FloorOf and CeilOf: the library's values");
	const float edges[] = {0.0f, -0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2567.999f, 4035.0001f, -3.0f, 8388607.5f, -8388607.5f, 16777216.0f,
	                       2147483000.0f, -2147483000.0f, 3.0e9f, -3.0e9f, 1.0e30f, std::numeric_limits<float>::infinity(),
	                       -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min()};
	bool same = true;
	for (float x : edges) {
		same = same && oc::FloorOf(x) == std::floor(x) && oc::CeilOf(x) == std::ceil(x);
	}
	Check(same, "the edges: zeros, halves, whole numbers, the range's ends, beyond it, the infinities, the smallest values");
	Check(std::isnan(oc::FloorOf(std::numeric_limits<float>::quiet_NaN())) && std::isnan(oc::CeilOf(std::numeric_limits<float>::quiet_NaN())), "not a number stays one");
	std::uint32_t state = 99u;
	int equal = 0;
	const int cases = 400000;
	for (int n = 0; n < cases; ++n) {
		state = state * 1664525u + 1013904223u;
		const float unit = static_cast<float>(state >> 8) / 16777216.0f - 0.5f;
		const float x = unit * ((n & 3) == 0 ? 4.0f : (n & 3) == 1 ? 20000.0f : (n & 3) == 2 ? 3.0e7f : 6.0e9f);
		equal += (oc::FloorOf(x) == std::floor(x) && oc::CeilOf(x) == std::ceil(x)) ? 1 : 0;
	}
	Check(equal == cases, "400 000 random values across four magnitudes");
}

// The fast path against the general one over random bounds, cameras and
// projections of the eye passes' kind: the same kind and the same rectangle.
void TestBoundRectPaths() {
	Section("BoundRect: the two-w path gives what the general path gives");
	std::uint32_t state = 12345u;
	const auto next = [&state]() {
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(state >> 8) / 16777216.0f;  // [0, 1)
	};
	const oc::Viewport ports[2] = {{0, 0, 2568, 2568, 0.0f, 1.0f}, {4036, 0, 4036, 3376, 0.0f, 1.0f}};
	int kinds = 0;
	int rects = 0;
	int testable = 0;
	int near = 0;
	int outside = 0;
	const int cases = 200000;
	for (int n = 0; n < cases; ++n) {
		// A camera looking along a random direction (an orthonormal view from two angles), a random origin.
		const float yaw = next() * 6.2831853f;
		const float pitch = (next() - 0.5f) * 2.0f;
		const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
		const float view[16] = {cy, -sy * sp, sy * cp, 0.0f, sy, cy * sp, -cy * cp, 0.0f, 0.0f, cp, sp, 0.0f, (next() - 0.5f) * 50.0f, (next() - 0.5f) * 50.0f, (next() - 0.5f) * 50.0f, 1.0f};
		// An off-centre perspective (D3D's row vectors): w = z, as every eye pass has it.
		const float nearZ = 5.0f + next() * 10.0f;
		const float farZ = 3000.0f + next() * 300000.0f;
		const float proj[16] = {0.6f + next(), 0.0f, 0.0f, 0.0f, 0.0f, 0.6f + next(), 0.0f, 0.0f, (next() - 0.5f) * 0.4f, (next() - 0.5f) * 0.4f, farZ / (farZ - nearZ), 1.0f, 0.0f, 0.0f, -nearZ * farZ / (farZ - nearZ), 0.0f};
		const float origin[3] = {(next() - 0.5f) * 400000.0f, (next() - 0.5f) * 400000.0f, (next() - 0.5f) * 2000.0f};
		const float reach = next() < 0.5f ? 600.0f : 20000.0f;
		oc::Bound bound{};
		bound.centre[0] = origin[0] + (next() - 0.5f) * reach;
		bound.centre[1] = origin[1] + (next() - 0.5f) * reach;
		bound.centre[2] = origin[2] + (next() - 0.5f) * reach;
		bound.radius = 1.0f + next() * (next() < 0.5f ? 60.0f : 1500.0f);
		const oc::Viewport& port = ports[n & 1];
		oc::ScreenRect a{};
		oc::ScreenRect b{};
		const RectKind ka = oc::BoundRectGeneral(bound, origin, view, proj, port, &a);
		const RectKind kb = oc::BoundRect(bound, origin, view, proj, port, &b);
		kinds += ka == kb ? 1 : 0;
		testable += ka == RectKind::kTestable ? 1 : 0;
		near += ka == RectKind::kCrossesNear ? 1 : 0;
		outside += ka == RectKind::kOutsideFrustum ? 1 : 0;
		if (ka == RectKind::kTestable && kb == RectKind::kTestable) {
			rects += (a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom && a.depth == b.depth && a.farDepth == b.farDepth) ? 1 : 0;
		}
	}
	Check(kinds == cases, "the same kind in every case");
	Check(rects == testable, "the same rectangle and depths in every testable case");
	Check(testable > 20000 && near > 20000 && outside > 2000, "the cases cover testable, near-crossing and outside bounds");
	// A projection whose w depends on x goes to the general path.
	const float skew[16] = {1.0f, 0.0f, 0.0f, 0.1f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, -5.0f, 0.0f};
	const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	oc::Bound far{};
	far.centre[2] = 100.0f;
	far.radius = 10.0f;
	oc::ScreenRect c{};
	oc::ScreenRect d{};
	Check(oc::BoundRect(far, zero, id, skew, ports[0], &c) == oc::BoundRectGeneral(far, zero, id, skew, ports[0], &d) && c.left == d.left && c.right == d.right,
	      "a w that depends on x: the general path's answer");
}

int main() {
	TestFloorCeil();
	TestBoundRectPaths();
	TestRect();
	TestNoRect();
	TestOrthographic();
	TestQuad();
	TestSameTarget();
	TestSampler();
	TestClassify();
	TestBlocked();
	TestFormat();
	TestOwner();
	TestNames();
	TestClasses();
	TestPasses();
	TestRadius();
	TestOwners();
	TestNameTable();
	return Failures();
}
