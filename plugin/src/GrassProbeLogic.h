#pragma once

// The grass probe's decisions ([Probe] Grass, GrassProbe.cpp), free of the
// engine (plugin/tests/GrassProbeLogicTest.cpp).
//
// The question: distant grass that goes and comes as the head turns (Nadi,
// Windows headset, single pass, 2026-11-05; not reproduced in the harness).
// Three things can take a patch of grass out of the picture, and the probe
// tells them apart per world pass:
//   - the build removed it: it no longer stands under the Grass node
//     ([0x00B36094]; the engine rebuilds the grass in a cone ahead of the
//     camera, ../OBVR/src/game/GrassCone.h has the reading);
//   - it stands there and is not drawn: the cull left it out, or its hidden
//     bit (NiAVObject +0x18 bit 0) is set;
//   - it is drawn and not seen: the shader fades it, which no count shows -
//     if the first two stay level while the grass goes, that is what is left.
// Each is counted by distance from the pass camera in bands of 1000 units
// (the Windows install fades grass from 6000 to 7000), next to the camera's
// heading. Cheap enough to leave on while playing: no Direct3D object, a few
// reads per patch and per draw.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::grass {

constexpr int kBands = 8;  // 0..1000, ..., 6000..7000, beyond

inline int Band(float distance) {
	if (!(distance >= 1000.0f)) {
		return 0;
	}
	const int band = static_cast<int>(distance / 1000.0f);
	return band >= kBands ? kBands - 1 : band;
}

struct Counts {
	std::uint32_t band[kBands] = {};
	std::uint32_t total = 0;
	float farthest = 0.0f;

	void Add(float distance) {
		++band[Band(distance)];
		++total;
		if (distance > farthest) {
			farthest = distance;
		}
	}
};

inline bool SameBands(const Counts& a, const Counts& b) {
	for (int i = 0; i < kBands; ++i) {
		if (a.band[i] != b.band[i]) {
			return false;
		}
	}
	return true;
}

// The view matrix of re/functions/0x00762640.md: the camera's direction is
// the third column (_13, _23, _33). Heading 0 is +y, 90 is +x - the game's
// compass, as OBVR's grass lines print it.
inline float YawDegrees(const float view[16]) {
	return std::atan2(view[2], view[6]) * 57.2957795f;
}

inline float PitchDegrees(const float view[16]) {
	const float up = view[10] > 1.0f ? 1.0f : (view[10] < -1.0f ? -1.0f : view[10]);
	return std::asin(up) * 57.2957795f;
}

// The angle between two headings, the short way round.
inline float TurnDegrees(float a, float b) {
	float turn = std::fabs(a - b);
	while (turn > 360.0f) {
		turn -= 360.0f;
	}
	return turn > 180.0f ? 360.0f - turn : turn;
}

// The fade the grass vertex shaders give a blade, computed from the
// constants the device holds at the draw. Read in the 24 GRASS20xx.vso of
// shader package 019 (vkd3d-compiler -x d3dbc -b d3d-asm): r0 =
// (dot(c9, p), dot(c10, p), dot(c11, p), dot(c12, p)) with p the blade's
// position (the instance's xyz from c20.., w = 1), length = sqrt(dp4(r0,
// r0)), near = sat((length - c5.x) / c5.y), far = sat((length - c5.z) /
// c5.w), oT5.w = near * (1 - far). The length is of the clip-space vector,
// not of the way to the camera: the distance is taken here from the same
// vector through the projection's two scales (clip.x / P11, clip.y / P22,
// clip.w - the view-space position, P11 and P22 the projection's _11 and
// _22), so the two can be set side by side.
struct Fade {
	float distance = 0.0f;  // from the camera, in units
	float length = 0.0f;    // what the shader measures instead
	float fade = 0.0f;      // 0 gone .. 1 drawn
	float ndcX = 0.0f;      // -1 the picture's left edge .. 1 its right
	float ndcY = 0.0f;
	bool inView = false;    // in front of the camera and inside the picture
};

