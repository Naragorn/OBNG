// Every flow of the small-block pool's search for a page to commit (src/engine/memory/PoolScanLogic.h): the
// key's values, the original's walk, when the walk is left out, the fast search against the walk over many tables,
// what a search costs the original, the comparison of the check mode, the counts.

#include <vector>

#include "Check.h"
#include "engine/memory/PoolScanLogic.h"

using namespace obng::engine::memory::pool_scan;
using namespace obng::test;

namespace {

struct Table {
	std::vector<std::uint16_t> pages;
	std::uint32_t Committed() const {
		std::uint32_t n = 0;
		for (const std::uint16_t page : pages) {
			n += page != kFreePage ? 1 : 0;
		}
		return n;
	}
	std::uint32_t Walk() const {
		return FirstFree(static_cast<std::uint32_t>(pages.size()), [this](std::uint32_t i) { return pages[i]; });
	}
	std::uint32_t Fast() const {
		return FirstFreeFast(static_cast<std::uint32_t>(pages.size()), Committed(), [this](std::uint32_t i) { return pages[i]; });
	}
};

}  // namespace

int main() {
	Section("The key's values");
	Check(Shipped() == 0 && ModeOf(Shipped()) == Mode::Off, "off as shipped");
	Check(ModeOf(1) == Mode::Route && ModeOf(2) == Mode::Check, "1 the route, 2 the check");
	Check(ModeOf(3) == Mode::Off && ModeOf(-1) == Mode::Off, "anything else is off");

	Section("The patch site");
	Check(kSite == 0x00402493 && kSite + sizeof(kSiteBytes) == 0x004024A3, "sixteen bytes up to the loop's head");
	Check(kResume == 0x004024C2 && kNone == 0xFFFFFFFFu && kFreePage == 0xFFFF, "the resume compares the index with -1");

	Section("The original's walk");
	Check(Table{{}}.Walk() == kNone, "a pool without pages has none");
	Check(Table{{kFreePage}}.Walk() == 0, "the first page");
	Check(Table{{0, 3, kFreePage, kFreePage}}.Walk() == 2, "the lowest of several");
	Check(Table{{0, 3, 0x1FF, 1}}.Walk() == kNone, "every page committed: none");
	Check(Table{{0, 0xFFFE, kFreePage}}.Walk() == 2, "0xFFFE is a count, not a free page");

	Section("When the walk is left out");
	Check(NoneLeft(0, 0) && NoneLeft(4, 4), "committed as many as reserved");
	Check(NoneLeft(5, 4), "a count over the reserved pages is taken as none left too");
	Check(!NoneLeft(0, 1) && !NoneLeft(3, 4), "one page short of full is walked");

	Section("The fast search is the walk");
	bool same = true;
	std::uint32_t tables = 0;
	// Every table of up to nine pages with a free or a used page at each place.
	for (std::uint32_t size = 0; size <= 9; ++size) {
		for (std::uint32_t bits = 0; bits < (1u << size); ++bits) {
			Table t;
			for (std::uint32_t i = 0; i < size; ++i) {
				t.pages.push_back((bits >> i) & 1 ? kFreePage : static_cast<std::uint16_t>(i));
			}
			same = same && t.Fast() == t.Walk();
			++tables;
		}
	}
	Check(same && tables == 1023, "over every table of up to nine pages");
	Table big;
	big.pages.assign(4096, 7);
	Check(big.Fast() == kNone && big.Walk() == kNone, "a full pool of 4 096 pages");
	big.pages[4095] = kFreePage;
	Check(big.Fast() == 4095 && big.Walk() == 4095, "its last page free");

	Section("A counter that disagrees with the table");
	Table t{{0, kFreePage, 0}};
	const auto entry = [&t](std::uint32_t i) { return t.pages[i]; };
	Check(FirstFreeFast(3, 3, entry) == kNone && t.Walk() == 1, "the fast search trusts the counter: none, where the walk finds page 1");
	Check(!Agrees(FirstFreeFast(3, 3, entry), t.Walk()) && Agrees(1, 1) && Agrees(kNone, kNone), "which the check mode sees");

	Section("What a search costs the original");
	Check(Walked(kNone, 4096) == 4096, "none: the whole table");
	Check(Walked(0, 4096) == 1 && Walked(99, 4096) == 100, "found: up to and with the page");

	Section("The slot of an element size");
	Check(SlotOf(8) == 2 && SlotOf(12) == 3 && SlotOf(0x108) == 66, "the size in words, as the game's own table 0x00B33080");
	Check(SlotOf(0x10000) == kSlots - 1 && SlotOf(0) == 0, "a size beyond the table goes to its last slot");

	Section("The counts");
	Counts counts;
	counts.Note(12, 4096, kNone, true);
	counts.Note(12, 4096, kNone, true);
	counts.Note(12, 4096, 17, false);
	counts.Note(0x20, 512, kNone, false);
	Check(counts.calls == 4 && counts.none == 3 && counts.found == 1, "searches, none and found");
	Check(counts.leftOut == 2 && counts.walkedOriginal == 4096 * 2 + 18 + 512 && counts.walkedSaved == 4096 * 2, "the walks left out and their entries");
	Check(counts.bySlot[3].calls == 3 && counts.bySlot[3].none == 2 && counts.bySlot[3].pages == 4096 && counts.bySlot[8].calls == 1, "by element size");
	counts.NoteMismatch();
	Check(counts.mismatches == 1, "a mismatch");
	const Top top = counts.Busiest();
	Check(top.slot[0] == 3 && top.slot[1] == 8 && top.used == 2, "the busiest sizes first, the unused left out");
	return Failures();
}
