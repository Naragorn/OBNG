#pragma once

// The occlusion culler's guards (OcclusionCullGuard.cpp), free of the engine
// so every flow has a test (plugin/tests/OcclusionCullGuardLogicTest.cpp):
//
// - Which object an address holds. The verdict table is keyed by the
//   geometry's address; the engine's allocator gives a freed block to the next
//   request of its size, so the geometry made in the frame after a cell change
//   may sit where a hidden one sat. Identity() names the object by what a new
//   one at the address would not share; Decide (OcclusionCullLogic.h) drops a
//   verdict whose identity is another.
// - What hangs under the far LOD's root ([Route] OcclusionCullLod).
// - What the culler did in the passes after the player's cell changed - the
//   moment Nadi saw the far land flicker (Windows, 2026-11-13).

#include "OcclusionCullLogic.h"

namespace obng::plugin::occlusion_cull::guard {

// ---- The object at an address ----------------------------------------------------

// The object's name (NiObjectNET +0x08, a pointer into the engine's string
// table), its parent, for a geometry its model data, and where it is. The
// place is needed: the cells' nodes have no name and one parent, and the
// copies of a mesh share name and data - a freed cell's subtree and the next
// one made can come back at the same addresses in the same arrangement. A new
// object that shares all four stands at the same place in the scene graph and
// in the world with the same mesh; that one is taken for the old (class C:
// not told apart). Never 0, which means "not said".
constexpr std::uint32_t Identity(std::uint32_t name, std::uint32_t parent, std::uint32_t data, std::uint32_t place = 0) {
	std::uint32_t h = name * 2654435761u;
	h = (h ^ (h >> 15) ^ parent) * 2246822519u;
	h = (h ^ (h >> 13) ^ data) * 3266489917u;
	h = (h ^ (h >> 14) ^ place) * 668265263u;
	h ^= h >> 16;
	return h == 0 ? 1u : h;
}

// Where a bound's centre lies, in squares of kPlaceUnits (a quarter of a
// cell's side): a cell four squares on is another place, an actor who walks
// changes his about every ten seconds - one frame drawn untested then.
constexpr float kPlaceUnits = 1024.0f;
constexpr std::int32_t SquareOf(float v) {
	const float scaled = v / kPlaceUnits;
	const std::int32_t whole = static_cast<std::int32_t>(scaled);
	return scaled < static_cast<float>(whole) ? whole - 1 : whole;  // the floor
}
constexpr std::uint32_t Place(float x, float y) {
	return static_cast<std::uint32_t>(SquareOf(x)) * 73856093u ^ static_cast<std::uint32_t>(SquareOf(y)) * 19349663u;
}

// ---- The far LOD ------------------------------------------------------------------

// A land LOD shape hangs four parents under "LODRoot" at most (the shape, its
// file's root, "LandLOD", "LODRoot"; a distant tree one more: "LOD Trees"
// under "DistantRefLOD"; read 0x00406950).
constexpr unsigned kLodLevels = 6;

// Whether `root` is among the object's first `levels` parents.
template <class ParentOf>
bool UnderRoot(std::uint32_t object, std::uint32_t root, ParentOf&& parentOf, unsigned levels = kLodLevels) {
	if (object == 0 || root == 0) {
		return false;
	}
	for (unsigned i = 0; i < levels; ++i) {
		object = parentOf(object);
		if (object == 0) {
			return false;
		}
		if (object == root) {
			return true;
		}
	}
	return false;
}

constexpr LodRule LodRuleFromIni(int value, LodRule fallback) {
	return value == 0 ? LodRule::kNeverSkip : value == 1 ? LodRule::kMaySkip : value == 2 ? LodRule::kCount : fallback;
}

// Whether a far LOD piece gets a test: not when it is drawn whatever comes back.
constexpr bool LodTested(LodRule rule) {
	return rule != LodRule::kNeverSkip;
}

constexpr const char* LodRuleName(LodRule rule) {
	return rule == LodRule::kNeverSkip ? "never left out" : rule == LodRule::kMaySkip ? "left out like the rest" : "drawn and counted";
}

// ---- The summary's guards line ------------------------------------------------------

inline int FormatGuards(const Stats& s, LodRule rule, char* out, std::size_t capacity) {
	oc::LineWriter line{out, capacity};
	const GuardCounts& g = s.guard;
	const double passes = s.eyePasses == 0 ? 1.0 : static_cast<double>(s.eyePasses);
	line.Text("Route: occlusion cull guards - %llu verdicts dropped for another object at the address (%llu of them hidden within the age: "
	          "the new object would have been left out), %llu results for a gone object dropped; far LOD (%s): %.1f asked a pass, %llu left out, "
	          "%llu drawn against a hidden verdict, %llu left out and seen by their own pass's test",
	          static_cast<unsigned long long>(g.identityResets), static_cast<unsigned long long>(g.identityResetsHidden),
	          static_cast<unsigned long long>(g.staleResults), LodRuleName(rule), static_cast<double>(g.lodAsked) / passes,
	          static_cast<unsigned long long>(g.lodSkipped), static_cast<unsigned long long>(g.lodKept),
	          static_cast<unsigned long long>(g.lodUnconfirmed));
	return line.at;
}

// ---- The passes after a cell change -------------------------------------------------

// Counts that only grow (the summary's Stats are reset every window).
struct Totals {
	std::uint64_t passes = 0;
	std::uint64_t draws = 0;
	std::uint64_t skipped = 0;
	std::uint64_t unconfirmed = 0;  // skips whose own pass's test saw the bound
	std::uint64_t lodSkipped = 0;
	std::uint64_t lodKept = 0;
	std::uint64_t lodUnconfirmed = 0;
	std::uint64_t identityResets = 0;
	std::uint64_t identityResetsHidden = 0;

