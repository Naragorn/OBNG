#pragma once
// [Route] AnimationNameArena: the list of an actor's animation file names made in a block of OBNG's own, not from
// the game's heap - the decisions over plain values (the plugin's AnimationNameArena.cpp).
//
// Read 2026-11-20: an actor's animation set up 0x004E3490 asks 0x00435830 (__thiscall(unused; folder, 0), RET 8) for
// the skeleton folder's KF files. With a kept list for the folder (0x00521190 on the table at 0x00B362C0) the function
// makes a fresh copy: a head node of 8 bytes, and for every kept name a copy of the string and - from the second
// name on - a node of 8 bytes that takes the head's name and is linked in right behind the head, so the copy runs
// from the last kept name to the first. 0x00475D80 takes the list apart again as it goes: it frees each name
// (0x00475F9D), each node behind the head (0x00475FB8) and at its end the head (0x00475FEF).
//
// Measured 2026-11-20 (run obng-profile-ride-anvil-late/20261120-175137, Nadi's mod list): an NPC has 323 KF files;
// the copy is 646 allocations (0.3 - 0.4 ms of a set up of 3.1 - 4.3 ms) and as many frees.
//
// The route makes the same list - the same names in the same order, nodes of the game's shape - in a block that is
// given back whole when the set up returns. The three frees in 0x00475D80 leave what lies in the block alone.
// Class A: the same list, name for name; the check builds both and compares them.
// 64-bit clean: the node's type is the caller's; the logic holds no engine pointer of its own.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace obng::engine::io::name_arena {

constexpr int Shipped() {
	return 0;
}

enum class Mode { Off, Route, Check };

constexpr Mode ModeOf(int ini) {
	return ini == 1 ? Mode::Route : ini == 2 ? Mode::Check : Mode::Off;
}

// The two calls of 0x00435830 in the animation set up 0x004E3490 (an NPC's, a creature's) and the three frees of the
// list in 0x00475D80: each an E8 call whose target is replaced.
constexpr std::uint32_t kNamesCalls[] = {0x004E38C5, 0x004E3B94};
constexpr std::uint32_t kNames = 0x00435830;
constexpr std::uint32_t kFreeCalls[] = {0x00475F9D, 0x00475FB8, 0x00475FEF};
constexpr std::uint32_t kFree = 0x00401F20;
constexpr std::size_t kArenaBytes = 256 * 1024;  // 323 names of some 30 bytes and their nodes are about 13 KB a list
constexpr std::size_t kFramesMax = 16;           // set ups inside one another

// The five bytes of a call to `target` placed at `site` (E8 and the distance from the next instruction).
inline void CallBytes(std::uint32_t site, std::uint32_t target, std::uint8_t out[5]) {
	const std::uint32_t distance = target - (site + 5);
	out[0] = 0xE8;
	std::memcpy(out + 1, &distance, 4);
}

// A block handed out from its start and given back to a mark: one list after another, a list inside another's set up
// on top of it.
class Arena {
public:
	Arena(std::uint8_t* memory, std::size_t size) : memory_(memory), size_(size) {}

	// `bytes` at a multiple of `align` (a power of two); null when the block is used up.
	void* Take(std::size_t bytes, std::size_t align) {
		const std::size_t start = (used_ + (align - 1)) & ~(align - 1);
		if (start > size_ || bytes > size_ - start) {
			return nullptr;
		}
		used_ = start + bytes;
		return memory_ + start;
	}
	std::size_t Mark() const { return used_; }
	void Rewind(std::size_t mark) {
		if (mark <= used_) {
			used_ = mark;
		}
	}
	// Whether the address lies in the block (used or not): what the frees in 0x00475D80 must leave alone.
	bool Holds(const void* address) const {
		const std::uint8_t* p = static_cast<const std::uint8_t*>(address);
		return memory_ != nullptr && p >= memory_ && p < memory_ + size_;
	}

private:
	std::uint8_t* memory_;
	std::size_t size_;
	std::size_t used_ = 0;
};

// The list as 0x00435830 makes it from the kept one (0x0043589D - 0x0043591E), in the arena. Node is the game's
// {name, next}; Kept is a node of the kept list with the same two members. Null when the arena cannot hold it or a
// kept node has no name (the arena is then as before; the caller asks the game).
template <typename Node, typename Kept>
Node* Build(Arena* arena, const Kept* kept) {
	const std::size_t mark = arena->Mark();
	Node* head = static_cast<Node*>(arena->Take(sizeof(Node), alignof(Node)));
	if (head == nullptr) {
		return nullptr;
	}
	head->name = nullptr;
	head->next = nullptr;
	for (; kept != nullptr; kept = kept->next) {
		if (kept->name == nullptr) {
			arena->Rewind(mark);
			return nullptr;
		}
		const std::size_t length = std::strlen(kept->name) + 1;
		char* copy = static_cast<char*>(arena->Take(length, 1));
		Node* node = head->name != nullptr ? static_cast<Node*>(arena->Take(sizeof(Node), alignof(Node))) : nullptr;
		if (copy == nullptr || (head->name != nullptr && node == nullptr)) {
			arena->Rewind(mark);
			return nullptr;
		}
		std::memcpy(copy, kept->name, length);
		if (node != nullptr) {
			node->name = head->name;
			node->next = head->next;
			head->next = node;
		}
		head->name = copy;
	}
	return head;
}

// Two lists of the game's shape, name for name (the check).
template <typename A, typename B>
bool SameList(const A* a, const B* b) {
	for (; a != nullptr && b != nullptr; a = a->next, b = b->next) {
		if ((a->name == nullptr) != (b->name == nullptr)) {
			return false;
		}
		if (a->name != nullptr && std::strcmp(a->name, b->name) != 0) {
			return false;
		}
	}
	return a == nullptr && b == nullptr;
}

// The lists handed out and not yet given back: each with the arena's mark before it and its head. A set up gives
// back the list it was called with, whether it took it apart or left early.
class Frames {
public:
	bool Push(std::size_t mark, const void* head) {
		if (count_ >= kFramesMax) {
			return false;
		}
		frames_[count_++] = {mark, head};
		return true;
	}
	// Whether the newest list is this one (the set up 0x00475D80 was called with it).
	bool TopIs(const void* head) const { return count_ != 0 && frames_[count_ - 1].head == head; }
	// The newest list's mark, the list forgotten. Only after TopIs.
	std::size_t Pop() { return frames_[--count_].mark; }
	std::size_t Count() const { return count_; }

private:
	struct Frame {
		std::size_t mark;
		const void* head;
	};
	Frame frames_[kFramesMax] = {};
	std::size_t count_ = 0;
};

struct Counts {
	std::uint64_t lists = 0;       // the calls of 0x00435830 at the two sites
	std::uint64_t built = 0;       // made in the arena
	std::uint64_t names = 0;       // their names
	std::uint64_t noKept = 0;      // no kept list for the folder: the game's (a directory listing)
	std::uint64_t noRoom = 0;      // the arena or the frames full: the game's
	std::uint64_t released = 0;    // lists given back at a set up's return
	std::uint64_t freesLeft = 0;   // frees in 0x00475D80 of an address in the arena, left alone
	std::uint64_t compared = 0;    // the check
	std::uint64_t mismatches = 0;
};

}  // namespace obng::engine::io::name_arena
