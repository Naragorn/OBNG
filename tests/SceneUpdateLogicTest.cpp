// Every flow of the scene update's logic (src/engine/scene/SceneUpdateLogic.h):
// the arithmetic's grouping element by element, the compose, the merge
// decision's table, the kinds, the flat order's build and its check against
// a changed tree, and the flat run against the original's recursion over
// random trees - the same calls in the same order, the same transforms and
// bounds bit for bit.

#include "Check.h"
#include "engine/scene/SceneUpdateLogic.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace obng::engine::scene::scene_update;

namespace {

bool SameBits(float a, float b) {
	std::uint32_t x;
	std::uint32_t y;
	std::memcpy(&x, &a, 4);
	std::memcpy(&y, &b, 4);
	return x == y;
}

bool SameBits(const Transform& a, const Transform& b) { return std::memcmp(&a, &b, sizeof(Transform)) == 0; }
bool SameBits(const Bound& a, const Bound& b) { return std::memcmp(&a, &b, sizeof(Bound)) == 0; }

const float kBig = 1.0e8f;  // 1e8 + 1 rounds to 1e8 in a float: the grouping shows

void TestMultiplyGrouping() {
	Section("NiMatrix3 multiply 0x007100A0: each element sums its first two products first");
	float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	float m[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
	float out[9];
	MultiplyMatrix(identity, m, out);
	Check(std::memcmp(out, m, sizeof(m)) == 0, "identity on the left leaves the right");
	MultiplyMatrix(m, identity, out);
	Check(std::memcmp(out, m, sizeof(m)) == 0, "identity on the right leaves the left");

	// out[0] = (r0*l0 + r3*l1) + r6*l2: with the products 1e8, -1e8, 1 the
	// instructions' grouping gives 1, the other grouping 0.
	float l[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
	float r[9] = {kBig, 0, 0, -kBig, 0, 0, 1, 0, 0};
	MultiplyMatrix(l, r, out);
	Check(SameBits(out[0], 1.0f) && SameBits(out[3], 1.0f) && SameBits(out[6], 1.0f), "column 0: the big pair cancels first, the 1 survives (elements 0, 3, 6)");
	float r2[9] = {0, kBig, 0, 0, -kBig, 0, 0, 1, 0};
	MultiplyMatrix(l, r2, out);
	Check(SameBits(out[1], 1.0f) && SameBits(out[4], 1.0f) && SameBits(out[7], 1.0f), "column 1 likewise (elements 1, 4, 7)");
	float r3[9] = {0, 0, kBig, 0, 0, -kBig, 0, 0, 1};
	MultiplyMatrix(l, r3, out);
	Check(SameBits(out[2], 1.0f) && SameBits(out[5], 1.0f) && SameBits(out[8], 1.0f), "column 2 likewise (elements 2, 5, 8)");
	// The other way round: the 1 in the first pair is lost against the big one.
	float r4[9] = {1, 0, 0, kBig, 0, 0, -kBig, 0, 0};
	MultiplyMatrix(l, r4, out);
	Check(SameBits(out[0], 0.0f), "element 0 with the 1 first: (1 + 1e8) + -1e8 = 0, as the x87 rounds it");
}

void TestTransformPointGrouping() {
	Section("NiMatrix3 times point 0x007101F0: element 0 sums rot[1]*v[1] + rot[0]*v[0] first");
	float rot[9] = {-kBig, kBig, 1, 0, 0, 0, 0, 0, 0};
	float v[3] = {1, 1, 1};
	float out[3];
	TransformPoint(rot, v, out);
	Check(SameBits(out[0], 1.0f), "element 0: the big pair first, the 1 survives");
	float rot2[9] = {0, 0, 0, kBig, -kBig, 1, kBig, -kBig, 1};
	TransformPoint(rot2, v, out);
	Check(SameBits(out[1], 1.0f) && SameBits(out[2], 1.0f), "elements 1 and 2: rot[r*3]*v0 + rot[r*3+1]*v1 first");
	float rot3[9] = {1, kBig, -kBig, 0, 0, 0, 0, 0, 0};
	TransformPoint(rot3, v, out);
	Check(SameBits(out[0], 0.0f), "element 0 the other way: (1e8 + 1) + -1e8 = 0");
}

void TestCompose() {
	Section("NiTransform compose 0x0053D7A0: scale, rotation, parent.scale * (parent.rot * local.pos) + parent.pos");
	Transform parent{{0, -1, 0, 1, 0, 0, 0, 0, 1}, {10, 20, 30}, 2.0f};  // a quarter turn about z, scale 2
	Transform local{{1, 0, 0, 0, 1, 0, 0, 0, 1}, {1, 2, 3}, 0.5f};
	Transform out;
	Compose(parent, local, out);
	Check(SameBits(out.scale, 1.0f), "scale: 0.5 * 2");
	Check(std::memcmp(out.rot, parent.rot, sizeof(out.rot)) == 0, "rotation: parent's times identity");
	// parent.rot * (1,2,3) = (-2, 1, 3); times 2 = (-4, 2, 6); plus (10,20,30)
	Check(SameBits(out.pos[0], 6.0f) && SameBits(out.pos[1], 22.0f) && SameBits(out.pos[2], 36.0f), "position: rotated, scaled, translated");
	Transform world;
	UpdateWorld(nullptr, local, world);
	Check(SameBits(world, local), "no parent: the world is the local");
	UpdateWorld(&parent, local, world);
	Check(SameBits(world, out), "a parent: the compose");
}

void TestMergeStep() {
	Section("The bound merge's decision (0x0070A12F..6E): child 0 skipped, own 0 copied over, else merged; a NaN is neither 0 nor skipped");
	const float nan = std::nanf("");
	Check(MergeStepFor(0.0f, 5.0f) == MergeStep::Copy, "own 0, child 5: copy");
	Check(MergeStepFor(-0.0f, 5.0f) == MergeStep::Copy, "own -0, child 5: copy (FCOM treats -0 as 0)");
	Check(MergeStepFor(nan, 5.0f) == MergeStep::Merge, "own NaN, child 5: merge (unordered sets C3, C2 and C0: even parity, the merge branch)");
	Check(MergeStepFor(3.0f, 5.0f) == MergeStep::Merge, "own 3, child 5: merge");
	Check(MergeStepFor(3.0f, 0.0f) == MergeStep::Skip, "child 0: skip");
	Check(MergeStepFor(3.0f, -0.0f) == MergeStep::Skip, "child -0: skip");
	Check(MergeStepFor(3.0f, nan) == MergeStep::Merge, "child NaN: not skipped, merged (the gate run of 16:53 showed the original doing so)");
	Check(MergeStepFor(0.0f, nan) == MergeStep::Copy, "own 0, child NaN: copied over");
	Check(MergeStepFor(0.0f, 0.0f) == MergeStep::Skip, "both 0: skip (the child's test comes first)");
}

void TestClassify() {
	Section("The kinds from the three vtable slots");
	Check(Classify({kNiNodeUpdateDownwardPass, kNiAVObjectUpdateWorldData, kNiNodeUpdateWorldBound}) == Kind::Node, "NiNode's three: Node");
	Check(Classify({kNiAVObjectUpdateDownwardPass, kNiAVObjectUpdateWorldData, 0x00722AA0}) == Kind::Geometry, "NiGeometry: Geometry");
	Check(Classify({kNiAVObjectUpdateDownwardPass, kNiAVObjectUpdateWorldData, 0x00864610}) == Kind::Geometry, "the BS geometry variant's bound: Geometry (its own +0x78 is called)");
	Check(Classify({0x0055D4C0, kNiAVObjectUpdateWorldData, kNiNodeUpdateWorldBound}) == Kind::Other, "BSFaceGenNiNode's downward pass: Other");
	Check(Classify({kNiNodeUpdateDownwardPass, 0x007416C0, kNiNodeUpdateWorldBound}) == Kind::Other, "a node with its own UpdateWorldData: Other");
	Check(Classify({kNiNodeUpdateDownwardPass, kNiAVObjectUpdateWorldData, 0x0060D0A0}) == Kind::Other, "a node-pass object with another bound update: Other");
	Check(Classify({kNiAVObjectUpdateDownwardPass, 0x0070D430, 0x0070CC70}) == Kind::Other, "a light: Other");
}

// ---- A tree in memory for the order and the run ---------------------------------------------

struct FakeObject {
	Kind kind = Kind::Node;
	std::vector<Handle> slots;  // 0 = empty
	bool controllers = false;
	bool collision = false;
	Transform local{{1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}, 1.0f};
	Transform world{};
	Bound bound{};
	Handle parent = 0;
};

struct FakeTree {
	std::map<Handle, FakeObject> objects;

	Kind KindOf(Handle h) const { return objects.at(h).kind; }
	std::uint32_t SlotCount(Handle h) const {
		const FakeObject& o = objects.at(h);
		return o.kind == Kind::Node ? static_cast<std::uint32_t>(o.slots.size()) : 0u;
	}
	Handle ChildAt(Handle h, std::uint32_t i) const { return objects.at(h).slots[i]; }
};

// The action port over the fake tree: does what the engine would, deterministically,
// and records the order of the calls that have side effects on objects.
struct FakePort {
	FakeTree* tree;
	std::vector<std::string> calls;

	bool HasControllers(Handle h) const { return tree->objects.at(h).controllers; }
	void UpdateObjectControllers(Handle h, float time) { calls.push_back("controllers " + std::to_string(h) + " " + std::to_string(time)); }
	const Transform* ParentWorld(Handle h) const {
		const Handle parent = tree->objects.at(h).parent;
		return parent == 0 ? nullptr : &tree->objects.at(parent).world;
	}
	Transform Local(Handle h) const { return tree->objects.at(h).local; }
	void WriteWorld(Handle h, const Transform& t) {
		tree->objects.at(h).world = t;
		calls.push_back("world " + std::to_string(h));
	}
	void AfterWorldData(Handle h) {
		if (tree->objects.at(h).collision) {
			calls.push_back("collision " + std::to_string(h));
		}
	}
	void UpdateWorldBound(Handle h) {
		FakeObject& o = tree->objects.at(h);
		o.bound = Bound{{o.world.pos[0], o.world.pos[1], o.world.pos[2]}, o.world.scale * 1.5f};
		calls.push_back("geometry bound " + std::to_string(h));
	}
	void UpdateDownwardOriginal(Handle h, float time, bool controllers) {
		// What the original would leave: a world and a bound; the subtree is its own.
		FakeObject& o = tree->objects.at(h);
		UpdateWorld(ParentWorld(h), o.local, o.world);
		o.bound = Bound{{o.world.pos[0] + 1, o.world.pos[1], o.world.pos[2]}, o.world.scale * 0.25f};
		calls.push_back("other " + std::to_string(h) + " " + std::to_string(time) + (controllers ? " c" : ""));
	}
	float BoundRadius(Handle h) const { return tree->objects.at(h).bound.radius; }
	void SetBoundRadius(Handle h, float r) { tree->objects.at(h).bound.radius = r; }
	void CopyBound(Handle to, Handle from) { tree->objects.at(to).bound = tree->objects.at(from).bound; }
	void MergeBound(Handle to, Handle from) {
		// A deterministic stand-in for NiBound::Merge (the real one is the engine's).
		Bound& a = tree->objects.at(to).bound;
		const Bound& b = tree->objects.at(from).bound;
		a.center[0] = (a.center[0] + b.center[0]) * 0.5f;
		a.center[1] = (a.center[1] + b.center[1]) * 0.5f;
		a.center[2] = (a.center[2] + b.center[2]) * 0.5f;
		a.radius = a.radius > b.radius ? a.radius + 0.5f * b.radius : b.radius + 0.5f * a.radius;
	}
};

FakeTree SmallTree() {
	// 1 (node): [2 (geometry), 0, 3 (other), 4 (node): [5 (geometry)]]
	FakeTree t;
	t.objects[1] = FakeObject{Kind::Node, {2, 0, 3, 4}};
	t.objects[2] = FakeObject{Kind::Geometry, {}};
	t.objects[3] = FakeObject{Kind::Other, {}};
	t.objects[4] = FakeObject{Kind::Node, {5}};
	t.objects[5] = FakeObject{Kind::Geometry, {}};
	for (auto& [h, o] : t.objects) {
		for (Handle c : o.slots) {
			if (c != 0) {
				t.objects[c].parent = h;
			}
		}
	}
	return t;
}

void TestBuildAndMatches() {
	Section("The flat order: depth-first, children in index order without the empty slots; Other not expanded");
	FakeTree t = SmallTree();
	Order order = Build(t, 1);
	Check(order.root == 1 && order.entries.size() == 5, "five entries for five objects");
	Check(order.entries[0].object == 1 && order.entries[1].object == 2 && order.entries[2].object == 3 && order.entries[3].object == 4 &&
	          order.entries[4].object == 5,
	      "the order 1, 2, 3, 4, 5");
	Check(order.entries[0].kind == Kind::Node && order.entries[1].kind == Kind::Geometry && order.entries[2].kind == Kind::Other &&
	          order.entries[3].kind == Kind::Node && order.entries[4].kind == Kind::Geometry,
	      "the kinds");
	Check(order.entries[0].parent == kNoParent && order.entries[1].parent == 0 && order.entries[2].parent == 0 && order.entries[3].parent == 0 &&
	          order.entries[4].parent == 3,
	      "the parents as entry indices");
	Check(order.entries[0].slotCount == 4 && order.entries[0].childCount == 3 && order.entries[3].slotCount == 1 && order.entries[3].childCount == 1 &&
	          order.entries[1].childCount == 0 && order.entries[2].childCount == 0,
	      "slot counts with the empty slot, child counts without");
	Check(order.children.size() == 4 && order.children[order.entries[0].childrenBegin] == 1 && order.children[order.entries[0].childrenBegin + 1] == 2 &&
	          order.children[order.entries[0].childrenBegin + 2] == 3 && order.children[order.entries[3].childrenBegin] == 4,
	      "the children lists point at the entries in index order");
	Check(Matches(order, t), "the unchanged tree matches");

	Section("Matches: every change of shape or kind is seen");
	FakeTree swapped = SmallTree();
	std::swap(swapped.objects[1].slots[0], swapped.objects[1].slots[2]);
	Check(!Matches(order, swapped), "two children swapped");
	FakeTree filled = SmallTree();
	filled.objects[1].slots[1] = 5;
	Check(!Matches(order, filled), "an empty slot filled");
	FakeTree grown = SmallTree();
	grown.objects[4].slots.push_back(0);
	Check(!Matches(order, grown), "a node's array grown by an empty slot (the slot count is part of the shape)");
	FakeTree emptied = SmallTree();
	emptied.objects[4].slots[0] = 0;
	Check(!Matches(order, emptied), "a child removed");
	FakeTree rekinded = SmallTree();
	rekinded.objects[2].kind = Kind::Other;
	Check(!Matches(order, rekinded), "a geometry became Other");
	FakeTree otherGrew = SmallTree();
	otherGrew.objects[3].kind = Kind::Node;
	otherGrew.objects[3].slots = {2};
	Check(!Matches(order, otherGrew), "an Other became a node");
	FakeTree nullOnly = SmallTree();
	nullOnly.objects[1].slots = {2, 3, 4, 0};
	Check(Matches(order, nullOnly), "the same children with only the empty slot moved: the same shape (empty slots take no part)");
	FakeTree single;
	single.objects[7] = FakeObject{Kind::Geometry, {}};
	Order one = Build(single, 7);
	Check(one.entries.size() == 1 && one.children.empty() && Matches(one, single), "a lone geometry: one entry, no children");
}

std::mt19937 g_random(20261109);

FakeTree RandomTree(unsigned& nextHandle, Handle* rootOut) {
	FakeTree t;
	std::uniform_int_distribution<int> kindDie(0, 9);
	std::uniform_int_distribution<int> slotDie(0, 4);
	std::uniform_real_distribution<float> value(-50.0f, 50.0f);
	std::uniform_real_distribution<float> scaleDie(0.25f, 3.0f);
	std::uniform_int_distribution<int> coin(0, 1);
	std::vector<std::pair<Handle, unsigned>> open;  // object, depth
	const Handle root = nextHandle++;
	*rootOut = root;
	t.objects[root] = FakeObject{Kind::Node, {}};
	open.push_back({root, 0});
	auto randomTransform = [&]() {
		Transform tr;
		for (float& f : tr.rot) {
			f = value(g_random) / 50.0f;
		}
		for (float& f : tr.pos) {
			f = value(g_random);
		}
		tr.scale = scaleDie(g_random);
		return tr;
	};
	while (!open.empty()) {
		auto [h, depth] = open.back();
		open.pop_back();
		FakeObject& o = t.objects[h];
		o.local = randomTransform();
		o.controllers = coin(g_random) == 1;
		o.collision = kindDie(g_random) == 0;
		o.bound = Bound{{value(g_random), value(g_random), value(g_random)}, value(g_random) > 0 ? value(g_random) : 0.0f};
		if (o.kind != Kind::Node) {
			continue;
		}
		const int slots = depth >= 5 ? 0 : slotDie(g_random);
		for (int i = 0; i < slots; ++i) {
			if (kindDie(g_random) == 0) {
				o.slots.push_back(0);
				continue;
			}
			const Handle child = nextHandle++;
			const int k = kindDie(g_random);
			t.objects[child] = FakeObject{k < 5 ? Kind::Node : k < 8 ? Kind::Geometry : Kind::Other, {}};
			t.objects[child].parent = h;
			t.objects[h].slots.push_back(child);
			open.push_back({child, depth + 1});
		}
	}
	return t;
}

void TestRunAgainstReference() {
	Section("The flat run against the original's recursion over random trees: the same calls, the same results bit for bit");
	unsigned next = 100;
	unsigned trees = 0;
	unsigned objects = 0;
	bool sameCalls = true;
	bool sameState = true;
	bool bothControllerModes = true;
	for (int n = 0; n < 300; ++n) {
		Handle root;
		FakeTree original = RandomTree(next, &root);
		objects += static_cast<unsigned>(original.objects.size());
		for (bool controllers : {true, false}) {
			FakeTree a = original;
			FakeTree b = original;
			FakePort pa{&a, {}};
			FakePort pb{&b, {}};
			Reference(a, pa, root, 0.125f, controllers);
			Order order = Build(b, root);
			Run(order, pb, 0.125f, controllers);
			// The calls with side effects on the objects, in order (the bound
			// merges are not among them: their order differs, their result
			// is compared below).
			if (pa.calls != pb.calls) {
				sameCalls = false;
			}
			for (const auto& [h, o] : a.objects) {
				const FakeObject& p = b.objects.at(h);
				if (!SameBits(o.world, p.world) || !SameBits(o.bound, p.bound)) {
					sameState = false;
				}
			}
			if (!controllers) {
				for (const std::string& c : pb.calls) {
					if (c.rfind("controllers", 0) == 0) {
						bothControllerModes = false;
					}
				}
			}
		}
		++trees;
	}
	Check(sameCalls, "the controller, world, collision, geometry-bound and Other calls come in the same order");
	Check(sameState, "every world transform and every bound ends the same, bit for bit");
	Check(bothControllerModes, "without the controller flag no controller is updated");
	std::printf("  %u trees, %u objects\n", trees, objects);
}

void TestRunLeavesBoundCentreWhenNoChild() {
	Section("A node without a contributing child: radius 0, the centre as it was (the original writes only +0x2C)");
	FakeTree t;
	t.objects[1] = FakeObject{Kind::Node, {2}};
	t.objects[2] = FakeObject{Kind::Geometry, {}};
	t.objects[2].parent = 1;
	t.objects[1].bound = Bound{{7, 8, 9}, 3.0f};
	t.objects[2].world.scale = 0.0f;  // the geometry's bound radius becomes 0: skipped
	t.objects[2].local.scale = 0.0f;
	FakePort p{&t, {}};
	Run(Build(t, 1), p, 0.0f, true);
	Check(SameBits(t.objects[1].bound.radius, 0.0f) && SameBits(t.objects[1].bound.center[0], 7.0f) && SameBits(t.objects[1].bound.center[2], 9.0f),
	      "radius 0, centre kept");
}

}  // namespace

int main() {
	TestMultiplyGrouping();
	TestTransformPointGrouping();
	TestCompose();
	TestMergeStep();
	TestClassify();
	TestBuildAndMatches();
	TestRunAgainstReference();
	TestRunLeavesBoundCentreWhenNoChild();
	return Failures();
}