inline float Saturate(float v) {
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// (length - start) / range saturated, a range of zero a step at the start.
inline float Ramp(float length, float start, float range) {
	if (range == 0.0f) {
		return length > start ? 1.0f : 0.0f;
	}
	return Saturate((length - start) / range);
}

inline Fade ShaderFade(const float alphaParam[4], const float rows[16], const float position[3], float p11, float p22) {
	float clip[4];
	for (int i = 0; i < 4; ++i) {
		clip[i] = rows[i * 4] * position[0] + rows[i * 4 + 1] * position[1] + rows[i * 4 + 2] * position[2] + rows[i * 4 + 3];
	}
	Fade out;
	out.length = std::sqrt(clip[0] * clip[0] + clip[1] * clip[1] + clip[2] * clip[2] + clip[3] * clip[3]);
	out.fade = Ramp(out.length, alphaParam[0], alphaParam[1]) * (1.0f - Ramp(out.length, alphaParam[2], alphaParam[3]));
	const float x = p11 != 0.0f ? clip[0] / p11 : 0.0f;
	const float y = p22 != 0.0f ? clip[1] / p22 : 0.0f;
	out.distance = std::sqrt(x * x + y * y + clip[3] * clip[3]);
	if (clip[3] > 0.0f) {
		out.ndcX = clip[0] / clip[3];
		out.ndcY = clip[1] / clip[3];
		out.inView = std::fabs(out.ndcX) <= 1.0f && std::fabs(out.ndcY) <= 1.0f;
	}
	return out;
}

// The part of the view by the picture's half-width: 0 its middle third,
// 1 between, 2 the outer third.
inline int ViewPart(float ndcX) {
	const float a = std::fabs(ndcX);
	return a <= 1.0f / 3.0f ? 0 : (a <= 2.0f / 3.0f ? 1 : 2);
}

struct FadeTable {
	struct Cell {
		std::uint32_t count = 0;
		float sum = 0.0f;
	};
	Cell cell[3][kBands] = {};
	std::uint32_t total = 0;

	// A sample outside the view is left out.
	void Add(const Fade& f) {
		if (!f.inView) {
			return;
		}
		Cell& c = cell[ViewPart(f.ndcX)][Band(f.distance)];
		++c.count;
		c.sum += f.fade;
		++total;
	}
};

// "middle [- - 3:0.50 ...] between [...] edge [...]": per distance band the
// samples and their mean fade, "-" for none.
inline int FormatFade(const FadeTable& table, char* out, std::size_t capacity) {
	static constexpr const char* kNames[3] = {"middle", "between", "edge"};
	int used = 0;
	for (int part = 0; part < 3; ++part) {
		int n = std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "%s%s [", part != 0 ? " " : "", kNames[part]);
		if (n < 0 || static_cast<std::size_t>(used + n) >= capacity) {
			return used;
		}
		used += n;
		for (int band = 0; band < kBands; ++band) {
			const FadeTable::Cell& c = table.cell[part][band];
			n = c.count == 0 ? std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "%s-", band != 0 ? " " : "")
			                 : std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "%s%u:%.2f", band != 0 ? " " : "", c.count,
			                                 static_cast<double>(c.sum / static_cast<float>(c.count)));
			if (n < 0 || static_cast<std::size_t>(used + n) >= capacity) {
				return used;
			}
			used += n;
		}
		n = std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "]");
		if (n < 0 || static_cast<std::size_t>(used + n) >= capacity) {
			return used;
		}
		used += n;
	}
	return used;
}

// One world pass.
struct Pass {
	float yaw = 0.0f;
	float pitch = 0.0f;
	float camera[3] = {};
	std::uint32_t groups = 0;  // the Grass node's children (patches, or groups of patches)
	Counts existing;           // the patches under the node
	Counts hidden;             // of them with the hidden bit
	Counts drawn;              // the grass geometries the pass drew
	FadeTable fade;            // the shader's fade at the blade positions the grass draws carried
	bool perEyeData = true;    // false: no eye data, the draws are a time window's
};

constexpr float kTurnDegrees = 3.0f;
constexpr std::uint32_t kLeastMs = 100;
constexpr std::uint32_t kLongestMs = 2000;

// Decides which passes are written: one that differs from the last written
// one (the bands of any of the three counts, or a turn of kTurnDegrees), not
// more often than every kLeastMs, and one every kLongestMs whatever happens.
struct Writer {
	Pass last;
	std::uint64_t lastMs = 0;
	bool any = false;

	bool Due(const Pass& pass, std::uint64_t nowMs) const {
		if (!any) {
			return true;
		}
		const std::uint64_t since = nowMs - lastMs;
		if (since >= kLongestMs) {
			return true;
		}
		if (since < kLeastMs) {
			return false;
		}
		return !SameBands(pass.existing, last.existing) || !SameBands(pass.hidden, last.hidden) || !SameBands(pass.drawn, last.drawn) ||
		       TurnDegrees(pass.yaw, last.yaw) >= kTurnDegrees;
	}
	void Wrote(const Pass& pass, std::uint64_t nowMs) {
		last = pass;
		lastMs = nowMs;
		any = true;
	}
};

inline int FormatBands(const Counts& counts, char* out, std::size_t capacity) {
	return std::snprintf(out, capacity, "[%u %u %u %u %u %u %u %u]", counts.band[0], counts.band[1], counts.band[2], counts.band[3],
	                     counts.band[4], counts.band[5], counts.band[6], counts.band[7]);
}

// `clock` is the wall clock's time of day, so a line can be found from "it
// happened around a quarter past nine".
inline int Format(const Pass& pass, std::uint64_t qpc, const char* clock, char* out, std::size_t capacity) {
	char existing[80];
	char hidden[80];
	char drawn[80];
	FormatBands(pass.existing, existing, sizeof(existing));
	FormatBands(pass.hidden, hidden, sizeof(hidden));
	FormatBands(pass.drawn, drawn, sizeof(drawn));
	char fade[420] = "";
	if (pass.fade.total != 0) {
		const int lead = std::snprintf(fade, sizeof(fade), "; the shader's fade at the blade positions of the grass draws (samples:mean per band) ");
		FormatFade(pass.fade, fade + lead, sizeof(fade) - static_cast<std::size_t>(lead));
	}
	return std::snprintf(out, capacity,
	                     "Probe: grass - heading %.1f pitch %.1f, camera %.0f %.0f %.0f; under the Grass node %u children, %u patches %s farthest "
	                     "%.0f, with the hidden bit %u %s; drawn in %s %u %s farthest %.0f%s; qpc %llu, %s",
	                     static_cast<double>(pass.yaw), static_cast<double>(pass.pitch), static_cast<double>(pass.camera[0]),
	                     static_cast<double>(pass.camera[1]), static_cast<double>(pass.camera[2]), pass.groups, pass.existing.total, existing,
	                     static_cast<double>(pass.existing.farthest), pass.hidden.total, hidden,
	                     pass.perEyeData ? "the pass" : "the last 100 ms (no eye data: not a single-pass frame)", pass.drawn.total, drawn,
	                     static_cast<double>(pass.drawn.farthest), fade, static_cast<unsigned long long>(qpc), clock);
}

}  // namespace obng::plugin::grass
