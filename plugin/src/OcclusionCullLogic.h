#pragma once

// The occlusion culler's decisions ([Route] OcclusionCull, OcclusionCull.cpp),
// free of the engine and of Direct3D so every flow has a test
// (plugin/tests/OcclusionCullLogicTest.cpp).
//
// What the culler does: in every single-pass eye pass, for every geometry the
// renderer's draw entries are asked to draw, it projects the geometry's world
// bound into both eyes' viewports (OcclusionProbeLogic.h BoundRect - the
// rectangle the probe measured with zero contradictions) and remembers the
// pair. After the pass's last draw the pairs are drawn as rectangles at the
// bound's nearest depth inside occlusion queries, depth test on and nothing
// written: a query that counts no pixel says the geometry's bound lay wholly
// behind what the pass drew, in both eyes. The results come back in the
// following frames; a geometry whose last result, at most maxAge frames old,
// said "hidden" is not drawn - unless the camera moved or turned more than
// the gate allows since that result's frame. A skipped geometry is still
// tested every frame, so it comes back as soon as a test sees it.
//
// Why that is sound for a camera that stands still (derived from the
// probe's measure): a hidden verdict for X means every pixel of X's rectangle
// holds a depth nearer than X's nearest point, written by geometry other
// than X. Whatever X hid lies behind those same pixels, so leaving X out
// uncovers nothing that was hidden only by X. A moving camera breaks the
// argument by the motion of one frame; the gate and the margin bound it, and
// the pass's own test measures what is left: a skipped geometry is tested
// like every other, against the frame it was skipped from. A test that
// finds its bound hidden confirms the skip (the geometry could not have
// shown); one that finds the bound seen leaves it unconfirmed - an upper
// bound of the popping, as loose as the bound is wider than the geometry.
// (Drawing a skipped shape again after the pass, to count its visible
// pixels, is not possible: Do_RenderShape needs the pass's record state,
// which is gone by then - run obng-cull/20261108-152718 crashed at
// 0x007FB4F2 reading a null record.)

#include "OcclusionProbeLogic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obng::plugin::occlusion_cull {

namespace oc = occlusion;

// ---- The settings' bounds -------------------------------------------------------

constexpr std::uint32_t kMaxAgeLimit = 8;      // frames; the camera ring below holds that many
constexpr std::uint32_t kMarginLimit = 64;     // pixels

// [Route] OcclusionCullMaxAge: how many frames a hidden verdict stays good.
constexpr std::uint32_t MaxAgeFromIni(int value) {
	return value < 1 ? 1u : (static_cast<std::uint32_t>(value) > kMaxAgeLimit ? kMaxAgeLimit : static_cast<std::uint32_t>(value));
}

// [Route] OcclusionCullMargin: pixels the rectangles grow on every side.
constexpr std::uint32_t MarginFromIni(int value) {
	return value < 0 ? 0u : (static_cast<std::uint32_t>(value) > kMarginLimit ? kMarginLimit : static_cast<std::uint32_t>(value));
}

struct Settings {
	std::uint32_t maxAge = 2;       // frames a hidden verdict is acted on
	std::uint32_t margin = 2;       // pixels
	float maxMoveUnits = 100.0f;    // the camera moved further since the verdict: no skip
	float maxTurnDegrees = 10.0f;   // the camera turned further since the verdict: no skip
};

// What the culler cannot run beside, by the entry they share (the finish
// 0x007AE070 and Do_RenderShape 0x007693E0), and what it needs: the first
// reason, or null when it may run.
inline const char* BlockedBy(bool stereoDraw, bool passSteps, bool constantUploads, bool occlusionProbe) {
	if (!stereoDraw) {
		return "[Route] StereoDraw=0 (the culler needs the route's eye data)";
	}
	if (passSteps) {
		return "[Probe] PassSteps";
	}
	if (constantUploads) {
		return "[Probe] ConstantUploads";
	}
	if (occlusionProbe) {
		return "[Probe] Occlusion";
	}
	return nullptr;
}

// ---- The second eye's view --------------------------------------------------------

// Eye 1's view matrix from eye 0's: the translation row moves by the eye
// offset projected on the axes, which are the matrix's columns
// (StereoEyeSwitchLogic.h ShiftedRows, derived from 0x00762640.md C2).
inline void SecondEyeView(const float view0[16], const float offset[3], float out[16]) {
	std::memcpy(out, view0, 16 * sizeof(float));
	for (int k = 0; k < 3; ++k) {
		const float axisDotOffset = view0[k] * offset[0] + view0[4 + k] * offset[1] + view0[8 + k] * offset[2];
		out[12 + k] = view0[12 + k] - axisDotOffset;
	}
}