	Totals& operator+=(const Totals& o) {
		passes += o.passes;
		draws += o.draws;
		skipped += o.skipped;
		unconfirmed += o.unconfirmed;
		lodSkipped += o.lodSkipped;
		lodKept += o.lodKept;
		lodUnconfirmed += o.lodUnconfirmed;
		identityResets += o.identityResets;
		identityResetsHidden += o.identityResetsHidden;
		return *this;
	}
	Totals Since(const Totals& then) const {
		Totals d;
		d.passes = passes - then.passes;
		d.draws = draws - then.draws;
		d.skipped = skipped - then.skipped;
		d.unconfirmed = unconfirmed - then.unconfirmed;
		d.lodSkipped = lodSkipped - then.lodSkipped;
		d.lodKept = lodKept - then.lodKept;
		d.lodUnconfirmed = lodUnconfirmed - then.lodUnconfirmed;
		d.identityResets = identityResets - then.identityResets;
		d.identityResetsHidden = identityResetsHidden - then.identityResetsHidden;
		return d;
	}
};

inline Totals TotalsOf(const Stats& s) {
	Totals t;
	t.passes = s.eyePasses;
	t.draws = s.Draws();
	t.skipped = s.Skipped();
	t.unconfirmed = s.skipsUnconfirmed;
	t.lodSkipped = s.guard.lodSkipped;
	t.lodKept = s.guard.lodKept;
	t.lodUnconfirmed = s.guard.lodUnconfirmed;
	t.identityResets = s.guard.identityResets;
	t.identityResetsHidden = s.guard.identityResetsHidden;
	return t;
}

constexpr std::uint32_t kWindowPasses = 32;  // the passes counted after a change: the verdicts' delay several times over
constexpr std::uint32_t kWindowLines = 64;   // windows written a session

// The player's cell from pass to pass. A change opens a window of
// kWindowPasses passes (a further change inside it is counted, the window
// runs on); Pass() returns true once when a window closes, with what the
// culler did inside it. The first cell seen and a null cell (a load) open
// nothing.
class CellWatch {
public:
	struct Window {
		std::uint32_t frame = 0;    // the pass the change was seen at
		std::uint32_t changes = 0;  // cell changes inside the window
		Totals counts;
	};

	bool Pass(std::uint32_t cell, std::uint32_t frame, const Totals& now, Window* out) {
		bool closed = false;
		if (open_ && frame - window_.frame >= kWindowPasses) {
			*out = window_;
			out->counts = now.Since(begin_);
			open_ = false;
			closed = true;
		}
		if (cell != 0 && cell_ != 0 && cell != cell_) {
			if (!open_) {
				open_ = true;
				window_ = Window{};
				window_.frame = frame;
				begin_ = now;
			}
			++window_.changes;
		}
		if (cell != 0) {
			cell_ = cell;
		}
		return closed;
	}

private:
	std::uint32_t cell_ = 0;
	bool open_ = false;
	Window window_;
	Totals begin_;
};

inline int FormatCellWindow(const CellWatch::Window& w, char* out, std::size_t capacity) {
	oc::LineWriter line{out, capacity};
	const Totals& c = w.counts;
	line.Text("Route: occlusion cull after a cell change - pass %u, %u change(s), the %llu passes after it: %llu draws, %llu left out, %llu of "
	          "them seen by their own pass's test; far LOD %llu left out (%llu seen), %llu drawn against a hidden verdict; %llu verdicts "
	          "dropped for another object at the address (%llu hidden within the age)",
	          w.frame, w.changes, static_cast<unsigned long long>(c.passes), static_cast<unsigned long long>(c.draws),
	          static_cast<unsigned long long>(c.skipped), static_cast<unsigned long long>(c.unconfirmed),
	          static_cast<unsigned long long>(c.lodSkipped), static_cast<unsigned long long>(c.lodUnconfirmed),
	          static_cast<unsigned long long>(c.lodKept), static_cast<unsigned long long>(c.identityResets),
	          static_cast<unsigned long long>(c.identityResetsHidden));
	return line.at;
}

}  // namespace obng::plugin::occlusion_cull::guard
