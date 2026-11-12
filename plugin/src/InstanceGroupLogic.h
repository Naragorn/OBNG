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
#include <utility>
#include <vector>

namespace obng::plugin::instance_groups {

// What is compared within a group of one mesh and pass, word by word:
//   0..9  the property state's ten slots (by the property's type, knowledge/
//         shadow-light-list-remove.md: 0 alpha, 1 fog, 2 material, 3 renderer
//         specific, 4 shade / shader, 5 stencil, 6 texturing, 7 vertex
//         colour, 8 wireframe, 9 z-buffer);
//   10    the vtable of slot 4's property (the shader property's class);
//   11    a hash of the material's values (NiMaterialProperty +0x1C..+0x53:
//         ambient, diffuse, specular, emissive, shine, alpha - xOBSE
//         NiProperties.h);
//   12    the shader property's flags (+0x1C), 13 its alpha (+0x20);
//   14    a hash of the shader property's lights, order-free (the list at
//         +0x6C: head +0x70, count +0x78), 15 their count;
//   16    the pass's two programs (pass +0x44 and +0x58, re/functions/
//         0x0077A1F0.md) folded into one word.
constexpr int kPropertyWords = 17;
constexpr int kWordMaterial = 11;
constexpr int kWordShaderFlags = 12;
constexpr int kWordShaderAlpha = 13;
constexpr int kWordLights = 14;
constexpr int kWordLightCount = 15;
constexpr int kWordPrograms = 16;

struct Key {
	std::uint32_t data = 0;
	std::uint32_t pass = 0;
	std::uint32_t props = 0;    // a hash of the property state's contents
	std::uint32_t effects = 0;  // a hash of the effect state's contents
	std::uint32_t words[kPropertyWords] = {};
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
	// Within the groups by mesh and pass: the draws compared with their
	// group's first, and how often each property word differs.
	std::uint64_t compared = 0;
	std::uint64_t wordDiffers[kPropertyWords] = {};
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
			if (level == 0) {
				for (std::size_t m = i + 1; m < j; ++m) {
					++c.compared;
					for (int w = 0; w < kPropertyWords; ++w) {
						c.wordDiffers[w] += keys[m].words[w] != keys[i].words[w] ? 1u : 0u;
					}
				}
			}
			++c.groups[level];
			c.inFour[level] += size >= 4 ? size : 0;
			c.largest[level] = std::max(c.largest[level], size);
			i = j;
		}
	}
	return c;
}

// The pass objects of the draws that share their mesh and pass with another:
// how many there are and how few cover most of those draws - the number of
// shader programs an instanced path would need its own versions of.
struct PassCoverage {
	std::uint64_t grouped = 0;      // draws in a group of two or more
	std::uint64_t passes = 0;       // distinct pass objects among them
	std::uint64_t forEighty = 0;    // passes needed for 80 % of those draws
	std::uint64_t forNinety = 0;
	std::uint64_t topShare[3] = {}; // draws of the three largest passes
};

inline PassCoverage Coverage(std::vector<Key>& keys) {
	PassCoverage out;
	std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return detail::LessAt(a, b, 0); });
	std::vector<std::pair<std::uint32_t, std::uint64_t>> perPass;
	for (std::size_t i = 0; i < keys.size();) {
		std::size_t j = i + 1;
		while (j < keys.size() && detail::SameAt(keys[i], keys[j], 0)) {
			++j;
		}
		if (j - i >= 2) {
			out.grouped += j - i;
			bool found = false;
			for (auto& p : perPass) {
				if (p.first == keys[i].pass) {
					p.second += j - i;
					found = true;
					break;
				}
			}
			if (!found) {
				perPass.emplace_back(keys[i].pass, j - i);
			}
		}
		i = j;
	}
	std::sort(perPass.begin(), perPass.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	out.passes = perPass.size();
	std::uint64_t running = 0;
	for (std::size_t k = 0; k < perPass.size(); ++k) {
		running += perPass[k].second;
		if (k < 3) {
			out.topShare[k] = perPass[k].second;
		}
		if (out.forEighty == 0 && running * 10 >= out.grouped * 8) {
			out.forEighty = k + 1;
		}
		if (out.forNinety == 0 && running * 10 >= out.grouped * 9) {
			out.forNinety = k + 1;
		}
	}
	return out;
}

struct Totals {
	std::uint64_t passes = 0;
	Counts sum;
	PassCoverage coverage;  // summed over the noted passes; forEighty / forNinety / passes the largest seen
	void AddCoverage(const PassCoverage& c) {
		coverage.grouped += c.grouped;
		coverage.passes = std::max(coverage.passes, c.passes);
		coverage.forEighty = std::max(coverage.forEighty, c.forEighty);
		coverage.forNinety = std::max(coverage.forNinety, c.forNinety);
		for (int k = 0; k < 3; ++k) {
			coverage.topShare[k] += c.topShare[k];
		}
	}
	void Add(const Counts& c) {
		++passes;
		sum.draws += c.draws;
		for (int level = 0; level < kLevels; ++level) {
			sum.groups[level] += c.groups[level];
			sum.inFour[level] += c.inFour[level];
			sum.largest[level] = std::max(sum.largest[level], c.largest[level]);
		}
		sum.compared += c.compared;
		for (int w = 0; w < kPropertyWords; ++w) {
			sum.wordDiffers[w] += c.wordDiffers[w];
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
	if (at >= 0 && static_cast<std::size_t>(at) < capacity && t.sum.compared != 0) {
		at += std::snprintf(out + at, capacity - at, " within the mesh groups %llu draws compared with their group's first, the word differing in %%:",
		                    static_cast<unsigned long long>(t.sum.compared));
		for (int w = 0; w < kPropertyWords && at >= 0 && static_cast<std::size_t>(at) < capacity; ++w) {
			at += std::snprintf(out + at, capacity - at, " %d:%.0f", w, 100.0 * static_cast<double>(t.sum.wordDiffers[w]) / static_cast<double>(t.sum.compared));
		}
	}
	if (at >= 0 && static_cast<std::size_t>(at) < capacity && t.coverage.grouped != 0) {
		const double g = static_cast<double>(t.coverage.grouped);
		at += std::snprintf(out + at, capacity - at,
		                    "; the grouped draws' pass objects: up to %llu a pass, %llu cover 80 %% and %llu cover 90 %% of them, the three largest %.0f / %.0f / %.0f %%",
		                    static_cast<unsigned long long>(t.coverage.passes), static_cast<unsigned long long>(t.coverage.forEighty),
		                    static_cast<unsigned long long>(t.coverage.forNinety), 100.0 * static_cast<double>(t.coverage.topShare[0]) / g,
		                    100.0 * static_cast<double>(t.coverage.topShare[1]) / g, 100.0 * static_cast<double>(t.coverage.topShare[2]) / g);
	}
	return at;
}

}  // namespace obng::plugin::instance_groups