// ---- The rectangles of one geometry -----------------------------------------------

enum class TestKind : std::uint8_t {
	kNone,         // no rectangle encloses the bound in at least one eye (near plane, no bound, viewport): draw, no test
	kBothOutside,  // the bound lies outside both eyes' frustums: nothing to draw, no test
	kTest,         // one or two rectangles to test
};

struct EyeRects {
	oc::ScreenRect rect[2]{};
	bool has[2] = {false, false};
};

// The rectangle grown by margin pixels on every side, kept inside the port.
inline void Inflate(oc::ScreenRect* rect, float margin, const oc::Viewport& port) {
	const float left = static_cast<float>(port.x);
	const float top = static_cast<float>(port.y);
	const float right = left + static_cast<float>(port.width);
	const float bottom = top + static_cast<float>(port.height);
	rect->left = rect->left - margin < left ? left : rect->left - margin;
	rect->top = rect->top - margin < top ? top : rect->top - margin;
	rect->right = rect->right + margin > right ? right : rect->right + margin;
	rect->bottom = rect->bottom + margin > bottom ? bottom : rect->bottom + margin;
}

// Both eyes' rectangles of a bound; kinds[] says what each eye's projection
// gave.
inline TestKind RectsFor(const oc::Bound& bound, const float origin[3], const float view0[16], const float view1[16], const float proj[16],
                         const oc::Viewport port[2], float margin, EyeRects* out, oc::RectKind kinds[2]) {
	const float* views[2] = {view0, view1};
	std::uint32_t testable = 0;
	for (int eye = 0; eye < 2; ++eye) {
		out->has[eye] = false;
		kinds[eye] = oc::BoundRect(bound, origin, views[eye], proj, port[eye], &out->rect[eye]);
		if (kinds[eye] == oc::RectKind::kTestable) {
			Inflate(&out->rect[eye], margin, port[eye]);
			out->has[eye] = true;
			++testable;
		} else if (kinds[eye] != oc::RectKind::kOutsideFrustum) {
			return TestKind::kNone;
		}
	}
	return testable == 0 ? TestKind::kBothOutside : TestKind::kTest;
}

// The rectangles as one triangle list of pre-transformed vertices (x, y,
// depth, rhw), six vertices a rectangle; returns the vertex count (6 or 12).
constexpr std::uint32_t kMaxRectVertices = 12;
inline std::uint32_t RectTriangles(const EyeRects& rects, float out[kMaxRectVertices * 4]) {
	std::uint32_t vertices = 0;
	for (int eye = 0; eye < 2; ++eye) {
		if (!rects.has[eye]) {
			continue;
		}
		float quad[16];
		oc::QuadVertices(rects.rect[eye], quad);
		static const int kOrder[6] = {0, 1, 2, 2, 1, 3};
		for (int corner : kOrder) {
			std::memcpy(out + vertices * 4, quad + corner * 4, 4 * sizeof(float));
			++vertices;
		}
	}
	return vertices;
}

// ---- The camera gate ---------------------------------------------------------------

struct Camera {
	float position[3] = {0.0f, 0.0f, 0.0f};
	float direction[3] = {0.0f, 0.0f, 0.0f};
};

// The camera of a pass: its position is row 4 of the inverse view
// (renderer+0xA00, 0x00762640.md C3), its direction column 2 of the view.
inline Camera CameraOf(const float view[16], const float inverseRow[3]) {
	Camera c;
	for (int k = 0; k < 3; ++k) {
		c.position[k] = inverseRow[k];
		c.direction[k] = view[k * 4 + 2];
	}
	return c;
}

inline float CosOfDegrees(float degrees) {
	return std::cos(degrees * 3.14159265358979f / 180.0f);
}

// Whether a verdict made with camera `then` may be acted on now.
inline bool MotionAllows(const Camera& now, const Camera& then, float maxMoveUnits, float minDirectionCos) {
	float moved = 0.0f;
	float cosine = 0.0f;
	for (int k = 0; k < 3; ++k) {
		const float d = now.position[k] - then.position[k];
		moved += d * d;
		cosine += now.direction[k] * then.direction[k];
	}
	return moved <= maxMoveUnits * maxMoveUnits && cosine >= minDirectionCos;
}

