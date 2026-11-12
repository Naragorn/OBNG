// Every flow of the GPU pass timer's bookkeeping (plugin/src/GpuPassTimerLogic.h).

#include "Check.h"
#include "GpuPassTimerLogic.h"

#include <string>

using namespace obng::plugin::gpu_passes;

namespace {

Span List(std::uint32_t finish, std::uint32_t pass, std::uint64_t begin, std::uint64_t end, std::uint32_t records = 1) {
	Span s;
	s.kind = Kind::kList;
	s.finish = finish;
	s.pass = pass;
	s.begin = begin;
	s.end = end;
	s.records = records;
	return s;
}

}  // namespace

int main() {
	Section("FinishTracker: a new finish at the first list, another accumulator, or a pass id that does not rise");
	{
		FinishTracker t;
		bool fresh = false;
		Check(t.Next(0x100, 4, &fresh) == 0 && fresh, "the first list: finish 0, fresh");
		Check(t.Next(0x100, 9, &fresh) == 0 && !fresh, "a higher id on the same accumulator: the same finish");
		Check(t.Next(0x100, 9, &fresh) == 1 && fresh, "the same id again: the next finish");
		Check(t.Next(0x100, 5, &fresh) == 2 && fresh, "a lower id: the next finish");
		Check(t.Next(0x200, 6, &fresh) == 3 && fresh, "another accumulator: the next finish");
	}

	Section("Totals: spans by kind, lists by finish and pass, the frame's span");
	{
		static Totals t;
		Span spans[5];
		spans[0].kind = Kind::kActorShadows;
		spans[0].begin = 1000;
		spans[0].end = 3000;
		spans[1] = List(0, 132, 3000, 8000, 40);
		spans[1].target = (2048u << 16) | 1024u;
		spans[2] = List(0, 140, 8000, 9000, 5);
		spans[3] = List(1, 132, 9000, 10000, 3);
		spans[4].kind = Kind::kImageSpace;
		spans[4].begin = 10000;
		spans[4].end = 11000;
		t.AddFrame(spans, 5, 1000000);  // a microsecond a tick: 1000 ticks a millisecond
		Check(t.frames == 1, "one frame");
		Check(t.shadowsMs == 2.0 && t.imageSpaceMs == 1.0, "actor shadows 2 ms, image space 1 ms");
		Check(t.finish[0].ms == 6.0 && t.finish[0].lists == 2 && t.finish[0].records == 45 && t.finish[0].target == ((2048u << 16) | 1024u),
		      "finish 0: two lists, 6 ms, 45 records, its target");
		Check(t.finish[1].ms == 1.0, "finish 1: 1 ms");
		Check(t.spanMs == 10.0, "the span from the first stamp to the last: 10 ms");
		Check(t.listCount == 3, "three lists by finish and pass");
		t.AddFrame(spans, 5, 1000000);
		Check(t.listCount == 3 && t.list[0].seen == 2 && t.list[0].ms == 10.0, "a second frame adds to the same lists");
		t.AddFrame(spans, 0, 1000000);
		t.AddFrame(spans, 5, 0);
		Check(t.frames == 2, "no spans or no frequency: not counted");
		Span backwards = List(2, 7, 500, 400);
		t.AddFrame(&backwards, 1, 1000);
		Check(t.finish[2].ms == 0.0, "an end before its begin counts as 0");
	}

	Section("Totals: a full list table drops and counts");
	{
		static Totals t;
		for (std::uint32_t i = 0; i < Totals::kLists + 3; ++i) {
			Span s = List(0, i, 0, 10);
			t.AddFrame(&s, 1, 1000);
		}
		Check(t.listCount == Totals::kLists && t.listsDropped == 3, "the table's size kept, three dropped");
	}

	Section("LargestLists and FormatFrame");
	{
		static Totals t;
		Span spans[4] = {List(0, 1, 0, 3), List(0, 2, 3, 10), List(0, 3, 10, 11), List(0, 4, 11, 16)};
		t.AddFrame(spans, 4, 1000);
		std::uint32_t order[2];
		const std::uint32_t n = LargestLists(t, order, 2);
		Check(n == 2 && t.list[order[0]].key == 2 && t.list[order[1]].key == 4, "the two largest, the largest first");
		std::uint32_t all[8];
		Check(LargestLists(t, all, 8) == 4 && t.list[all[3]].key == 3, "room for all: all four, the smallest last");
		char line[600];
		FormatFrame(t, line, sizeof(line));
		Check(std::string(line).find("1 frames read back") != std::string::npos && std::string(line).find("#0 16.000") != std::string::npos,
		      "the line: frames and finish 0's time");
	}
	return Failures();
}
