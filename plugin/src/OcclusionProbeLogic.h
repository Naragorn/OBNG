#pragma once

// The occlusion probe's decisions ([Probe] Occlusion, OcclusionProbe.cpp),
// free of the engine and of Direct3D so every flow has a test
// (plugin/tests/OcclusionProbeLogicTest.cpp).
//
// The question the probe answers: which share of the geometries the game
// draws in an eye's world pass lies wholly behind other geometry, so that an
// occlusion culler could leave it out. The measure is the test such a culler
// makes - the screen rectangle of the geometry's world bound at the bound's
// nearest depth against the depth buffer - made with the pass's finished
// depth buffer, which is the best depth a culler can have. It is therefore
// an upper bound of what a culler working from an earlier frame's coarser
// depth can reach.
//
// Per sampled draw three occlusion queries (IDirect3DQuery9, pixels that
// pass the depth test):
//   A  the bound's rectangle, drawn right before the geometry
//   B  the geometry's own draw
//   C  the bound's rectangle again, after the pass's last draw
//   D  the control: C's rectangle at depth 0, which nothing can hide
// A == 0 with B > 0 is a contradiction - the bound encloses the geometry, so
// a hidden bound cannot have a visible geometry - and counts how far the
// rectangle (the origin, the matrices, the bound's field) can be trusted.
// C == 0 is the probe's answer; B == 0 says how much of it the draw order
// already shows. D == 0 means the end rectangle was not rasterised at all
// (a state the probe did not neutralise): such a draw's C is left out and
// counted, so C == 0 can only come from the depth test.
//
// The matrices (read, re/functions/0x00762640.md): view at renderer+0x980 in
// D3D row-vector form with the camera's axes as columns and the translation
// in the fourth row; projection at +0x9C0, perspective (_34 = 1) or
// orthographic (_44 = 1), depth 0 at the near plane and 1 at the far one.
// World positions enter relative to the origin global 0x00B3F92C (the
// packer's subtraction is read in pseudocode only - the contradiction count
// is what measures it).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::occlusion {

// NiBound: the centre and the radius (geometry+0x20..+0x2C, read in
// re/functions/0x007693E0.md).
struct Bound {
	float centre[3];
	float radius;
};
static_assert(sizeof(Bound) == 16, "NiBound is four floats");

// D3DVIEWPORT9's layout (d3d9types.h).
struct Viewport {
	std::uint32_t x;
	std::uint32_t y;
	std::uint32_t width;
	std::uint32_t height;
	float minZ;
	float maxZ;
};
static_assert(sizeof(Viewport) == 24, "D3DVIEWPORT9 is 24 bytes");

// The rectangle in render-target pixels (left and top inclusive, right and
// bottom exclusive) and the depth-buffer value of the bound's nearest face.
struct ScreenRect {
	float left;
	float top;
	float right;
	float bottom;
	float depth;
};

enum class RectKind : std::uint8_t {
	kTestable,        // a rectangle inside the viewport
	kNoBound,         // radius not above 0, or a value that is not finite
	kCrossesNear,     // the bound's box reaches the near plane or the camera: no rectangle encloses it
	kOutsideFrustum,  // the box lies beside the viewport or beyond the far plane
	kViewport,        // the viewport has no area or a depth range other than 0..1
};

// How many draws one sampled pass holds; the rest of the pass is counted.
constexpr std::uint32_t kMaxDraws = 4096;

