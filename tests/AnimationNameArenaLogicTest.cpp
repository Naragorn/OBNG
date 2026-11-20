// Every flow of the animation file names' list made in an arena (src/engine/io/AnimationNameArenaLogic.h), against
// the list as the game's 0x00435830 makes it.

#include "Check.h"
#include "engine/io/AnimationNameArenaLogic.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace obng::engine::io::name_arena;

namespace {

struct Node {
	char* name;
	Node* next;
};

// The kept list of a folder: nodes in the order of `names`.
std::vector<Node> KeptOf(std::vector<std::string>& names) {
	std::vector<Node> nodes(names.size());
	for (std::size_t i = 0; i < names.size(); ++i) {
		nodes[i].name = names[i].data();
		nodes[i].next = i + 1 < names.size() ? &nodes[i + 1] : nullptr;
	}
	return nodes;
}

// 0x00435830's copy as read (0x00435878 - 0x00435920), with malloc for the game's allocator.
Node* GameCopy(const Node* kept) {
	Node* head = static_cast<Node*>(std::malloc(sizeof(Node)));
	head->name = nullptr;
	head->next = nullptr;
	for (; kept != nullptr; kept = kept->next) {
		char* copy = static_cast<char*>(std::malloc(std::strlen(kept->name) + 1));
		std::strcpy(copy, kept->name);
		if (head->name != nullptr) {
			Node* node = static_cast<Node*>(std::malloc(sizeof(Node)));
			node->name = head->name;
			node->next = nullptr;
			node->next = head->next;
			head->next = node;
		}
		head->name = copy;
	}
	return head;
}

std::string Joined(const Node* list) {
	std::string out;
	for (; list != nullptr; list = list->next) {
		out += list->name != nullptr ? list->name : "(none)";
		out += ' ';
	}
	return out;
}

}  // namespace

