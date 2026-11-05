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

// One world pass.
struct Pass {
	float yaw = 0.0f;
	float pitch = 0.0f;
	float camera[3] = {};
	std::uint32_t groups = 0;  // the Grass node's children
	Counts existing;           // the patches under the node
	Counts hidden;             // of them with the hidden bit
	Counts drawn;              // the grass geometries the pass drew
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
	return std::snprintf(out, capacity,
	                     "Probe: grass - heading %.1f pitch %.1f, camera %.0f %.0f %.0f; under the Grass node %u groups, %u patches %s farthest "
	                     "%.0f, with the hidden bit %u %s; drawn in %s %u %s farthest %.0f; qpc %llu, %s",
	                     static_cast<double>(pass.yaw), static_cast<double>(pass.pitch), static_cast<double>(pass.camera[0]),
	                     static_cast<double>(pass.camera[1]), static_cast<double>(pass.camera[2]), pass.groups, pass.existing.total, existing,
	                     static_cast<double>(pass.existing.farthest), pass.hidden.total, hidden,
	                     pass.perEyeData ? "the pass" : "the last 100 ms (no eye data: not a single-pass frame)", pass.drawn.total, drawn,
	                     static_cast<double>(pass.drawn.farthest), static_cast<unsigned long long>(qpc), clock);
}

}  // namespace obng::plugin::grass
