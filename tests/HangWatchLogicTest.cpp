// Every flow of the hang watch's decision (src/engine/diagnostics/HangWatchLogic.h).

#include "Check.h"
#include "engine/diagnostics/HangWatchLogic.h"

using namespace obng::engine::diagnostics::hang_watch;

int main() {
	Section("Steps keep coming");
	State state;
	Check(!Decide(state, 0, 1000), "the first look starts the clock, no dump");
	Check(!Decide(state, 0, 1000 + kStallMs - 1), "no step yet, but not for long enough");
	Check(!Decide(state, 5, 1000 + kStallMs + 5000), "a step came: the clock starts again");
	Check(!Decide(state, 9, 1000 + 2 * kStallMs + 5000), "steps coming: never a dump");

	Section("A stall");
	State stall;
	Decide(stall, 100, 0);
	Check(!Decide(stall, 100, kStallMs - 1), "one millisecond short: not yet");
	Check(Decide(stall, 100, kStallMs), "no step for the limit: the dump");
	Check(!Decide(stall, 100, kStallMs + 1000) && !Decide(stall, 100, 10 * kStallMs), "one dump a stall");
	Check(!Decide(stall, 101, 10 * kStallMs + 1), "a step ends the stall");
	Check(!Decide(stall, 101, 11 * kStallMs), "the next stall is timed from that step");
	Check(Decide(stall, 101, 11 * kStallMs + 1), "and gets its own dump");

	Section("At most three dumps a run");
	State many;
	std::uint64_t now = 0, steps = 0;
	unsigned dumps = 0;
	for (int i = 0; i < 6; ++i) {
		Decide(many, ++steps, now);
		now += kStallMs;
		dumps += Decide(many, steps, now) ? 1 : 0;
		now += 1;
	}
	Check(dumps == kMaxDumps && many.dumps == kMaxDumps, "the fourth stall and later are not written");

	Section("A stack word worth writing");
	Check(!InExeCode(0) && !InExeCode(0x00400FFF) && InExeCode(0x00401000) && InExeCode(0x005515E2) && InExeCode(0x00A27FFF) && !InExeCode(0x00A28000)
	          && !InExeCode(0x00B39C00),
	      "the game's code section, not its data");
	return Failures();
}
