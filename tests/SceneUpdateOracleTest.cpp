// The flat scene update against the original's own bytes: NiNode::
// UpdateDownwardPass 0x0070A0D0 (through the NiNode vtable 0x00A7E38C) runs
// on trees of fake NiNodes laid out as the engine's, mapped Oblivion.exe
// code doing the compose (0x0070C120, 0x0053D7A0, 0x007100A0, 0x007101F0)
// and the bound merges (0x0072A6B0 with the CRT square root 0x00982C30),
// and Run (SceneUpdateLogic.h) over the engine port (SceneUpdate.h) does the
// same tree from the same values. Compared per object: the world transform's
// 13 floats and the bound's 4, bit for bit, under the game's two control
// words 0x007F and 0x0C7F.
//
// The leaves that give the merges something to merge are Other objects: a
// vtable copied from NiNode's with +0x60 pointing at a stub here that does
// the original's world data (+0x74) and then writes a bound from the world
// (radius 0 for some, so the skip shows). Both implementations reach that
// stub through the same virtual call. Controllers, property lists and
// collision objects stay null: those are the original's own calls in both
// implementations and need the game.

#include "Check.h"
#include "ExeImage.h"
#include "engine/scene/SceneUpdate.h"
#include "engine/scene/SceneUpdateLogic.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace obng::engine::scene::scene_update;
namespace eng = obng::engine::scene::scene_update::engine;
namespace image = obng::tests::exe_image;

namespace {

constexpr std::uint32_t kObjectSize = 0xC0;
constexpr std::uint32_t kVtableSize = 0xA0;

alignas(16) std::uint8_t g_leafVtable[kVtableSize];

// The leaf's downward pass: the engine's own world data, then a bound from it.
void __thiscall LeafDownward(void* self, float /*time*/, std::uint8_t /*controllers*/) {
	const Handle h = static_cast<Handle>(reinterpret_cast<std::uintptr_t>(self));
	reinterpret_cast<eng::NoArgFn>(eng::Slot(h, eng::kSlotUpdateWorldData))(self);
	Transform world;
	std::memcpy(&world, eng::Pointer(h + eng::kWorldTransformOffset), sizeof(world));
	// No arithmetic here: the stub runs under whatever rounding the caller
	// has (the original's run under the MXCSR's default, Run's under the
	// copied x87 mode), and its own sums would differ between the two.
	Bound b;
	b.center[0] = world.pos[0];
	b.center[1] = world.pos[1];
	b.center[2] = world.pos[2];
	// A small scale gives radius 0 (skipped by the merge), a large one a NaN
	// radius (the gate run of 2026-11-09 16:53 met one in the game: merged,
	// not skipped) - decided from the values, so both copies of a tree decide
	// alike.
	b.radius = world.scale < 0.6f ? 0.0f : world.scale > 3.6f ? std::nanf("") : world.scale;
	std::memcpy(eng::Pointer(h + eng::kWorldBoundOffset), &b, sizeof(b));
}

struct Tree {
	std::vector<std::uint8_t*> blocks;
	std::vector<std::vector<std::uint32_t>> arrays;
	Handle root = 0;
	unsigned nodes = 0;
	unsigned leaves = 0;
	unsigned emptySlots = 0;

	~Tree() {
		for (std::uint8_t* b : blocks) {
			delete[] b;
		}
	}
};

void Write(Handle h, std::uint32_t offset, const void* value, std::size_t size) { std::memcpy(eng::Pointer(h + offset), value, size); }

Handle NewObject(Tree& t, bool node) {
	std::uint8_t* block = new std::uint8_t[kObjectSize]();
	t.blocks.push_back(block);
	const Handle h = static_cast<Handle>(reinterpret_cast<std::uintptr_t>(block));
	const std::uint32_t vtable = node ? eng::kNiNodeVtable : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_leafVtable));
	Write(h, 0, &vtable, 4);
	return h;
}

