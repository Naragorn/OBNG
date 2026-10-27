// Tests of the rebuild of 0x0047D0F0 (proposed TimeInfo::Unfreeze), the second
// half of the clock freeze bracket. Evidence: re/functions/0x0047D0F0.md and
// re/export/time-info/0x0047D0F0/asm.s.
//
// The mapped control flow has four flows (re/functions/0x0047D0F0.md, "Control flow"):
//   F1  counter zero       - JZ 0x0047D113 at 0x0047D0F7 taken: nothing written, no tick (the refusal
//                            path: an unbalanced extra unfreeze is absorbed)
//   F2  still nested       - SUB AL,1 at 0x0047D0F9 leaves a nonzero byte, JNZ at 0x0047D0FD taken:
//                            the byte is stored, no tick
//   F3  outermost unfreeze - the decrement reaches 0: GetTickCount once (0x0047D0FF), then
//                            +0x10 := now - +0x10 (0x0047D107/0x0047D10D) and
//                            +0x14 := now - +0x14 (0x0047D10A/0x0047D110)
//   F3w the same with now < field: the 32-bit SUB wraps, no clamp (0x0047D107/0x0047D10A)
// The expected numbers of F2, F3 and F3w are the emulator's (tools/difftest/emulate.py on the
// exported bytes, as re/functions/0x0047D0F0.md reports them); an emulator run, not a game run.
//
// No float and no x87 instruction is in the body (read, asm.s), so no result here depends on
// the x87 precision control.
//
// Not covered: the EAX value the original leaves on exit (AL = 0 on F1, the new counter on F2,
// the new +0x14 on F3, read). Whether a caller reads it is unverified, so the rebuild returns
// nothing; an in-game differential check would have to compare it if a caller turns out to use it.

#include "Check.h"
#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_Unfreeze.h"
#include "engine/time/TimeInfo_UnfreezeLogic.h"

#include <cstdint>
#include <cstring>

namespace logic = obng::engine::time::unfreeze;
namespace engine = obng::engine::time::unfreeze::engine;
using obng::engine::time::TimeInfo;

namespace {

// A GetTickCount stand-in that counts how often it is read.
struct FakeTickSource {
	std::uint32_t now = 0;
	int calls = 0;

