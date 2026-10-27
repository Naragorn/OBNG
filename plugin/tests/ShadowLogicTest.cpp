// Every flow of the shadow check's logic (plugin/src/ShadowLogic.h): the
// bit-for-bit comparison field by field, the recovery of the original's tick
// from a bracket's rewrite, the mismatch counter with its log budget, the
// summary clock, and the planner histogram.

#include "Check.h"
#include "ShadowLogic.h"

#include <cstring>

using namespace obng::plugin::shadow;

namespace {

State Base() {
	State s{};
	s.info.freezeDepth = 0;
	s.info.pad[0] = 0x11;
	s.info.pad[1] = 0x22;
	s.info.pad[2] = 0x33;
	s.info.fixedStepMs = 0.0f;
	s.info.fixedStepCarryMs = 0.0f;
	s.info.frameSeconds = 0.013f;
	s.info.clockMs = 1013;
	s.info.startTick = 7;
	s.clockMs = 1013;
	s.previousClockMs = 1000;
	return s;
}

void TestDifferences() {
	Section("Two snapshots compared bit for bit");
	const State a = Base();
	State b = Base();
	Check(Differences(a, b) == 0, "identical snapshots: no field differs");

	b.info.frameSeconds = 0.014f;
	Check(Differences(a, b) == (1u << static_cast<unsigned>(Field::FrameSeconds)), "a different float: frameSeconds differs, nothing else");

	b = Base();
	std::uint32_t negZero = 0x80000000u;
	std::memcpy(&b.info.frameSeconds, &negZero, 4);
	State zero = Base();
	zero.info.frameSeconds = 0.0f;
	Check(Differences(zero, b) == (1u << static_cast<unsigned>(Field::FrameSeconds)), "-0.0f against +0.0f: frameSeconds differs");

	b = Base();
	b.info.pad[1] = 0x99;
	Check(Differences(a, b) == (1u << static_cast<unsigned>(Field::Pad)), "a pad byte counts: the original leaves them, so must the rebuild");

	b = Base();
	b.clockMs = 5;
	b.previousClockMs = 6;
	const std::uint32_t both = (1u << static_cast<unsigned>(Field::GlobalClockMs)) | (1u << static_cast<unsigned>(Field::GlobalPreviousClockMs));
	Check(Differences(a, b) == both, "both globals differ: both bits set, nothing else");

	b = Base();
	b.info.freezeDepth = 1;
	b.info.startTick = 8;
	const std::uint32_t two = (1u << static_cast<unsigned>(Field::FreezeDepth)) | (1u << static_cast<unsigned>(Field::StartTick));
	Check(Differences(a, b) == two, "freezeDepth and startTick differ");
	Check(std::strcmp(FieldName(Field::FixedStepCarryMs), "fixedStepCarryMs") == 0 && std::strcmp(FieldName(Field::Count), "?") == 0,
	      "field names for the log; Count is no field");
	Check(Bits(a, Field::FrameSeconds) == 0x3C54FDF4u, "Bits gives the float's pattern (0.013f)");
	Check(Bits(a, Field::Pad) == 0x332211u, "Bits packs the three pad bytes");
}

void TestRecoverTick() {
	Section("The bracket's GetTickCount reading recovered from its rewrite");
	State before = Base();
	before.info.clockMs = 1013;
	before.info.startTick = 7;
	State after = before;
	const std::uint32_t now = 5000;
	after.info.clockMs = now - before.info.clockMs;
	after.info.startTick = now - before.info.startTick;
	RecoveredTick t = RecoverFreezeTick(before, after, true);
	Check(t.transition && t.now == now && t.consistent, "outermost: now = after + before from +0x10, +0x14 agrees");

	after.info.startTick += 1;
	t = RecoverFreezeTick(before, after, true);
	Check(t.transition && t.now == now && !t.consistent, "the two fields disagree: recovered from +0x10, flagged inconsistent");

	t = RecoverFreezeTick(before, before, false);
	Check(!t.transition && t.consistent && t.now == 0, "no transition: no tick was read, nothing to recover");

	before.info.clockMs = 0xFFFFFFF0u;
	after = before;
	after.info.clockMs = 0x00000010u - before.info.clockMs;  // now = 0x10, wraps
	after.info.startTick = 0x00000010u - before.info.startTick;
	t = RecoverFreezeTick(before, after, true);
	Check(t.now == 0x10u && t.consistent, "the recovery wraps modulo 2^32 like the SUB");
}

void TestCounter() {
	Section("The counter and its log budget");
	Counter c;
	Check(!c.Record(false, 2) && c.calls == 1 && c.mismatches == 0, "a matching call: counted, nothing written");
	Check(c.Record(true, 2) && c.mismatches == 1 && c.linesWritten == 1, "first mismatch: written");
	Check(c.Record(true, 2) && c.linesWritten == 2, "second mismatch: written, the budget is 2");
	Check(!c.Record(true, 2) && c.mismatches == 3 && c.linesWritten == 2, "third mismatch: counted, not written");
	Counter none;
	Check(!none.Record(true, 0) && none.mismatches == 1, "a budget of 0 writes nothing and still counts");
}

void TestSummaryClock() {
	Section("The summary clock");
	SummaryClock s;
	Check(!s.Due(1000, 10000), "the first call starts the clock and is not due");
	Check(!s.Due(5000, 10000), "4 s later: not due");
	Check(s.Due(11000, 10000), "10 s after the start: due");
	Check(!s.Due(11500, 10000), "half a second after a summary: not due");
	Check(s.Due(21000, 10000), "10 s after the last summary: due again");
	SummaryClock wrap;
	wrap.Due(0xFFFFFF00u, 1000);
	Check(wrap.Due(0x00000400u, 1000), "GetTickCount wrapping past zero: the difference still counts");
}

void TestPlanner() {
	Section("The planner histogram");
	PlannerSample p;
	p.Add(0, 1);
	p.Add(0, 1);
	p.Add(0, 0);
	p.Add(0, 7);
	Check(p.frames == 4 && p.stepCounts[1] == 2 && p.stepCounts[0] == 1 && p.stepCounts[3] == 1 && p.lastType == 0,
	      "four frames: 0, 1, 1, 7 steps -> buckets 1, 2, 0, 1; the type remembered");
	p.Add(10, 2);
	Check(p.lastType == 10 && p.stepCounts[2] == 1, "a later frame of type 10 with 2 steps");
	p.Reset();
	Check(p.frames == 0 && p.stepCounts[1] == 0 && p.lastType == 0, "reset clears everything");
}

}  // namespace

int main() {
	TestDifferences();
	TestRecoverTick();
	TestCounter();
	TestSummaryClock();
	TestPlanner();
	return Failures();
}
