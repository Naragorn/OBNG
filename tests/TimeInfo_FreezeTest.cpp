// Tests of the rebuild of 0x0047D120 (proposed TimeInfo::Freeze): the first
// half of the clock-freeze bracket on a TimeInfo (TimeInfo.h, 0x18 bytes).
//
// The body has one branch (re/functions/0x0047D120.md, control flow):
//   B1  old counter != 0 (TEST AL,AL 0x0047D12A, JNZ 0x0047D12E)
//       taken:        nested entry - the counter is incremented, nothing else;
//       fall-through: outer entry  - GetTickCount (0x0047D130), then
//                     +0x10 := now - +0x10 and +0x14 := now - +0x14.
// The increment (ADD CL,0x1 0x0047D127, stored 0x0047D12C) runs before B1 on
// every call (read). The sections below are the mapping's four test flows,
// plus the per-decision units, the untouched bytes (claim 5) and the
// re-entry of the outer path after the 8-bit wrap (claim 8, derived).
//
// No floating point in the body (read: no x87 or SSE instruction in
// 0x0047D120..0x0047D145), so no precision-control reading applies here.

#include "Check.h"
#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_Freeze.h"
#include "engine/time/TimeInfo_FreezeLogic.h"

#include <cstdint>
#include <cstring>

namespace time_ns = obng::engine::time;
namespace freeze = obng::engine::time::freeze;

namespace {

// GetTickCount (IAT slot 0x00A280D0) as a port: returns a set tick and counts
// the calls.
struct FakeClock {
	std::uint32_t now = 0;
	int calls = 0;