// The rectangle of the bound: the box of the bound's sphere in view space
// (the view's rotation keeps the radius), its eight corners projected, the
// extent rounded outward and widened by one pixel, clipped to the viewport;
// the depth is the smallest of the corners'.
inline RectKind BoundRect(const Bound& bound, const float origin[3], const float view[16], const float proj[16], const Viewport& port,
                          ScreenRect* out) {
	if (!std::isfinite(bound.radius) || !(bound.radius > 0.0f) || !std::isfinite(bound.centre[0]) || !std::isfinite(bound.centre[1]) ||
	    !std::isfinite(bound.centre[2])) {
		return RectKind::kNoBound;
	}
	if (port.width == 0 || port.height == 0 || port.minZ != 0.0f || port.maxZ != 1.0f) {
		return RectKind::kViewport;
	}
	const float rel[3] = {bound.centre[0] - origin[0], bound.centre[1] - origin[1], bound.centre[2] - origin[2]};
	float centre[3];
	for (int column = 0; column < 3; ++column) {
		centre[column] = rel[0] * view[column] + rel[1] * view[4 + column] + rel[2] * view[8 + column] + view[12 + column];
	}
	float minX = 0.0f;
	float maxX = 0.0f;
	float minY = 0.0f;
	float maxY = 0.0f;
	float minZ = 0.0f;
	for (int corner = 0; corner < 8; ++corner) {
		const float x = centre[0] + ((corner & 1) != 0 ? bound.radius : -bound.radius);
		const float y = centre[1] + ((corner & 2) != 0 ? bound.radius : -bound.radius);
		const float z = centre[2] + ((corner & 4) != 0 ? bound.radius : -bound.radius);
		const float clipX = x * proj[0] + y * proj[4] + z * proj[8] + proj[12];
		const float clipY = x * proj[1] + y * proj[5] + z * proj[9] + proj[13];
		const float clipZ = x * proj[2] + y * proj[6] + z * proj[10] + proj[14];
		const float clipW = x * proj[3] + y * proj[7] + z * proj[11] + proj[15];
		if (!(clipW > 0.0f) || !(clipZ >= 0.0f)) {
			return RectKind::kCrossesNear;
		}
		const float ndcX = clipX / clipW;
		const float ndcY = clipY / clipW;
		const float ndcZ = clipZ / clipW;
		if (corner == 0) {
			minX = maxX = ndcX;
			minY = maxY = ndcY;
			minZ = ndcZ;
			continue;
		}
		minX = ndcX < minX ? ndcX : minX;
		maxX = ndcX > maxX ? ndcX : maxX;
		minY = ndcY < minY ? ndcY : minY;
		maxY = ndcY > maxY ? ndcY : maxY;
		minZ = ndcZ < minZ ? ndcZ : minZ;
	}
	if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY) || !std::isfinite(minZ)) {
		return RectKind::kNoBound;
	}
	if (minZ > 1.0f) {
		return RectKind::kOutsideFrustum;
	}
	const float portLeft = static_cast<float>(port.x);
	const float portTop = static_cast<float>(port.y);
	const float portRight = portLeft + static_cast<float>(port.width);
	const float portBottom = portTop + static_cast<float>(port.height);
	const auto clamp = [](float value, float low, float high) { return value < low ? low : (value > high ? high : value); };
	// x grows to the right, y (the camera's up) to the port's top.
	const float left = clamp(std::floor(portLeft + (minX * 0.5f + 0.5f) * static_cast<float>(port.width)) - 1.0f, portLeft, portRight);
	const float right = clamp(std::ceil(portLeft + (maxX * 0.5f + 0.5f) * static_cast<float>(port.width)) + 1.0f, portLeft, portRight);
	const float top = clamp(std::floor(portTop + (0.5f - maxY * 0.5f) * static_cast<float>(port.height)) - 1.0f, portTop, portBottom);
	const float bottom = clamp(std::ceil(portTop + (0.5f - minY * 0.5f) * static_cast<float>(port.height)) + 1.0f, portTop, portBottom);
	if (!(left < right) || !(top < bottom)) {
		return RectKind::kOutsideFrustum;
	}
	*out = ScreenRect{left, top, right, bottom, minZ};
	return RectKind::kTestable;
}

// The rectangle as a triangle strip of four pre-transformed vertices
// (D3DFVF_XYZRHW: x, y, depth, rhw): Direct3D 9 puts a pixel's centre on the
// whole coordinate, so the edges sit half a pixel up and left of the
// rectangle's.
inline void QuadVertices(const ScreenRect& rect, float out[16]) {
	const float xs[2] = {rect.left - 0.5f, rect.right - 0.5f};
	const float ys[2] = {rect.top - 0.5f, rect.bottom - 0.5f};
	for (int i = 0; i < 4; ++i) {
		out[i * 4 + 0] = xs[i & 1];
		out[i * 4 + 1] = ys[i >> 1];
		out[i * 4 + 2] = rect.depth;
		out[i * 4 + 3] = 1.0f;
	}
}

// What a draw's rectangle was made for: the depth buffer bound (its address,
// only compared) and the viewport. The end test runs on the same pair only.
struct Target {
	std::uint32_t depthBuffer = 0;
	Viewport port{};
};

inline bool SameTarget(const Target& a, const Target& b) {
	return a.depthBuffer != 0 && a.depthBuffer == b.depthBuffer && a.port.x == b.port.x && a.port.y == b.port.y &&
	       a.port.width == b.port.width && a.port.height == b.port.height;
}

// Which eye passes are sampled: one in `every`, and none while the last
// sampled pass still waits for its queries' results - a pass that falls due
// meanwhile is made up for by the first free one.
struct Sampler {
	std::uint32_t every = 31;
	std::uint32_t count = 0;

