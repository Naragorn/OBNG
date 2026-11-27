// Every flow of NiTimeController's scaled time without its lock
// (src/engine/animation/ScaledTimeLogic.h): the first call's marks, the step,
// the cycle types, the clamp, the reverse flag, and the memo with its hit that
// leaves the reverse flag out.

#include "Check.h"
#include "engine/animation/ScaledTimeLogic.h"

#include <cmath>
#include <limits>

using namespace obng::engine::animation::scaled_time;

namespace {

Controller Running(std::uint8_t flags, float low, float high) {
	Controller c;
	c.flags = flags;
	c.low = low;
	c.high = high;
	c.start = 0.0f;
	c.last = 0.0f;
	return c;
}

// One call on a controller that has run before; no memo.
float Once(Controller c, float time) { return Compute(c, time, nullptr); }

void TestTheFirstCall() {
	Section("The first call: the marks set, the step by bit 0");
	Controller c;
	c.flags = 0x04;  // clamp
	c.low = 0.0f;
	c.high = 100.0f;
	c.scaled = 55.0f;
	Check(Compute(c, 7.0f, nullptr) == 7.0f, "bit 0 clear: the first step is the time itself");
	Check(c.start == 7.0f && c.last == 7.0f && c.scaled == 7.0f, "start and last set, the scaled sum begun at 0");
	Check(Compute(c, 9.0f, nullptr) == 9.0f && c.start == 7.0f && c.last == 9.0f && c.scaled == 9.0f, "the next call: the step is the difference, start kept");
	Controller d;
	d.flags = 0x05;
	d.high = 100.0f;
	Check(Compute(d, 7.0f, nullptr) == 0.0f && d.scaled == 0.0f && d.last == 7.0f, "bit 0 set: the first step is 0");
	Controller e = Running(0x04, 0.0f, 100.0f);
	e.start = -kUnset;
	Check(Compute(e, 3.0f, nullptr) == 3.0f && e.start == 3.0f, "a start not set with a last that is: only the start set");
}

void TestTheStep() {
	Section("The step: frequency times the difference onto the sum, plus the phase");
	Controller c = Running(0x04, 0.0f, 100.0f);
	c.frequency = 2.0f;
	c.phase = 1.5f;
	c.scaled = 10.0f;
	c.last = 4.0f;
	Check(Compute(c, 5.0f, nullptr) == 13.5f, "10 + 2 x 1, plus 1.5");
	Check(c.scaled == 12.0f && c.last == 5.0f, "the sum without the phase kept, the time kept");
}

void TestTheLoop() {
	Section("The cycle type 0: the offset from low wrapped by the span");
	Check(Once(Running(0x00, 2.0f, 6.0f), 11.0f) == 3.0f, "11 in [2, 6]: 9 mod 4 = 1, plus 2");
	Check(Once(Running(0x00, 2.0f, 6.0f), -1.0f) == 3.0f, "-1: -3 mod 4 = -3, plus 2 = -1 below low: plus the span");
	Check(Once(Running(0x00, 5.0f, 5.0f), 11.0f) == 5.0f, "a span of 0: low");
	Check(Once(Running(0x00, 2.0f, 6.0f), 6.0f) == 2.0f, "the high key itself wraps to low");
}

void TestTheReverse() {
	Section("The cycle type 1: the time itself wrapped by twice the span, folded back");
	Check(Once(Running(0x02, 0.0f, 4.0f), 3.0f) == 3.0f, "inside the first span: as it is");
	Check(Once(Running(0x02, 0.0f, 4.0f), 5.0f) == 3.0f, "past the span: folded back, 8 - 5");
	Check(Once(Running(0x02, 0.0f, 4.0f), -1.0f) == 1.0f, "negative: -1 + 8 = 7, folded: 1");
	Check(Once(Running(0x02, 1.0f, 5.0f), 5.0f) == 4.0f, "low is not taken off first: 5 mod 8 = 5, folded 3, plus low 1");
	Check(Once(Running(0x02, 3.0f, 3.0f), 5.0f) == 3.0f, "a span of 0: low");
}

void TestTheClampAndTheUnsetKeys() {
	Section("The clamp, the cycle types without a wrap, the keys not set");
	Check(Once(Running(0x04, 2.0f, 6.0f), 9.0f) == 6.0f, "type 2 above high: high");
	Check(Once(Running(0x04, 2.0f, 6.0f), 1.0f) == 2.0f, "below low: low");
	Check(Once(Running(0x06, 2.0f, 6.0f), 4.0f) == 4.0f, "type 3 inside: as it is");
	Check(Once(Running(0x00, 2.0f, -kUnset), 11.0f) == -kUnset, "a high key not set: no wrap, and the clamp gives that high");
	Check(Once(Running(0x00, kUnset, 6.0f), 4.0f) == kUnset, "a low key not set: no wrap, and the clamp gives that low");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	Check(std::isnan(Once(Running(0x04, 2.0f, 6.0f), nan)), "a NaN time is neither above nor below: it stays");
}

void TestTheReverseFlag() {
	Section("Bit 4: the result mirrored in [low, high]");
	Check(Once(Running(0x14, 2.0f, 6.0f), 3.0f) == 5.0f, "6 - (3 - 2)");
	Check(Once(Running(0x10, 2.0f, 6.0f), 11.0f) == 5.0f, "after the wrap: 3 mirrored");
}

void TestTheMemo() {
	Section("The memo: the last call's keys give the last result");
	Memo memo;
	Controller a = Running(0x00, 2.0f, 6.0f);
	Check(Compute(a, 11.0f, &memo) == 3.0f, "a miss computes");
	Check(memo.result == 3.0f && memo.time == 11.0f && memo.low == 2.0f && memo.high == 6.0f && memo.cycle == 0, "and fills the memo");
	memo.result = 77.0f;  // to see the hit
	Controller b = Running(0x00, 2.0f, 6.0f);
	Check(Compute(b, 11.0f, &memo) == 77.0f && b.last == 11.0f && b.scaled == 11.0f, "the same keys: the memo's result, the controller's fields written all the same");
	Controller reversed = Running(0x10, 2.0f, 6.0f);
	Check(Compute(reversed, 11.0f, &memo) == 77.0f, "a hit leaves the reverse flag out: the last result as it is");
	Controller c = Running(0x00, 2.0f, 6.0f);
	Check(Compute(c, 12.0f, &memo) == 4.0f && memo.time == 12.0f, "another time: a miss");
	Controller d = Running(0x00, 1.0f, 6.0f);
	d.scaled = 12.0f;
	d.last = 12.0f;
	Check(Compute(d, 12.0f, &memo) == 2.0f && memo.low == 1.0f, "another low: a miss (11 mod 5 = 1, plus 1)");
	Controller e = Running(0x00, 1.0f, 7.0f);
	Compute(e, 12.0f, &memo);
	Check(memo.high == 7.0f, "another high: a miss");
	Controller f = Running(0x04, 1.0f, 7.0f);
	Check(Compute(f, 12.0f, &memo) == 7.0f && memo.cycle == 2, "another cycle type: a miss");
	Controller g = Running(0x14, 1.0f, 7.0f);
	Check(Compute(g, 13.0f, &memo) == 1.0f && memo.result == 1.0f, "a miss with the reverse flag keeps the mirrored result");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	Controller h = Running(0x04, 1.0f, 7.0f);
	Compute(h, nan, &memo);
	memo.result = 55.0f;
	Controller i = Running(0x04, 1.0f, 7.0f);
	Check(std::isnan(Compute(i, nan, &memo)), "a NaN time never hits");
}

}  // namespace

int main() {
	TestTheFirstCall();
	TestTheStep();
	TestTheLoop();
	TestTheReverse();
	TestTheClampAndTheUnsetKeys();
	TestTheReverseFlag();
	TestTheMemo();
	return Failures();
}