	std::uint32_t TickCount() {
		++calls;
		return now;
	}
};

// A TimeInfo with every byte set to a recognisable pattern, so untouched
// bytes can be compared afterwards.
time_ns::TimeInfo PatternedTimeInfo(std::uint8_t depth, std::uint32_t clockMs, std::uint32_t startTick) {
	time_ns::TimeInfo info;
	std::memset(&info, 0xA5, sizeof(info));
	info.freezeDepth = depth;
	info.clockMs = clockMs;
	info.startTick = startTick;
	return info;
}

// Every byte outside +0x00, +0x10..+0x13 and +0x14..+0x17 is equal.
bool OtherBytesEqual(const time_ns::TimeInfo& a, const time_ns::TimeInfo& b) {
	unsigned char left[sizeof(time_ns::TimeInfo)];
	unsigned char right[sizeof(time_ns::TimeInfo)];
	std::memcpy(left, &a, sizeof(left));
	std::memcpy(right, &b, sizeof(right));
	return std::memcmp(left + 0x01, right + 0x01, 0x10 - 0x01) == 0;
}

void Units() {
	Section("Units: the three decisions as pure functions");
	Check(freeze::NextFreezeDepth(0) == 1, "0 -> 1 (ADD CL,0x1 0x0047D127)");
	Check(freeze::NextFreezeDepth(1) == 2, "1 -> 2");
	Check(freeze::NextFreezeDepth(0xFE) == 0xFF, "0xFE -> 0xFF");
	Check(freeze::NextFreezeDepth(0xFF) == 0x00, "0xFF wraps to 0x00 (8-bit add, no check)");
	Check(freeze::IsOuterEntry(0), "old 0: outer entry (B1 not taken)");
	Check(!freeze::IsOuterEntry(1), "old 1: nested (B1 taken)");
	Check(!freeze::IsOuterEntry(0x80), "old 0x80: nested - TEST AL,AL is a zero test, not a sign test");
	Check(!freeze::IsOuterEntry(0xFF), "old 0xFF: nested");
	Check(freeze::FrozenForm(1000, 400) == 600u, "now 1000, field 400 -> 600 (SUB 0x0047D138)");
	Check(freeze::FrozenForm(5, 10) == 0xFFFFFFFBu, "now 5, field 10 -> 0xFFFFFFFB (32-bit wrap)");
	Check(freeze::FrozenForm(0, 0x80000000u) == 0x80000000u, "now 0, field 2^31 -> 2^31 (no signed overflow trap)");
	const freeze::ClockFields shifted = freeze::Shift({400, 100}, 1000);
	Check(shifted.clockMs == 600u && shifted.startTick == 900u, "Shift rewrites both fields from the same tick");
}

void OuterEntry() {
	Section("Flow 1 (B1 fall-through): counter 0 - becomes 1, both fields become now - field, one GetTickCount");
	time_ns::TimeInfo info = PatternedTimeInfo(0, 400, 100);
	const time_ns::TimeInfo before = info;
	FakeClock clock;
	clock.now = 1000;
	freeze::RunFreeze(info, clock);
	Check(info.freezeDepth == 1, "+0x00: 0 -> 1 (0x0047D12C)");
	Check(clock.calls == 1, "GetTickCount called once (0x0047D130)");
	Check(info.clockMs == 600u, "+0x10 := 1000 - 400 (0x0047D138, 0x0047D13E)");
	Check(info.startTick == 900u, "+0x14 := 1000 - 100 (0x0047D13B, 0x0047D141)");
	Check(OtherBytesEqual(info, before), "+0x01..+0x0F untouched (claim 5)");
}

void NestedEntry() {
	Section("Flow 2 (B1 taken): counter 1 - becomes 2, fields unchanged, no GetTickCount");
	time_ns::TimeInfo info = PatternedTimeInfo(1, 400, 100);
	const time_ns::TimeInfo before = info;
	FakeClock clock;
	clock.now = 1000;
	freeze::RunFreeze(info, clock);
	Check(info.freezeDepth == 2, "+0x00: 1 -> 2");
	Check(clock.calls == 0, "GetTickCount not called (JNZ 0x0047D12E to 0x0047D144)");
	Check(info.clockMs == 400u && info.startTick == 100u, "+0x10 and +0x14 unchanged");
	Check(OtherBytesEqual(info, before), "+0x01..+0x0F untouched");
}

void WrappingEntry() {
	Section("Flow 3 (B1 taken on 0xFF): the counter wraps to 0, fields unchanged, no GetTickCount");
	time_ns::TimeInfo info = PatternedTimeInfo(0xFF, 400, 100);
	const time_ns::TimeInfo before = info;
	FakeClock clock;
	clock.now = 1000;
	freeze::RunFreeze(info, clock);
	Check(info.freezeDepth == 0, "+0x00: 0xFF -> 0x00 (8-bit wrap, no saturation)");
	Check(clock.calls == 0, "the old value 0xFF is nonzero: B1 taken, GetTickCount not called");
	Check(info.clockMs == 400u && info.startTick == 100u, "+0x10 and +0x14 unchanged");
	Check(OtherBytesEqual(info, before), "+0x01..+0x0F untouched");
}

void UnsignedSubtraction() {
	Section("Flow 4 (B1 fall-through, now below the fields): plain 32-bit subtraction, no check");
	time_ns::TimeInfo info = PatternedTimeInfo(0, 10, 0xFFFFFFF0u);
	FakeClock clock;
	clock.now = 5;
	freeze::RunFreeze(info, clock);
	Check(info.freezeDepth == 1, "+0x00: 0 -> 1");
	Check(clock.calls == 1, "GetTickCount called once");
	Check(info.clockMs == 0xFFFFFFFBu, "+0x10 := 5 - 10 = 0xFFFFFFFB");
	Check(info.startTick == 0x15u, "+0x14 := 5 - 0xFFFFFFF0 = 0x15 (tick counter wrapped since the start)");
}

void OuterPathAfterWrap() {
	Section("Derived (claim 8): 256 entries without an exit wrap the counter to 0; the 257th takes the outer path again");
	time_ns::TimeInfo info = PatternedTimeInfo(0, 400, 100);
	FakeClock clock;
	clock.now = 1000;
	for (int entry = 0; entry < 256; ++entry) {
		freeze::RunFreeze(info, clock);
	}
	Check(info.freezeDepth == 0, "after 256 entries the counter is 0");
	Check(clock.calls == 1, "only the first of them read the tick");
	Check(info.clockMs == 600u && info.startTick == 900u, "the fields keep the freezeDepth form of the first entry");
	clock.now = 1500;
	freeze::RunFreeze(info, clock);
	Check(info.freezeDepth == 1, "the 257th entry: 0 -> 1");
	Check(clock.calls == 2, "and reads the tick again");
	Check(info.clockMs == 900u && info.startTick == 600u, "and rewrites the already rewritten fields: 1500 - 600, 1500 - 900");
}

void EngineShape() {
	Section("Engine shape: address and layout the binding relies on");
	Check(time_ns::kTimeInfoFreeze == 0x0047D120u, "kTimeInfoFreeze is 0x0047D120");
	Check(sizeof(time_ns::freeze::engine::FreezeFn) == 4, "the function pointer is 4 bytes (32-bit build)");
}

} // namespace

int main() {
	Units();
	OuterEntry();
	NestedEntry();
	WrappingEntry();
	UnsignedSubtraction();
	OuterPathAfterWrap();
	EngineShape();
	return Failures();
}
