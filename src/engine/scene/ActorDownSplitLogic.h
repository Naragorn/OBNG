#pragma once
// A skeleton's down pass in two parts, so that the first can run on a worker
// thread ([Route] ActorThreads in the plugin's ActorThreads.cpp;
// docs/subsystems/actor-batch.md "The split").
//
// The flat run (SceneUpdateLogic.h RunForward) does per entry, in the order's
// order: the controllers, the world transform, the collision object's update,
// the geometry's bound; an Other entry's whole subtree goes to the engine.
// The collision update is the one step that reaches outside the skeleton (the
// Havok world, a global list) and it may write its node from the Havok body -
// which the node's children then depend on (measured 2026-11-12: 13-45 % of
// the hard sets change their node). An Other subtree is engine code not read
// for threads (a FaceGen head, a light, a particle system).
//
//   RunWorker  the part free of both: per entry the controllers the port
//              lets a worker run (it holds back every class not read for
//              threads), the world transform and a plain geometry's bound.
//              It notes the entries that have something left (held
//              controllers, a collision object, an Other entry, a geometry
//              whose bound the engine computes) and keeps each plain
//              geometry's bound and flags from before.
//   RunSweep   the rest, on the loop's thread: the noted entries in the
//              order's order - the held controllers, the world composed
//              again after them, the collision update. As long as none of
//              that changed its node's world nothing else is touched. From
//              the first change on every entry is walked: a node below a
//              changed one gets its world composed again before its own
//              updates, a plain geometry its bound again from the kept bound
//              and flags - what the flat run computes for it, since its
//              inputs are now the flat run's.
//   The nodes' bounds (RunBounds) follow the sweep.
//
// What ends the same as the flat run, bit for bit (the test runs both over
// random trees): every world transform, every collision update's and Other
// entry's view of its own node and its parent, every bound and flags word -
// with two exceptions. Something that reads a LATER entry's world sees this
// frame's instead of the frame before's: an Other entry, a collision update,
// a controller, and a skinned geometry's bound taken again after a change
// when one of its bones comes after it in the order. And a held controller
// runs after the controllers of the later entries and of its own chain that
// a worker ran. Class B (another order). The collision updates, the Other
// entries and the held controllers keep the flat run's order among
// themselves.
//
// 64-bit clean: handles and plain values only.
#include "engine/scene/SceneUpdateLogic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace obng::engine::scene::actor_down_split {

using scene_update::Bound;
using scene_update::Entry;
using scene_update::Handle;
using scene_update::Kind;
using scene_update::kNoParent;
using scene_update::Order;
using scene_update::Transform;

// The action port (SceneUpdateLogic.h) and what the split reads beside it:
//   bool WorkerControllers(Handle, float time)  the controllers a worker may run; true when it held some back
//   void HeldControllers(Handle, float time)    those it held back
//   bool HasCollision(Handle)          +0xA8 set
//   Transform World(Handle)            +0x64
//   uint16_t Flags(Handle)             +0x18
//   void WriteFlags(Handle, uint16_t)  +0x18
template <class Port>
concept SplitPort = scene_update::ActionPort<Port> && requires(Port& p, Handle h, std::uint16_t f, float t) {
	{ p.WorkerControllers(h, t) } -> std::same_as<bool>;
	{ p.HeldControllers(h, t) };
	{ p.HasCollision(h) } -> std::same_as<bool>;
	{ p.World(h) } -> std::same_as<Transform>;
	{ p.Flags(h) } -> std::same_as<std::uint16_t>;
	{ p.WriteFlags(h, f) };
};

struct GeometryBefore {
	std::uint32_t entry = 0;
	Bound bound{};
	std::uint16_t flags = 0;
};

// What the worker's pass leaves for the sweep, one per skeleton; kept
// between frames so that its vectors are allocated once.
inline constexpr std::uint32_t kHeld = 0x80000000u;  // a stop with held controllers
inline constexpr std::uint32_t kEntry = 0x7FFFFFFFu;

struct Pending {
	std::vector<std::uint32_t> stops;      // entry indices, ascending; kHeld set on those with held controllers
	std::vector<GeometryBefore> geometry;  // the plain geometries, ascending by entry
	std::vector<std::uint8_t> marks;       // the sweep's: 1 a node whose world a collision update changed or that was composed again
	void Clear() {
		stops.clear();
		geometry.clear();
	}
};

