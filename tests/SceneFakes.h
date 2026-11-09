#pragma once
// Fake engine records for the scene update's oracle tests: random
// transforms and bounds, and a skin instance laid out as 0x0072BB30 reads it
// (the instance: +0x08 skin data, +0x10 root parent, +0x14 the bone node
// pointers; the skin data: +0x0C skin-to-root, +0x40 bone count, +0x44 a
// pointer to the per-bone records of 0x4C bytes with the bind bound at
// +0x34; the bones and the root parent as objects with a world at +0x64).
#include "engine/scene/SceneUpdateLogic.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace obng::tests::scene_fakes {

using obng::engine::scene::scene_update::Bound;
using obng::engine::scene::scene_update::Transform;

inline std::mt19937& Random() {
	static std::mt19937 random(20261109);
	return random;
}

inline Transform RandomTransform() {
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	std::uniform_real_distribution<float> pos(-2000.0f, 2000.0f);
	std::uniform_real_distribution<float> scale(0.05f, 5.0f);
	Transform t;
	for (float& f : t.rot) {
		f = unit(Random());
	}
	for (float& f : t.pos) {
		f = pos(Random());
	}
	t.scale = scale(Random());
	return t;
}

// With edges: a zero radius, a NaN radius or centre, a huge radius or centre.
inline Bound RandomBound(bool edges) {
	std::uniform_real_distribution<float> pos(-500.0f, 500.0f);
	std::uniform_real_distribution<float> radius(0.0f, 300.0f);
	std::uniform_int_distribution<int> die(0, 19);
	Bound b{{pos(Random()), pos(Random()), pos(Random())}, radius(Random())};
	if (edges) {
		switch (die(Random())) {
			case 0: b.radius = 0.0f; break;
			case 1: b.radius = std::nanf(""); break;
			case 2: b.center[0] = std::nanf(""); break;
			case 3: b.radius = 1.0e30f; break;
			case 4: b.center[1] = 1.0e20f; break;
			default: break;
		}
	}
	return b;
}

struct FakeSkin {
	std::vector<std::uint8_t> instance = std::vector<std::uint8_t>(0x20);
	std::vector<std::uint8_t> data = std::vector<std::uint8_t>(0x48);
	std::vector<std::uint8_t> boneData;
	std::vector<std::vector<std::uint8_t>> nodes;  // bones, then the root parent
	std::vector<std::uint32_t> bonePointers;

	explicit FakeSkin(unsigned bones) {
		boneData.resize(bones * 0x4C);
		const Transform skinToRoot = RandomTransform();
		std::memcpy(data.data() + 0x0C, &skinToRoot, sizeof(skinToRoot));
		const std::uint32_t count = bones;
		std::memcpy(data.data() + 0x40, &count, 4);
		const std::uint32_t boneDataPtr = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(boneData.data()));
		std::memcpy(data.data() + 0x44, &boneDataPtr, 4);
		for (unsigned i = 0; i < bones; ++i) {
			const Bound bound = RandomBound(true);
			std::memcpy(boneData.data() + i * 0x4C + 0x34, &bound, sizeof(bound));
			nodes.emplace_back(0xA0);
			const Transform world = RandomTransform();
			std::memcpy(nodes.back().data() + 0x64, &world, sizeof(world));
		}
		nodes.emplace_back(0xA0);
		const Transform rootWorld = RandomTransform();
		std::memcpy(nodes.back().data() + 0x64, &rootWorld, sizeof(rootWorld));
		for (unsigned i = 0; i < bones; ++i) {
			bonePointers.push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(nodes[i].data())));
		}
		const std::uint32_t dataPtr = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data.data()));
		const std::uint32_t rootPtr = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(nodes.back().data()));
		const std::uint32_t bonesPtr = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bonePointers.data()));
		std::memcpy(instance.data() + 0x08, &dataPtr, 4);
		std::memcpy(instance.data() + 0x10, &rootPtr, 4);
		std::memcpy(instance.data() + 0x14, &bonesPtr, 4);
	}
	FakeSkin(const FakeSkin&) = delete;
	FakeSkin& operator=(const FakeSkin&) = delete;

	std::uint32_t Handle() const { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(instance.data())); }

	// The port the logic's SkinBound reads through.
	std::uint32_t BoneCount() const {
		std::uint32_t n;
		std::memcpy(&n, data.data() + 0x40, 4);
		return n;
	}
	Bound BoneBound(std::uint32_t i) const {
		Bound b;
		std::memcpy(&b, boneData.data() + i * 0x4C + 0x34, sizeof(b));
		return b;
	}
	Transform BoneWorld(std::uint32_t i) const {
		Transform t;
		std::memcpy(&t, nodes[i].data() + 0x64, sizeof(t));
		return t;
	}
	Transform RootParentWorld() const {
		Transform t;
		std::memcpy(&t, nodes.back().data() + 0x64, sizeof(t));
		return t;
	}
	Transform SkinToRoot() const {
		Transform t;
		std::memcpy(&t, data.data() + 0x0C, sizeof(t));
		return t;
	}
};

}  // namespace obng::tests::scene_fakes