// The cameras of the last frames, by frame number.
constexpr std::uint32_t kCameraRing = 16;
static_assert(kCameraRing > kMaxAgeLimit, "a verdict's camera must still be in the ring");
struct Cameras {
	Camera ring[kCameraRing];
	void Set(std::uint32_t frame, const Camera& camera) { ring[frame % kCameraRing] = camera; }
	const Camera& At(std::uint32_t frame) const { return ring[frame % kCameraRing]; }
};

// ---- The verdict table -------------------------------------------------------------

enum class Decision : std::uint8_t {
	kDraw,         // drawn (no verdict, a visible one, an untestable bound, or the gate refused)
	kSkip,         // not drawn: hidden by a verdict at most maxAge frames old
	kSkipFrustum,  // not drawn: outside both eyes' frustums this frame
};

struct Verdict {
	std::uint32_t geometry = 0;     // the key (0: empty)
	std::uint32_t seenFrame = 0;    // the last frame the geometry was asked about
	std::uint32_t hiddenFrame = 0;  // the frame of the last test that found it hidden; 0 none, or a later test saw it
	Decision decision = Decision::kDraw;  // this frame's, for the geometry's later passes
};

constexpr std::uint32_t kTableSlots = 16384;
constexpr std::uint32_t kProbeLength = 16;
constexpr std::uint32_t kStaleFrames = 64;  // a slot not asked about for this long may be taken
static_assert((kTableSlots & (kTableSlots - 1)) == 0, "the table's size is a power of two");

struct Table {
	Verdict slots[kTableSlots];

	static std::uint32_t Home(std::uint32_t geometry) {
		return ((geometry >> 3) * 2654435761u) >> 18;  // 32 - 14 bits
	}

	// The geometry's slot; when it has none, an empty or stale slot within
	// the probe length, claimed for it; null when none is free.
	Verdict* Claim(std::uint32_t geometry, std::uint32_t frame) {
		Verdict* spare = nullptr;
		for (std::uint32_t i = 0; i < kProbeLength; ++i) {
			Verdict& v = slots[(Home(geometry) + i) & (kTableSlots - 1)];
			if (v.geometry == geometry) {
				return &v;
			}
			if (spare == nullptr && (v.geometry == 0 || frame - v.seenFrame > kStaleFrames)) {
				spare = &v;
			}
		}
		if (spare != nullptr) {
			*spare = Verdict{};
			spare->geometry = geometry;
		}
		return spare;
	}

	// The geometry's slot or null; nothing is claimed.
	Verdict* Find(std::uint32_t geometry) {
		for (std::uint32_t i = 0; i < kProbeLength; ++i) {
			Verdict& v = slots[(Home(geometry) + i) & (kTableSlots - 1)];
			if (v.geometry == geometry) {
				return &v;
			}
		}
		return nullptr;
	}

	// A test's result: hidden sets the verdict's frame, visible clears a
	// verdict that is not newer than the test.
	void Record(std::uint32_t geometry, std::uint32_t testFrame, bool hidden) {
		Verdict* v = Find(geometry);
		if (v == nullptr) {
			return;
		}
		if (hidden) {
			v->hiddenFrame = testFrame > v->hiddenFrame ? testFrame : v->hiddenFrame;
		} else if (v->hiddenFrame <= testFrame) {
			v->hiddenFrame = 0;
		}
	}
};

// ---- The tests in flight -----------------------------------------------------------

constexpr std::uint32_t kBatches = 4;      // frames whose tests may be outstanding at once
constexpr std::uint32_t kMaxTests = 4096;  // tests one pass may hold; the rest is drawn untested

struct Test {
	std::uint32_t geometry = 0;
	EyeRects rects;
	bool skipped = false;  // the geometry was left out of the pass this test belongs to
};

struct Batch {
	std::uint32_t frame = 0;
	std::uint32_t count = 0;
	std::uint32_t cursor = 0;  // the first test whose result is not read yet
	bool pending = false;      // issued, results outstanding
	Test tests[kMaxTests];
};

struct Ring {
	Batch batches[kBatches];

	// The batch for a frame's tests; a batch still pending there is lost.
	Batch& Begin(std::uint32_t frame, bool* lost) {
		Batch& b = batches[frame % kBatches];
		*lost = b.pending;
		b.frame = frame;
		b.count = 0;
		b.cursor = 0;
		b.pending = false;
		return b;
	}