// The same tree twice (the generator is seeded the same): shape, transforms
// and starting bounds equal, addresses apart.
void BuildTree(Tree& t, std::uint32_t seed) {
	std::mt19937 random(seed);
	std::uniform_int_distribution<int> slotDie(0, 4);
	std::uniform_int_distribution<int> die(0, 9);
	std::uniform_real_distribution<float> value(-100.0f, 100.0f);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	std::uniform_real_distribution<float> scaleDie(0.1f, 4.0f);
	struct Pending {
		Handle object;
		unsigned depth;
	};
	auto fill = [&](Handle h) {
		Transform local;
		for (float& f : local.rot) {
			f = unit(random);
		}
		for (float& f : local.pos) {
			f = value(random);
		}
		local.scale = scaleDie(random);
		Write(h, eng::kLocalTransformOffset, &local, sizeof(local));
		Bound stale{{value(random), value(random), value(random)}, value(random)};
		Write(h, eng::kWorldBoundOffset, &stale, sizeof(stale));
	};
	t.root = NewObject(t, true);
	++t.nodes;
	fill(t.root);
	std::vector<Pending> open{{t.root, 0}};
	while (!open.empty()) {
		const Pending p = open.back();
		open.pop_back();
		const int slots = p.depth >= 5 ? 0 : slotDie(random);
		t.arrays.emplace_back();
		std::vector<std::uint32_t>& array = t.arrays.back();
		for (int i = 0; i < slots; ++i) {
			if (die(random) == 0) {
				array.push_back(0);
				++t.emptySlots;
				continue;
			}
			const bool node = die(random) < 6;
			const Handle child = NewObject(t, node);
			fill(child);
			Write(child, eng::kParentOffset, &p.object, 4);
			array.push_back(child);
			if (node) {
				++t.nodes;
				open.push_back({child, p.depth + 1});
			} else {
				++t.leaves;
			}
		}
		// The NiTArray's pieces the code reads: data +0xB0, first free +0xB6
		// (and the size +0xB4, the count +0xB8 for a tidy object).
		const std::uint32_t data = array.empty() ? 0u : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(array.data()));
		const std::uint16_t count = static_cast<std::uint16_t>(array.size());
		Write(p.object, eng::kChildrenOffset, &data, 4);
		Write(p.object, 0xB4, &count, 2);
		Write(p.object, eng::kChildSlotsOffset, &count, 2);
		Write(p.object, 0xB8, &count, 2);
	}
}

// ---- The controller walk's fakes: what 0x0047C930 calls, recorded --------------------------------
struct WalkCall {
	char kind;
	void* self;
	float time;
	bool operator==(const WalkCall& o) const { return kind == o.kind && self == o.self && std::memcmp(&time, &o.time, 4) == 0; }
};
std::vector<WalkCall> g_walkCalls;
void __thiscall PropertyUpdateStub(void* self, float time) { g_walkCalls.push_back({'P', self, time}); }
void __thiscall ControllerUpdateStub(void* self, float time) { g_walkCalls.push_back({'C', self, time}); }
std::uint32_t g_walkVtable[0x20];

struct WalkObject {
	std::vector<std::uint8_t> object = std::vector<std::uint8_t>(kObjectSize);
	std::vector<std::vector<std::uint32_t>> blocks;  // nodes (3 dwords), items and controllers (16 dwords)
	std::uint32_t Block(std::size_t dwords) {
		blocks.emplace_back(dwords);
		return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(blocks.back().data()));
	}
	Handle Handle_() const { return static_cast<Handle>(reinterpret_cast<std::uintptr_t>(object.data())); }
};

void BuildWalkObject(WalkObject& w, std::mt19937& random) {
	std::uniform_int_distribution<int> count(0, 5);
	std::uniform_int_distribution<int> die(0, 3);
	const std::uint32_t vt = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(g_walkVtable));
	auto thing = [&]() {
		const std::uint32_t t = w.Block(16);
		std::memcpy(eng::Pointer(t), &vt, 4);
		return t;
	};
	// The property list, built back to front: node {next, prev, item}.
	std::uint32_t head = 0;
	for (int i = count(random); i > 0; --i) {
		const std::uint32_t node = w.Block(3);
		std::uint32_t item = 0;
		const int d = die(random);
		if (d != 0) {  // d == 0: a node without an item
			item = thing();
			if (d >= 2) {  // an item with a controller
				const std::uint32_t c = thing();
				std::memcpy(eng::Pointer(item + 0x0C), &c, 4);
			}
		}
		const std::uint32_t fields[3] = {head, 0, item};
		std::memcpy(eng::Pointer(node), fields, sizeof(fields));
		head = node;
	}
	std::memcpy(w.object.data() + eng::kPropertyListOffset, &head, 4);
	// The controller chain, next at +0x34.
	std::uint32_t chain = 0;
	for (int i = count(random) / 2; i > 0; --i) {
		const std::uint32_t c = thing();
		std::memcpy(eng::Pointer(c + 0x34), &chain, 4);
		chain = c;
	}
	std::memcpy(w.object.data() + eng::kControllerOffset, &chain, 4);
}

