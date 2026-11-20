// Every flow of the task end probe's table (src/engine/io/TaskEndsLogic.h).

#include "Check.h"
#include "engine/io/TaskEndsLogic.h"

using namespace obng::engine::io::task_ends;

int main() {
	Section("An empty table");
	Table table;
	Check(Find(table, 0x00A45BE8u) == nullptr && Used(table) == 0, "no kind yet");

	Section("Ends by kind");
	Add(table, 0x00A45BE8u, 100);
	Add(table, 0x00A45BE8u, 300);
	Add(table, 0x00A3F000u, 50);
	Check(Used(table) == 2, "two kinds");
	const Kind* cell = Find(table, 0x00A45BE8u);
	Check(cell != nullptr && cell->calls == 2 && cell->ticks == 400 && cell->longest == 300, "calls, sum and the longest of one kind");
	const Kind* second = Find(table, 0x00A3F000u);
	Check(second != nullptr && second->calls == 1 && second->ticks == 50 && second->longest == 50, "the other kind beside it");
	Add(table, 0x00A45BE8u, 200);
	Check(Find(table, 0x00A45BE8u)->longest == 300 && Find(table, 0x00A45BE8u)->calls == 3, "a shorter end keeps the longest");

	Section("More kinds than rows");
	Table full;
	for (std::uint32_t i = 0; i < kKinds; ++i) {
		Add(full, 0x1000u + i, 10);
	}
	Check(Used(full) == kKinds && full.other.calls == 0, "the table is full, nothing in the rest yet");
	Add(full, 0x9000u, 70);
	Add(full, 0x9001u, 30);
	Check(Used(full) == kKinds && Find(full, 0x9000u) == nullptr, "a further kind takes no row");
	Check(full.other.calls == 2 && full.other.ticks == 100 && full.other.longest == 70, "it is counted in the rest");
	Add(full, 0x1000u + 3, 5);
	Check(Find(full, 0x1000u + 3)->calls == 2 && full.other.calls == 2, "a known kind still finds its row");

	Section("Long ends");
	Check(!IsLong(1999, 2000) && IsLong(2000, 2000) && IsLong(5000, 2000), "long from the limit on");
	LongEnds longs;
	for (unsigned i = 0; i < kLongEnds + 3; ++i) {
		Keep(longs, LongEnd{0x00A45BE8u, 100u + i, 5000u + i, 1, 2});
	}
	Check(longs.count == kLongEnds && longs.dropped == 3, "the list holds its size, the rest is counted");
	Check(longs.ends[0].ticks == 100 && longs.ends[kLongEnds - 1].ticks == 100 + kLongEnds - 1, "the first ones are kept in their order");
	return Failures();
}