	// Every pending batch dropped (its queries are gone: a device reset);
	// returns how many were.
	std::uint32_t DropAll() {
		std::uint32_t dropped = 0;
		for (Batch& b : batches) {
			if (b.pending) {
				b.pending = false;
				++dropped;
			}
		}
		return dropped;
	}

	// The oldest pending batch, or null.
	Batch* OldestPending() {
		Batch* oldest = nullptr;
		for (Batch& b : batches) {
			if (b.pending && (oldest == nullptr || b.frame < oldest->frame)) {
				oldest = &b;
			}
		}
		return oldest;
	}
};

enum class DrawKind : std::uint8_t { kShape, kSkinned, kDirectShape, kDirectStrips, kCount };

inline DrawKind KindOf(oc::DrawKind kind) {
	switch (kind) {
		case oc::DrawKind::kShape:
			return DrawKind::kShape;
		case oc::DrawKind::kSkinned:
			return DrawKind::kSkinned;
		case oc::DrawKind::kDirectShape:
			return DrawKind::kDirectShape;
		case oc::DrawKind::kDirectStrips:
		case oc::DrawKind::kCount:
			break;
	}
	return DrawKind::kDirectStrips;
}

// ---- What happened since the last summary --------------------------------------

struct KindCounts {
	std::uint64_t draws = 0;         // asked about in an eye pass
	std::uint64_t skipped = 0;       // by a verdict
	std::uint64_t skippedFrustum = 0;
	std::uint64_t untestable = 0;
	std::uint64_t repeats = 0;       // the geometry's later passes in the same frame
};

struct Stats {
	std::uint64_t eyePasses = 0;
	std::uint64_t passesWithoutEyes = 0;  // the route had no eye data: the pass ran untouched
	KindCounts kinds[static_cast<int>(DrawKind::kCount)];
	std::uint64_t untestableNear = 0;
	std::uint64_t untestableNoBound = 0;
	std::uint64_t untestableViewport = 0;
	std::uint64_t outsideEyeViewport = 0;  // the device's viewport was not eye 0's: drawn untouched
	std::uint64_t testsIssued = 0;
	std::uint64_t testsBeyondCapacity = 0;
	std::uint64_t tableFull = 0;
	std::uint64_t resultsHidden = 0;
	std::uint64_t resultsVisible = 0;
	std::uint64_t batchesLost = 0;
	std::uint64_t motionBlocked = 0;  // a hidden verdict the camera gate refused
	std::uint64_t skippedAtRecord = 0;  // skips decided at the pass record (DrawRenderPass 0x007A9820), before the light constants
	std::uint64_t deviceResets = 0;     // the device's Reset seen: the queries and the state block given up and made anew
	std::uint64_t skipsConfirmed = 0;   // a skipped geometry whose test of the same pass found its bound hidden: it could not have shown
	std::uint64_t skipsUnconfirmed = 0; // ... found its bound seen: the upper bound of the popping
	std::uint64_t decideTicks = 0;
	std::uint64_t endTestsTicks = 0;
	std::uint64_t collectTicks = 0;
	std::uint64_t passTicks = 0;  // the eye pass's finish, culler included

	std::uint64_t Draws() const {
		std::uint64_t n = 0;
		for (const KindCounts& k : kinds) {
			n += k.draws;
		}
		return n;
	}
	std::uint64_t Skipped() const {
		std::uint64_t n = 0;
		for (const KindCounts& k : kinds) {
			n += k.skipped + k.skippedFrustum;
		}
		return n;
	}
	void Reset() { *this = Stats{}; }
};

// ---- The decision for one draw -----------------------------------------------------

struct Decided {
	Decision decision = Decision::kDraw;
	bool newThisFrame = false;  // the first time this frame: a test is due when the kind allows one
	Verdict* verdict = nullptr;
};

// `frame` counts eye passes from 1. The table keeps the decision for the
// geometry's later passes in the same frame.
inline Decided Decide(Table& table, std::uint32_t geometry, std::uint32_t frame, TestKind kind, const Settings& settings,
                      const Cameras& cameras, Stats* stats) {
	Decided out;
	out.verdict = table.Claim(geometry, frame);
	if (out.verdict == nullptr) {
		++stats->tableFull;
		return out;
	}
	Verdict& v = *out.verdict;
	if (v.seenFrame == frame) {
		out.decision = v.decision;
		return out;
	}
	out.newThisFrame = true;
	v.seenFrame = frame;
	v.decision = Decision::kDraw;
	if (kind == TestKind::kBothOutside) {
		v.decision = Decision::kSkipFrustum;
	} else if (kind == TestKind::kTest && v.hiddenFrame != 0 && frame - v.hiddenFrame <= settings.maxAge) {
		if (MotionAllows(cameras.At(frame), cameras.At(v.hiddenFrame), settings.maxMoveUnits, CosOfDegrees(settings.maxTurnDegrees))) {
			v.decision = Decision::kSkip;
		} else {
			++stats->motionBlocked;
		}
	}
	out.decision = v.decision;
	return out;
}

