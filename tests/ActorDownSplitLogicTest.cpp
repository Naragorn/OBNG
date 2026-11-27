// Every flow of a skeleton's down pass in two parts (src/engine/scene/
// ActorDownSplitLogic.h): the worker's pass and the sweep against the flat run
// they replace - the same world transforms, bounds, flags, Havok-side values
// and views bit for bit over random trees, and each flow of the sweep by hand.

#include "Check.h"
#include "engine/scene/ActorDownSplitLogic.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <vector>

using namespace obng::engine::scene::scene_update;
namespace split = obng::engine::scene::actor_down_split;

namespace {

enum Collision { kNone, kReads, kHardSet, kBlend, kWritesSame };

struct Object {
	Kind kind = Kind::Node;
	std::vector<Handle> slots;
	Handle parent = 0;
	bool controllers = false;  // a controller a worker may run: it writes the local
	bool heldMoves = false;    // a held controller that writes the local
	bool heldQuiet = false;    // a held controller that leaves the node alone
	Collision collision = kNone;
	Transform body{{1, 0, 0, 0, 1, 0, 0, 0, 1}, {7, 8, 9}, 2.0f};  // the Havok side
	Transform local{{1, 0, 0, 0, 1, 0, 0, 0, 1}, {1, 2, 3}, 1.0f};
	Transform world{};
	Bound bound{};
	Bound modelBound{{0, 0, 0}, 1.5f};
	std::uint16_t flags = 0x80;
	Handle bone = 0;  // a geometry whose bound follows another object's world (a skin's bone)
};

struct Seen {
	char what;
	Handle object;
	Transform world;
	bool operator==(const Seen& o) const { return what == o.what && object == o.object && std::memcmp(&world, &o.world, sizeof(Transform)) == 0; }
};

struct Tree {
	std::map<Handle, Object> objects;
	Kind KindOf(Handle h) const { return objects.at(h).kind; }
	std::uint32_t SlotCount(Handle h) const { return objects.at(h).kind == Kind::Node ? static_cast<std::uint32_t>(objects.at(h).slots.size()) : 0u; }
	Handle ChildAt(Handle h, std::uint32_t i) const { return objects.at(h).slots[i]; }
};

struct NoSkin {
	std::uint32_t BoneCount() const { return 0; }
	Bound BoneBound(std::uint32_t) const { return Bound{}; }
	Transform BoneWorld(std::uint32_t) const { return Transform{}; }
	Transform RootParentWorld() const { return Transform{}; }
	Transform SkinToRoot() const { return Transform{}; }
};

struct Port {
	Tree* tree;
	std::vector<Seen> seen;            // what each collision update and each Other entry found
	std::vector<Handle> controllerCalls;
	std::vector<Handle> engineBounds;  // UpdateWorldBound calls

	Object& At(Handle h) { return tree->objects.at(h); }
	const Object& At(Handle h) const { return tree->objects.at(h); }

