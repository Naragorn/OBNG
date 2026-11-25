// Every flow of the far grid's part of a cell crossing kept for the next step (src/engine/world/FarGridDeferLogic.h):
// the key, keeping the shift and the update, an update outside a crossing, the step's start with everything still
// as kept, after a jump, a load or another worldspace, a jump's own attach in the same frame, a second crossing before
// the step, the counts.

#include "Check.h"
#include "engine/world/FarGridDeferLogic.h"

using namespace obng::engine::world::far_grid_defer;
using namespace obng::test;

namespace {

constexpr std::uint32_t kGrid = 0x2A000000, kOtherGrid = 0x2B000000, kWorld = 0x3C000000, kOtherWorld = 0x3D000000;

}  // namespace

int main() {
	Section("The key");
	Check(ModeOf(1) == Mode::On, "1 is on");
	Check(ModeOf(0) == Mode::Off && ModeOf(2) == Mode::Off && ModeOf(-1) == Mode::Off, "anything else off");

	Section("The read facts as constants");
	Check(kShiftBytes[0] == 0x8B && kShiftBytes[5] == 0xFF && kShiftBytes[6] == 0xD0 && sizeof(kShiftBytes) == 7, "the seven bytes from the slot's load to the call");
	Check(kTesFarGrid == 4 && kTesCenterX == 0x20 && kTesCenterY == 0x24 && kTesWorld == 0x74, "TES's far grid, centre and worldspace");

	Section("Nothing kept");
	{
		Pending p;
		const Taken t = Take(p, kGrid, 5, -3, kWorld);
		Check(t.shift == Outcome::None && t.update == Outcome::None, "a step without a crossing: nothing to run");
		Check(!KeepsUpdate(p, kGrid, 5, -3), "an update with no shift kept (after a load or a jump) runs in place");
	}

	Section("A crossing: the shift and the update kept, run at the step's start");
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		Check(KeepsUpdate(p, kGrid, 5, -3), "the attach's update behind the kept shift is kept too");
		Check(!KeepsUpdate(p, kOtherGrid, 5, -3), "an update of another grid is not");
		KeepUpdate(p, kWorld);
		const Taken t = Take(p, kGrid, 5, -3, kWorld);
		Check(t.shift == Outcome::Run && t.update == Outcome::Run, "the grid, the centre and the worldspace as kept: both run");
		Check(!p.shift && !p.update, "nothing kept afterwards");
		const Taken again = Take(p, kGrid, 5, -3, kWorld);
		Check(again.shift == Outcome::None && again.update == Outcome::None, "the next step finds nothing");
	}
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		const Taken t = Take(p, kGrid, 5, -3, kWorld);
		Check(t.shift == Outcome::Run && t.update == Outcome::None, "the far LOD off (no update called): the shift alone");
	}

	Section("Something changed in between: dropped");
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		KeepUpdate(p, kWorld);
		const Taken t = Take(p, kGrid, 9, 1, kWorld);
		Check(t.shift == Outcome::Drop && t.update == Outcome::Drop, "another centre: a jump shifted and updated by itself");
	}
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		KeepUpdate(p, kWorld);
		Check(Take(p, kGrid, 5, -3, kOtherWorld).update == Outcome::Drop, "another worldspace: the update dropped");
	}
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		KeepUpdate(p, kWorld);
		const Taken t = Take(p, kOtherGrid, 5, -3, kWorld);
		Check(t.shift == Outcome::Drop && t.update == Outcome::Drop, "another far grid (a load made a new one)");
	}
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		KeepUpdate(p, kWorld);
		const Taken t = Take(p, 0, 5, -3, kWorld);
		Check(t.shift == Outcome::Drop && t.update == Outcome::Drop, "no far grid now");
	}
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		KeepUpdate(p, 0);
		Check(Take(p, kGrid, 5, -3, 0).update == Outcome::Drop, "an update kept for no worldspace is never run");
	}
	{
		Pending p;
		KeepShift(p, 0, 5, -3);
		Check(Take(p, 0, 5, -3, kWorld).shift == Outcome::Drop, "a shift kept for no grid is never run");
	}

	Section("A jump or a load in the same frame, after a crossing's shift was kept");
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		Check(!KeepsUpdate(p, kGrid, 9, 1), "its attach (TES at another centre, its own shift ran in place) updates in place");
		const Taken t = Take(p, kGrid, 9, 1, kWorld);
		Check(t.shift == Outcome::Drop && t.update == Outcome::None, "the crossing's kept shift is dropped at the step, nothing else runs");
	}
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		Check(KeepsUpdate(p, kGrid, 5, -3), "a jump to the very centre the crossing went to: its update is kept");
		KeepUpdate(p, kWorld);
		const Taken t = Take(p, kGrid, 5, -3, kWorld);
		Check(t.shift == Outcome::Run && t.update == Outcome::Run, "and both run at the step (the grid already there: a shift by nothing)");
	}

	Section("A second crossing before the step's start");
	{
		Pending p;
		KeepShift(p, kGrid, 5, -3);
		KeepUpdate(p, kWorld);
		KeepShift(p, kGrid, 6, -3);
		Check(p.merged == 1 && p.x == 6 && p.y == -3, "the later centre kept, the merge counted");
		const Taken t = Take(p, kGrid, 6, -3, kWorld);
		Check(t.shift == Outcome::Run && t.update == Outcome::Run, "one run brings the far grid to the later centre");
	}

	Section("The counts");
	{
		Counts c;
		Count(c, Taken{});
		Check(c.kept == 0, "a step with nothing kept is not counted");
		Count(c, Taken{Outcome::Run, Outcome::Run});
		Count(c, Taken{Outcome::Run, Outcome::None});
		Count(c, Taken{Outcome::Drop, Outcome::Drop});
		Count(c, Taken{Outcome::Run, Outcome::Drop});
		Check(c.kept == 4 && c.run == 2 && c.dropped == 2, "run, run without an update, dropped, and a dropped update");
	}
	return Failures();
}