void SetControlWord(std::uint16_t word) { asm volatile("fldcw %0" : : "m"(word)); }
std::uint16_t GetControlWord() {
	std::uint16_t word;
	asm volatile("fnstcw %0" : "=m"(word));
	return word;
}

struct Snapshot {
	std::vector<Transform> worlds;
	std::vector<Bound> bounds;
};

Snapshot Take(const Tree& t) {
	Snapshot s;
	for (std::uint8_t* b : t.blocks) {
		Transform w;
		Bound bd;
		std::memcpy(&w, b + eng::kWorldTransformOffset, sizeof(w));
		std::memcpy(&bd, b + eng::kWorldBoundOffset, sizeof(bd));
		s.worlds.push_back(w);
		s.bounds.push_back(bd);
	}
	return s;
}

}  // namespace

int main() {
	std::string where;
	if (!image::Map(&where)) {
		std::printf("SKIPPED: Oblivion.exe not available at %s (set OBNG_ORACLE_EXE)\n", where.c_str());
		return 77;
	}
	const unsigned char downward[] = {0x53, 0x8B, 0x5C, 0x24, 0x0C, 0x84, 0xDB};
	const unsigned char merge[] = {0x83, 0xEC, 0x20, 0x56, 0x8B, 0xF1};
	if (!image::BytesAre(kNiNodeUpdateDownwardPass, downward, sizeof(downward)) || !image::BytesAre(eng::kBoundMergeAddress, merge, sizeof(merge))) {
		std::printf("the bytes at 0x0070A0D0 or 0x0072A6B0 are not the functions' - not the reference build\n");
		return 1;
	}
	Section("The NiNode vtable's slots are the ones the kinds rest on");
	const std::uint32_t vt = eng::kNiNodeVtable;
	Check(eng::Dword(vt, eng::kSlotUpdateDownwardPass) == kNiNodeUpdateDownwardPass && eng::Dword(vt, eng::kSlotUpdateWorldData) == kNiAVObjectUpdateWorldData &&
	          eng::Dword(vt, eng::kSlotUpdateWorldBound) == kNiNodeUpdateWorldBound && eng::Dword(vt, eng::kSlotUpdateUpwardPass) == 0x00709EC0,
	      "+0x60 0x0070A0D0, +0x74 0x0070C120, +0x78 0x0070A360, +0x94 0x00709EC0");

	// The leaf vtable: NiNode's with the downward pass replaced.
	std::memcpy(g_leafVtable, reinterpret_cast<const void*>(vt), kVtableSize);
	const std::uint32_t stub = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&LeafDownward));
	std::memcpy(g_leafVtable + eng::kSlotUpdateDownwardPass, &stub, 4);

	Section("Kinds through the engine tree port");
	{
		Tree t;
		BuildTree(t, 1);
		eng::Tree tree;
		Check(tree.KindOf(t.root) == Kind::Node, "a fake NiNode is a Node");
		bool leafOther = true;
		for (std::uint8_t* b : t.blocks) {
			const Handle h = static_cast<Handle>(reinterpret_cast<std::uintptr_t>(b));
			std::uint32_t vtable;
			std::memcpy(&vtable, b, 4);
			if (vtable != vt && tree.KindOf(h) != Kind::Other) {
				leafOther = false;
			}
		}
		Check(leafOther, "the leaves with the stub are Other");
	}

	Section("The controller walk inline against 0x0047C930: the same calls on the same objects in the same order");
	{
		const std::uint32_t p = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&PropertyUpdateStub));
		const std::uint32_t c = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&ControllerUpdateStub));
		g_walkVtable[0x50 / 4] = p;
		g_walkVtable[0x54 / 4] = c;
		std::mt19937 random(7);
		unsigned differ = 0;
		unsigned calls = 0;
		for (int n = 0; n < 2000; ++n) {
			WalkObject w;
			BuildWalkObject(w, random);
			for (const bool properties : {true, false}) {
				g_walkCalls.clear();
				reinterpret_cast<eng::UpdateObjectControllersFn>(eng::kUpdateObjectControllersAddress)(eng::Pointer(w.Handle_()), 0.375f, properties ? 1 : 0);
				const std::vector<WalkCall> original = g_walkCalls;
				g_walkCalls.clear();
				eng::Port port;
				port.UpdateObjectControllersWith(w.Handle_(), 0.375f, properties);
				calls += static_cast<unsigned>(original.size());
				if (!(original == g_walkCalls)) {
					++differ;
				}
			}
		}
		std::printf("  2000 objects, both flags, %u calls recorded; %u objects differ\n", calls, differ);
		Check(differ == 0 && calls > 1000, "every call sequence equal to the original's");
	}

	Section("The original's downward pass against Run, bit for bit, under 0x007F and 0x0C7F");
	const std::uint16_t entryWord = GetControlWord();
	unsigned trees = 0;
	unsigned objects = 0;
	unsigned worldDiffers = 0;
	unsigned boundDiffers = 0;
	unsigned matchedOrders = 0;
	unsigned perWord[2][2] = {};
	for (const std::uint16_t word : {std::uint16_t{0x007F}, std::uint16_t{0x0C7F}}) {
		for (std::uint32_t seed = 1; seed <= 150; ++seed) {
			Tree a;
			Tree b;
			BuildTree(a, seed);
			BuildTree(b, seed);
			if (a.blocks.size() != b.blocks.size()) {
				std::printf("  the generator is not deterministic\n");
				return 1;
			}
			objects += static_cast<unsigned>(a.blocks.size());
			// The original on a: NiNode::UpdateDownwardPass(time, controllers=1) through the vtable.
			SetControlWord(word);
			reinterpret_cast<eng::UpdateDownwardFn>(eng::Slot(a.root, eng::kSlotUpdateDownwardPass))(eng::Pointer(a.root), 0.25f, 1);
			SetControlWord(entryWord);
			// Run on b.
			eng::Tree tree;
			eng::Port port;
			Order order = Build(tree, b.root);
			if (Matches(order, tree)) {
				++matchedOrders;
			}
			SetControlWord(word);
			{
				eng::RoundingFromX87 rounding;  // as the plugin's UpdateSubtree does
				Run(order, port, 0.25f, true);
			}
			SetControlWord(entryWord);
			const Snapshot sa = Take(a);
			const Snapshot sb = Take(b);
			unsigned w = 0;
			unsigned bd = 0;
			for (std::size_t i = 0; i < sa.worlds.size(); ++i) {
				if (std::memcmp(&sa.worlds[i], &sb.worlds[i], sizeof(Transform)) != 0) {
					++w;
					if (worldDiffers + w == 1) {
						// The first difference, for reading: which floats, and whether the parent's world agreed.
						const Handle ha = static_cast<Handle>(reinterpret_cast<std::uintptr_t>(a.blocks[i]));
						const Handle hb = static_cast<Handle>(reinterpret_cast<std::uintptr_t>(b.blocks[i]));
						const Handle pa = eng::Dword(ha, eng::kParentOffset);
						const Handle pb = eng::Dword(hb, eng::kParentOffset);
						bool parentSame = (pa == 0) == (pb == 0);
						if (pa != 0 && pb != 0) {
							parentSame = std::memcmp(eng::Pointer(pa + eng::kWorldTransformOffset), eng::Pointer(pb + eng::kWorldTransformOffset), sizeof(Transform)) == 0;
						}
						std::printf("  first world difference: object %zu of tree %u under %04X, parent world %s\n", i, seed, word, parentSame ? "same" : "DIFFERS");
						const float* fa = reinterpret_cast<const float*>(&sa.worlds[i]);
						const float* fb = reinterpret_cast<const float*>(&sb.worlds[i]);
						for (int k = 0; k < 13; ++k) {
							if (std::memcmp(&fa[k], &fb[k], 4) != 0) {
								std::printf("    float %d: original %.9g ours %.9g\n", k, fa[k], fb[k]);
							}
						}
					}
				}
				if (std::memcmp(&sa.bounds[i], &sb.bounds[i], sizeof(Bound)) != 0) {
					++bd;
				}
			}
			worldDiffers += w;
			boundDiffers += bd;
			perWord[word == 0x007F ? 0 : 1][0] += w;
			perWord[word == 0x007F ? 0 : 1][1] += bd;
			++trees;
		}
	}
	std::printf("  under 0x007F: %u worlds, %u bounds differ; under 0x0C7F: %u worlds, %u bounds differ\n", perWord[0][0], perWord[0][1], perWord[1][0],
	            perWord[1][1]);
	std::printf("  %u trees, %u objects; %u world transforms differ, %u bounds differ; errno stub calls %u\n", trees, objects, worldDiffers,
	            boundDiffers, image::g_errnoCalls);
	Check(matchedOrders == trees, "every order matches its own tree");
	Check(worldDiffers == 0, "every world transform equals the original's, bit for bit");
	Check(boundDiffers == 0, "every bound equals the original's, bit for bit");
	return Failures();
}