// Counts a decision in its kind's row.
inline void Count(Stats* stats, DrawKind kind, Decision decision, TestKind testKind, bool newThisFrame, const oc::RectKind kinds[2]) {
	KindCounts& k = stats->kinds[static_cast<int>(kind)];
	++k.draws;
	if (!newThisFrame) {
		++k.repeats;
	}
	if (decision == Decision::kSkip) {
		++k.skipped;
	} else if (decision == Decision::kSkipFrustum) {
		++k.skippedFrustum;
	}
	if (testKind == TestKind::kNone && newThisFrame) {
		++k.untestable;
		for (int eye = 0; eye < 2; ++eye) {
			if (kinds[eye] == oc::RectKind::kCrossesNear) {
				++stats->untestableNear;
				break;
			}
			if (kinds[eye] == oc::RectKind::kNoBound) {
				++stats->untestableNoBound;
				break;
			}
			if (kinds[eye] == oc::RectKind::kViewport) {
				++stats->untestableViewport;
				break;
			}
		}
	}
}

// ---- Reading the results ------------------------------------------------------------

enum class Read : std::uint8_t { kDone, kNotYet, kFailed };

// Reads what is there, oldest batch first; stops at the first result that is
// not ready (the device finishes in order). `read(batch, test, &pixels)`
// gives one test's result. A failed read drops the batch.
template <class ReadFn>
void Collect(Ring* ring, Table* table, Stats* stats, ReadFn&& read) {
	for (;;) {
		Batch* b = ring->OldestPending();
		if (b == nullptr) {
			return;
		}
		while (b->cursor < b->count) {
			Test& t = b->tests[b->cursor];
			std::uint32_t pixels = 0;
			const Read r = read(*b, b->cursor, &pixels);
			if (r == Read::kNotYet) {
				return;
			}
			if (r == Read::kFailed) {
				++stats->batchesLost;
				b->pending = false;
				break;
			}
			const bool hidden = pixels == 0;
			table->Record(t.geometry, b->frame, hidden);
			if (hidden) {
				++stats->resultsHidden;
			} else {
				++stats->resultsVisible;
			}
			if (t.skipped) {
				if (hidden) {
					++stats->skipsConfirmed;
				} else {
					++stats->skipsUnconfirmed;
				}
			}
			++b->cursor;
		}
		if (b->cursor >= b->count) {
			b->pending = false;
		}
	}
}

// ---- The summary ------------------------------------------------------------------------

inline const char* KindName(DrawKind kind) {
	switch (kind) {
		case DrawKind::kShape:
			return "shapes";
		case DrawKind::kSkinned:
			return "skinned";
		case DrawKind::kDirectShape:
			return "direct shapes";
		case DrawKind::kDirectStrips:
			return "direct strips";
		case DrawKind::kCount:
			break;
	}
	return "?";
}

inline unsigned Percent(std::uint64_t part, std::uint64_t whole) {
	return whole == 0 ? 0u : static_cast<unsigned>(part * 100 / whole);
}

