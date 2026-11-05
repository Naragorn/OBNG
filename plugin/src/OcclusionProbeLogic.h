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
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
	kNoBound,         // radius not above 0, or a value that is not finite (0: a sample's zero state)
	kTestable,        // a rectangle inside the viewport
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

// Whose geometry a draw is: the player's third-person node (TESObjectREFR
// +0x3C) or first-person node (PlayerCharacter +0x5D0) among the geometry's
// ancestors (NiAVObject +0x1C), the geometry itself included; everything
// else is the others'. Objects are 32-bit handles, the parent is read
// through a port. The walk ends after kMaxAncestors steps.
enum class Owner : std::uint8_t { kOther, kPlayerThird, kPlayerFirst, kCount };
constexpr std::uint32_t kMaxAncestors = 48;

template <class ParentOf>
Owner OwnerOf(std::uint32_t object, std::uint32_t playerThird, std::uint32_t playerFirst, ParentOf&& parentOf) {
	for (std::uint32_t step = 0; step < kMaxAncestors && object != 0; ++step) {
		if (object == playerThird) {
			return Owner::kPlayerThird;
		}
		if (object == playerFirst) {
			return Owner::kPlayerFirst;
		}
		object = parentOf(object);
	}
	return Owner::kOther;
}

inline const char* OwnerName(Owner owner) {
	switch (owner) {
		case Owner::kOther:
			return "others";
		case Owner::kPlayerThird:
			return "player third person";
		case Owner::kPlayerFirst:
			return "player first person";
		case Owner::kCount:
			break;
	}
	return "?";
}

// A geometry's name (NiObjectNET +0x08) as a table key and a log word: cut
// to the row's width, control characters and the quote replaced.
constexpr std::size_t kNameWidth = 32;
inline void CopyName(const char* name, char out[kNameWidth]) {
	std::size_t i = 0;
	for (; name != nullptr && i + 1 < kNameWidth && name[i] != '\0'; ++i) {
		const unsigned char c = static_cast<unsigned char>(name[i]);
		out[i] = c < 0x20 || c == 0x7F ? '?' : (c == '"' ? '\'' : static_cast<char>(c));
	}
	for (; i < kNameWidth; ++i) {
		out[i] = '\0';
	}
}

// One sampled draw with its results.
struct Sample {
	DrawKind kind = DrawKind::kShape;
	RectKind rect = RectKind::kNoBound;
	bool endTested = false;         // query C ran (the target at the pass's end was the draw's)
	std::uint32_t drawPixels = 0;   // B
	std::uint32_t boundAtDraw = 0;  // A, when the rectangle is testable
	std::uint32_t boundAtEnd = 0;   // C, when endTested
	std::uint32_t controlAtEnd = 0; // D, when endTested
	std::uint64_t ticks = 0;        // the original draw's time
	std::uint32_t passId = 0;       // the shader pass the draw ran under ([0x00B42E90], re/functions/0x007A9820.md)
	float radius = 0.0f;            // the bound's
	Owner owner = Owner::kOther;
	char name[kNameWidth] = {};
};

// Why a draw without a pixel was or was not shown by its bound.
enum class NoPixelClass : std::uint8_t {
	kBoundHidden,     // the bound was hidden at the draw: a bound test finds it
	kBoundSeen,       // the bound was seen: the sphere is larger than what the geometry covers
	kNearPlane,       // the bound reaches the near plane: no bound test possible
	kNoBound,
	kOutsideFrustum,
	kViewport,
	kCount,
};

inline NoPixelClass ClassOf(const Sample& s) {
	switch (s.rect) {
		case RectKind::kTestable:
			return s.boundAtDraw == 0 ? NoPixelClass::kBoundHidden : NoPixelClass::kBoundSeen;
		case RectKind::kCrossesNear:
			return NoPixelClass::kNearPlane;
		case RectKind::kNoBound:
			return NoPixelClass::kNoBound;
		case RectKind::kOutsideFrustum:
			return NoPixelClass::kOutsideFrustum;
		case RectKind::kViewport:
			break;
	}
	return NoPixelClass::kViewport;
}