	std::uint32_t operator()() {
		++calls;
		return now;
	}
};

// A TimeInfo whose untouched bytes carry recognisable values, to show they stay as they were.
TimeInfo MakeTimeInfo(std::uint8_t depth, std::uint32_t clockMs, std::uint32_t startTick) {
	TimeInfo info{};
	info.freezeDepth = depth;
	info.pad[0] = 0xA1;
	info.pad[1] = 0xA2;
	info.pad[2] = 0xA3;
	info.fixedStepMs = 16.0f;
	info.fixedStepCarryMs = 0.5f;
	info.frameSeconds = 0.025f;
	info.clockMs = clockMs;
	info.startTick = startTick;
	return info;
}

bool SameBytesOutside0x00And0x10And0x14(const TimeInfo& before, const TimeInfo& after) {
	const auto* a = reinterpret_cast<const unsigned char*>(&before);
	const auto* b = reinterpret_cast<const unsigned char*>(&after);
	return std::memcmp(a + 0x01, b + 0x01, 0x10 - 0x01) == 0;
}

bool SameBytes(const TimeInfo& before, const TimeInfo& after) {
	return std::memcmp(&before, &after, sizeof(TimeInfo)) == 0;
}

void ClassifyEveryCounterValue() {
	Section("Classification: which flow each counter byte takes (0x0047D0F5..0x0047D0FD)");
	Check(logic::ClassifyUnfreeze(0x00) == logic::UnfreezeFlow::CounterZero, "0 takes JZ at 0x0047D0F7 (F1)");
	Check(logic::ClassifyUnfreeze(0x01) == logic::UnfreezeFlow::Outermost, "1 decrements to 0, JNZ at 0x0047D0FD falls through (F3)");
	Check(logic::ClassifyUnfreeze(0x02) == logic::UnfreezeFlow::StillNested, "2 decrements to 1, JNZ taken (F2)");
	Check(logic::ClassifyUnfreeze(0xFF) == logic::UnfreezeFlow::StillNested, "0xFF decrements to 0xFE, JNZ taken (F2)");
	int mismatches = 0;
	for (unsigned value = 0; value <= 0xFF; ++value) {
		const auto depth = static_cast<std::uint8_t>(value);
		const logic::UnfreezeFlow expected = value == 0 ? logic::UnfreezeFlow::CounterZero
			: value == 1                                ? logic::UnfreezeFlow::Outermost
														: logic::UnfreezeFlow::StillNested;
		if (logic::ClassifyUnfreeze(depth) != expected) {
			++mismatches;
		}
	}
	Check(mismatches == 0, "all 256 byte values: 0 -> F1, 1 -> F3, 2..0xFF -> F2");
}

void F1CounterZero() {
	Section("F1: counter zero - JZ 0x0047D113 at 0x0047D0F7, nothing written, GetTickCount not called");
	FakeTickSource tick{5000, 0};
	const logic::ClockFields before{0, 1010, 510};
	const logic::ClockFields after = logic::Unfreeze(before, tick);
	Check(after.depth == 0, "the counter stays 0: not decremented below zero (guard at 0x0047D0F5)");
	Check(after.clockMs == 1010 && after.startTick == 510, "+0x10 and +0x14 unchanged");
	Check(tick.calls == 0, "GetTickCount (0x0047D0FF) is not reached");

	Section("F1 on the layout: an unbalanced extra unfreeze writes no byte at all");
	FakeTickSource layoutTick{5000, 0};
	TimeInfo info = MakeTimeInfo(0, 1010, 510);
	const TimeInfo original = info;
	engine::Unfreeze(info, layoutTick);
	Check(SameBytes(original, info), "all 0x18 bytes as they were (no store on this path, read)");
	Check(layoutTick.calls == 0, "no tick read");
}

void F2StillNested() {
	Section("F2: still nested - SUB AL,1 / MOV [ESI],AL / JNZ 0x0047D113 taken");
	FakeTickSource tick{5000, 0};
	const logic::ClockFields two = logic::Unfreeze(logic::ClockFields{2, 1010, 510}, tick);
	Check(two.depth == 1, "2 -> 1 (emulator: 2 -> 1)");
	Check(two.clockMs == 1010 && two.startTick == 510, "+0x10 and +0x14 unchanged");
	const logic::ClockFields top = logic::Unfreeze(logic::ClockFields{0xFF, 1010, 510}, tick);
	Check(top.depth == 0xFE, "0xFF -> 0xFE: an unsigned byte, no sign handling (emulator: 0xFF -> 0xFE)");
	Check(top.clockMs == 1010 && top.startTick == 510, "+0x10 and +0x14 unchanged at 0xFF too");
	Check(tick.calls == 0, "GetTickCount is not reached on F2");

	Section("F2 on the layout: only the byte at +0x00 changes");
	FakeTickSource layoutTick{5000, 0};
	TimeInfo info = MakeTimeInfo(3, 1010, 510);
	const TimeInfo original = info;
	engine::Unfreeze(info, layoutTick);
	Check(info.freezeDepth == 2, "3 -> 2 stored at +0x00 (0x0047D0FB)");
	Check(SameBytesOutside0x00And0x10And0x14(original, info), "+0x01..+0x0F unchanged (pad, +0x04, +0x08, +0x0C)");
	Check(info.clockMs == 1010 && info.startTick == 510, "+0x10 and +0x14 unchanged");
	Check(layoutTick.calls == 0, "no tick read");
}

void F3OutermostUnfreeze() {
	Section("F3: outermost unfreeze - counter 1 -> 0, one tick, +0x10 := now - +0x10, +0x14 := now - +0x14");
	FakeTickSource tick{3510, 0};
	const logic::ClockFields after = logic::Unfreeze(logic::ClockFields{1, 1010, 510}, tick);
	Check(after.depth == 0, "0 stored at +0x00 (0x0047D0FB)");
	Check(after.clockMs == 2500, "+0x10: 3510 - 1010 = 2500 (emulator: 2500)");
	Check(after.startTick == 3000, "+0x14: 3510 - 510 = 3000 (emulator: 3000)");
	Check(tick.calls == 1, "GetTickCount read exactly once (0x0047D0FF); both fields use the same now");

	Section("F3 on the layout: +0x00, +0x10, +0x14 written, nothing else");
	FakeTickSource layoutTick{3510, 0};
	TimeInfo info = MakeTimeInfo(1, 1010, 510);
	const TimeInfo original = info;
	engine::Unfreeze(info, layoutTick);
	Check(info.freezeDepth == 0, "+0x00 = 0");
	Check(info.clockMs == 2500 && info.startTick == 3000, "+0x10 = 2500, +0x14 = 3000");
	Check(SameBytesOutside0x00And0x10And0x14(original, info), "+0x0C (frameSeconds) is not reset, +0x04/+0x08 and pad untouched");
	Check(layoutTick.calls == 1, "one tick read");

	Section("F3 then F1: a second unfreeze after the outermost one is absorbed");
	engine::Unfreeze(info, layoutTick);
	Check(info.freezeDepth == 0 && info.clockMs == 2500 && info.startTick == 3000, "nothing changes on the extra call");
	Check(layoutTick.calls == 1, "and the clock is not read again");
}

void F3wWrap() {
	Section("F3w: outermost unfreeze with now below the field - the 32-bit SUB wraps, no clamp");
	FakeTickSource tick{0x00000DB6, 0};
	const logic::ClockFields after = logic::Unfreeze(logic::ClockFields{1, 0xFFFFF000u, 0x00010000u}, tick);
	Check(after.depth == 0, "counter 1 -> 0");
	Check(after.clockMs == 0x00001DB6u, "+0x10: 0xDB6 - 0xFFFFF000 = 0x00001DB6 mod 2^32 (emulator: 0x00001DB6)");
	Check(after.startTick == 0xFFFF0DB6u, "+0x14: 0xDB6 - 0x00010000 = 0xFFFF0DB6 mod 2^32 (emulator: 0xFFFF0DB6)");
	Check(tick.calls == 1, "one tick read");

	Section("F3w edge: now equal to the field gives 0, now one below gives 0xFFFFFFFF");
	FakeTickSource edgeTick{100, 0};
	const logic::ClockFields edge = logic::Unfreeze(logic::ClockFields{1, 100, 101}, edgeTick);
	Check(edge.clockMs == 0, "100 - 100 = 0");
	Check(edge.startTick == 0xFFFFFFFFu, "100 - 101 = 0xFFFFFFFF, not clamped to 0");
}

void PureTickApplication() {
	Section("The transform alone: ApplyOutermostUnfreeze(fields, now) is now - field per field");
	const logic::ClockFields after = logic::ApplyOutermostUnfreeze(logic::ClockFields{1, 1010, 510}, 3510);
	Check(after.depth == 0 && after.clockMs == 2500 && after.startTick == 3000, "the same result as F3");
	static_assert(logic::ApplyOutermostUnfreeze(logic::ClockFields{1, 0xFFFFF000u, 0x00010000u}, 0xDB6).clockMs == 0x1DB6u,
		"constexpr: wraps modulo 2^32");
	Check(true, "constexpr evaluation of the wrap holds (static_assert above)");
}

void EngineShape() {
	Section("Engine-facing shape: addresses and the views' offsets");
	Check(engine::kUnfreezeAddress == 0x0047D0F0u, "the function at 0x0047D0F0");
	Check(engine::kGetTickCountIatSlot == 0x00A280D0u,
		"GetTickCount through the IAT slot 0x00A280D0 (0x0047D0FF here, 0x0047D130 in 0x0047D120)");
	Check(engine::kFreezeDepthOffset == 0x00 && engine::kLastMsOffset == 0x10 && engine::kStartTickOffset == 0x14,
		"the three fields the body touches: +0x00, +0x10, +0x14");
}

} // namespace

int main() {
	ClassifyEveryCounterValue();
	F1CounterZero();
	F2StillNested();
	F3OutermostUnfreeze();
	F3wWrap();
	PureTickApplication();
	EngineShape();
	return Failures();
}
