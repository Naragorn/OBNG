// Every flow of the sampled timer (plugin/src/SampledTimerLogic.h): which frames are timed, and what a timed
// frame's ticks stand for.

#include "Check.h"
#include "SampledTimerLogic.h"

using namespace obng::plugin::sampled_timer;
using namespace obng::test;

int main() {
	Section("Which frames are timed");
	Check(kEvery == 4, "one frame of four");
	Check(Times(0) && Times(4) && Times(8) && Times(4000000000ull), "every fourth, from the first");
	Check(!Times(1) && !Times(2) && !Times(3) && !Times(5) && !Times(7), "none of the three between");
	std::uint32_t timed = 0;
	for (std::uint64_t frame = 0; frame < 1000; ++frame) {
		timed += Times(frame) ? 1u : 0u;
	}
	Check(timed == 250, "250 of 1 000 frames");

	Section("What a timed frame's ticks stand for");
	Check(Whole(0) == 0 && Whole(1) == 4 && Whole(1000) == 4000, "the measured ticks times the frames a timed one stands for");
	// A section of the same cost in every frame: the sum over the timed frames, scaled, is the sum over all.
	std::uint64_t sampled = 0;
	std::uint64_t all = 0;
	for (std::uint64_t frame = 0; frame < 400; ++frame) {
		all += 37;
		if (Times(frame)) {
			sampled += Whole(37);
		}
	}
	Check(sampled == all, "an even cost comes out exact");
	return Failures();
}
