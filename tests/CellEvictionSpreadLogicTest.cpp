// Every flow of the cell eviction's spread over frames (src/engine/world/CellEvictionSpreadLogic.h).

#include "Check.h"
#include "engine/world/CellEvictionSpreadLogic.h"

using namespace obng::engine::world::cell_eviction_spread;

namespace {

// A cell task's end in the frame's pump that would evict.
EndFacts Evicting() {
	EndFacts f;
	f.enabled = true;
	f.framePump = true;
	f.cellTask = true;
	f.hasCell = true;
	f.queued = true;
	f.cellHasNode = true;
	f.exterior = true;
	f.lastSlotTaken = true;
	return f;
}

}  // namespace

int main() {
	Section("The INI's value");
	Check(ModeOf(0) == Mode::Off && ModeOf(1) == Mode::On && ModeOf(2) == Mode::Off, "0 off, 1 on, anything else off");

	Section("A task's end: held when it would evict");
	Check(WayOf(Evicting()) == EndWay::Hold, "a cell task in the frame's pump whose push evicts: held");
	EndFacts f = Evicting();
	f.enabled = false;
	Check(WayOf(f) == EndWay::Run, "the route off: run");
	f = Evicting();
	f.framePump = false;
	Check(WayOf(f) == EndWay::Run, "a pump call outside the frame's (a wait for the queue): run");
	f = Evicting();
	f.flushing = true;
	Check(WayOf(f) == EndWay::Run, "the route ending held tasks itself: run");
	f = Evicting();
	f.menuMode = true;
	Check(WayOf(f) == EndWay::Run, "menu mode: run");
	f = Evicting();
	f.cellTask = false;
	Check(WayOf(f) == EndWay::Run, "another kind of task: run");
	f = Evicting();
	f.cancelled = true;
	Check(WayOf(f) == EndWay::Run, "a cancelled task pushes nothing: run");
	f = Evicting();
	f.hasCell = false;
	Check(WayOf(f) == EndWay::Run, "no cell: run");
	f = Evicting();
	f.queued = false;
	Check(WayOf(f) == EndWay::Run, "the record's flag off: no push, run");
	f = Evicting();
	f.cellHasNode = false;
	Check(WayOf(f) == EndWay::Run, "a cell without its node is not pushed: run");
	f = Evicting();
	f.exterior = false;
	Check(WayOf(f) == EndWay::Run, "an interior goes to the other buffer: run");
	f = Evicting();
	f.inBuffer = true;
	Check(WayOf(f) == EndWay::Run, "the cell in the buffer already only moves up: run");
	f = Evicting();
	f.lastSlotTaken = false;
	Check(WayOf(f) == EndWay::Run, "the buffer not full: nothing evicted, run");

	Section("A task's end while one is held");
	f = Evicting();
	f.held = 1;
	f.lastSlotTaken = false;
	f.queued = false;
	Check(WayOf(f) == EndWay::Hold, "a cell task behind a held one is held too, whatever it would do: the order stays");
	f.cellTask = false;
	Check(WayOf(f) == EndWay::Run, "another kind of task still runs");
	f = Evicting();
	f.held = kMaxHeld;
	Check(WayOf(f) == EndWay::Run, "the list full: run");

	Section("A frame's step");
	Check(StepOf(0, 0, false, true, false) == StepWay::None, "nothing held: nothing");
	Check(StepOf(1, 3, false, true, false) == StepWay::Strip, "a victim with 3D left: a slice");
	Check(StepOf(1, 3, false, true, true) == StepWay::RunEnd, "the victim stripped: the oldest end runs");
	Check(StepOf(1, 3, false, false, false) == StepWay::RunEnd, "the last slot empty (the buffer was purged): the end runs");
	Check(StepOf(2, 3, true, true, false) == StepWay::Flush, "menu mode: everything at once");
	Check(StepOf(1, kMaxFrames, false, true, false) == StepWay::Strip, "at the frame limit still a slice");
	Check(StepOf(1, kMaxFrames + 1, false, true, false) == StepWay::Flush, "past it: everything at once");

	Section("Which references are stripped ahead");
	Check(Strippable(kPlacedReference, false, true), "a placed reference with 3D");
	Check(Strippable(kCharacter, false, true) && Strippable(kCreature, false, true), "a character or a creature with 3D");
	Check(!Strippable(kPlacedReference, false, false) && !Strippable(kCharacter, false, false), "without 3D: nothing to do");
	Check(!Strippable(kPlacedReference, true, true) && !Strippable(kCharacter, true, true), "never the player");
	Check(!Strippable(0x30, false, true) && !Strippable(0x34, false, true), "no other kind of form");
	Check(IsActor(kCharacter) && IsActor(kCreature) && !IsActor(kPlacedReference), "which kinds are actors");

	Section("The slice's budget");
	Check(GoOn(0, 2000, false, 0) && GoOn(1999, 2000, false, 40), "a cheap reference under the budget: on");
	Check(!GoOn(2000, 2000, false, 40) && !GoOn(5000, 2000, false, 1), "at or past it: stop");
	Check(GoOn(0, 2000, true, 0), "an actor as the slice's first work: on");
	Check(GoOn(9000, 2000, true, 0), "an actor first even past the budget (the walk itself took it): on, so the strip ends");
	Check(!GoOn(10, 2000, true, 1), "an actor after other work: the next slice");
	return Failures();
}
