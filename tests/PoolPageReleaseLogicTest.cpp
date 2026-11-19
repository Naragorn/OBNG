// Every flow of the memory pools' page release held back at a cell crossing (src/engine/memory/PoolPageReleaseLogic.h).

#include "Check.h"
#include "engine/memory/PoolPageReleaseLogic.h"

using namespace obng::engine::memory::pool_page_release;

int main() {
	Section("The INI's value: 1 skips, 2 measures, everything else is off");
	Check(ModeFrom(0) == Mode::Off && ModeFrom(3) == Mode::Off && ModeFrom(0xFFFFFFFFu) == Mode::Off, "0 and unknown values: off");
	Check(ModeFrom(1) == Mode::Skip, "1: the release skipped at a crossing");
	Check(ModeFrom(2) == Mode::Measure, "2: the purge timed, nothing skipped");

	Section("Which purge is a cell crossing's: the two calls in the grid's move 0x00444FB0");
	Check(FromCrossing(0x00445376), "after the call at 0x00445371 (the interior buffer emptied)");
	Check(FromCrossing(0x004454F4), "after the call at 0x004454EF (every crossing)");
	Check(!FromCrossing(0x00445684) && !FromCrossing(0x004458DB) && !FromCrossing(0x004459F0), "0x004455E0's three calls: not a crossing");
	Check(!FromCrossing(0x00445371) && !FromCrossing(0), "a call's own address or none: not a return address of the crossing");

	Section("The decision for one page release");
	Check(Decide(Mode::Skip, 7, 7) == Act::Skip, "skip mode, inside this thread's crossing purge: skipped");
	Check(Decide(Mode::Skip, 0, 7) == Act::Original, "skip mode, no crossing purge running: the original");
	Check(Decide(Mode::Skip, 9, 7) == Act::Original, "skip mode, another thread's crossing purge: the original (it is not ours)");
	Check(Decide(Mode::Measure, 7, 7) == Act::Original, "measure mode: never skipped");
	Check(Decide(Mode::Off, 7, 7) == Act::Original, "off: never skipped");

	Section("A pool's fields summed: reserved, committed and the committed peak in bytes");
	{
		PoolTotals totals;
		totals.Add(PoolFields{0x800, 100, 300});
		totals.Add(PoolFields{0x400, 0, 2});
		Check(totals.pools == 2, "two pools");
		Check(totals.reservedBytes == (0x800ull + 0x400ull) * kPageBytes, "reserved: the pages a pool reserved at its making");
		Check(totals.committedBytes == 100ull * kPageBytes && totals.peakBytes == 302ull * kPageBytes, "committed now and the peaks, in pages of 4 KB");
		PoolTotals empty;
		Check(empty.pools == 0 && empty.reservedBytes == 0 && empty.committedBytes == 0, "no pool: all zero");
	}

	Section("The counts: purges by where they come from, releases by what happened");
	{
		Counts c;
		c.NotePurge(true, 5.0);
		c.NotePurge(true, 12.5);
		c.NotePurge(false, 40.0);
		Check(c.crossingPurges == 2 && c.otherPurges == 1, "two at a crossing, one elsewhere");
		Check(c.crossingMs == 17.5 && c.longestCrossingMs == 12.5, "the crossing's time in all and its longest");
		Check(c.otherMs == 40.0 && c.longestOtherMs == 40.0, "a purge elsewhere counted apart");
		c.NoteRelease(Act::Skip, 0.0);
		c.NoteRelease(Act::Original, 3.0);
		c.NoteRelease(Act::Original, 1.0);
		Check(c.skipped == 1 && c.released == 2, "one skipped, two run");
		Check(c.releaseMs == 4.0 && c.longestReleaseMs == 3.0, "the release's own time, only when it ran");
	}
	return Failures();
}