	bool HasControllers(Handle h) const { return At(h).controllers || At(h).heldMoves || At(h).heldQuiet; }
	bool WorkerControllers(Handle h, float time) {
		if (At(h).controllers) {
			At(h).local.pos[1] += time;  // the animation: the local written
			controllerCalls.push_back(h);
		}
		return At(h).heldMoves || At(h).heldQuiet;
	}
	void HeldControllers(Handle h, float time) {
		if (At(h).heldMoves) {
			At(h).local.pos[2] += time;
			seen.push_back({'m', h, Transform{}});
		}
		if (At(h).heldQuiet) {
			seen.push_back({'q', h, Transform{}});
		}
	}
	// The flat run's: the whole chain.
	void UpdateObjectControllers(Handle h, float time) {
		WorkerControllers(h, time);
		HeldControllers(h, time);
	}
	const Transform* ParentWorld(Handle h) const { return At(h).parent == 0 ? nullptr : &At(At(h).parent).world; }
	Transform Local(Handle h) const { return At(h).local; }
	void WriteWorld(Handle h, const Transform& t) { At(h).world = t; }
	void AfterWorldData(Handle h) {
		Object& o = At(h);
		if (o.collision == kNone) {
			return;
		}
		seen.push_back({'c', h, o.world});
		switch (o.collision) {
			case kReads:
				o.body = o.world;  // the body keyed towards the node
				break;
			case kHardSet:
				o.world = o.body;  // the node from the body
				o.local = o.body;
				o.flags |= 0x01;   // and a flag of the node beside the bound's
				break;
			case kBlend:
				o.world.pos[0] = o.world.pos[0] * 0.5f + o.body.pos[0];  // depends on what the node held
				break;
			default:
				break;  // kWritesSame: the node written with what it holds
		}
	}
	void Bounds(Handle h) {
		Object& o = At(h);
		const Bound before = o.bound;
		TransformBound(o.modelBound, o.bone != 0 ? At(o.bone).world : o.world, o.bound);
		o.flags = GeometryBoundFlags(o.flags, before, o.bound);
	}
	void UpdateWorldBound(Handle h) {
		Bounds(h);
		engineBounds.push_back(h);
	}
	void GeometryBoundSelf(Handle h) { Bounds(h); }
	void UpdateDownwardOriginal(Handle h, float, bool) {
		Object& o = At(h);
		UpdateWorld(ParentWorld(h), o.local, o.world);
		seen.push_back({'o', h, o.world});
		o.bound = Bound{{o.world.pos[0] + 1, o.world.pos[1], o.world.pos[2]}, o.world.scale * 0.25f};
	}
	float BoundRadius(Handle h) const { return At(h).bound.radius; }
	void SetBoundRadius(Handle h, float r) { At(h).bound.radius = r; }
	void CopyBound(Handle to, Handle from) { At(to).bound = At(from).bound; }
	void MergeBound(Handle to, Handle from) { obng::engine::scene::scene_update::MergeBound(At(to).bound, At(from).bound); }
	Bound ReadBound(Handle h) const { return At(h).bound; }
	void WriteBound(Handle h, const Bound& b) { At(h).bound = b; }
	bool HasCollision(Handle h) const { return At(h).collision != kNone; }
	Transform World(Handle h) const { return At(h).world; }
	std::uint16_t Flags(Handle h) const { return At(h).flags; }
	void WriteFlags(Handle h, std::uint16_t f) { At(h).flags = f; }
};

void Link(Tree& t) {
	for (auto& [h, o] : t.objects) {
		for (Handle c : o.slots) {
			if (c != 0) {
				t.objects[c].parent = h;
			}
		}
	}
}

bool SameObjects(const Tree& a, const Tree& b) {
	for (const auto& [h, o] : a.objects) {
		const Object& p = b.objects.at(h);
		if (std::memcmp(&o.world, &p.world, sizeof(Transform)) != 0 || std::memcmp(&o.bound, &p.bound, sizeof(Bound)) != 0 || o.flags != p.flags ||
		    std::memcmp(&o.body, &p.body, sizeof(Transform)) != 0 || std::memcmp(&o.local, &p.local, sizeof(Transform)) != 0) {
			return false;
		}
	}
	return true;
}

struct Outcome {
	Tree flat;
	Tree parts;
	Port flatPort;
	Port partsPort;
	split::Pending pending;
	split::SweepResult result;
	bool same = false;
};

// The flat run on one copy, the two parts and the nodes' bounds on another.
Outcome Both(const Tree& start, Handle root, float time, bool controllers, const std::vector<std::uint8_t>& unused = {}) {
	Outcome out;
	out.flat = start;
	out.parts = start;
	out.flatPort = Port{&out.flat, {}, {}, {}};
	out.partsPort = Port{&out.parts, {}, {}, {}};
	Order order = Build(out.flat, root);
	order.unused = unused;
	Run(order, out.flatPort, time, controllers);
	split::RunWorker(order, out.partsPort, time, controllers, out.pending);
	out.result = split::RunSweep(order, out.partsPort, time, controllers, out.pending);
	RunBounds(order, out.partsPort);
	out.same = SameObjects(out.flat, out.parts) && out.flatPort.seen == out.partsPort.seen && out.flatPort.controllerCalls == out.partsPort.controllerCalls &&
	           out.flatPort.engineBounds == out.partsPort.engineBounds;
	return out;
}

Tree RandomTree(std::mt19937& random, Handle* root) {
	Tree t;
	Handle next = 1;
	std::uniform_int_distribution<int> die(0, 99);
	std::uniform_real_distribution<float> value(-50.0f, 50.0f);
	*root = next++;
	t.objects[*root] = Object{};
	std::vector<Handle> nodes{*root};
	const int count = 3 + die(random) % 40;
	for (int n = 0; n < count; ++n) {
		const Handle parent = nodes[static_cast<std::size_t>(die(random)) % nodes.size()];
		const Handle h = next++;
		Object o;
		const int kind = die(random);
		o.kind = kind < 55 ? Kind::Node : kind < 80 ? Kind::GeometryPlain : kind < 88 ? Kind::Geometry : Kind::Other;
		o.controllers = die(random) < 50;
		o.heldMoves = o.kind != Kind::Other && die(random) < 8;
		o.heldQuiet = o.kind != Kind::Other && die(random) < 12;
		const int collision = die(random);
		o.collision = o.kind == Kind::Other || collision < 60 ? kNone : collision < 70 ? kReads : collision < 82 ? kHardSet : collision < 92 ? kBlend : kWritesSame;
		for (float& f : o.local.pos) {
			f = value(random);
		}
		for (float& f : o.body.pos) {
			f = value(random);
		}
		o.local.scale = 0.5f + static_cast<float>(die(random)) / 50.0f;
		o.flags = die(random) < 70 ? 0x80 : 0;
		o.bound = Bound{{value(random), value(random), value(random)}, 1.0f};
		if (die(random) < 15) {
			t.objects[parent].slots.push_back(0);  // an empty slot
		}
		t.objects[parent].slots.push_back(h);
		// A bone that comes before the geometry in the order: one of its ancestors.
		if ((o.kind == Kind::GeometryPlain || o.kind == Kind::Geometry) && die(random) < 40) {
			o.bone = parent;
		}
		t.objects[h] = o;
		if (o.kind == Kind::Node) {
			nodes.push_back(h);
		}
	}
	Link(t);
	return t;
}

void TestAgainstTheFlatRun() {
	Section("The two parts against the flat run over random trees: every transform, bound, flags word, body and view the same");
	std::mt19937 random(20261127);
	unsigned trees = 0;
	unsigned same = 0;
	unsigned quiet = 0;
	unsigned withStopsQuiet = 0;
	unsigned changed = 0;
	std::uint64_t recomposed = 0;
	std::uint64_t rebound = 0;
	bool noControllers = true;
	for (int n = 0; n < 600; ++n) {
		Handle root;
		const Tree start = RandomTree(random, &root);
		for (bool controllers : {true, false}) {
			const Outcome out = Both(start, root, 0.125f, controllers);
			++trees;
			same += out.same ? 1u : 0u;
			quiet += out.result.changed == 0 ? 1u : 0u;
			withStopsQuiet += out.result.changed == 0 && !out.pending.stops.empty() ? 1u : 0u;
			changed += out.result.changed != 0 ? 1u : 0u;
			recomposed += out.result.recomposed;
			rebound += out.result.rebound;
			noControllers = noControllers && (controllers || out.partsPort.controllerCalls.empty());
		}
	}
	Check(same == trees, "every tree ends the same in both");
	Check(withStopsQuiet != 0 && changed != 0 && recomposed != 0 && rebound != 0, "the quiet sweep, the walk, the worlds composed again and the bounds taken again all occurred");
	Check(noControllers, "without the controller flag no controller is updated");
	std::printf("  %u runs: %u quiet (%u of them with stops), %u with a change; %llu worlds composed again, %llu bounds taken again\n", trees, quiet,
	            withStopsQuiet, changed, static_cast<unsigned long long>(recomposed), static_cast<unsigned long long>(rebound));
}

// 1 (node): [2 (node): [3 (node): [4 (plain geometry)]], 5 (other), 6 (geometry)]
Tree Chain() {
	Tree t;
	t.objects[1] = Object{Kind::Node, {2, 5, 6}};
	t.objects[2] = Object{Kind::Node, {3}};
	t.objects[3] = Object{Kind::Node, {4}};
	t.objects[4] = Object{Kind::GeometryPlain, {}};
	t.objects[5] = Object{Kind::Other, {}};
	t.objects[6] = Object{Kind::Geometry, {}};
	Link(t);
	return t;
}

void TestTheWorkersPass() {
	Section("The worker's pass: what it does and what it leaves");
	Tree t = Chain();
	t.objects[2].controllers = true;
	t.objects[2].heldQuiet = true;
	t.objects[3].collision = kReads;
	t.objects[5].controllers = true;
	Port port{&t, {}, {}, {}};
	const Order order = Build(t, 1);
	split::Pending pending;
	pending.stops = {99};
	pending.geometry.push_back({});
	split::RunWorker(order, port, 0.5f, true, pending);
	Check(port.seen.empty() && port.engineBounds.empty(), "no collision update, no Other entry, no engine bound on the worker");
	Check(port.controllerCalls == std::vector<Handle>{2}, "the controllers of the entries it runs, not the Other entry's");
	Check(pending.stops == std::vector<std::uint32_t>({1 | split::kHeld, 2, 4, 5}),
	      "left: the entry with held controllers (marked), the entry with a collision object, the Other entry, the geometry with the engine's bound");
	Check(pending.geometry.size() == 1 && pending.geometry[0].entry == 3 && pending.geometry[0].flags == 0x80 && pending.geometry[0].bound.radius == 0.0f,
	      "the plain geometry's bound and flags kept from before its bound");
	Check(t.objects[4].bound.radius == 1.5f && t.objects[4].world.pos[0] == 4.0f, "its world and its bound taken (three levels of the local's 1)");
	Check(t.objects[6].bound.radius == 0.0f && t.objects[5].world.scale == 0.0f, "the engine's geometry bound and the Other entry not touched");
	split::RunWorker(order, port, 0.5f, false, pending);
	Check(port.controllerCalls.size() == 1 && pending.stops == std::vector<std::uint32_t>({2, 4, 5}), "without the flag no controller run and none held");
}

void TestTheQuietSweep() {
	Section("The sweep while nothing changes: the stops alone, in order");
	Tree t = Chain();
	t.objects[3].collision = kReads;
	t.objects[6].collision = kWritesSame;
	const Outcome out = Both(t, 1, 0.5f, true);
	Check(out.same, "the same as the flat run");
	Check(out.result.changed == 0 && out.result.recomposed == 0 && out.result.rebound == 0, "nothing composed or bound again");
	Check(out.partsPort.seen.size() == 3 && out.partsPort.seen[0].object == 3 && out.partsPort.seen[1].object == 5 && out.partsPort.seen[2].object == 6,
	      "the collision update, the Other entry and the second collision update in the order's order");
	Check(out.partsPort.engineBounds == std::vector<Handle>{6}, "the engine's bound once, on the loop's thread");
	const Outcome none = Both(Chain(), 1, 0.5f, true);
	Check(none.same && none.pending.stops.size() == 2, "without a collision object: the Other entry and the engine's geometry alone");
}

void TestTheWalk() {
	Section("The sweep after a change: the worlds below composed again before their own updates");
	Tree t = Chain();
	t.objects[2].collision = kHardSet;
	t.objects[3].collision = kBlend;
	t.objects[6].collision = kReads;
	const Outcome out = Both(t, 1, 0.5f, true);
	Check(out.same, "the same as the flat run");
	Check(out.result.changed == 2, "the hard set changed its node, and the blend below it its own");
	Check(out.result.recomposed == 2, "composed again: the node below the hard set and the geometry below that");
	Check(out.result.rebound == 1, "the plain geometry's bound taken again");
	Check(out.partsPort.seen[1].object == 3 && out.partsPort.seen[1].world.pos[0] == 9.0f, "the blend found its node composed onto the hard set's world (7 + 2 x 1)");
	Check(out.parts.objects.at(5).world.pos[0] == 2.0f, "the Other entry beside the changed branch composed onto its own parent");
	Check(out.partsPort.seen.back().object == 6, "the entries after the change still get their updates");
}

void TestHeldControllers() {
	Section("Held controllers: run in the sweep before the node's collision update, the world composed again after them");
	Tree t = Chain();
	t.objects[2].heldQuiet = true;
	t.objects[3].heldMoves = true;
	t.objects[3].collision = kReads;
	const Outcome out = Both(t, 1, 0.5f, true);
	Check(out.same, "the same as the flat run");
	Check(out.partsPort.seen.size() == 4 && out.partsPort.seen[0].what == 'q' && out.partsPort.seen[1].what == 'm' && out.partsPort.seen[2].what == 'c',
	      "the quiet one, then the moving one and its node's collision update, in the order's order");
	Check(out.partsPort.seen[2].world.pos[2] == 9.5f, "the collision update found the world composed from the local the held controller wrote");
	Check(out.result.changed == 1 && out.result.recomposed == 1 && out.result.rebound == 1,
	      "the quiet one changed nothing; the moving one changed its node: the geometry below composed and bound again");
	Tree quiet = Chain();
	quiet.objects[2].heldQuiet = true;
	const Outcome still = Both(quiet, 1, 0.5f, false);
	Check(still.same && still.partsPort.seen.size() == 1, "without the controller flag no held controller runs either");
}

void TestTheChangedGeometry() {
	Section("A plain geometry changed by its own collision update: its bound again, with the flags the flat run leaves");
	Tree t = Chain();
	Object& g = t.objects[4];
	g.collision = kHardSet;
	TransformBound(g.modelBound, g.body, g.bound);  // the frame before left the bound of the body's place
	const Outcome out = Both(t, 1, 0.5f, true);
	Check(out.same, "the same as the flat run");
	Check(out.result.changed == 1 && out.result.recomposed == 0 && out.result.rebound == 1, "one change, nothing below it, its own bound again");
	Check((out.parts.objects.at(4).flags & 0x80) != 0, "bit 7 stays: the bound is the frame before's, though the worker's first bound was another");
	Check((out.parts.objects.at(4).flags & 0x01) != 0, "the flag the collision update set beside it stays too");
}

void TestTheLastEntryAndTheEnginesGeometry() {
	Section("A change at the last entry, and the engine's geometry below a change");
	Tree t = Chain();
	t.objects[6].collision = kHardSet;
	const Outcome last = Both(t, 1, 0.5f, true);
	Check(last.same && last.result.changed == 1 && last.result.recomposed == 0 && last.result.rebound == 0, "the last entry changed: nothing follows");
	Check(last.partsPort.engineBounds == std::vector<Handle>{6}, "its bound once, after its collision update");
	Tree below = Chain();
	below.objects[1].collision = kHardSet;
	below.objects[6].collision = kBlend;
	const Outcome root = Both(below, 1, 0.5f, true);
	Check(root.same && root.result.changed == 2 && root.result.recomposed == 4, "the root changed: every entry it runs itself composed again, the geometry's blend after it");
	Check(root.partsPort.seen[1].object == 5 && root.partsPort.seen[1].world.pos[0] == 9.0f, "the Other entry found the root's new world (7 + 2 x 1)");
	Check(root.partsPort.engineBounds == std::vector<Handle>{6}, "the engine's bound once");
}

void TestUnusedEntries() {
	Section("Entries the order leaves out: neither part touches them");
	Tree t = Chain();
	t.objects[7] = Object{Kind::Node, {}};
	t.objects[3].slots.push_back(7);
	t.objects[2].collision = kHardSet;
	t.objects[7].controllers = true;
	Link(t);
	const Order order = Build(t, 1);
	std::vector<std::uint8_t> unused(order.entries.size(), 0);
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		unused[i] = order.entries[i].object == 7 ? 1 : 0;
	}
	const Outcome out = Both(t, 1, 0.5f, true, unused);
	Check(out.same, "the same as the flat run");
	Check(out.parts.objects.at(7).world.scale == 0.0f && out.partsPort.controllerCalls.empty(), "the unused bone below the changed node: no controllers, no world");
	Check(out.result.recomposed == 2, "composed again: the two used entries below the change");
}

