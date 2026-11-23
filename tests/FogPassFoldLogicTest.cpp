// Every flow of the fold of a lit geometry's pass without lights (src/engine/render/FogPassFoldLogic.h): the
// modes, a list's shape over a small memory of nodes, which shapes fold in which mode, the marking, the counts.

#include <vector>

#include "Check.h"
#include "engine/render/FogPassFoldLogic.h"

using namespace obng::engine::render::fog_pass_fold;
using namespace obng::test;

namespace {

// Nodes by handle (0 is none): the node before and the record's pass id.
struct Node {
	std::uint32_t prev;
	std::uint16_t pass;
};

struct List {
	std::vector<Node> nodes = std::vector<Node>(1);
	std::uint32_t last = 0;
	std::uint32_t count = 0;
	void Add(std::uint16_t pass) {
		nodes.push_back(Node{last, pass});
		last = static_cast<std::uint32_t>(nodes.size() - 1);
		++count;
	}
	Shape Of(bool building = true, bool extra = false) const {
		return ShapeOf(
		    building, extra, count, last, [this](std::uint32_t n) { return nodes[n].prev; }, [this](std::uint32_t n) { return nodes[n].pass; });
	}
};

}  // namespace

int main() {
	Section("The key's values");
	Check(Shipped() == 0 && ModeOf(Shipped()) == Mode::Off, "off as shipped");
	Check(ModeOf(1) == Mode::FoldSingle && ModeOf(2) == Mode::Census && ModeOf(3) == Mode::FoldAll, "1 single lists, 2 the census, 3 every list");
	Check(ModeOf(4) == Mode::Off && ModeOf(-1) == Mode::Off, "anything else is off");

	Section("A specular light pass by its id");
	Check(!IsSpecularLight(0x114) && IsSpecularLight(0x115) && IsSpecularLight(0x123) && IsSpecularLight(0x129) && !IsSpecularLight(0x12A),
	      "0x115..0x129 and nothing beside");
	Check(!IsSpecularLight(0x60) && !IsSpecularLight(0xE8) && !IsSpecularLight(0x177) && !IsSpecularLight(0x190), "not a base, further light, shadow or plain pass");

	Section("A list's shape");
	List none;
	Check(none.Of() == Shape::Empty, "no record");
	Check(none.Of(false) == Shape::Counted, "the builder only counts: no list to look at, whatever is there");
	List one;
	one.Add(0x60);
	Check(one.Of() == Shape::Single, "one base pass");
	Check(one.Of(false) == Shape::Counted, "counting comes first");
	Check(one.Of(true, true) == Shape::Extra, "the property's extra object: another record may sit behind the one to mark");
	List chain;
	chain.Add(0x60);
	chain.Add(0xE8);
	chain.Add(0x108);
	Check(chain.Of() == Shape::Several, "a base pass, further lights and the texture pass");
	List shiny = chain;
	shiny.Add(0x123);
	Check(shiny.Of() == Shape::Specular, "a specular light pass at the end");
	List shinyFirst;
	shinyFirst.Add(0x115);
	shinyFirst.Add(0x60);
	Check(shinyFirst.Of() == Shape::Specular, "a specular light pass found at the far end of the walk");
	List shinyAlone;
	shinyAlone.Add(0x129);
	Check(shinyAlone.Of() == Shape::Specular, "a list of one specular light pass is not a single list");
	List withShadow = one;
	withShadow.Add(0x177);
	Check(withShadow.Of() == Shape::Several, "a shadow receiver pass is no specular pass");

	Section("A walk that stops where the count or the nodes end");
	List past = shinyFirst;
	past.count = 1;  // the count says one record: the specular pass behind it is not this list's
	Check(past.Of() == Shape::Single, "no further than the count");
	List torn = chain;
	torn.count = 9;  // more than there are nodes
	Check(torn.Of() == Shape::Several, "a walk that runs out of nodes ends there");
	List headless = one;
	headless.last = 0;
	Check(headless.Of() == Shape::Empty, "a count without a last node is taken as empty");

	Section("Which shapes fold");
	const Shape all[] = {Shape::Counted, Shape::Empty, Shape::Extra, Shape::Specular, Shape::Single, Shape::Several};
	bool offFolds = false, censusFolds = false;
	for (const Shape s : all) {
		offFolds = offFolds || Folds(Mode::Off, s);
		censusFolds = censusFolds || Folds(Mode::Census, s);
	}
	Check(!offFolds && !censusFolds, "off and the census fold nothing");
	Check(Folds(Mode::FoldSingle, Shape::Single) && !Folds(Mode::FoldSingle, Shape::Several), "1: the single lists only");
	Check(Folds(Mode::FoldAll, Shape::Single) && Folds(Mode::FoldAll, Shape::Several), "3: the lists of several records too");
	for (const Mode m : {Mode::FoldSingle, Mode::FoldAll}) {
		Check(!Folds(m, Shape::Counted) && !Folds(m, Shape::Empty) && !Folds(m, Shape::Extra) && !Folds(m, Shape::Specular),
		      "never a counted, empty, extra or specular list");
	}

	Section("The alternation");
	Check(Active(false, 0) && Active(false, 1) && Active(false, 7), "without it the route is always in force");
	Check(!Active(true, 0), "with it a run starts with the game's ending");
	Check(Active(true, 1) && !Active(true, 2) && Active(true, 3), "and every switch changes sides");
	Check(kRebuildBit == 0x40 && (kRebuildBit & 0x1F) == 0, "the rebuild bit is none of the mask's five");

	Section("The builder's own marking");
	Check(MarksLast(0x60) && MarksLast(0x108) && MarksLast(0x191), "any last record");
	Check(!MarksLast(0x190) && !MarksLast(0x192), "but a 0x190 or a 0x192");

	Section("The counts");
	Counts counts;
	Count(counts, Shape::Single, true, true);
	Count(counts, Shape::Single, true, false);
	Count(counts, Shape::Specular, false, false);
	Count(counts, Shape::Counted, false, false);
	Check(counts.calls == 4 && counts.folded == 2 && counts.marked == 1, "calls, folds and marks");
	Check(counts.byShape[static_cast<unsigned>(Shape::Single)] == 2 && counts.byShape[static_cast<unsigned>(Shape::Specular)] == 1 &&
	          counts.byShape[static_cast<unsigned>(Shape::Counted)] == 1 && counts.byShape[static_cast<unsigned>(Shape::Several)] == 0,
	      "by shape");
	return Failures();
}