struct SweepResult {
	std::uint32_t changed = 0;     // nodes whose world their held controllers or their collision update changed
	std::uint32_t recomposed = 0;  // worlds composed again below a changed node
	std::uint32_t rebound = 0;     // plain geometry bounds taken again
};

inline bool Left(const Order& order, std::size_t i) { return !order.unused.empty() && order.unused[i] != 0; }

template <SplitPort Port>
void RunWorker(const Order& order, Port& port, float time, bool controllers, Pending& pending) {
	pending.Clear();
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		if (Left(order, i)) {
			continue;
		}
		const Entry& e = order.entries[i];
		const std::uint32_t index = static_cast<std::uint32_t>(i);
		if (e.kind == Kind::Other) {
			pending.stops.push_back(index);
			continue;
		}
		const bool held = controllers && port.HasControllers(e.object) && port.WorkerControllers(e.object, time);
		Transform world;
		scene_update::UpdateWorld(port.ParentWorld(e.object), port.Local(e.object), world);
		port.WriteWorld(e.object, world);
		if (held || e.kind == Kind::Geometry || port.HasCollision(e.object)) {
			pending.stops.push_back(index | (held ? kHeld : 0u));
		}
		if (e.kind == Kind::GeometryPlain) {
			pending.geometry.push_back({index, port.ReadBound(e.object), port.Flags(e.object)});
			port.GeometryBoundSelf(e.object);
		}
	}
}

inline bool SameWorld(const Transform& a, const Transform& b) { return std::memcmp(&a, &b, sizeof(Transform)) == 0; }

// The loop's thread's part of one noted entry, a plain geometry's bound
// apart, in the flat run's order - the controllers, the world, the collision
// update: true when the node's world is not what the worker left.
template <SplitPort Port>
bool AtStop(const Entry& e, bool held, Port& port, float time, bool controllers) {
	if (e.kind == Kind::Other) {
		port.UpdateDownwardOriginal(e.object, time, controllers);
		return false;
	}
	const Transform before = port.World(e.object);
	if (held) {
		port.HeldControllers(e.object, time);
		Transform world;
		scene_update::UpdateWorld(port.ParentWorld(e.object), port.Local(e.object), world);
		port.WriteWorld(e.object, world);
	}
	port.AfterWorldData(e.object);
	const bool changed = !SameWorld(before, port.World(e.object));
	if (e.kind == Kind::Geometry) {
		port.UpdateWorldBound(e.object);
	}
	return changed;
}

// A plain geometry's bound again, from the bound and the bound's flag (bit 7
// of the flags word) the worker found.
template <SplitPort Port>
void BoundAgain(const Entry& e, Port& port, const GeometryBefore& before) {
	port.WriteBound(e.object, before.bound);
	port.WriteFlags(e.object, static_cast<std::uint16_t>((port.Flags(e.object) & ~0x80u) | (before.flags & 0x80u)));
	port.GeometryBoundSelf(e.object);
}

// Every entry after the stop `at` (an index into the stops), whose node's
// world changed.
template <SplitPort Port>
void WalkFrom(const Order& order, Port& port, float time, bool controllers, Pending& pending, std::size_t at, SweepResult& result) {
	const std::size_t from = pending.stops[at] & kEntry;
	std::size_t stop = at + 1;
	pending.marks.assign(order.entries.size(), 0);
	pending.marks[from] = 1;
	std::size_t geometry = 0;
	while (geometry < pending.geometry.size() && pending.geometry[geometry].entry < from) {
		++geometry;
	}
	for (std::size_t i = from; i < order.entries.size(); ++i) {
		if (Left(order, i)) {
			continue;
		}
		const Entry& e = order.entries[i];
		if (i != from) {
			const bool below = e.parent != kNoParent && pending.marks[e.parent] != 0;
			if (below && e.kind != Kind::Other) {
				Transform world;
				scene_update::UpdateWorld(port.ParentWorld(e.object), port.Local(e.object), world);
				port.WriteWorld(e.object, world);
				pending.marks[i] = 1;
				++result.recomposed;
			}
			if (stop < pending.stops.size() && (pending.stops[stop] & kEntry) == i) {
				if (AtStop(e, (pending.stops[stop] & kHeld) != 0, port, time, controllers)) {
					pending.marks[i] = 1;
					++result.changed;
				}
				++stop;
			}
		}
		if (e.kind == Kind::GeometryPlain) {
			BoundAgain(e, port, pending.geometry[geometry++]);
			++result.rebound;
		}
	}
}