// Per eye pass where it can be, so runs with and without the culler compare;
// the tail carries the window's totals and the performance counter for
// tools/cull-summary.py, which sums windows between a scenario's marks.
inline int FormatSummary(const Stats& s, const Settings& settings, double ticksPerMillisecond, std::uint64_t qpc, char* out,
                         std::size_t capacity) {
	oc::LineWriter line{out, capacity};
	const double passes = s.eyePasses == 0 ? 1.0 : static_cast<double>(s.eyePasses);
	auto perPass = [&](std::uint64_t n) { return static_cast<double>(n) / passes; };
	auto ms = [&](std::uint64_t ticks) { return ticksPerMillisecond > 0.0 ? static_cast<double>(ticks) / ticksPerMillisecond / passes : 0.0; };
	(void)settings;
	line.Text("Route: occlusion cull - %llu eye passes (%llu without eye data); per pass %.0f draws, %.0f skipped (%u %%), %.0f outside both "
	          "frustums, %.0f untestable (near %.0f, no bound %.0f, viewport %.0f), %.0f repeats, %.0f outside eye 0's viewport",
	          static_cast<unsigned long long>(s.eyePasses),
	          static_cast<unsigned long long>(s.passesWithoutEyes), perPass(s.Draws()), perPass(s.Skipped()), Percent(s.Skipped(), s.Draws()),
	          perPass([&] {
		          std::uint64_t n = 0;
		          for (const KindCounts& k : s.kinds) {
			          n += k.skippedFrustum;
		          }
		          return n;
	          }()),
	          perPass([&] {
		          std::uint64_t n = 0;
		          for (const KindCounts& k : s.kinds) {
			          n += k.untestable;
		          }
		          return n;
	          }()),
	          perPass(s.untestableNear), perPass(s.untestableNoBound), perPass(s.untestableViewport),
	          perPass([&] {
		          std::uint64_t n = 0;
		          for (const KindCounts& k : s.kinds) {
			          n += k.repeats;
		          }
		          return n;
	          }()),
	          perPass(s.outsideEyeViewport));
	line.Text("; by kind:");
	for (int i = 0; i < static_cast<int>(DrawKind::kCount); ++i) {
		const KindCounts& k = s.kinds[i];
		if (k.draws == 0) {
			continue;
		}
		line.Text(" %s %.0f drawn %.0f skipped", KindName(static_cast<DrawKind>(i)), perPass(k.draws), perPass(k.skipped + k.skippedFrustum));
	}
	line.Text("; %.0f skips a pass decided at the pass record; tests %.0f a pass (%llu beyond capacity, %llu table full), results %llu hidden / "
	          "%llu visible, %llu batches lost, %llu verdicts refused by the camera gate, %llu device resets",
	          perPass(s.skippedAtRecord), perPass(s.testsIssued), static_cast<unsigned long long>(s.testsBeyondCapacity),
	          static_cast<unsigned long long>(s.tableFull), static_cast<unsigned long long>(s.resultsHidden),
	          static_cast<unsigned long long>(s.resultsVisible), static_cast<unsigned long long>(s.batchesLost),
	          static_cast<unsigned long long>(s.motionBlocked), static_cast<unsigned long long>(s.deviceResets));
	line.Text("; skips tested in their own pass: %llu confirmed hidden, %llu unconfirmed (%u %%, the popping's upper bound)",
	          static_cast<unsigned long long>(s.skipsConfirmed), static_cast<unsigned long long>(s.skipsUnconfirmed),
	          Percent(s.skipsUnconfirmed, s.skipsConfirmed + s.skipsUnconfirmed));
	auto totalMs = [&](std::uint64_t ticks) { return ticksPerMillisecond > 0.0 ? static_cast<double>(ticks) / ticksPerMillisecond : 0.0; };
	if (ticksPerMillisecond > 0.0) {
		line.Text("; per pass: decide %.3f ms, end tests %.3f ms, collect %.3f ms, eye pass %.3f ms", ms(s.decideTicks), ms(s.endTestsTicks),
		          ms(s.collectTicks), ms(s.passTicks));
	} else {
		line.Text("; ticks: decide %llu, end tests %llu, collect %llu, eye pass %llu", static_cast<unsigned long long>(s.decideTicks),
		          static_cast<unsigned long long>(s.endTestsTicks), static_cast<unsigned long long>(s.collectTicks),
		          static_cast<unsigned long long>(s.passTicks));
	}
	line.Text("; totals qpc %llu passes %llu draws %llu skipped %llu tests %llu hidden %llu visible %llu skipsConfirmed %llu skipsUnconfirmed %llu "
	          "passMs %.3f cullMs %.3f",
	          static_cast<unsigned long long>(qpc), static_cast<unsigned long long>(s.eyePasses), static_cast<unsigned long long>(s.Draws()),
	          static_cast<unsigned long long>(s.Skipped()), static_cast<unsigned long long>(s.testsIssued),
	          static_cast<unsigned long long>(s.resultsHidden), static_cast<unsigned long long>(s.resultsVisible),
	          static_cast<unsigned long long>(s.skipsConfirmed), static_cast<unsigned long long>(s.skipsUnconfirmed), totalMs(s.passTicks),
	          totalMs(s.decideTicks + s.endTestsTicks + s.collectTicks));
	return line.at;
}

}  // namespace obng::plugin::occlusion_cull