int main() {
	Section("The switch: 1 routes, 2 checks, anything else is off; off as shipped");
	Check(ModeOf(0) == Mode::Off && ModeOf(1) == Mode::Route && ModeOf(2) == Mode::Check && ModeOf(3) == Mode::Off, "0, 1, 2 and another value");
	Check(ModeOf(Shipped()) == Mode::Off, "off as shipped");

	Section("A call's five bytes: E8 and the distance from the instruction after it");
	{
		std::uint8_t bytes[5];
		CallBytes(0x004E38C5, kNames, bytes);
		const std::uint8_t games[5] = {0xE8, 0x66, 0x1F, 0xF5, 0xFF};  // 0x00435830 - 0x004E38CA = 0xFFF51F66
		Check(std::memcmp(bytes, games, 5) == 0, "the game's own call of 0x00435830 at 0x004E38C5");
		CallBytes(0x00475F9D, kFree, bytes);
		const std::uint8_t release[5] = {0xE8, 0x7E, 0xBF, 0xF8, 0xFF};  // 0x00401F20 - 0x00475FA2 = 0xFFF8BF7E
		Check(std::memcmp(bytes, release, 5) == 0, "the game's own call of the free at 0x00475F9D");
		CallBytes(0x1000, 0x2000, bytes);
		const std::uint8_t forward[5] = {0xE8, 0xFB, 0x0F, 0x00, 0x00};
		Check(std::memcmp(bytes, forward, 5) == 0, "a call forward");
	}

	Section("The arena: handed out from the start, aligned, given back to a mark, full at its end");
	{
		alignas(8) std::uint8_t memory[64];
		Arena arena(memory, sizeof(memory));
		Check(arena.Mark() == 0, "empty at first");
		void* a = arena.Take(3, 1);
		void* b = arena.Take(8, 4);
		Check(a == memory && b == memory + 4 && arena.Mark() == 12, "three bytes, then eight at the next multiple of four");
		const std::size_t mark = arena.Mark();
		Check(arena.Take(52, 1) == memory + 12 && arena.Mark() == 64, "the rest to the last byte");
		Check(arena.Take(1, 1) == nullptr && arena.Mark() == 64, "nothing left: null, the arena as it was");
		arena.Rewind(mark);
		Check(arena.Mark() == 12 && arena.Take(1, 1) == memory + 12, "given back to the mark and handed out again");
		arena.Rewind(40);
		Check(arena.Mark() == 13, "a mark past what is used changes nothing");
		Check(arena.Holds(memory) && arena.Holds(memory + 63) && !arena.Holds(memory + 64) && !arena.Holds(&arena), "Holds is the whole block, used or not");
		Arena tight(memory, 10);
		Check(tight.Take(7, 1) != nullptr && tight.Take(4, 4) == nullptr, "an aligned start past the end is refused");
		Arena none(nullptr, 0);
		Check(none.Take(1, 1) == nullptr && !none.Holds(nullptr), "no block at all");
	}

	Section("The list: the same names in the same order as the game's copy - from the last kept name to the first");
	{
		alignas(8) static std::uint8_t memory[4096];
		for (const std::vector<std::string>& given : {std::vector<std::string>{}, std::vector<std::string>{"idle.kf"},
		                                              std::vector<std::string>{"a.kf", "b.kf"},
		                                              std::vector<std::string>{"idle.kf", "walkforward.kf", "", "onehandattackleft.kf", "death.kf"}}) {
			std::vector<std::string> names = given;
			const std::vector<Node> kept = KeptOf(names);
			const Node* first = kept.empty() ? nullptr : kept.data();
			Arena arena(memory, sizeof(memory));
			Node* ours = Build<Node, Node>(&arena, first);
			Node* games = GameCopy(first);
			const std::string what = "kept " + std::to_string(names.size()) + " names: " + Joined(ours);
			Check(ours != nullptr && SameList(ours, games) && Joined(ours) == Joined(games), what.c_str());
			bool inside = true;
			for (const Node* node = ours; node != nullptr; node = node->next) {
				inside = inside && arena.Holds(node) && (node->name == nullptr || arena.Holds(node->name));
			}
			Check(inside, "every node and every name lies in the arena");
		}
	}

	Section("The list refused: the arena too small at the head, at a name or at a node; a kept node without a name");
	{
		std::vector<std::string> names = {"abcdefg.kf", "hijklmn.kf", "opqrstu.kf"};
		const std::vector<Node> kept = KeptOf(names);
		alignas(8) std::uint8_t memory[128];
		const std::size_t node = sizeof(Node);
		const std::size_t whole = node + 3 * 11 + 2 * node + 8;  // the head, three names, two nodes, alignment
		Arena enough(memory, whole);
		Check(Build<Node, Node>(&enough, kept.data()) != nullptr, "the whole list fits");
		Arena noHead(memory, node - 1);
		Check(Build<Node, Node>(&noHead, kept.data()) == nullptr && noHead.Mark() == 0, "no room for the head");
		Arena noName(memory, node + 5);
		Check(Build<Node, Node>(&noName, kept.data()) == nullptr && noName.Mark() == 0, "no room for the first name: given back");
		Arena noNode(memory, node + 11 + 11 + 2);
		Check(Build<Node, Node>(&noNode, kept.data()) == nullptr && noNode.Mark() == 0, "no room for the second name's node: given back");
		Arena used(memory, sizeof(memory));
		used.Take(5, 1);
		Check(Build<Node, Node>(&used, kept.data()) != nullptr && used.Mark() > 5, "a list on top of what is used");
		std::vector<Node> broken = kept;
		broken[1].name = nullptr;
		broken[0].next = &broken[1];
		broken[1].next = &broken[2];
		Arena arena(memory, sizeof(memory));
		arena.Take(3, 1);
		Check(Build<Node, Node>(&arena, broken.data()) == nullptr && arena.Mark() == 3, "a kept node without a name: refused, the arena as before");
	}

	Section("Two lists compare name for name");
	{
		std::vector<std::string> a = {"x", "y"}, b = {"x", "y"}, c = {"x", "z"}, d = {"x"};
		const std::vector<Node> ka = KeptOf(a), kb = KeptOf(b), kc = KeptOf(c), kd = KeptOf(d);
		Check(SameList(ka.data(), kb.data()), "the same names");
		Check(!SameList(ka.data(), kc.data()), "another name");
		Check(!SameList(ka.data(), kd.data()) && !SameList(kd.data(), ka.data()), "another length, either way");
		Check(SameList<Node, Node>(nullptr, nullptr) && !SameList<Node, Node>(ka.data(), nullptr), "no list against no list, and against one");
		Node empty{nullptr, nullptr};
		Node named{a[0].data(), nullptr};
		Check(SameList(&empty, &empty) && !SameList(&empty, &named) && !SameList(&named, &empty), "an empty head is not a named one");
	}

	Section("The frames: the newest list is the one a set up gives back; sixteen at most");
	{
		Frames frames;
		int one = 0, two = 0;
		Check(!frames.TopIs(&one) && frames.Count() == 0, "nothing handed out");
		Check(frames.Push(0, &one) && frames.TopIs(&one) && !frames.TopIs(&two), "one list out");
		Check(frames.Push(40, &two) && frames.TopIs(&two) && !frames.TopIs(&one), "a second inside the first's set up");
		Check(frames.Pop() == 40 && frames.TopIs(&one) && frames.Count() == 1, "the inner given back: its mark, the outer on top again");
		Check(frames.Pop() == 0 && frames.Count() == 0, "the outer given back");
		for (std::size_t i = 0; i < kFramesMax; ++i) {
			frames.Push(i, &one);
		}
		Check(!frames.Push(99, &two) && frames.Count() == kFramesMax && frames.TopIs(&one), "the seventeenth is refused");
	}
	return Failures();
}
