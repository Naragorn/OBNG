// The occlusion culler's decisions (plugin/src/OcclusionCullLogic.h): the
// settings' bounds and what blocks the culler, the second eye's view, both
// eyes' rectangles and their triangles, the camera gate, the verdict table,
// the decision per draw, the batches in flight and the reading of their
// results, and the summary line.

#include "Check.h"
#include "OcclusionCullLogic.h"

#include <cmath>
#include <cstring>
#include <string>

namespace cull = obng::plugin::occlusion_cull;
namespace oc = obng::plugin::occlusion;
using cull::Batch;
using cull::Camera;
using cull::Cameras;
using cull::Decided;
using cull::Decision;
using cull::DrawKind;
using cull::EyeRects;
using cull::Read;
using cull::Ring;
using cull::Settings;
using cull::Stats;
using cull::Table;
using cull::TestKind;
using cull::Verdict;
using oc::Bound;
using oc::RectKind;
using oc::Viewport;

namespace {

// The view of 0x00762640 for a camera looking along +y with x to the right
// and z up (the probe test's), the translation row zero.
void LookAlongY(float view[16]) {
	std::memset(view, 0, 16 * sizeof(float));
	view[0] = 1.0f;
	view[9] = 1.0f;
	view[6] = 1.0f;
	view[15] = 1.0f;
}

// A symmetric frustum with tangents 1, near 10, far 1000.
void Perspective(float proj[16]) {
	std::memset(proj, 0, 16 * sizeof(float));
	const float n = 10.0f;
	const float f = 1000.0f;
	proj[0] = 1.0f;
	proj[5] = 1.0f;
	proj[10] = f / (f - n);
	proj[11] = 1.0f;
	proj[14] = -(f * n) / (f - n);
}

const Viewport kPort0{0, 0, 800, 600, 0.0f, 1.0f};
const Viewport kPort1{800, 0, 800, 600, 0.0f, 1.0f};
const float kOrigin[3] = {1000.0f, 2000.0f, 3000.0f};

Bound At(float x, float y, float z, float radius) {
	return Bound{{kOrigin[0] + x, kOrigin[1] + y, kOrigin[2] + z}, radius};
}

bool Near(float a, float b, float tolerance = 0.01f) {
	return std::fabs(a - b) <= tolerance;
}

struct Scene {
	float view0[16];
	float view1[16];
	float proj[16];
	Viewport ports[2];
	Scene() {
		LookAlongY(view0);
		const float offset[3] = {4.5f, 0.0f, 0.0f};  // eye 1 to the right
		cull::SecondEyeView(view0, offset, view1);
		Perspective(proj);
		ports[0] = kPort0;
		ports[1] = kPort1;
	}
	TestKind Rects(const Bound& b, float margin, EyeRects* out, RectKind kinds[2]) const {
		return cull::RectsFor(b, kOrigin, view0, view1, proj, ports, margin, out, kinds);
	}
};

void TestSettings() {
	Section("The INI values are bounded");
	Check(cull::MaxAgeFromIni(0) == 1 && cull::MaxAgeFromIni(-3) == 1, "max age below one reads as one");
	Check(cull::MaxAgeFromIni(2) == 2 && cull::MaxAgeFromIni(8) == 8, "one to eight as given");
	Check(cull::MaxAgeFromIni(9) == 8 && cull::MaxAgeFromIni(1000) == 8, "above eight capped");
	Check(cull::MarginFromIni(-1) == 0 && cull::MarginFromIni(0) == 0, "a negative margin reads as none");
	Check(cull::MarginFromIni(2) == 2 && cull::MarginFromIni(64) == 64 && cull::MarginFromIni(65) == 64, "margins to 64 pixels");

	Section("What blocks the culler, in order");
	Check(std::string(cull::BlockedBy(false, false, false, false)).find("StereoDraw") != std::string::npos, "without the route: no eye data");
	Check(std::string(cull::BlockedBy(false, true, true, true)).find("StereoDraw") != std::string::npos, "the route's absence comes first");
	Check(std::string(cull::BlockedBy(true, true, false, false)) == "[Probe] PassSteps", "the pass steps hold the finish and Do_RenderShape");
	Check(std::string(cull::BlockedBy(true, false, true, false)) == "[Probe] ConstantUploads", "the uploads probe holds the finish");
	Check(std::string(cull::BlockedBy(true, false, false, true)) == "[Probe] Occlusion", "the probe holds both entries");
	Check(cull::BlockedBy(true, false, false, false) == nullptr, "the route on, no probe: the culler may run");
}

void TestSecondEyeView() {
	Section("Eye 1's view is eye 0's with the translation row moved along the axes");
	float view0[16];
	LookAlongY(view0);
	view0[12] = 1.0f;
	view0[13] = 2.0f;
	view0[14] = 3.0f;
	float view1[16];
	const float offset[3] = {4.5f, 0.0f, 0.0f};
	cull::SecondEyeView(view0, offset, view1);
	Check(Near(view1[12], 1.0f - 4.5f) && Near(view1[13], 2.0f) && Near(view1[14], 3.0f), "the right axis is x: _41 moves by the offset");
	bool rest = true;
	for (int i = 0; i < 12; ++i) {
		rest = rest && view1[i] == view0[i];
	}
	Check(rest && view1[15] == view0[15], "the axes and _44 unchanged");
	const float up[3] = {0.0f, 0.0f, 2.0f};
	cull::SecondEyeView(view0, up, view1);
	Check(Near(view1[12], 1.0f) && Near(view1[13], 2.0f - 2.0f) && Near(view1[14], 3.0f), "an offset along the up axis moves _42");
}

void TestRects() {
	Section("A rectangle grows by the margin and stays inside the port");
	oc::ScreenRect r{100.0f, 100.0f, 200.0f, 200.0f, 0.5f};
	cull::Inflate(&r, 4.0f, kPort0);
	Check(r.left == 96.0f && r.top == 96.0f && r.right == 204.0f && r.bottom == 204.0f && r.depth == 0.5f, "four pixels on every side, depth kept");
	oc::ScreenRect edge{1.0f, 598.0f, 799.0f, 600.0f, 0.5f};
	cull::Inflate(&edge, 4.0f, kPort0);
	Check(edge.left == 0.0f && edge.top == 594.0f && edge.right == 800.0f && edge.bottom == 600.0f, "clamped to the port");

	Section("Both eyes' rectangles of a bound");
	const Scene scene;
	EyeRects rects;
	RectKind kinds[2];
	Check(scene.Rects(At(0.0f, 100.0f, 0.0f, 10.0f), 0.0f, &rects, kinds) == TestKind::kTest && rects.has[0] && rects.has[1],
	      "a bound ahead of both eyes: two rectangles");
	Check(kinds[0] == RectKind::kTestable && kinds[1] == RectKind::kTestable, "both testable");
	Check(rects.rect[0].left >= 0.0f && rects.rect[0].right <= 800.0f && rects.rect[1].left >= 800.0f && rects.rect[1].right <= 1600.0f,
	      "each rectangle lies in its eye's port");
	Check(rects.rect[1].left < rects.rect[0].left + 800.0f, "eye 1, to the right of eye 0, sees the bound further left in its port");
	EyeRects plain;
	RectKind plainKinds[2];
	scene.Rects(At(0.0f, 100.0f, 0.0f, 10.0f), 0.0f, &plain, plainKinds);
	EyeRects wide;
	RectKind wideKinds[2];
	scene.Rects(At(0.0f, 100.0f, 0.0f, 10.0f), 3.0f, &wide, wideKinds);
	Check(wide.rect[0].left == plain.rect[0].left - 3.0f && wide.rect[0].right == plain.rect[0].right + 3.0f &&
	          wide.rect[1].top == plain.rect[1].top - 3.0f && wide.rect[1].bottom == plain.rect[1].bottom + 3.0f,
	      "the margin widens both");
	// A bound at the left edge: eye 0 (x -100..-98 over y 99..101) reaches into its port, eye 1, 4.5 units to the
	// right (x -104.5..-102.5), does not: every corner's x/y is below -1.
	Check(scene.Rects(At(-99.0f, 100.0f, 0.0f, 1.0f), 0.0f, &rects, kinds) == TestKind::kTest && rects.has[0] && !rects.has[1] &&
	          kinds[1] == RectKind::kOutsideFrustum,
	      "a bound at the left edge: eye 0's rectangle only, eye 1 outside");
	Check(scene.Rects(At(-500.0f, 100.0f, 0.0f, 1.0f), 0.0f, &rects, kinds) == TestKind::kBothOutside && !rects.has[0] && !rects.has[1],
	      "a bound beside both frustums: nothing to draw");
	Check(scene.Rects(At(0.0f, 5.0f, 0.0f, 10.0f), 0.0f, &rects, kinds) == TestKind::kNone && kinds[0] == RectKind::kCrossesNear,
	      "a bound around the camera: no rectangle encloses it");
	Check(scene.Rects(At(0.0f, 100.0f, 0.0f, 0.0f), 0.0f, &rects, kinds) == TestKind::kNone && kinds[0] == RectKind::kNoBound, "no bound: none");
	Scene badPort;
	badPort.ports[1] = Viewport{800, 0, 0, 600, 0.0f, 1.0f};
	Check(badPort.Rects(At(0.0f, 100.0f, 0.0f, 10.0f), 0.0f, &rects, kinds) == TestKind::kNone && kinds[1] == RectKind::kViewport,
	      "an eye port without area: none, whatever the other eye");

	Section("The rectangles as triangles");
	scene.Rects(At(0.0f, 100.0f, 0.0f, 10.0f), 0.0f, &rects, kinds);
	float vertices[cull::kMaxRectVertices * 4];
	Check(cull::RectTriangles(rects, vertices) == 12, "two rectangles: twelve vertices");
	float quad[16];
	oc::QuadVertices(rects.rect[0], quad);
	Check(std::memcmp(vertices, quad, 4 * sizeof(float)) == 0 && std::memcmp(vertices + 4, quad + 4, 4 * sizeof(float)) == 0 &&
	          std::memcmp(vertices + 8, quad + 8, 4 * sizeof(float)) == 0,
	      "the first triangle is the quad's corners 0, 1, 2");
	Check(std::memcmp(vertices + 12, quad + 8, 4 * sizeof(float)) == 0 && std::memcmp(vertices + 16, quad + 4, 4 * sizeof(float)) == 0 &&
	          std::memcmp(vertices + 20, quad + 12, 4 * sizeof(float)) == 0,
	      "the second is 2, 1, 3");
	oc::QuadVertices(rects.rect[1], quad);
	Check(std::memcmp(vertices + 24, quad, 4 * sizeof(float)) == 0, "then eye 1's rectangle");
	rects.has[0] = false;
	Check(cull::RectTriangles(rects, vertices) == 6 && std::memcmp(vertices, quad, 4 * sizeof(float)) == 0, "one rectangle: six vertices, eye 1's");
}

void TestCameraGate() {
	Section("The camera of a pass and the gate");
	float view[16];
	LookAlongY(view);
	const float inverseRow[3] = {10.0f, 20.0f, 30.0f};
	const Camera c = cull::CameraOf(view, inverseRow);
	Check(c.position[0] == 10.0f && c.position[1] == 20.0f && c.position[2] == 30.0f, "the position is the inverse view's row 4");
	Check(c.direction[0] == 0.0f && c.direction[1] == 1.0f && c.direction[2] == 0.0f, "the direction is the view's column 2");
	Camera moved = c;
	moved.position[0] += 60.0f;
	moved.position[2] += 80.0f;
	Check(cull::MotionAllows(moved, c, 100.0f, cull::CosOfDegrees(10.0f)), "moved 100 units: allowed at a 100-unit gate");
	moved.position[0] += 1.0f;
	Check(!cull::MotionAllows(moved, c, 100.0f, cull::CosOfDegrees(10.0f)), "a hair further: refused");
	Camera turned = c;
	turned.direction[0] = std::sin(9.0f * 3.14159265f / 180.0f);
	turned.direction[1] = std::cos(9.0f * 3.14159265f / 180.0f);
	Check(cull::MotionAllows(turned, c, 100.0f, cull::CosOfDegrees(10.0f)), "turned 9 degrees: allowed at a 10-degree gate");
	turned.direction[0] = std::sin(11.0f * 3.14159265f / 180.0f);
	turned.direction[1] = std::cos(11.0f * 3.14159265f / 180.0f);
	Check(!cull::MotionAllows(turned, c, 100.0f, cull::CosOfDegrees(10.0f)), "turned 11 degrees: refused");

	Section("The camera ring keeps the last frames' cameras by frame number");
	Cameras cameras;
	Camera a;
	a.position[0] = 1.0f;
	Camera b;
	b.position[0] = 2.0f;
	cameras.Set(5, a);
	cameras.Set(5 + cull::kCameraRing, b);
	Check(cameras.At(5).position[0] == 2.0f, "a frame a ring later takes the slot");
	cameras.Set(6, a);
	Check(cameras.At(6).position[0] == 1.0f && cameras.At(5).position[0] == 2.0f, "neighbours keep their own");
}

void TestTable() {
	Section("The verdict table claims, finds and records");
	Table table;
	Verdict* v = table.Claim(0x1000, 1);
	Check(v != nullptr && v->geometry == 0x1000 && v->seenFrame == 0 && v->hiddenFrame == 0, "a new geometry gets an empty slot");
	v->seenFrame = 1;
	Check(table.Claim(0x1000, 2) == v, "the same geometry finds its slot");
	Check(table.Find(0x1000) == v && table.Find(0x2000) == nullptr, "find without claiming");
	table.Record(0x2000, 1, true);
	Check(table.Find(0x2000) == nullptr, "a result for an unknown geometry claims nothing");
	table.Record(0x1000, 3, true);
	Check(v->hiddenFrame == 3, "a hidden result sets the verdict's frame");
	table.Record(0x1000, 2, false);
	Check(v->hiddenFrame == 3, "an older visible result does not clear a newer hidden one");
	table.Record(0x1000, 3, false);
	Check(v->hiddenFrame == 0, "a visible result of the same frame clears it");
	table.Record(0x1000, 4, true);
	table.Record(0x1000, 2, true);
	Check(v->hiddenFrame == 4, "an older hidden result does not move the frame back");

	Section("Collisions: the probe length, then a stale slot");
	Table crowded;
	const std::uint32_t home = Table::Home(0x3000);
	// Fill the whole probe run with geometries that never leave.
	for (std::uint32_t i = 0; i < cull::kProbeLength; ++i) {
		Verdict& slot = crowded.slots[(home + i) & (cull::kTableSlots - 1)];
		slot.geometry = 0x80000000u + i;
		slot.seenFrame = 100;
	}
	Check(crowded.Claim(0x3000, 100) == nullptr, "no free slot within the probe length: null");
	crowded.slots[(home + 7) & (cull::kTableSlots - 1)].seenFrame = 100 - cull::kStaleFrames - 1;
	Verdict* taken = crowded.Claim(0x3000, 100);
	Check(taken == &crowded.slots[(home + 7) & (cull::kTableSlots - 1)] && taken->geometry == 0x3000 && taken->hiddenFrame == 0,
	      "a slot not asked about for long is taken and cleared");
	Check(crowded.Find(0x80000007u) == nullptr, "the evicted geometry is gone");
}

void TestDecide() {
	Section("The decision for one draw");
	Settings settings;
	settings.maxAge = 2;
	Cameras cameras;
	Camera still;
	still.direction[1] = 1.0f;
	for (std::uint32_t f = 0; f < cull::kCameraRing; ++f) {
		cameras.Set(f, still);
	}
	Stats stats;
	Table table;
	Decided d = cull::Decide(table, 0x1000, 1, TestKind::kTest, settings, cameras, &stats);
	Check(d.decision == Decision::kDraw && d.newThisFrame && d.verdict != nullptr && d.verdict->seenFrame == 1, "no verdict yet: drawn, new this frame");
	Decided again = cull::Decide(table, 0x1000, 1, TestKind::kTest, settings, cameras, &stats);
	Check(again.decision == Decision::kDraw && !again.newThisFrame && again.verdict == d.verdict, "the same frame again: the kept decision, not new");
	table.Record(0x1000, 1, true);
	Check(cull::Decide(table, 0x1000, 2, TestKind::kTest, settings, cameras, &stats).decision == Decision::kSkip, "hidden one frame ago: skipped");
	Check(cull::Decide(table, 0x1000, 3, TestKind::kTest, settings, cameras, &stats).decision == Decision::kSkip, "two frames: still within max age");
	Check(cull::Decide(table, 0x1000, 4, TestKind::kTest, settings, cameras, &stats).decision == Decision::kDraw, "three frames: too old, drawn");
	Check(cull::Decide(table, 0x1000, 4, TestKind::kTest, settings, cameras, &stats).decision == Decision::kDraw && d.verdict->decision == Decision::kDraw,
	      "the frame's decision is kept");
	table.Record(0x1000, 4, true);
	Check(cull::Decide(table, 0x1000, 5, TestKind::kNone, settings, cameras, &stats).decision == Decision::kDraw,
	      "a hidden verdict but no rectangle this frame: drawn");
	Check(cull::Decide(table, 0x1000, 6, TestKind::kBothOutside, settings, cameras, &stats).decision == Decision::kSkipFrustum,
	      "outside both frustums: skipped without a verdict");
	Check(cull::Decide(table, 0x5000, 6, TestKind::kBothOutside, settings, cameras, &stats).decision == Decision::kSkipFrustum,
	      "also for a geometry never seen");
	Camera moved = still;
	moved.position[0] = 500.0f;
	cameras.Set(7, moved);
	table.Record(0x1000, 6, true);
	Check(cull::Decide(table, 0x1000, 7, TestKind::kTest, settings, cameras, &stats).decision == Decision::kDraw && stats.motionBlocked == 1,
	      "hidden, but the camera moved too far since: drawn and counted");
	Table full;
	const std::uint32_t home = Table::Home(0x7000);
	for (std::uint32_t i = 0; i < cull::kProbeLength; ++i) {
		full.slots[(home + i) & (cull::kTableSlots - 1)].geometry = 0x90000000u + i;
		full.slots[(home + i) & (cull::kTableSlots - 1)].seenFrame = 1;
	}
	Decided none = cull::Decide(full, 0x7000, 1, TestKind::kTest, settings, cameras, &stats);
	Check(none.decision == Decision::kDraw && !none.newThisFrame && none.verdict == nullptr && stats.tableFull == 1, "no slot: drawn, counted");

	Section("The counts by kind");
	Stats counts;
	const RectKind testable[2] = {RectKind::kTestable, RectKind::kTestable};
	const RectKind near[2] = {RectKind::kTestable, RectKind::kCrossesNear};
	const RectKind noBound[2] = {RectKind::kNoBound, RectKind::kNoBound};
	const RectKind port[2] = {RectKind::kViewport, RectKind::kViewport};
	cull::Count(&counts, DrawKind::kShape, Decision::kDraw, TestKind::kTest, true, testable);
	cull::Count(&counts, DrawKind::kShape, Decision::kDraw, TestKind::kTest, false, testable);
	cull::Count(&counts, DrawKind::kSkinned, Decision::kSkip, TestKind::kTest, true, testable);
	cull::Count(&counts, DrawKind::kDirectShape, Decision::kSkipFrustum, TestKind::kBothOutside, true, testable);
	cull::Count(&counts, DrawKind::kDirectStrips, Decision::kDraw, TestKind::kNone, true, near);
	cull::Count(&counts, DrawKind::kDirectStrips, Decision::kDraw, TestKind::kNone, true, noBound);
	cull::Count(&counts, DrawKind::kDirectStrips, Decision::kDraw, TestKind::kNone, true, port);
	cull::Count(&counts, DrawKind::kDirectStrips, Decision::kDraw, TestKind::kNone, false, port);
	const cull::KindCounts& shapes = counts.kinds[static_cast<int>(DrawKind::kShape)];
	Check(shapes.draws == 2 && shapes.repeats == 1 && shapes.skipped == 0, "shapes: two draws, one a repeat");
	Check(counts.kinds[static_cast<int>(DrawKind::kSkinned)].skipped == 1, "a skinned skip");
	Check(counts.kinds[static_cast<int>(DrawKind::kDirectShape)].skippedFrustum == 1, "a frustum skip");
	const cull::KindCounts& strips = counts.kinds[static_cast<int>(DrawKind::kDirectStrips)];
	Check(strips.untestable == 3 && counts.untestableNear == 1 && counts.untestableNoBound == 1 && counts.untestableViewport == 1,
	      "untestable once per new geometry, by its first reason");
	Check(counts.Draws() == 8 && counts.Skipped() == 2, "the totals");
}

void TestRingAndCollect() {
	Section("The batches in flight");
	Ring ring;
	bool lost = false;
	Batch& b1 = ring.Begin(1, &lost);
	Check(!lost && b1.frame == 1 && b1.count == 0 && !b1.pending, "a fresh batch");
	b1.count = 2;
	b1.pending = true;
	Batch& b2 = ring.Begin(2, &lost);
	Check(!lost && &b2 != &b1, "the next frame takes another batch");
	b2.count = 1;
	b2.pending = true;
	Check(ring.OldestPending() == &b1, "the oldest pending batch first");
	Batch& wrapped = ring.Begin(1 + cull::kBatches, &lost);
	Check(lost && &wrapped == &b1 && wrapped.count == 0 && !wrapped.pending, "a ring later the same slot: the pending batch is lost");

	Section("Reading the results, oldest first, stopping at the first not ready");
	Ring r;
	Table table;
	Stats stats;
	Batch& a = r.Begin(1, &lost);
	a.tests[0].geometry = 0x1000;
	a.tests[1].geometry = 0x2000;
	a.tests[1].check = true;
	a.count = 2;
	a.pending = true;
	Batch& b = r.Begin(2, &lost);
	b.tests[0].geometry = 0x3000;
	b.count = 1;
	b.pending = true;
	table.Claim(0x1000, 1);
	table.Claim(0x2000, 1);
	table.Claim(0x3000, 2);
	int reads = 0;
	cull::Collect(&r, &table, &stats, [&](const Batch& batch, std::uint32_t test, std::uint32_t* pixels, std::uint32_t* checkPixels) {
		++reads;
		if (batch.frame == 1 && test == 0) {
			*pixels = 0;
			return Read::kDone;
		}
		if (batch.frame == 1 && test == 1) {
			*pixels = 40;
			*checkPixels = 7;
			return Read::kDone;
		}
		return Read::kNotYet;
	});
	Check(reads == 3 && a.cursor == 2 && !a.pending && b.pending && b.cursor == 0, "batch 1 read whole, batch 2 not ready yet");
	Check(table.Find(0x1000)->hiddenFrame == 1 && table.Find(0x2000)->hiddenFrame == 0, "the results went to the table");
	Check(stats.resultsHidden == 1 && stats.resultsVisible == 1 && stats.checkDrawn == 1 && stats.checkWithPixels == 1 && stats.checkPixels == 7,
	      "the counts, the check's pixels included");
	cull::Collect(&r, &table, &stats, [&](const Batch&, std::uint32_t, std::uint32_t*, std::uint32_t*) { return Read::kFailed; });
	Check(!b.pending && stats.batchesLost == 1 && table.Find(0x3000)->hiddenFrame == 0, "a failed read drops the batch");
	cull::Collect(&r, &table, &stats, [&](const Batch&, std::uint32_t, std::uint32_t*, std::uint32_t*) { return Read::kDone; });
	Check(stats.resultsHidden == 1 && stats.resultsVisible == 1, "nothing pending: nothing read");
}

void TestSummary() {
	Section("The summary line");
	Stats s;
	s.eyePasses = 10;
	s.passesWithoutEyes = 1;
	s.kinds[static_cast<int>(DrawKind::kShape)].draws = 1000;
	s.kinds[static_cast<int>(DrawKind::kShape)].skipped = 300;
	s.kinds[static_cast<int>(DrawKind::kShape)].skippedFrustum = 100;
	s.kinds[static_cast<int>(DrawKind::kShape)].repeats = 50;
	s.kinds[static_cast<int>(DrawKind::kSkinned)].draws = 200;
	s.kinds[static_cast<int>(DrawKind::kSkinned)].untestable = 20;
	s.untestableNear = 20;
	s.testsIssued = 600;
	s.resultsHidden = 400;
	s.resultsVisible = 150;
	s.batchesLost = 2;
	s.motionBlocked = 5;
	s.decideTicks = 20000;
	s.endTestsTicks = 30000;
	s.collectTicks = 10000;
	s.passTicks = 400000;
	Settings settings;
	char line[1200];
	Check(cull::FormatSummary(s, settings, 1000.0, 777, line, sizeof(line)) > 0, "written");
	std::string text(line);
	Check(text.find("Route: occlusion cull - 10 eye passes (1 without eye data)") == 0, "the head");
	Check(text.find("per pass 120 draws, 40 skipped (33 %), 10 outside both frustums, 2 untestable (near 2, no bound 0, viewport 0), 5 repeats") !=
	          std::string::npos,
	      "per pass, the shares");
	Check(text.find("shapes 100 drawn 40 skipped skinned 20 drawn 0 skipped;") != std::string::npos, "by kind, kinds without draws left out");
	Check(text.find("tests 60 a pass (0 beyond capacity, 0 table full), results 400 hidden / 150 visible, 2 batches lost, 5 verdicts refused by the camera gate") !=
	          std::string::npos,
	      "the tests");
	Check(text.find("check:") == std::string::npos, "no check line without the check mode");
	Check(text.find("per pass: decide 2.000 ms, end tests 3.000 ms, collect 1.000 ms, eye pass 40.000 ms") != std::string::npos, "the cost per pass");
	Check(text.find("; totals qpc 777 passes 10 draws 1200 skipped 400 tests 600 hidden 400 visible 150 checkDrawn 0 checkVisible 0 passMs 400.000 cullMs 60.000") !=
	          std::string::npos,
	      "the totals for the summary tool");
	settings.check = true;
	s.checkDrawn = 400;
	s.checkWithPixels = 8;
	s.checkPixels = 1234;
	s.checkDirectDrawn = 30;
	cull::FormatSummary(s, settings, 0.0, 778, line, sizeof(line));
	text = line;
	Check(text.find("(check mode: skipped draws drawn and counted)") != std::string::npos, "the check mode is named");
	Check(text.find("check: 400 skipped shapes drawn again at the pass's end, 8 of them visible (2 %), 1234 pixels; 30 skipped direct draws drawn unmeasured") !=
	          std::string::npos,
	      "the check's figures");
	Check(text.find("ticks: decide 20000, end tests 30000, collect 10000, eye pass 400000") != std::string::npos, "ticks without a calibration");
	Check(text.find("checkDrawn 400 checkVisible 8 passMs 0.000 cullMs 0.000") != std::string::npos, "the totals without a calibration: no milliseconds");
	Stats empty;
	cull::FormatSummary(empty, Settings{}, 1000.0, 0, line, sizeof(line));
	Check(std::string(line).find("0 eye passes") != std::string::npos && std::string(line).find("(0 %)") != std::string::npos, "nothing divides by zero");
}

}  // namespace

int main() {
	TestSettings();
	TestSecondEyeView();
	TestRects();
	TestCameraGate();
	TestTable();
	TestDecide();
	TestRingAndCollect();
	TestSummary();
	return Failures();
}
