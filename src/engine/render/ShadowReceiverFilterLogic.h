#pragma once
// The actor shadows' receiver filter - the decisions over plain values
// ([Route] ActorShadowReceiverMin / ActorShadowReceiverMaxGap in the plugin's
// ShadowReceiverFilter.cpp).
//
// An actor shadow light (+0xF4 set by 0x007D3370 from the caster registration
// 0x007C6C30, its caster node at +0x130) collects every lit geometry in its
// reach through AddGeometry 0x007D5790 (from its range walk 0x007D59E0); the
// geometry's shader property then holds the light, and its pass builder
// 0x0085AE20 adds one more render pass (0x177..0x17A) per such light. In the
// Market District those passes are 174 of the eye pass's 860 records a frame
// (docs/performance-backlog.md, L3's census).
//
// Here a geometry is not added to an actor shadow light when its world bound
// radius (+0x2C) is under `minRadius`, or when its bound sphere lies more
// than `maxGap` units from the caster's bound centre: it gets no receiver pass
// and shows no shadow of that actor (class C, on purpose). Lights that cast no
// shadow are not touched. A geometry added before keeps nothing: the light's
// collection drops the ones it did not find again at its end (0x007D6A40 ->
// RemoveGeometry), so the property loses the light and rebuilds its passes
// without it.
//
// 64-bit clean: plain values only.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::engine::render::shadow_receiver_filter {

struct Settings {
	float minRadius = 0.0f;  // units; 0 off
	float maxGap = 0.0f;     // units; 0 off
	bool alternate = false;  // on in every second summary window (a measurement)
	bool On() const { return minRadius > 0.0f || maxGap > 0.0f; }
};

inline float Clamp(float v, float top) {
	return v > 0.0f ? (v > top ? top : v) : 0.0f;  // NaN fails the compare: 0
}

inline Settings SettingsFrom(float minRadius, float maxGap, int alternate) {
	Settings s;
	s.minRadius = Clamp(minRadius, 1000.0f);
	s.maxGap = Clamp(maxGap, 100000.0f);
	s.alternate = alternate != 0;
	return s;
}

constexpr bool OnInWindow(const Settings& s, std::uint32_t window) {
	return s.On() && (!s.alternate || window % 2 == 0);
}

// The receiver's bound sphere from the caster's bound centre: the distance of
// the centres less the receiver's radius (negative with the caster inside).
inline float GapOf(const float caster[3], const float centre[3], float radius) {
	const float dx = centre[0] - caster[0];
	const float dy = centre[1] - caster[1];
	const float dz = centre[2] - caster[2];
	return std::sqrt(dx * dx + dy * dy + dz * dz) - radius;
}

// What AddGeometry is asked about.
struct Receiver {
	bool castsShadow = false;  // the light's +0xF4
	bool hasCaster = false;    // the light's +0x130
	float radius = 0.0f;       // the geometry's world bound radius
	float gap = 0.0f;          // GapOf the caster's centre
	bool skinned = false;      // the geometry's skin instance (+0xB8)
};

enum class Action : std::uint8_t { kAdd, kSkip, kOtherLight };

inline Action Decide(const Settings& s, const Receiver& r, bool onNow) {
	if (!r.castsShadow || !r.hasCaster) {
		return Action::kOtherLight;
	}
	if (!onNow || !s.On()) {
		return Action::kAdd;
	}
	if (s.minRadius > 0.0f && r.radius < s.minRadius) {
		return Action::kSkip;  // NaN: not under
	}
	if (s.maxGap > 0.0f && r.gap > s.maxGap) {
		return Action::kSkip;  // NaN: not beyond
	}
	return Action::kAdd;
}

constexpr std::uint32_t kBuckets = 7;
constexpr const char* kRadiusName[kBuckets] = {"<4", "4-8", "8-16", "16-32", "32-64", "64-128", ">=128"};
constexpr const char* kGapName[kBuckets] = {"<=0", "0-64", "64-128", "128-256", "256-512", "512-1024", ">=1024"};

inline std::uint32_t RadiusBucket(float radius) {
	const float edges[kBuckets - 1] = {4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f};
	for (std::uint32_t i = 0; i < kBuckets - 1; ++i) {
		if (radius < edges[i]) {
			return i;
		}
	}
	return kBuckets - 1;  // and NaN
}

inline std::uint32_t GapBucket(float gap) {
	if (gap <= 0.0f) {
		return 0;
	}
	const float edges[kBuckets - 2] = {64.0f, 128.0f, 256.0f, 512.0f, 1024.0f};
	for (std::uint32_t i = 0; i < kBuckets - 2; ++i) {
		if (gap < edges[i]) {
			return i + 1;
		}
	}
	return kBuckets - 1;  // and NaN
}

struct Counts {
	std::uint64_t calls = 0;
	std::uint64_t otherLights = 0;
	std::uint64_t skipped = 0;
	std::uint64_t byRadius[2][kBuckets] = {};  // [skinned][bucket], the shadow lights' calls, added or not
	std::uint64_t byGap[kBuckets] = {};

	void Note(const Receiver& r, Action a) {
		++calls;
		if (a == Action::kOtherLight) {
			++otherLights;
			return;
		}
		++byRadius[r.skinned ? 1 : 0][RadiusBucket(r.radius)];
		++byGap[GapBucket(r.gap)];
		if (a == Action::kSkip) {
			++skipped;
		}
	}
};

inline void FormatSummary(const Counts& c, const Settings& s, bool onNow, std::uint64_t frames, char* out, std::size_t size) {
	const double n = frames != 0 ? static_cast<double>(frames) : 1.0;
	int used = std::snprintf(out, size,
	                         "Route: actor shadow receivers (left out: radius under %.1f, beyond %.1f units of the caster; %s in this window%s) - %llu "
	                         "frames; AddGeometry %llu calls, %llu for other lights; left out %llu (%.2f a step); the shadow lights' receivers a step by "
	                         "radius, unskinned / skinned:",
	                         s.minRadius, s.maxGap, onNow ? "on" : "off", s.alternate ? ", alternating by window" : "", static_cast<unsigned long long>(frames),
	                         static_cast<unsigned long long>(c.calls), static_cast<unsigned long long>(c.otherLights),
	                         static_cast<unsigned long long>(c.skipped), static_cast<double>(c.skipped) / n);
	for (std::uint32_t b = 0; b < kBuckets && used > 0 && static_cast<std::size_t>(used) < size; ++b) {
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %.2f / %.2f;", kRadiusName[b],
		                      static_cast<double>(c.byRadius[0][b]) / n, static_cast<double>(c.byRadius[1][b]) / n);
	}
	if (used > 0 && static_cast<std::size_t>(used) < size) {
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " by gap:");
	}
	for (std::uint32_t b = 0; b < kBuckets && used > 0 && static_cast<std::size_t>(used) < size; ++b) {
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %.2f;", kGapName[b], static_cast<double>(c.byGap[b]) / n);
	}
}

}  // namespace obng::engine::render::shadow_receiver_filter
