// Every flow of the animation list's append without the walk (src/engine/io/AnimationLazyAppendLogic.h), against the
// game's own append 0x0067B1E0 written out over a small memory of nodes.

#include <vector>

#include "Check.h"
#include "engine/io/AnimationLazyAppendLogic.h"

using namespace obng::engine::io::lazy_append;

namespace {

struct Node {
	std::uint32_t item = 0;
	std::uint32_t next = 0;
};

// Nodes by handle; handle 0 is "none". allocFails makes the next new node fail, as the game's allocator may.
struct Memory {
	std::vector<Node> nodes = std::vector<Node>(1);
	bool allocFails = false;
	std::uint32_t New() {
		if (allocFails) {
			return 0;
		}
		nodes.push_back(Node{});
		return static_cast<std::uint32_t>(nodes.size() - 1);
	}
};

// 0x0067B1E0, read: walk from the head to the last node; an empty last node takes the item, else a new node does.
void GameAppend(Memory& memory, std::uint32_t head, std::uint32_t item) {
	if (item == 0) {
		return;
	}
	std::uint32_t node = head;
	while (memory.nodes[node].next != 0) {
		node = memory.nodes[node].next;
	}
	if (memory.nodes[node].item == 0) {
		memory.nodes[node].item = item;
		return;
	}
	const std::uint32_t fresh = memory.New();
	if (fresh != 0) {
		memory.nodes[fresh].item = item;
	}
	memory.nodes[node].next = fresh;
}

// The route's append, as the plugin writes it over the logic.
std::uint64_t RouteAppend(Memory& memory, Scope& scope, std::uint32_t head, std::uint32_t item) {
	std::uint64_t steps = 0;
	if (PutOf(item, 1) == Put::Nothing) {
		return steps;
	}
	const std::uint32_t last = LastOf(StartOf(scope, head), [&memory](std::uint32_t node) { return memory.nodes[node].next; }, steps);
	std::uint32_t kept = last;
	if (PutOf(item, memory.nodes[last].item) == Put::IntoLast) {
		memory.nodes[last].item = item;
	} else {
		const std::uint32_t fresh = memory.New();
		if (fresh != 0) {
			memory.nodes[fresh].item = item;
			kept = fresh;
		}
		memory.nodes[last].next = fresh;
	}
	Keep(scope, head, kept);
	return steps;
}

bool Same(const Memory& a, const Memory& b) {
	if (a.nodes.size() != b.nodes.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.nodes.size(); ++i) {
		if (a.nodes[i].item != b.nodes[i].item || a.nodes[i].next != b.nodes[i].next) {
			return false;
		}
	}
	return true;
}

}  // namespace

int main() {
	Section("The INI's value");
	Check(!OnOf(0) && OnOf(1) && !OnOf(2) && !OnOf(-1), "1 is on, anything else is off");
	Check(Shipped() == 0, "off as shipped until the game's gates have run");

	Section("What an append does with the item");
	Check(PutOf(0, 0) == Put::Nothing && PutOf(0, 7) == Put::Nothing, "no item: nothing");
	Check(PutOf(5, 0) == Put::IntoLast, "an empty last node (the empty head) takes it");
	Check(PutOf(5, 7) == Put::NewNode, "a full last node gets a new one behind it");

	Section("The scope of one set up");
	Scope scope;
	Check(StartOf(scope, 10) == 10, "outside a set up the walk starts at the head");
	Keep(scope, 10, 14);
	Check(scope.head == 0 && scope.tail == 0 && StartOf(scope, 10) == 10, "and nothing is kept there");
	Begin(scope);
	Check(StartOf(scope, 10) == 10, "inside one, before the first append: the head");
	Keep(scope, 10, 14);
	Check(StartOf(scope, 10) == 14, "then the kept last node of the same list");
	Check(StartOf(scope, 20) == 20, "another list: its head");
	End(scope);
	Check(scope.depth == 0 && StartOf(scope, 10) == 10, "the set up's end forgets it");
	End(scope);
	Check(scope.depth == 0, "an end without a begin stays at none");
	Begin(scope);
	Keep(scope, 10, 14);
	Begin(scope);
	Check(scope.depth == 2 && StartOf(scope, 10) == 10, "a set up inside a set up starts clean");
	End(scope);
	Check(scope.depth == 1 && StartOf(scope, 10) == 10, "and leaves the outer one with nothing kept (it walks once more)");
	End(scope);

	Section("The same list as the game's append, node for node");
	Memory game, route;
	const std::uint32_t headGame = game.New(), headRoute = route.New();
	Scope one;
	Begin(one);
	std::uint64_t steps = 0;
	GameAppend(game, headGame, 0);
	steps += RouteAppend(route, one, headRoute, 0);
	Check(Same(game, route), "no item: untouched");
	for (std::uint32_t item = 100; item < 373; ++item) {
		GameAppend(game, headGame, item);
		steps += RouteAppend(route, one, headRoute, item);
	}
	Check(Same(game, route), "273 items in one set up: the same nodes in the same order");
	Check(steps == 0, "and no node passed on the way (the game's append passes 37 000)");
	End(one);
	GameAppend(game, headGame, 999);
	steps = RouteAppend(route, one, headRoute, 999);
	Check(Same(game, route) && steps == 272, "outside a set up the route walks like the game");

	Section("Two lists inside one set up");
	Memory game2, route2;
	const std::uint32_t a1 = game2.New(), b1 = game2.New(), a2 = route2.New(), b2 = route2.New();
	Scope two;
	Begin(two);
	for (std::uint32_t i = 1; i <= 6; ++i) {
		GameAppend(game2, i % 2 ? a1 : b1, 50 + i);
		RouteAppend(route2, two, i % 2 ? a2 : b2, 50 + i);
	}
	End(two);
	Check(Same(game2, route2), "alternating lists: each append finds its own list's end");

	Section("A list that already has nodes when the set up begins");
	Memory game3, route3;
	const std::uint32_t h3 = game3.New(), r3 = route3.New();
	for (std::uint32_t i = 1; i <= 4; ++i) {
		GameAppend(game3, h3, i);
		GameAppend(route3, r3, i);
	}
	Scope three;
	Begin(three);
	GameAppend(game3, h3, 5);
	std::uint64_t first = RouteAppend(route3, three, r3, 5);
	GameAppend(game3, h3, 6);
	std::uint64_t second = RouteAppend(route3, three, r3, 6);
	End(three);
	Check(Same(game3, route3) && first == 3 && second == 0, "the first append walks to the end, the next ones start there");

	Section("The allocator fails");
	Memory game4, route4;
	const std::uint32_t h4 = game4.New(), r4 = route4.New();
	Scope four;
	Begin(four);
	GameAppend(game4, h4, 1);
	RouteAppend(route4, four, r4, 1);
	game4.allocFails = route4.allocFails = true;
	GameAppend(game4, h4, 2);
	RouteAppend(route4, four, r4, 2);
	Check(Same(game4, route4), "no node: the last node's next is cleared, as the game does");
	game4.allocFails = route4.allocFails = false;
	GameAppend(game4, h4, 3);
	RouteAppend(route4, four, r4, 3);
	End(four);
	Check(Same(game4, route4), "and the next append still lands at the end");
	return Failures();
}
