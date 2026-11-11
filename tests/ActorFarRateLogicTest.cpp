// Every flow of the far actors' update rate (src/engine/scene/ActorFarRateLogic.h).

#include "Check.h"
#include "engine/scene/ActorFarRateLogic.h"

#include <limits>

using namespace obng::engine::scene::actor_far_rate;

int main() {
	Section("EveryFor: the divisor only for a far actor with the switch on");
	Check(EveryFor(9000.0f, 6000.0f, 4) == 4, "far: every fourth call");
	Check(EveryFor(6000.0f, 6000.0f, 4) == 1 && EveryFor(100.0f, 6000.0f, 4) == 1, "at the distance or nearer: every call");
	Check(EveryFor(9000.0f, 6000.0f, 1) == 1 && EveryFor(9000.0f, 6000.0f, 0) == 1, "off: every call");
	Check(EveryFor(std::numeric_limits<float>::quiet_NaN(), 6000.0f, 4) == 1, "a distance that is not a number: every call");
	Check(EveryFor(std::numeric_limits<float>::infinity(), 6000.0f, 2) == 2, "infinitely far is far");

	Section("PhaseOf: spread by the handle, 0 when off");
	Check(PhaseOf(0x1000, 4) == 0 && PhaseOf(0x1040, 4) == 1 && PhaseOf(0x1080, 4) == 2 && PhaseOf(0x10C0, 4) == 3 && PhaseOf(0x1100, 4) == 0,
	      "objects 0x40 apart take turns");
	Check(PhaseOf(0x1040, 1) == 0 && PhaseOf(0x1040, 0) == 0, "off: no phase");

	Section("RunsNow: the first call always, then one call in n at the root's phase");
	State s;
	Check(RunsNow(s, 0x1040, 4) && s.ran && s.calls == 1, "the first call runs whatever the phase");
	// calls 1, 2, 3, 4, 5, 6, 7 with phase 1: (call + 1) % 4 == 0 at 3 and 7
	bool ran[7];
	for (int i = 0; i < 7; ++i) {
		ran[i] = RunsNow(s, 0x1040, 4);
	}
	Check(!ran[0] && !ran[1] && ran[2] && !ran[3] && !ran[4] && !ran[5] && ran[6], "then every fourth, at its phase");
	Check(s.calls == 8, "every call counted");
	State near;
	bool all = true;
	for (int i = 0; i < 9; ++i) {
		all = all && RunsNow(near, 0x1040, 1);
	}
	Check(all && near.calls == 9, "a near actor (every 1): every call runs");
	State switching;
	RunsNow(switching, 0x1000, 1);
	RunsNow(switching, 0x1000, 1);
	Check(!RunsNow(switching, 0x1000, 4) && !RunsNow(switching, 0x1000, 4) && RunsNow(switching, 0x1000, 4), "near then far: the count goes on (calls 2, 3 skipped, 4 runs at phase 0)");
	Check(RunsNow(switching, 0x1000, 1), "and near again: at once");

	Section("Two actors with different phases do not run on the same call");
	State a;
	State b;
	RunsNow(a, 0x1000, 2);
	RunsNow(b, 0x1040, 2);
	int together = 0;
	for (int i = 0; i < 10; ++i) {
		const bool ra = RunsNow(a, 0x1000, 2);
		const bool rb = RunsNow(b, 0x1040, 2);
		together += ra && rb ? 1 : 0;
		Check(ra != rb, "exactly one of the two");
	}
	Check(together == 0, "never together");
	return Failures();
}