// Draws and their time, all and those without a pixel.
struct Tally {
	std::uint64_t draws = 0;
	std::uint64_t noPixel = 0;
	std::uint64_t ticks = 0;
	std::uint64_t ticksNoPixel = 0;

	void Add(const Sample& s) {
		++draws;
		ticks += s.ticks;
		if (s.drawPixels == 0) {
			++noPixel;
			ticksNoPixel += s.ticks;
		}
	}
};

// The draws by shader pass id.
constexpr std::uint32_t kPassRows = 64;
struct PassRow {
	std::uint32_t id = 0;
	Tally tally;
	std::uint64_t noPixelBoundSeen = 0;  // without a pixel though the bound was seen at the draw
	std::uint64_t hiddenAtEnd = 0;       // the bound hidden at the pass's end
};
struct PassTable {
	PassRow rows[kPassRows];
	std::uint32_t used = 0;
	Tally beyond;

	const PassRow* Find(std::uint32_t id) const {
		for (std::uint32_t i = 0; i < used; ++i) {
			if (rows[i].id == id) {
				return &rows[i];
			}
		}
		return nullptr;
	}
	void Add(const Sample& s) {
		PassRow* row = const_cast<PassRow*>(Find(s.passId));
		if (row == nullptr) {
			if (used == kPassRows) {
				beyond.Add(s);
				return;
			}
			row = &rows[used++];
			row->id = s.passId;
		}
		row->tally.Add(s);
		if (s.rect != RectKind::kTestable) {
			return;
		}
		if (s.drawPixels == 0 && s.boundAtDraw != 0) {
			++row->noPixelBoundSeen;
		}
		if (s.endTested && s.controlAtEnd != 0 && s.boundAtEnd == 0) {
			++row->hiddenAtEnd;
		}
	}
};

// The draws by the bound's radius: below 64, 256, 1024, 4096 units, and above.
constexpr int kRadiusBuckets = 5;
inline int RadiusBucket(float radius) {
	if (!(radius >= 64.0f)) {
		return 0;
	}
	return radius < 256.0f ? 1 : (radius < 1024.0f ? 2 : (radius < 4096.0f ? 3 : 4));
}

// The draws by geometry name, kind and owner.
constexpr std::uint32_t kNameRows = 192;
struct NameRow {
	char name[kNameWidth] = {};
	DrawKind kind = DrawKind::kShape;
	Owner owner = Owner::kOther;
	Tally tally;
};
struct NameTable {
	NameRow rows[kNameRows];
	std::uint32_t used = 0;
	Tally beyond;

	void Add(const Sample& s, Owner owner) {
		for (std::uint32_t i = 0; i < used; ++i) {
			if (rows[i].kind == s.kind && rows[i].owner == owner && std::memcmp(rows[i].name, s.name, kNameWidth) == 0) {
				rows[i].tally.Add(s);
				return;
			}
		}
		if (used == kNameRows) {
			beyond.Add(s);
			return;
		}
		NameRow& row = rows[used++];
		std::memcpy(row.name, s.name, kNameWidth);
		row.kind = s.kind;
		row.owner = owner;
		row.tally.Add(s);
	}
	// The row at `rank` among the rows with a draw without a pixel, ordered
	// by their time without a pixel (then by their place); null past them.
	const NameRow* Ranked(std::uint32_t rank) const {
		const NameRow* last = nullptr;
		for (std::uint32_t taken = 0; taken <= rank; ++taken) {
			const NameRow* best = nullptr;
			for (std::uint32_t i = 0; i < used; ++i) {
				const NameRow& row = rows[i];
				if (row.tally.noPixel == 0) {
					continue;
				}
				const bool afterLast = last == nullptr || row.tally.ticksNoPixel < last->tally.ticksNoPixel ||
				                       (row.tally.ticksNoPixel == last->tally.ticksNoPixel && &row > last);
				if (afterLast && (best == nullptr || row.tally.ticksNoPixel > best->tally.ticksNoPixel)) {
					best = &row;
				}
			}
			if (best == nullptr) {
				return nullptr;
			}
			last = best;
		}
		return last;
	}
};