void TestALaterBone() {
	Section("A geometry bound to a bone that comes after it: the same while nothing changes, this frame's bone after a change");
	// 1 (node): [2 (plain geometry, bone 3), 3 (node)]
	Tree t;
	t.objects[1] = Object{Kind::Node, {2, 3}};
	t.objects[2] = Object{Kind::GeometryPlain, {}};
	t.objects[3] = Object{Kind::Node, {}};
	t.objects[2].bone = 3;
	t.objects[3].world.pos[0] = 100.0f;  // the frame before
	t.objects[3].world.scale = 1.0f;
	Link(t);
	const Outcome quiet = Both(t, 1, 0.5f, true);
	Check(quiet.same && quiet.parts.objects.at(2).bound.center[0] == 100.0f, "nothing changed: the bound follows the bone of the frame before, as in the flat run");
	t.objects[1].collision = kHardSet;
	const Outcome changed = Both(t, 1, 0.5f, true);
	Check(changed.flat.objects.at(2).bound.center[0] == 100.0f, "the flat run: the bone of the frame before");
	Check(changed.parts.objects.at(2).bound.center[0] == 2.0f, "the two parts after a change: the bone as the worker left it this frame (1 + 1)");
	Check(std::memcmp(&changed.flat.objects.at(3).world, &changed.parts.objects.at(3).world, sizeof(Transform)) == 0, "the worlds still end the same");
}

