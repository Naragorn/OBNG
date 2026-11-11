#pragma once
// A far actor's skeleton update at a lower rate - the decisions, over plain
// values ([Route] ActorFarEvery in the plugin's FastSceneUpdate.cpp).
//
// Measured 2026-11-11 at Anvil's docks: half of the 50 actors the engine
// updates a frame stand 10 000 units and more from the player, each with the
// whole skeleton update of ActorAnimData::ApplyAnimData 0x00471F20 every
// frame. With this, an actor farther than a distance gets that update on
// every n-th of its calls only; between them its skeleton - the bones'
// world transforms, the bounds, the collision bodies that follow the bones -
// stays as the last update left it. A different computation on purpose
// (class C): not comparable with the original.
//
// The actors' turns are spread: which of the n calls is the one that runs
// depends on a phase taken from the actor's handle, so a crowd does not
// update on the same frame. An actor's first call always runs (its skeleton
// is never left unposed), and so does every call of an actor that is near
// or whose distance is not a number.
//
// 64-bit clean: the actor's root is a std::uint32_t handle.
#include <cstdint>

namespace obng::engine::scene::actor_far_rate {

struct State {
	std::uint32_t calls = 0;  // this root's calls so far
	bool ran = false;         // it has had an update
};

// On which of every n calls a far actor updates: the divisor for this
// distance - 1 (every call) when near, when off (every <= 1), or when the
// distance is not a number.
constexpr std::uint32_t EveryFor(float distance, float farDistance, std::uint32_t every) {
	return every > 1 && distance > farDistance ? every : 1u;  // a NaN compares false: every call
}

// The phase of a root among n turns, from its handle (objects are aligned,
// so the low bits say nothing).
constexpr std::uint32_t PhaseOf(std::uint32_t root, std::uint32_t every) {
	return every > 1 ? (root >> 6) % every : 0u;
}

// One call of the root's update: whether it runs. Counts the call.
constexpr bool RunsNow(State& state, std::uint32_t root, std::uint32_t every) {
	const std::uint32_t call = state.calls++;
	if (!state.ran || every <= 1) {
		state.ran = true;
		return true;
	}
	return (call + PhaseOf(root, every)) % every == 0;
}

}  // namespace obng::engine::scene::actor_far_rate
