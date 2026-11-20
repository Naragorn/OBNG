// Every flow of the quest targets' rebuild one frame later (src/engine/world/QuestTargetDeferLogic.h).

#include "Check.h"
#include "engine/world/QuestTargetDeferLogic.h"

using namespace obng::engine::world::quest_target_defer;

int main() {
	Section("The INI's value");
	Check(ModeOf(0) == Mode::Off && ModeOf(1) == Mode::On && ModeOf(2) == Mode::Off, "0 off, 1 on, anything else off");

	Section("Nothing kept");
	Pending none;
	Check(Take(none, 0x1000u) == Outcome::None, "no call kept: nothing to run");

	Section("A crossing's call");
	Pending one;
	Keep(one, 0x1000u, 0x1000u);
	Check(one.pending && one.player == 0x1000u && one.argument == 0x1000u, "kept with its arguments");
	Check(Take(one, 0x1000u) == Outcome::Run, "the same player at the next step: run");
	Check(!one.pending && Take(one, 0x1000u) == Outcome::None, "run once");

	Section("Another player since (a load in between)");
	Pending other;
	Keep(other, 0x1000u, 0x1000u);
	Check(Take(other, 0x2000u) == Outcome::Drop && !other.pending, "dropped");
	Pending gone;
	Keep(gone, 0, 0);
	Check(Take(gone, 0) == Outcome::Drop, "no player: dropped");

	Section("Two calls before a step");
	Pending two;
	Keep(two, 0x1000u, 0x1000u);
	Keep(two, 0x1000u, 0x1000u);
	Check(two.merged == 1, "the second merged into one run");
	Check(Take(two, 0x1000u) == Outcome::Run && Take(two, 0x1000u) == Outcome::None, "one run serves both");
	return Failures();
}