void TestHangsBelow() {
	Section("A skeleton whose root hangs below another root of the batch is held back");
	struct Parents {
		std::map<Handle, Handle> parent;
		Handle Parent(Handle h) const {
			auto it = parent.find(h);
			return it != parent.end() ? it->second : 0;
		}
	};
	const Parents tree{{{10, 9}, {9, 8}, {8, 7}, {20, 19}, {19, 10}}};
	const auto isRoot = [](Handle h) { return h == 10 || h == 20 || h == 30; };
	Check(!split::HangsBelow(tree, 30, isRoot, 8), "no parent: free");
	Check(!split::HangsBelow(tree, 10, isRoot, 8), "parents up to the top, none a root: free");
	Check(split::HangsBelow(tree, 20, isRoot, 8), "a root two levels up: held back");
	Check(!split::HangsBelow(tree, 10, isRoot, 3), "the top reached exactly at the depth: free");
	Check(split::HangsBelow(tree, 10, isRoot, 2), "a chain longer than the depth: held back");
	Check(split::HangsBelow(tree, 20, isRoot, 1), "a depth of 1 looks at the parent alone: the chain goes on, held back");
}

void TestTheCounts() {
	Section("The window's counts and their line");
	split::Counts c;
	char line[900];
	split::FormatSummary(c, 3, false, 0.0, line, sizeof(line));
	Check(std::strstr(line, "(3 workers) - 0 batches in parts: 0.000 ms a batch") != nullptr && std::strstr(line, "0.0 stops") != nullptr,
	      "an empty window: zeros, no division by nothing");
	split::Pending pending;
	pending.stops = {1, 2, 3};
	split::Add(c, pending, split::SweepResult{2, 5, 1});
	split::Add(c, pending, split::SweepResult{});
	c.batches[1] = 1;
	c.ticks[1] = 3000;
	c.workerTicks = 1000;
	c.batches[0] = 2;
	c.ticks[0] = 8000;
	c.held = 1;
	c.inconsistent = 7;
	split::FormatSummary(c, 2, true, 1000.0, line, sizeof(line));
	Check(c.skeletons == 2 && c.stops == 6 && c.withChange == 1 && c.changed == 2 && c.recomposed == 5 && c.rebound == 1, "two skeletons added, one of them walked");
	Check(std::strstr(line, "(the check on one thread, 2 workers) - 1 batches in parts: 3.000 ms a batch (the workers' part 1.000), 2.0 skeletons a batch, 1.00 held back") != nullptr,
	      "the batches in parts by their time");
	Check(std::strstr(line, "2 batches flat on one thread: 4.000 ms a batch; a skeleton: 3.0 stops, 1.000 changed nodes (50.0 % of the skeletons walked), 2.50 worlds "
	                        "composed again, 0.50 bounds taken again; inconsistent worlds 7") != nullptr,
	      "the flat side and the sweep's work a skeleton");
}

}  // namespace

int main() {
	TestAgainstTheFlatRun();
	TestTheWorkersPass();
	TestTheQuietSweep();
	TestTheWalk();
	TestHeldControllers();
	TestTheChangedGeometry();
	TestTheLastEntryAndTheEnginesGeometry();
	TestUnusedEntries();
	TestALaterBone();
	TestHangsBelow();
	TestTheCounts();
	return Failures();
}
