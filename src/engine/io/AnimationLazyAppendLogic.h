#pragma once
// [Route] AnimationLazyAppend: the list of an actor's animation files still to be cloned, appended to without a walk
// from its head each time - the decisions over plain values (the plugin's AnimationLazyAppend.cpp).
//
// Read 2026-11-20: an actor's animation set up 0x00475D80 hands each of its KF files to 0x00474070. In play (the
// setting iAnimationClonePerLoop:BackgroundLoad, 5, and no menu) only the idle and two more groups get their sequence
// at once; every other file is appended to the animation data's list at +0xB4 by 0x0067B1E0 (the call at 0x00474348)
// and cloned later, five a loop, by 0x00476D10. The append walks the list from its head to its end every time: a
// node is {item, next}, the head node is part of the owner, an empty head takes the first item, each further item a
// new node of 8 bytes.
//
// Measured 2026-11-20 (run obng-profile-ride-anvil-late/20261120-175137, Nadi's mod list): an NPC has 323 KF files,
// 273 of them appended in one set up - 37 000 steps, 0.27 - 0.45 ms of a set up of 3.1 - 4.3 ms.
//
// The route keeps the list's last node between the appends of one set up. Inside a set up nothing else touches that
// list (it belongs to the animation data being made); outside one nothing is kept and the walk is the game's.
// Class A: the same list, node for node.
// 64-bit clean: nodes as 32-bit handles behind the caller's reads.
#include <cstdint>

namespace obng::engine::io::lazy_append {

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

// 1 routes the append; 2 checks it in the game: the append walks from the head as the game does and the game goes on
// with that, while the node the kept end would have led to is compared with it (the class A gate).
enum class Mode { Off, Route, Check };

constexpr Mode ModeOf(int ini) {
	return ini == 1 ? Mode::Route : ini == 2 ? Mode::Check : Mode::Off;
}

// One thread's set up in progress: the list appended to last and its last node.
struct Scope {
	unsigned depth = 0;
	std::uint32_t head = 0;
	std::uint32_t tail = 0;
};

// A set up begins or ends: whatever was kept belongs to another list's past.
constexpr void Begin(Scope& scope) {
	++scope.depth;
	scope.head = 0;
	scope.tail = 0;
}

constexpr void End(Scope& scope) {
	if (scope.depth != 0) {
		--scope.depth;
	}
	scope.head = 0;
	scope.tail = 0;
}

// The node the walk to the list's end starts from: the kept last node for the same list inside a set up, else the head.
constexpr std::uint32_t StartOf(const Scope& scope, std::uint32_t head) {
	return scope.depth != 0 && scope.head == head && scope.tail != 0 ? scope.tail : head;
}

constexpr void Keep(Scope& scope, std::uint32_t head, std::uint32_t tail) {
	if (scope.depth != 0) {
		scope.head = head;
		scope.tail = tail;
	}
}

// The last node from a start node; next(node) reads a node's next handle. steps counts the nodes passed.
template <typename Next>
std::uint32_t LastOf(std::uint32_t from, Next next, std::uint64_t& steps) {
	std::uint32_t node = from;
	for (std::uint32_t after = next(node); after != 0; after = next(node)) {
		node = after;
		++steps;
	}
	return node;
}

// What an append does with the item, as 0x0067B1E0 does: nothing for no item, the last node's own slot when that is
// empty (only an empty head is), else a new node behind it.
enum class Put { Nothing, IntoLast, NewNode };

constexpr Put PutOf(std::uint32_t item, std::uint32_t lastItem) {
	return item == 0 ? Put::Nothing : lastItem == 0 ? Put::IntoLast : Put::NewNode;
}

}  // namespace obng::engine::io::lazy_append