	bool Take(bool pending) {
		const std::uint32_t due = every == 0 ? 1u : every;
		if (count < due) {
			++count;
		}
		if (count < due || pending) {
			return false;
		}
		count = 0;
		return true;
	}
};

// A batch whose results have not come after this many eye passes is dropped
// (a lost device, a driver that never finishes a query).
constexpr std::uint32_t kPassesToWait = 600;
inline bool GiveUp(std::uint32_t passesWaited) {
	return passesWaited > kPassesToWait;
}

enum class DrawKind : std::uint8_t {
	kShape,         // Do_RenderShape 0x007693E0 without a skin instance
	kSkinned,       // Do_RenderShape with one
	kDirectShape,   // DrawShapeDirect 0x007694F0
	kDirectStrips,  // DrawTriStripsDirect 0x00769700
	kCount,
};

inline const char* KindName(DrawKind kind) {
	switch (kind) {
		case DrawKind::kShape:
			return "shapes";
		case DrawKind::kSkinned:
			return "skinned shapes";
		case DrawKind::kDirectShape:
			return "direct shapes";
		case DrawKind::kDirectStrips:
			return "direct strips";
		case DrawKind::kCount:
			break;
	}
	return "?";
}

// One sampled draw with its three results.
struct Sample {
	DrawKind kind = DrawKind::kShape;
	RectKind rect = RectKind::kNoBound;
	bool endTested = false;         // query C ran (the target at the pass's end was the draw's)
	std::uint32_t drawPixels = 0;   // B
	std::uint32_t boundAtDraw = 0;  // A, when the rectangle is testable
	std::uint32_t boundAtEnd = 0;   // C, when endTested
	std::uint32_t controlAtEnd = 0; // D, when endTested
	std::uint64_t ticks = 0;        // the original draw's time
};

struct KindStats {
	std::uint64_t draws = 0;
	std::uint64_t testable = 0;
	std::uint64_t noBound = 0;
	std::uint64_t crossesNear = 0;
	std::uint64_t outsideFrustum = 0;
	std::uint64_t viewport = 0;
	std::uint64_t drewNothing = 0;             // B == 0, every draw
	std::uint64_t hiddenAtDraw = 0;            // A == 0
	std::uint64_t endTested = 0;
	std::uint64_t hiddenAtEnd = 0;             // C == 0: the answer
	std::uint64_t hiddenAtEndDrewNothing = 0;  // of those, B == 0 too
	std::uint64_t contradictions = 0;          // A == 0 and B > 0
	std::uint64_t controlFailed = 0;           // D == 0: the end test not readable, left out of endTested
	std::uint64_t ticks = 0;
	std::uint64_t ticksDrewNothing = 0;
	std::uint64_t ticksHiddenAtEnd = 0;
};

struct Stats {
	KindStats kinds[static_cast<int>(DrawKind::kCount)];
	std::uint64_t passesSampled = 0;
	std::uint64_t drawsUnsampled = 0;        // draws outside a sampled eye pass
	std::uint64_t drawsBeyondCapacity = 0;   // draws of a sampled pass past kMaxDraws
	std::uint64_t drawsInInnerPasses = 0;    // draws of a pass that ran inside a sampled eye pass
	std::uint64_t batchesLost = 0;

	void Add(const Sample& s) {
		if (static_cast<int>(s.kind) >= static_cast<int>(DrawKind::kCount)) {
			return;
		}
		KindStats& k = kinds[static_cast<int>(s.kind)];
		++k.draws;
		k.ticks += s.ticks;
		const bool drewNothing = s.drawPixels == 0;
		if (drewNothing) {
			++k.drewNothing;
			k.ticksDrewNothing += s.ticks;
		}
		switch (s.rect) {
			case RectKind::kNoBound:
				++k.noBound;
				return;
			case RectKind::kCrossesNear:
				++k.crossesNear;
				return;
			case RectKind::kOutsideFrustum:
				++k.outsideFrustum;
				return;
			case RectKind::kViewport:
				++k.viewport;
				return;
			case RectKind::kTestable:
				break;
		}
		++k.testable;
		if (s.boundAtDraw == 0) {
			++k.hiddenAtDraw;
			if (!drewNothing) {
				++k.contradictions;
			}
		}
		if (!s.endTested) {
			return;
		}
		if (s.controlAtEnd == 0) {
			++k.controlFailed;
			return;
		}
		++k.endTested;
		if (s.boundAtEnd == 0) {
			++k.hiddenAtEnd;
			k.ticksHiddenAtEnd += s.ticks;
			if (drewNothing) {
				++k.hiddenAtEndDrewNothing;
			}
		}
	}