template <SplitPort Port>
SweepResult RunSweep(const Order& order, Port& port, float time, bool controllers, Pending& pending) {
	SweepResult result;
	for (std::size_t at = 0; at < pending.stops.size(); ++at) {
		const std::uint32_t stop = pending.stops[at];
		if (AtStop(order.entries[stop & kEntry], (stop & kHeld) != 0, port, time, controllers)) {
			++result.changed;
			WalkFrom(order, port, time, controllers, pending, at, result);
			break;
		}
	}
	return result;
}

// ---- A skeleton that hangs below another of the batch -----------------------------------------
//
// A skeleton's root reads its parent's world. When that parent lies inside
// another skeleton of the same batch (a root below another root), the two
// down passes depend on each other and the one below is held back: it runs
// whole on the loop's thread in its place, after the workers. A chain longer
// than `depth` counts as below (not shown to be free).
//   Handle Parent(Handle)   +0x1C, 0 at the top
template <class Tree, class IsRoot>
bool HangsBelow(const Tree& tree, Handle root, IsRoot isRoot, std::uint32_t depth) {
	Handle at = tree.Parent(root);
	for (std::uint32_t i = 0; i < depth; ++i) {
		if (at == 0) {
			return false;
		}
		if (isRoot(at)) {
			return true;
		}
		at = tree.Parent(at);
	}
	return at != 0;
}

// ---- The counts of a summary window -----------------------------------------------------------

struct Counts {
	std::uint64_t batches[2] = {};     // [0] the flat run on the loop's thread, [1] in two parts
	std::uint64_t ticks[2] = {};       // the down phase, its sweeps and bounds included
	std::uint64_t workerTicks = 0;     // of ticks[1]: the workers' part (on threads its wall time)
	std::uint64_t skeletons = 0;       // run in two parts
	std::uint64_t held = 0;            // run whole on the loop's thread: they hang below another skeleton of the batch
	std::uint64_t stops = 0;
	std::uint64_t withChange = 0;      // skeletons the sweep had to walk
	std::uint64_t changed = 0;
	std::uint64_t recomposed = 0;
	std::uint64_t rebound = 0;
	std::uint64_t inconsistent = 0;    // the check: worlds that are not their parent's world times their local, without a collision object
};

inline void Add(Counts& c, const Pending& pending, const SweepResult& r) {
	++c.skeletons;
	c.stops += pending.stops.size();
	c.withChange += r.changed != 0 ? 1u : 0u;
	c.changed += r.changed;
	c.recomposed += r.recomposed;
	c.rebound += r.rebound;
}

inline double Per(std::uint64_t sum, std::uint64_t count) { return count != 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0; }

inline void FormatSummary(const Counts& c, std::uint32_t threads, bool check, double ticksPerMs, char* out, std::size_t size) {
	const double perMs = ticksPerMs > 0.0 ? ticksPerMs : 1.0;
	std::snprintf(out, size,
	              "Route: actors' down passes in two parts (%s%u workers) - %llu batches in parts: %.3f ms a batch (the workers' part %.3f), %.1f skeletons a "
	              "batch, %.2f held back; %llu batches flat on one thread: %.3f ms a batch; a skeleton: %.1f stops, %.3f changed nodes (%.1f %% of the "
	              "skeletons walked), %.2f worlds composed again, %.2f bounds taken again; inconsistent worlds %llu",
	              check ? "the check on one thread, " : "", threads, static_cast<unsigned long long>(c.batches[1]), Per(c.ticks[1], c.batches[1]) / perMs,
	              Per(c.workerTicks, c.batches[1]) / perMs, Per(c.skeletons, c.batches[1]), Per(c.held, c.batches[1]),
	              static_cast<unsigned long long>(c.batches[0]), Per(c.ticks[0], c.batches[0]) / perMs, Per(c.stops, c.skeletons), Per(c.changed, c.skeletons),
	              100.0 * Per(c.withChange, c.skeletons), Per(c.recomposed, c.skeletons), Per(c.rebound, c.skeletons),
	              static_cast<unsigned long long>(c.inconsistent));
}

}  // namespace obng::engine::scene::actor_down_split
