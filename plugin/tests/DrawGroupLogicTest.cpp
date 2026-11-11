// Every flow of the draw group counts (plugin/src/DrawGroupLogic.h).

#include "Check.h"
#include "DrawGroupLogic.h"

using namespace obng::plugin::draw_groups;

namespace {

DrawNote Note(std::uint32_t data, std::uint32_t shader, std::uint32_t parent, std::uint32_t grand, bool hidden, bool skinned = false) {
	DrawNote n;
	n.data = data;
	n.shader = shader;
	n.ancestor[0] = parent;
	n.ancestor[1] = grand;
	n.hidden = hidden;
	n.skinned = skinned;
	return n;
}

void TestMeshes() {
	Section("Same mesh: groups over (model data, shader) among the drawn, unskinned draws");
	const std::vector<DrawNote> notes = {
	    Note(1, 10, 100, 1000, false), Note(1, 10, 101, 1000, false), Note(1, 10, 102, 1000, false), Note(1, 10, 103, 1000, false),
	    Note(1, 11, 104, 1000, false),                    // the same mesh with another shader: its own group
	    Note(2, 10, 105, 1000, false), Note(2, 10, 106, 1000, false),
	    Note(3, 10, 107, 1000, false),                    // alone
	    Note(1, 10, 108, 1000, true),                     // hidden: not drawn, not grouped
	    Note(4, 10, 109, 1000, false, true), Note(4, 10, 110, 1000, false, true),  // skinned: counted apart
	};
	const PassCounts c = Count(notes);
	Check(c.draws == 11 && c.hidden == 1 && c.drawn == 10 && c.drawnSkinned == 2, "noted, hidden, drawn, skinned");
	Check(c.meshes == 3 && c.meshAndShader == 4, "three meshes, four (mesh, shader) pairs");
	Check(c.inGroupsOfTwo == 6 && c.inGroupsOfFour == 4 && c.largestGroup == 4, "six draws share their pair, four of them in a group of four");
	const PassCounts none = Count({});
	Check(none.draws == 0 && none.meshes == 0 && none.largestGroup == 0 && none.nodes[0] == 0, "no draws: nothing");
}

void TestHiddenNodes() {
	Section("Hidden together: a node with every noted draw hidden, at least two");
	const std::vector<DrawNote> notes = {
	    Note(1, 1, 100, 1000, true), Note(2, 1, 100, 1000, true),   // node 100: both hidden
	    Note(3, 1, 101, 1000, true), Note(4, 1, 101, 1000, false),  // node 101: one visible
	    Note(5, 1, 102, 1000, true),                                // node 102: one draw only
	    Note(6, 1, 200, 2000, true), Note(7, 1, 201, 2000, true), Note(8, 1, 201, 2000, true),  // below 2000 all hidden
	    Note(9, 1, 0, 0, true),                                     // no parent
	};
	const PassCounts c = Count(notes);
	Check(c.nodes[0] == 5 && c.hiddenNodes[0] == 2 && c.hiddenBelow[0] == 4, "level 1: nodes 100 and 201, four draws");
	Check(c.nodes[1] == 2 && c.hiddenNodes[1] == 1 && c.hiddenBelow[1] == 3, "level 2: node 2000 with its three; 1000 has a visible draw");
	Check(c.nodes[2] == 0 && c.hiddenBelow[2] == 0 && c.hiddenNodes[3] == 0, "levels without ancestors: nothing");
}

void TestTotals() {
	Section("Totals: sums over passes, the largest group kept");
	Totals t;
	PassCounts a;
	a.draws = 10;
	a.drawn = 6;
	a.hidden = 4;
	a.largestGroup = 3;
	a.hiddenBelow[1] = 2;
	a.nodes[0] = 5;
	PassCounts b = a;
	b.largestGroup = 7;
	t.Add(a);
	t.Add(b);
	Check(t.passes == 2 && t.draws == 20 && t.drawn == 12 && t.hidden == 8 && t.largest == 7 && t.hiddenBelow[1] == 4 && t.nodes[0] == 10, "summed");
}

void TestNodeBook() {
	Section("NodeBook: a node is a candidate when its last pass's draws were all hidden, two or more");
	static NodeBook book;
	Check(!book.Candidate(500, 100, 2), "an unknown node is none");
	book.Note(500, 100, true);
	Check(!book.Candidate(500, 100, 2), "not within the pass that notes it");
	Check(!book.Candidate(500, 101, 2), "one hidden draw is not enough");
	book.Note(500, 101, true);
	book.Note(500, 101, true);
	Check(book.Candidate(500, 102, 2) && book.Candidate(500, 103, 2), "two hidden draws: a candidate in the next pass and the one after");
	Check(!book.Candidate(500, 104, 2), "older than the age: no longer");
	book.Note(500, 104, true);
	book.Note(500, 104, false);
	Check(!book.Candidate(500, 105, 2), "one visible draw among them: none");
	book.Note(500, 105, true);
	book.Note(500, 105, true);
	book.Note(500, 105, true);
	Check(book.Candidate(500, 106, 2), "a new pass starts the count again: three hidden");
	Section("NodeBook: a node left out stays a candidate while it is being left out");
	book.LeftOut(500, 106);
	Check(book.Candidate(500, 107, 2) && book.Candidate(500, 108, 2), "left out in 106: asked in 107 and 108 without any note");
	book.LeftOut(500, 108);
	Check(book.Candidate(500, 110, 2) && !book.Candidate(500, 111, 2), "each leaving out renews it; beyond the age it ends");
	static NodeBook fresh;
	Check(fresh.LeftOut(7, 1000), "the first leaving out of a node says so (the node log's line)");
	Check(!fresh.LeftOut(7, 1001), "the next one does not");
	Check(fresh.Candidate(7, 1001, 2) && !fresh.Candidate(8, 1001, 2), "a node first heard of by being left out; its neighbour is none");
	Section("NodeBook: a full probe run drops the note, a stale slot is taken over");
	static NodeBook full;
	// Nodes that share a home: multiples of 2^32 / kSlots collide after the multiply only by luck, so fill by search.
	std::uint32_t placed = 0;
	std::uint32_t last = 0;
	const std::uint32_t home = (9u * 2654435761u) >> 20;
	for (std::uint32_t n = 10; placed < NodeBook::kProbe + 4 && n < 4000000u; ++n) {
		if (((n * 2654435761u) >> 20) == home) {
			full.Note(n, 50, true);
			full.Note(n, 50, true);
			++placed;
			last = n;
		}
	}
	Check(placed == NodeBook::kProbe + 4 && !full.Candidate(last, 51, 2), "the notes beyond the probe run are dropped: that node is none");
	full.Note(last, 50 + NodeBook::kStale + 1, true);
	full.Note(last, 50 + NodeBook::kStale + 1, true);
	Check(full.Candidate(last, 50 + NodeBook::kStale + 2, 2), "after the stale time the run's slots are free for it");
}

}  // namespace

int main() {
	TestNodeBook();
	TestMeshes();
	TestHiddenNodes();
	TestTotals();
	return Failures();
}