	void Reset() { *this = Stats{}; }
};

// What the probe cannot run beside, by the entry they share: the first one
// that is on, or null when the probe may run.
inline const char* BlockedBy(bool passSteps, bool drawEntries, bool constantUploads, bool stereoDraw) {
	if (passSteps) {
		return "[Probe] PassSteps";  // the finish 0x007AE070 and Do_RenderShape 0x007693E0
	}
	if (drawEntries) {
		return "[Probe] DrawEntries";  // slots 90 and 91
	}
	if (constantUploads) {
		return "[Probe] ConstantUploads";  // the finish
	}
	if (stereoDraw) {
		return "[Route] StereoDraw";  // slots 90 and 91
	}
	return nullptr;
}

// The head line; returns the characters written.
inline int FormatHead(const Stats& stats, std::uint32_t every, std::uint64_t qpc, char* out, std::size_t capacity) {
	std::uint64_t draws = 0;
	for (const KindStats& k : stats.kinds) {
		draws += k.draws;
	}
	return std::snprintf(out, capacity,
	                     "Probe: occlusion - %llu eye passes sampled (1 in %u), %llu draws in them, %llu beyond the %u a pass holds, %llu in "
	                     "passes inside them, %llu batches lost, %llu draws unsampled; qpc %llu",
	                     static_cast<unsigned long long>(stats.passesSampled), every == 0 ? 1u : every, static_cast<unsigned long long>(draws),
	                     static_cast<unsigned long long>(stats.drawsBeyondCapacity), kMaxDraws,
	                     static_cast<unsigned long long>(stats.drawsInInnerPasses), static_cast<unsigned long long>(stats.batchesLost),
	                     static_cast<unsigned long long>(stats.drawsUnsampled), static_cast<unsigned long long>(qpc));
}

// One kind's line, or nothing (0) for a kind that drew nothing.
inline int FormatKind(const Stats& stats, DrawKind kind, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (static_cast<int>(kind) >= static_cast<int>(DrawKind::kCount)) {
		return 0;
	}
	const KindStats& k = stats.kinds[static_cast<int>(kind)];
	if (k.draws == 0) {
		return 0;
	}
	int at = std::snprintf(out, capacity,
	                       "Probe: occlusion %s - %llu draws: %llu testable (near plane %llu, outside the frustum %llu, no bound %llu, viewport "
	                       "%llu); no pixel drawn %llu; bound hidden at the draw %llu, at the pass's end %llu of %llu tested (%llu of them drew "
	                       "no pixel); contradictions %llu, failed controls %llu; ",
	                       KindName(kind), static_cast<unsigned long long>(k.draws), static_cast<unsigned long long>(k.testable),
	                       static_cast<unsigned long long>(k.crossesNear), static_cast<unsigned long long>(k.outsideFrustum),
	                       static_cast<unsigned long long>(k.noBound), static_cast<unsigned long long>(k.viewport),
	                       static_cast<unsigned long long>(k.drewNothing), static_cast<unsigned long long>(k.hiddenAtDraw),
	                       static_cast<unsigned long long>(k.hiddenAtEnd), static_cast<unsigned long long>(k.endTested),
	                       static_cast<unsigned long long>(k.hiddenAtEndDrewNothing), static_cast<unsigned long long>(k.contradictions),
	                       static_cast<unsigned long long>(k.controlFailed));
	if (at < 0 || static_cast<std::size_t>(at) >= capacity) {
		return at;
	}
	if (ticksPerMillisecond > 0.0) {
		at += std::snprintf(out + at, capacity - static_cast<std::size_t>(at), "draw time %.3f ms, %.3f ms hidden at the end, %.3f ms without a pixel",
		                    static_cast<double>(k.ticks) / ticksPerMillisecond, static_cast<double>(k.ticksHiddenAtEnd) / ticksPerMillisecond,
		                    static_cast<double>(k.ticksDrewNothing) / ticksPerMillisecond);
	} else {
		at += std::snprintf(out + at, capacity - static_cast<std::size_t>(at),
		                    "draw time %llu ticks, %llu ticks hidden at the end, %llu ticks without a pixel",
		                    static_cast<unsigned long long>(k.ticks), static_cast<unsigned long long>(k.ticksHiddenAtEnd),
		                    static_cast<unsigned long long>(k.ticksDrewNothing));
	}
	return at;
}

}  // namespace obng::plugin::occlusion
