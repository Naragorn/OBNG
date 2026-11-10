#pragma once
// What an eye pass's draws have in common - two counts for two possible
// rebuilds, over plain values (the culler notes the draws, OcclusionCull.cpp).
//
// Same mesh: draws that share their model data (and their shader object)
// could be one instanced draw. The count is an upper bound: the same mesh
// and shader with different textures or lights still cannot be merged.
//
// Hidden together: when every draw below a scene node is hidden, the node
// could be left out before its draws are collected at all. Per level above
// the geometry (1 = its parent) the hidden draws are counted that sit below
// a node all of whose noted draws are hidden.
//
// 64-bit clean: handles are std::uint32_t.
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace obng::plugin::draw_groups {

inline constexpr unsigned kLevels = 4;

struct DrawNote {
	std::uint32_t data = 0;                // the geometry's model data
	std::uint32_t shader = 0;              // its shader object
	std::uint32_t ancestor[kLevels] = {};  // its parent, the parent's parent, ...; 0 where the chain ends
	bool hidden = false;                   // the culler left it out
	bool skinned = false;
};

struct PassCounts {
	std::uint32_t draws = 0;             // noted
	std::uint32_t hidden = 0;
	std::uint32_t drawn = 0;             // noted and not hidden
	std::uint32_t drawnSkinned = 0;
	std::uint32_t meshes = 0;            // distinct model data among the drawn, unskinned
	std::uint32_t meshAndShader = 0;     // distinct (model data, shader) among them
	std::uint32_t inGroupsOfTwo = 0;     // drawn unskinned draws whose (data, shader) another shares
	std::uint32_t inGroupsOfFour = 0;    // ... that at least three others share
	std::uint32_t largestGroup = 0;
	std::uint32_t hiddenBelow[kLevels] = {};   // hidden draws below a node (at that level) with every noted draw hidden and at least two of them
	std::uint32_t hiddenNodes[kLevels] = {};   // such nodes
	std::uint32_t nodes[kLevels] = {};         // nodes at that level with any noted draw
};

inline PassCounts Count(const std::vector<DrawNote>& notes) {
	PassCounts c;
	std::unordered_map<std::uint64_t, std::uint32_t> pairs;
	std::unordered_map<std::uint32_t, std::uint32_t> meshes;
	for (const DrawNote& n : notes) {
		++c.draws;
		if (n.hidden) {
			++c.hidden;
			continue;
		}
		++c.drawn;
		if (n.skinned) {
			++c.drawnSkinned;
			continue;
		}
		++meshes[n.data];
		++pairs[(static_cast<std::uint64_t>(n.data) << 32) | n.shader];
	}
	c.meshes = static_cast<std::uint32_t>(meshes.size());
	c.meshAndShader = static_cast<std::uint32_t>(pairs.size());
	for (const auto& [key, count] : pairs) {
		if (count >= 2) {
			c.inGroupsOfTwo += count;
		}
		if (count >= 4) {
			c.inGroupsOfFour += count;
		}
		c.largestGroup = count > c.largestGroup ? count : c.largestGroup;
	}
	for (unsigned level = 0; level < kLevels; ++level) {
		struct Below {
			std::uint32_t all = 0;
			std::uint32_t hidden = 0;
		};
		std::unordered_map<std::uint32_t, Below> nodes;
		for (const DrawNote& n : notes) {
			if (n.ancestor[level] == 0) {
				continue;
			}
			Below& b = nodes[n.ancestor[level]];
			++b.all;
			b.hidden += n.hidden ? 1u : 0u;
		}
		c.nodes[level] = static_cast<std::uint32_t>(nodes.size());
		for (const auto& [node, b] : nodes) {
			if (b.all >= 2 && b.hidden == b.all) {
				c.hiddenBelow[level] += b.hidden;
				++c.hiddenNodes[level];
			}
		}
	}
	return c;
}

struct Totals {
	std::uint64_t passes = 0;
	std::uint64_t draws = 0, hidden = 0, drawn = 0, drawnSkinned = 0, meshes = 0, meshAndShader = 0, inTwo = 0, inFour = 0;
	std::uint32_t largest = 0;
	std::uint64_t hiddenBelow[kLevels] = {}, hiddenNodes[kLevels] = {}, nodes[kLevels] = {};
	void Add(const PassCounts& c) {
		++passes;
		draws += c.draws;
		hidden += c.hidden;
		drawn += c.drawn;
		drawnSkinned += c.drawnSkinned;
		meshes += c.meshes;
		meshAndShader += c.meshAndShader;
		inTwo += c.inGroupsOfTwo;
		inFour += c.inGroupsOfFour;
		largest = c.largestGroup > largest ? c.largestGroup : largest;
		for (unsigned i = 0; i < kLevels; ++i) {
			hiddenBelow[i] += c.hiddenBelow[i];
			hiddenNodes[i] += c.hiddenNodes[i];
			nodes[i] += c.nodes[i];
		}
	}
};

// Which scene nodes are worth a test of their own: a node below which every
// draw asked about in its last pass was hidden (two or more), or that was
// left out itself not long ago (its draws are then no longer asked about,
// so the first rule alone would forget it). The culler notes each newly
// asked draw at its ancestors (Note), asks at the cull walk (Candidate) and
// says when it left a node out (LeftOut). Passes are numbered by the
// culler's frame counter; a note older than maxAge passes says nothing.
class NodeBook {
public:
	static constexpr std::uint32_t kSlots = 4096;  // a power of two
	static constexpr std::uint32_t kProbe = 16;
	static constexpr std::uint32_t kStale = 64;    // passes after which a slot may be taken over

	void Note(std::uint32_t node, std::uint32_t pass, bool hidden) {
		Slot* s = Claim(node, pass);
		if (s == nullptr) {
			return;
		}
		if (s->pass != pass) {
			s->pass = pass;
			s->all = 0;
			s->hidden = 0;
		}
		s->all += s->all < 0xFFFFu ? 1u : 0u;
		s->hidden += hidden && s->hidden < 0xFFFFu ? 1u : 0u;
	}

	void LeftOut(std::uint32_t node, std::uint32_t pass) {
		if (Slot* s = Claim(node, pass)) {
			s->leftOutPass = pass;
			s->everLeftOut = true;
		}
	}

	bool Candidate(std::uint32_t node, std::uint32_t pass, std::uint32_t maxAge) const {
		const Slot* s = Find(node);
		if (s == nullptr) {
			return false;
		}
		if (s->everLeftOut && pass - s->leftOutPass <= maxAge) {
			return true;
		}
		return s->pass != pass && pass - s->pass <= maxAge && s->all >= 2 && s->hidden == s->all;
	}

private:
	struct Slot {
		std::uint32_t node = 0;
		std::uint32_t pass = 0;
		std::uint32_t leftOutPass = 0;
		std::uint16_t all = 0;
		std::uint16_t hidden = 0;
		bool everLeftOut = false;
		bool used = false;
	};
	static std::uint32_t Home(std::uint32_t node) { return (node * 2654435761u) >> 20; }
	const Slot* Find(std::uint32_t node) const {
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			const Slot& s = slots_[(Home(node) + i) & (kSlots - 1)];
			if (s.used && s.node == node) {
				return &s;
			}
		}
		return nullptr;
	}
	// The node's slot, a free one, or one not touched for kStale passes; null when the probe run is all in use.
	Slot* Claim(std::uint32_t node, std::uint32_t pass) {
		Slot* spare = nullptr;
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			Slot& s = slots_[(Home(node) + i) & (kSlots - 1)];
			if (s.used && s.node == node) {
				return &s;
			}
			if (spare == nullptr && (!s.used || (pass - s.pass > kStale && pass - s.leftOutPass > kStale))) {
				spare = &s;
			}
		}
		if (spare != nullptr) {
			*spare = Slot{};
			spare->node = node;
			spare->used = true;
			// A fresh slot has seen no pass: its pass must not read as "this one" or "a recent one".
			spare->pass = pass - kStale - 1;
			spare->leftOutPass = pass - kStale - 1;
		}
		return spare;
	}
	Slot slots_[kSlots];
};

}  // namespace obng::plugin::draw_groups