// A draw's count and time in one class.
struct ClassTally {
	std::uint64_t draws = 0;
	std::uint64_t ticks = 0;
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
	ClassTally classes[static_cast<int>(DrawKind::kCount)][static_cast<int>(NoPixelClass::kCount)];
	PassTable passes;
	Tally radius[kRadiusBuckets];
	Tally owners[static_cast<int>(DrawKind::kCount)][static_cast<int>(Owner::kCount)];
	NameTable names;

	void Add(const Sample& s) {
		if (static_cast<int>(s.kind) >= static_cast<int>(DrawKind::kCount)) {
			return;
		}
		const Owner owner = static_cast<int>(s.owner) < static_cast<int>(Owner::kCount) ? s.owner : Owner::kOther;
		if (s.drawPixels == 0) {
			ClassTally& c = classes[static_cast<int>(s.kind)][static_cast<int>(ClassOf(s))];
			++c.draws;
			c.ticks += s.ticks;
		}
		passes.Add(s);
		radius[RadiusBucket(s.radius)].Add(s);
		owners[static_cast<int>(s.kind)][static_cast<int>(owner)].Add(s);
		names.Add(s, owner);
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


// A line written piece by piece; a piece that does not fit ends the line.
struct LineWriter {
	char* out;
	std::size_t capacity;
	int at = 0;

	void Text(const char* format, ...) {
		if (at < 0 || static_cast<std::size_t>(at) >= capacity) {
			return;
		}
		va_list arguments;
		va_start(arguments, format);
		const int written = std::vsnprintf(out + at, capacity - static_cast<std::size_t>(at), format, arguments);
		va_end(arguments);
		at = written < 0 ? -1 : at + written;
	}
	// "X.XXX ms" with the calibration, "N ticks" without one.
	void Time(std::uint64_t ticks, double ticksPerMillisecond) {
		if (ticksPerMillisecond > 0.0) {
			Text("%.3f ms", static_cast<double>(ticks) / ticksPerMillisecond);
		} else {
			Text("%llu ticks", static_cast<unsigned long long>(ticks));
		}
	}
	// "N draws, M without a pixel, time T, T without a pixel"
	void TallyText(const Tally& t, double ticksPerMillisecond) {
		Text("%llu draws, %llu without a pixel, time ", static_cast<unsigned long long>(t.draws), static_cast<unsigned long long>(t.noPixel));
		Time(t.ticks, ticksPerMillisecond);
		Text(", ");
		Time(t.ticksNoPixel, ticksPerMillisecond);
		Text(" without a pixel");
	}
};

// One kind's draws without a pixel by class, or nothing (0) when it has none.
inline int FormatClasses(const Stats& stats, DrawKind kind, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (static_cast<int>(kind) >= static_cast<int>(DrawKind::kCount)) {
		return 0;
	}
	const ClassTally* classes = stats.classes[static_cast<int>(kind)];
	std::uint64_t draws = 0;
	for (int c = 0; c < static_cast<int>(NoPixelClass::kCount); ++c) {
		draws += classes[c].draws;
	}
	if (draws == 0) {
		return 0;
	}
	static const char* const kClassNames[] = {"bound hidden", "bound seen", "near plane", "no bound", "outside the frustum", "viewport"};
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion no-pixel %s - ", KindName(kind));
	for (int c = 0; c < static_cast<int>(NoPixelClass::kCount); ++c) {
		line.Text("%s%s %llu (", c == 0 ? "" : ", ", kClassNames[c], static_cast<unsigned long long>(classes[c].draws));
		line.Time(classes[c].ticks, ticksPerMillisecond);
		line.Text(")");
	}
	return line.at;
}

// The pass table's row `index`, or nothing (0) past the used rows.
inline int FormatPass(const Stats& stats, std::uint32_t index, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (index >= stats.passes.used) {
		return 0;
	}
	const PassRow& row = stats.passes.rows[index];
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion pass 0x%X - %llu draws, %llu without a pixel (%llu with the bound seen), %llu hidden at the end, time ", row.id,
	          static_cast<unsigned long long>(row.tally.draws), static_cast<unsigned long long>(row.tally.noPixel),
	          static_cast<unsigned long long>(row.noPixelBoundSeen), static_cast<unsigned long long>(row.hiddenAtEnd));
	line.Time(row.tally.ticks, ticksPerMillisecond);
	line.Text(", ");
	line.Time(row.tally.ticksNoPixel, ticksPerMillisecond);
	line.Text(" without a pixel");
	return line.at;
}

inline int FormatPassesBeyond(const Stats& stats, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (stats.passes.beyond.draws == 0) {
		return 0;
	}
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion passes beyond the table - ");
	line.TallyText(stats.passes.beyond, ticksPerMillisecond);
	return line.at;
}

// The draws by the bound's radius, or nothing (0) without a draw.
inline int FormatRadius(const Stats& stats, double ticksPerMillisecond, char* out, std::size_t capacity) {
	std::uint64_t draws = 0;
	for (const Tally& t : stats.radius) {
		draws += t.draws;
	}
	if (draws == 0) {
		return 0;
	}
	static const char* const kBucketNames[kRadiusBuckets] = {"below 64", "below 256", "below 1024", "below 4096", "4096 and above"};
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion radius - ");
	for (int b = 0; b < kRadiusBuckets; ++b) {
		const Tally& t = stats.radius[b];
		line.Text("%s%s: %llu draws, %llu without a pixel (", b == 0 ? "" : "; ", kBucketNames[b], static_cast<unsigned long long>(t.draws),
		          static_cast<unsigned long long>(t.noPixel));
		line.Time(t.ticksNoPixel, ticksPerMillisecond);
		line.Text(" of ");
		line.Time(t.ticks, ticksPerMillisecond);
		line.Text(")");
	}
	return line.at;
}

// One kind's draws of one owner, or nothing (0) without a draw.
inline int FormatOwner(const Stats& stats, DrawKind kind, Owner owner, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (static_cast<int>(kind) >= static_cast<int>(DrawKind::kCount) || static_cast<int>(owner) >= static_cast<int>(Owner::kCount)) {
		return 0;
	}
	const Tally& t = stats.owners[static_cast<int>(kind)][static_cast<int>(owner)];
	if (t.draws == 0) {
		return 0;
	}
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion owner %s, %s - ", OwnerName(owner), KindName(kind));
	line.TallyText(t, ticksPerMillisecond);
	return line.at;
}

// The name table's row at `rank` by time without a pixel, or nothing (0)
// past the rows that have such a draw.
inline int FormatName(const Stats& stats, std::uint32_t rank, double ticksPerMillisecond, char* out, std::size_t capacity) {
	const NameRow* row = stats.names.Ranked(rank);
	if (row == nullptr) {
		return 0;
	}
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion name \"%s\" (%s, %s) - ", row->name, KindName(row->kind), OwnerName(row->owner));
	line.TallyText(row->tally, ticksPerMillisecond);
	return line.at;
}

inline int FormatNamesBeyond(const Stats& stats, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (stats.names.beyond.draws == 0) {
		return 0;
	}
	LineWriter line{out, capacity};
	line.Text("Probe: occlusion names beyond the table - ");
	line.TallyText(stats.names.beyond, ticksPerMillisecond);
	return line.at;
}

}  // namespace obng::plugin::occlusion
