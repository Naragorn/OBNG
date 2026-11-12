// Every flow of the references' animation update rate (src/engine/scene/ReferenceAnimRateLogic.h).

#include "Check.h"
#include "engine/scene/ReferenceAnimRateLogic.h"

#include <memory>

using namespace obng::engine::scene::reference_anim_rate;

int main() {
	Section("The phase: from the handle, spread over the turns");
	Check(PhaseOf(0x1000, 1) == 0 && PhaseOf(0x1000, 0) == 0, "one turn or none: no phase");
	Check(PhaseOf(0x1000, 4) == (0x100u % 4) && PhaseOf(0x1010, 4) == (0x101u % 4), "the handle without its low four bits, by the turns");
	Check(PhaseOf(0x1010, 4) != PhaseOf(0x1020, 4), "neighbours sixteen bytes apart get different turns");

	Section("The route off: every call runs and nothing is noted");
	{
		auto table = std::make_unique<Table>();
		Counts c;
		Check(table->Decide(0x2000, 1, 1, c) == Call::Run && table->Decide(0x2000, 2, 0, c) == Call::Run, "every 1 or 0: run");
		table->Found(true, c);
		Check(table->Used() == 0 && c.calls == 2 && c.skipped == 0 && c.becameBusy == 0, "no slot taken, nothing found");
		Check(table->Decide(0, 3, 4, c) == Call::Run && table->Used() == 0, "a null reference: run, no slot");
	}

	Section("An idle reference: asked on its turn only");
	{
		auto table = std::make_unique<Table>();
		Counts c;
		const std::uint32_t ref = 0x3000;  // phase 0 of 4: its turn in frames 4, 8, ...
		Check(PhaseOf(ref, 4) == 0, "the test's reference has phase 0");
		Check(table->Decide(ref, 1, 4, c) == Call::Run && c.runNew == 1, "the first call runs: the table did not know it");
		table->Found(false, c);
		Check(c.becameBusy == 0 && c.becameIdle == 0, "the first finding is no change");
		Check(table->Decide(ref, 2, 4, c) == Call::Skip && table->Decide(ref, 3, 4, c) == Call::Skip, "frames 2 and 3: left out");
		table->Found(true, c);
		Check(c.becameBusy == 0, "a finding after a call left out is ignored");
		Check(table->Decide(ref, 4, 4, c) == Call::Run && c.runTurn == 1, "frame 4: its turn");
		table->Found(false, c);
		Check(table->Decide(ref, 5, 4, c) == Call::Skip && c.skipped == 3 && c.calls == 5, "left out again; the calls and the skips counted");
		const std::uint32_t other = 0x3010;  // phase 1 of 4: its turn in frames 3, 7, ...
		table->Decide(other, 5, 4, c);
		table->Found(false, c);
		Check(table->Decide(other, 6, 4, c) == Call::Skip && table->Decide(other, 7, 4, c) == Call::Run, "another handle has another turn");
		table->Found(false, c);
	}

	Section("A busy reference: every frame, and the changes counted");
	{
		auto table = std::make_unique<Table>();
		Counts c;
		const std::uint32_t ref = 0x3000;
		table->Decide(ref, 1, 4, c);
		table->Found(true, c);
		Check(table->Decide(ref, 2, 4, c) == Call::Run && c.runBusy == 1, "found busy: the next frame runs");
		table->Found(true, c);
		Check(table->Decide(ref, 3, 4, c) == Call::Run && c.runBusy == 2, "and the one after");
		table->Found(false, c);
		Check(c.becameIdle == 1, "found idle now: counted");
		Check(table->Decide(ref, 5, 4, c) == Call::Skip, "idle: left out off its turn");
		Check(table->Decide(ref, 8, 4, c) == Call::Run, "its turn");
		table->Found(true, c);
		Check(c.becameBusy == 1, "found busy on its turn: noticed late, counted");
		Check(table->Decide(ref, 9, 4, c) == Call::Run, "busy again: every frame");
		Check(table->Decide(ref, 10, 4, c) == Call::Run, "a call whose finding was never said keeps what was known");
	}

	Section("A reference not seen for long is treated as new");
	{
		auto table = std::make_unique<Table>();
		Counts c;
		const std::uint32_t ref = 0x3000;
		table->Decide(ref, 1, 4, c);
		table->Found(false, c);
		const std::uint32_t late = 1 + Table::kStale + 2;  // not its turn: (late + 0) % 4 != 0
		Check(late % 4 != 0, "the test's late frame is not the reference's turn");
		Check(table->Decide(ref, late, 4, c) == Call::Run && c.runNew == 2, "after the stale time: run as new, whatever the turn");
		table->Found(true, c);
		Check(c.becameBusy == 0, "and its finding is a first one, no change");
		Check(table->Used() == 1, "the same slot");
	}

	Section("A full probe run: run without a slot; a stale slot is taken over");
	{
		auto table = std::make_unique<Table>();
		Counts c;
		// References that share a home slot: found by search (the multiply spreads them).
		const std::uint32_t home = (0x4000u * 2654435761u) >> 18;
		std::uint32_t found[Table::kProbe + 1] = {};
		std::uint32_t n = 0;
		for (std::uint32_t r = 0x4000; n < Table::kProbe + 1 && r < 0x40000000u; r += 16) {
			if (((r * 2654435761u) >> 18) == home) {
				found[n++] = r;
			}
		}
		Check(n == Table::kProbe + 1, "seventeen references with one home found");
		for (std::uint32_t i = 0; i < Table::kProbe; ++i) {
			table->Decide(found[i], 10, 4, c);
			table->Found(false, c);
		}
		Check(table->Used() == Table::kProbe, "sixteen slots taken");
		const std::uint64_t before = c.runNoRoom;
		Check(table->Decide(found[Table::kProbe], 11, 4, c) == Call::Run && c.runNoRoom == before + 1, "the seventeenth: run, no room");
		table->Found(true, c);
		Check(table->Decide(found[Table::kProbe], 12, 4, c) == Call::Run && c.runNoRoom == before + 2, "and again: it is never left out");
		Check(table->Decide(found[Table::kProbe], 10 + Table::kStale + 1, 4, c) == Call::Run && c.runNoRoom == before + 2 && table->Used() == Table::kProbe,
		      "once the others are stale one of their slots is taken over");
	}

	Section("The clock: a frame is a change of the engine's clock");
	{
		Clock clock;
		clock.See(0);
		Check(clock.frame == 1, "the first reading is a frame, even at 0");
		clock.See(0);
		clock.See(0);
		Check(clock.frame == 1, "the same reading again: the same frame");
		clock.See(13);
		clock.See(13);
		clock.See(27);
		Check(clock.frame == 3, "each change is one frame");
	}
	return Failures();
}
