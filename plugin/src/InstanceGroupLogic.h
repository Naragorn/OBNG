#pragma once
// Which of an eye pass's draws could be one instanced draw - a count, over
// plain values ([Route] StereoDrawTimer's instance count, StereoDrawTimer.h).
//
// A draw is one call of a shader's pass setup (slot 12) in the whole draw: the
// geometry's model data, the pass object, the property state and the dynamic
// effect state. Three keys, each finer than the one before:
//   mesh    - the model data and the pass object: the same vertices drawn the
//             same way (the upper bound DrawGroupLogic.h counted on 2026-11-10);
//   props   - and the same property state contents (NiPropertyState, 0x30
//             bytes in xOBSE's NiNodes.h: ten dwords after the reference
//             count, read here as the property pointers - unverified): the same
//             textures and material;
//   effects - and the same dynamic effect state contents (NiDynamicEffectState,
//             0x20 bytes, the five dwords after its flag byte - their meaning
//             unverified): the same lights, as far as equal contents say so.
// Even the finest key does not make two draws one: the engine uploads each
// draw's lights in its model space (0x007C9140), so an instanced draw needs
// its own vertex shaders. The count only says how much there is to gain.
//
// 64-bit clean: every handle is a std::uint32_t.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace obng::plugin::instance_groups {

struct Key {
	std::uint32_t data = 0;
	std::uint32_t pass = 0;
	std::uint32_t props = 0;    // a hash of the property state's contents
	std::uint32_t effects = 0;  // a hash of the effect state's contents
};

// FNV-1a over dwords: equal contents give equal hashes (a collision can only
// merge two groups, never split one - the counts are upper bounds).
inline std::uint32_t HashWords(const std::uint32_t* words, std::size_t count) {
	std::uint32_t h = 2166136261u;
	for (std::size_t i = 0; i < count; ++i) {
		for (int b = 0; b < 4; ++b) {
			h ^= (words[i] >> (8 * b)) & 0xFFu;
			h *= 16777619u;
		}
	}
	return h;
}

constexpr int kLevels = 3;  // mesh, props, effects

struct Counts {
	std::uint64_t draws = 0;
	std::uint64_t groups[kLevels] = {};     // distinct keys at each level
	std::uint64_t inFour[kLevels] = {};     // draws in a group of four or more
	std::uint64_t largest[kLevels] = {};
};

namespace detail {
inline bool SameAt(const Key& a, const Key& b, int level) {
	return a.data == b.data && a.pass == b.pass && (level < 1 || a.props == b.props) && (level < 2 || a.effects == b.effects);
}
inline bool LessAt(const Key& a, const Key& b, int level) {
	if (a.data != b.data) {
		return a.data < b.data;
	}
	if (a.pass != b.pass) {
		return a.pass < b.pass;
	}
	if (level >= 1 && a.props != b.props) {
		return a.props < b.props;
	}
	return level >= 2 && a.effects < b.effects;
}
}  // namespace detail

// One pass's keys counted (the vector is sorted in place).
inline Counts Count(std::vector<Key>& keys) {
	Counts c;
	c.draws = keys.size();
	for (int level = 0; level < kLevels; ++level) {
		std::sort(keys.begin(), keys.end(), [level](const Key& a, const Key& b) { return detail::LessAt(a, b, level); });
		for (std::size_t i = 0; i < keys.size();) {
			std::size_t j = i + 1;
			while (j < keys.size() && detail::SameAt(keys[i], keys[j], level)) {
				++j;
			}
			const std::uint64_t size = j - i;
			++c.groups[level];
			c.inFour[level] += size >= 4 ? size : 0;
			c.largest[level] = std::max(c.largest[level], size);
			i = j;
		}
	}
	return c;
}

struct Totals {
	std::uint64_t passes = 0;
	Counts sum;
	void Add(const Counts& c) {
		++passes;
		sum.draws += c.draws;
		for (int level = 0; level < kLevels; ++level) {
			sum.groups[level] += c.groups[level];
			sum.inFour[level] += c.inFour[level];
			sum.largest[level] = std::max(sum.largest[level], c.largest[level]);
		}
	}
};

inline int Format(const Totals& t, char* out, std::size_t capacity) {
	if (t.passes == 0 || t.sum.draws == 0) {
		return std::snprintf(out, capacity, "Route: stereo draw instance groups - none noted");
	}
	const double per = 1.0 / static_cast<double>(t.passes);
	const double draws = static_cast<double>(t.sum.draws) * per;
	static const char* const kNames[kLevels] = {"by mesh and pass", "and properties", "and effect state"};
	int at = std::snprintf(out, capacity, "Route: stereo draw instance groups - %llu passes noted, %.0f pass setups a pass;",
	                       static_cast<unsigned long long>(t.passes), draws);
	for (int level = 0; level < kLevels && at >= 0 && static_cast<std::size_t>(at) < capacity; ++level) {
		const double groups = static_cast<double>(t.sum.groups[level]) * per;
		at += std::snprintf(out + at, capacity - at, " %s %.0f groups (%.0f %% fewer draws), %.0f draws in groups of four or more, the largest %llu;",
		                    kNames[level], groups, 100.0 * (draws - groups) / draws, static_cast<double>(t.sum.inFour[level]) * per,
		                    static_cast<unsigned long long>(t.sum.largest[level]));
	}
	return at;
}

}  // namespace obng::plugin::instance_groups
