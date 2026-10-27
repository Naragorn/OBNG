// Tests of the rebuild of 0x0047D170 (proposed TimeInfo::Update): one
// GetTickCount value into the frame's seconds at +0x0C of a TimeInfo
// (TimeInfo.h, 0x18 bytes).
//
// The mapped control flow (re/functions/0x0047D170.md, revision 3):
//   B1  freezeDepth:              byte [this+0x00] != 0          JNZ 0x0047D18F
//   B2  variable vs fixed:   [this+0x04] == 0.0f (ordered)   JNP 0x0047D19D
//   B3  fixed: sign of n:    low dword of trunc(sum) >= 0    JGE 0x0047D1D9
//   B4  variable: zero:      delta != 0                      JNZ 0x0047D1F6
//   B5  variable: sign:      delta >= 0 (signed)             JGE 0x0047D20B
//   B6  variable: ceiling:   fdelta <= 166.66667f            JNZ 0x0047D21E
//   B7  variable: ms sign:   ms >= 0 (signed)                JGE 0x0047D22F
//                            (always taken - claim 17, derived)
// Each flow below names its branch outcomes. Expected bit patterns marked
// "measured" are the emulator's output on the original bytes (the note's
// flow table, F1..F19: C = committed case in tools/difftest/cases, S = the
// mapping's scratch case); "derived" ones are computed from read facts and
// say from which.
//
// Precision. The rebuild computes the 53-bit reading (x87 precision control
// 53 bits, round to nearest, all exceptions masked). The measured cases ran
// at PC 64 (C1..C6, S cases) and PC 24 (C7, S N4); claim 21 makes +0x0C the
// same at every precision for products in the float normal range, so those
// patterns hold for the 53-bit reading too (derived). Where PC 24 differs
// (a product in the float subnormal range, claim 21), the section
// "53-bit reading" pins the 53-bit result.

#include "Check.h"
#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_Update.h"
#include "engine/time/TimeInfo_UpdateLogic.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>

namespace time_ns = obng::engine::time;
namespace update = obng::engine::time::update;

namespace {

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

// A TimeInfo with every byte set to 0xA5, then the given fields, so bytes the
// body must not write can be compared afterwards.
time_ns::TimeInfo Patterned(std::uint8_t depth, float stepMs, float carryMs, std::uint32_t clockMs, std::uint32_t startTick) {
	time_ns::TimeInfo info;
	std::memset(&info, 0xA5, sizeof(info));
	info.freezeDepth = depth;
	info.fixedStepMs = stepMs;
	info.fixedStepCarryMs = carryMs;
	info.clockMs = clockMs;
	info.startTick = startTick;
	return info;
}

bool SameBytes(const time_ns::TimeInfo& a, const time_ns::TimeInfo& b, std::size_t from, std::size_t to) {
	unsigned char left[sizeof(time_ns::TimeInfo)];
	unsigned char right[sizeof(time_ns::TimeInfo)];
	std::memcpy(left, &a, sizeof(left));
	std::memcpy(right, &b, sizeof(right));
	return std::memcmp(left + from, right + from, to - from) == 0;
}

// The globals 0x00B39DB0 / 0x00B39DB4 before the call: a recognisable old
// value, so the copy at 0x0047D181 is visible.
update::TickGlobals OldGlobals() {
	return update::TickGlobals{0x11111111u, 0x22222222u};
}

struct Run {
	time_ns::TimeInfo info;
	update::TickGlobals globals;
	update::UpdateTrace trace;
};

Run RunOn(time_ns::TimeInfo info, std::uint32_t tick, float multiplier = 1.0f) {
	Run run{info, OldGlobals(), {}};
	run.trace = update::RunUpdate(run.info, tick, run.globals, multiplier);
	return run;
}

void Constants() {
	Section("Constants: the image's bytes (data.json of 0x0047D170, read)");
	Check(Bits(update::kTwoPow32) == 0x4F800000u, "0x00A2FC78 = 4294967296.0f (bytes 00 00 80 4f)");
	Check(Bits(update::kFrameCeilingMs) == 0x4326AAABu, "0x00A3D14C = 166.66667f (bytes ab aa 26 43)");
	Check(std::bit_cast<std::uint64_t>(update::kMillisecondsToSeconds) == 0x3F50624DE0000000ull,
	      "0x00A30E40 = the double with bytes 00 00 00 e0 4d 62 50 3f");
	Check(update::kMillisecondsToSeconds == static_cast<double>(0.001f), "0x00A30E40 is (double)0.001f, not the double nearest 0.001");
	Check(update::kCeilingMs == 0xA6u, "the clamp value MOV ESI,0xa6 0x0047D220 = 166");
	Check(update::kZeroDeltaMs == 1u, "the zero-delta value MOV ESI,0x1 0x0047D1F8");
}

void Units() {
	Section("Units: the decisions as pure functions");
	// 0x0047D176 SUB EDX,[ECX+0x14]
	Check(update::RelativeTick(3020, 2000) == 1020u, "RelativeTick: tick - startTick");
	Check(update::RelativeTick(5, 10) == 0xFFFFFFFBu, "RelativeTick wraps modulo 2^32");
	// 0x0047D179..0x0047D186
	const update::TickGlobals next = update::NextTickGlobals(update::TickGlobals{7, 3}, 1020);
	Check(next.currentTickMs == 1020u && next.previousTickMs == 7u, "NextTickGlobals: old current to previous, relative tick to current");
	// B1
	Check(!update::IsFrozen(0), "B1: depth 0 is not freezeDepth");
	Check(update::IsFrozen(1), "B1: depth 1 is freezeDepth");
	Check(update::IsFrozen(0x80), "B1: depth 0x80 is freezeDepth (CMP byte,0 / JNZ is a zero test)");
	Check(update::IsFrozen(0xFF), "B1: depth 0xFF is freezeDepth");
	// B2
	Check(update::IsVariableMode(0.0f), "B2: +0.0 is variable mode");
	Check(update::IsVariableMode(-0.0f), "B2: -0.0 is variable mode (ordered equal, claim 10)");
	Check(!update::IsVariableMode(16.708334f), "B2: a positive step is fixed mode");
	Check(!update::IsVariableMode(-5.0f), "B2: a negative step is fixed mode");
	Check(!update::IsVariableMode(std::numeric_limits<float>::quiet_NaN()), "B2: NaN is fixed mode (unordered: C3=C2=C0=1, even parity, JNP not taken)");
	Check(!update::IsVariableMode(FromBits(0x00000001u)), "B2: the smallest subnormal is fixed mode");
	Check(!update::IsVariableMode(std::numeric_limits<float>::infinity()), "B2: +inf is fixed mode");
	// The unsigned-conversion pattern (B3, B5, B7 share it)
	Check(update::ReadAsUnsigned(0) == 0.0, "ReadAsUnsigned(0) = 0 (JGE taken)");
	Check(update::ReadAsUnsigned(166) == 166.0, "ReadAsUnsigned(166) = 166 (JGE taken)");
	Check(update::ReadAsUnsigned(0x7FFFFFFFu) == 2147483647.0, "ReadAsUnsigned(INT_MAX) = 2^31 - 1 (JGE taken)");
	Check(update::ReadAsUnsigned(0x80000000u) == 2147483648.0, "ReadAsUnsigned(0x80000000) = 2^31 (fix-up: -2^31 + 2^32)");
	Check(update::ReadAsUnsigned(0xFFFFFFFFu) == 4294967295.0, "ReadAsUnsigned(0xFFFFFFFF) = 2^32 - 1 (fix-up: -1 + 2^32, exact at 53 bits)");
	// B4..B6 through FrameMs
	Check(update::VariableDelta(1013, 1000) == 13u, "VariableDelta: relative tick - clockMs");
	Check(update::VariableDelta(999, 1000) == 0xFFFFFFFFu, "VariableDelta wraps modulo 2^32");
	Check(!update::ExceedsCeiling(166), "B6: 166 is not above 166.66667f");
	Check(update::ExceedsCeiling(167), "B6: 167 is above 166.66667f");
	Check(update::ExceedsCeiling(0xFFFFFFFFu), "B6: a signed-negative delta reads as d + 2^32 and is above the ceiling (claim 16)");
	Check(update::FrameMs(0) == 1u, "FrameMs: B4 not taken - zero reads as 1 ms");
	Check(update::FrameMs(1) == 1u, "FrameMs: 1 stays 1");
	Check(update::FrameMs(13) == 13u, "FrameMs: B5 taken, B6 taken - 13 stays 13");
	Check(update::FrameMs(166) == 166u, "FrameMs: B6 taken at the edge - 166 stays 166");
	Check(update::FrameMs(167) == 166u, "FrameMs: B6 not taken - 167 clamps to 166");
	Check(update::FrameMs(0x7FFFFFFFu) == 166u, "FrameMs: B5 taken, B6 not taken - INT_MAX clamps to 166");
	Check(update::FrameMs(0x80000000u) == 166u, "FrameMs: B5 not taken - INT_MIN clamps to 166");
	Check(update::FrameMs(0xFFFFFFFFu) == 166u, "FrameMs: B5 not taken - -1 clamps to 166");
	// FISTP qword under RC = toward zero (0x0047D1B8..0x0047D1C3)
	Check(update::TruncateToInt64(17.375f) == 17, "TruncateToInt64(17.375) = 17 (toward zero, not ROUND as pseudo.c has it, claim 23)");
	Check(update::TruncateToInt64(16.708334f) == 16, "TruncateToInt64(16.708334) = 16");
	Check(update::TruncateToInt64(-5.5f) == -5, "TruncateToInt64(-5.5) = -5");
	Check(update::TruncateToInt64(3e9f) == 3000000000ll, "TruncateToInt64(3e9) = 3000000000 (qword target, no 32-bit overflow)");
	Check(update::TruncateToInt64(-9223372036854775808.0f) == std::numeric_limits<std::int64_t>::min(), "TruncateToInt64(-2^63) = -2^63");
	Check(update::TruncateToInt64(9223372036854775808.0f) == std::numeric_limits<std::int64_t>::min(),
	      "TruncateToInt64(2^63): out of range, the integer indefinite 0x8000000000000000");
	Check(update::TruncateToInt64(std::numeric_limits<float>::quiet_NaN()) == std::numeric_limits<std::int64_t>::min(),
	      "TruncateToInt64(NaN): the integer indefinite");
	Check(update::TruncateToInt64(-std::numeric_limits<float>::infinity()) == std::numeric_limits<std::int64_t>::min(),
	      "TruncateToInt64(-inf): the integer indefinite");
}

void FixedStepUnits() {
	Section("Units: the fixed step (0x0047D19F..0x0047D1E8)");
	const update::FixedStep plain = update::AdvanceFixedStep(0.0f, FromBits(0x4185AAABu));
	Check(plain.wholeMs == 16u, "carry 0 + 16.708334: 16 whole ms (B3 taken)");
	Check(Bits(plain.carryMs) == 0x3F355560u, "carry 0x3F355560 (measured C5)");
	const update::FixedStep negative = update::AdvanceFixedStep(0.0f, -5.0f);
	Check(negative.wholeMs == 0xFFFFFFFBu, "carry 0 + -5: n = 0xFFFFFFFB (B3 not taken)");
	Check(Bits(negative.carryMs) == 0xCF800000u, "carry -5 - (2^32 - 5) = -2^32 (measured S F1)");
}

void SecondsUnits() {
	Section("Units: the seconds (0x0047D238..0x0047D24D)");
	const update::Seconds thirteen = update::FrameSecondsFrom(update::ReadAsUnsigned(13), 1.0f);
	Check(Bits(thirteen.unscaled) == 0x3C54FDF4u && Bits(thirteen.scaled) == 0x3C54FDF4u, "13 ms at multiplier 1: 0x3C54FDF4 twice (measured C1)");
	const update::Seconds half = update::FrameSecondsFrom(update::ReadAsUnsigned(13), 0.5f);
	Check(Bits(half.unscaled) == 0x3C54FDF4u, "multiplier 0.5: the first store 0x0047D241 is unscaled (measured C6)");
	Check(Bits(half.scaled) == 0x3BD4FDF4u, "multiplier 0.5: the second store 0x0047D24D is scaled (measured C6)");
	const update::Seconds huge = update::FrameSecondsFrom(static_cast<double>(3e9f), 3.0e38f);
	Check(Bits(huge.unscaled) == 0x4A371B01u, "a 3e9 ms step: unscaled 0x4A371B01 (as F17)");
	Check(Bits(huge.scaled) == 0x7F800000u, "times 3e38 lies above FLT_MAX: +inf (overflow masked - assumed; derived, x87 FSTP rounds to float)");
}

void VariableNormal() {
	Section("F1 (C1): variable, B4 taken, B5 taken, B6 taken, B7 taken - 13 ms");
	const time_ns::TimeInfo before = Patterned(0, 0.0f, FromBits(0xA5A5A5A5u), 1000, 0);
	const Run run = RunOn(before, 1013);
	Check(run.trace.path == update::Path::Variable, "the variable path");
	Check(run.globals.previousTickMs == 0x11111111u, "0x00B39DB4 := old 0x00B39DB0 (0x0047D181)");
	Check(run.globals.currentTickMs == 1013u, "0x00B39DB0 := tick - start (0x0047D186)");
	Check(run.info.clockMs == 1013u, "+0x10 := tick - start (0x0047D23E)");
	Check(Bits(run.trace.unscaledSeconds) == 0x3C54FDF4u, "first +0x0C store 0x3C54FDF4 (0x0047D241)");
	Check(Bits(run.info.frameSeconds) == 0x3C54FDF4u, "+0x0C = 0x3C54FDF4 (0x0047D24D)");
	Check(run.trace.frameMs == 13u, "13 ms");
	Check(SameBytes(run.info, before, 0x00, 0x0C), "+0x00..+0x0B untouched (freezeDepth byte, pad, step, carry)");
	Check(SameBytes(run.info, before, 0x14, 0x18), "+0x14 untouched");
}

void VariableZero() {
	Section("F2 (C2): variable, B4 not taken - zero delta reads as 1 ms");
	const Run run = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 1000);
	Check(run.trace.frameMs == 1u, "1 ms (MOV ESI,0x1 0x0047D1F8, JMP over B5/B6)");
	Check(Bits(run.info.frameSeconds) == 0x3A83126Fu, "+0x0C = 0x3A83126F");
	Check(run.info.clockMs == 1000u, "+0x10 = 1000");
	Check(run.globals.currentTickMs == 1000u, "0x00B39DB0 = 1000");
}

void VariableClamp() {
	Section("F3 (C3), F12, F13: variable, B6 - the ceiling 166.66667f");
	const Run clamp = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 1500);
	Check(clamp.trace.frameMs == 166u, "F3: 500 ms clamps to 166 (B6 not taken)");
	Check(Bits(clamp.info.frameSeconds) == 0x3E29FBE8u, "F3: +0x0C = 0x3E29FBE8");
	Check(clamp.info.clockMs == 1500u, "F3: +0x10 = the real 1500, not last + 166 (claim 18)");
	const Run below = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 1166);
	Check(below.trace.frameMs == 166u, "F12: 166 ms stays 166 (B6 taken)");
	Check(Bits(below.info.frameSeconds) == 0x3E29FBE8u && below.info.clockMs == 1166u, "F12: 0x3E29FBE8, +0x10 = 1166");
	const Run above = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 1167);
	Check(above.trace.frameMs == 166u, "F13: 167 ms clamps to 166 (B6 not taken)");
	Check(Bits(above.info.frameSeconds) == 0x3E29FBE8u && above.info.clockMs == 1167u, "F13: 0x3E29FBE8, +0x10 = 1167");
	const Run intMax = RunOn(Patterned(0, 0.0f, 0.0f, 0, 0), 0x7FFFFFFFu);
	Check(intMax.trace.frameMs == 166u, "F14: delta INT_MAX - B5 taken, B6 not taken: 166");
	Check(Bits(intMax.info.frameSeconds) == 0x3E29FBE8u && intMax.info.clockMs == 0x7FFFFFFFu, "F14: 0x3E29FBE8, +0x10 = 0x7FFFFFFF");
}

void VariableNegative() {
	Section("F8..F10: variable, B5 not taken - a signed-negative delta reads as unsigned and clamps to 166");
	const Run f8 = RunOn(Patterned(0, 0.0f, 0.0f, 3000, 2000), 3020);
	Check(f8.trace.frameMs == 166u, "F8: start 2000, last 3000, tick 3020 (delta -1980): 166");
	Check(Bits(f8.info.frameSeconds) == 0x3E29FBE8u, "F8: +0x0C = 0x3E29FBE8");
	Check(f8.info.clockMs == 1020u && f8.globals.currentTickMs == 1020u, "F8: +0x10 and 0x00B39DB0 = 1020 (claim 3)");
	Check(f8.info.startTick == 2000u, "F8: +0x14 untouched");
	const Run f9 = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 999);
	Check(f9.trace.frameMs == 166u && Bits(f9.info.frameSeconds) == 0x3E29FBE8u && f9.info.clockMs == 999u, "F9: delta -1: 166, 0x3E29FBE8, +0x10 = 999");
	const Run f10 = RunOn(Patterned(0, 0.0f, 0.0f, 0x80000000u, 0), 0);
	Check(f10.trace.frameMs == 166u && Bits(f10.info.frameSeconds) == 0x3E29FBE8u && f10.info.clockMs == 0u, "F10: delta INT_MIN: 166, 0x3E29FBE8, +0x10 = 0");
}

void NegativeZeroStep() {
	Section("F18: +0x04 = -0.0 - B2 taken, the variable path");
	const time_ns::TimeInfo before = Patterned(0, FromBits(0x80000000u), 0.0f, 1000, 0);
	const Run run = RunOn(before, 1013);
	Check(run.trace.path == update::Path::Variable, "the variable path");
	Check(Bits(run.info.frameSeconds) == 0x3C54FDF4u && run.info.clockMs == 1013u, "0x3C54FDF4, +0x10 = 1013");
	Check(Bits(run.info.fixedStepMs) == 0x80000000u, "+0x04 keeps -0.0");
}

void B7Unreachable() {
	Section("B7 (claim 17, derived): ms reaching the multiply is in [1, 166], so the fix-up at 0x0047D231 never runs");
	const std::uint32_t deltas[] = {0u, 1u, 13u, 166u, 167u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
	bool allNonNegative = true;
	for (const std::uint32_t delta : deltas) {
		const std::uint32_t ms = update::FrameMs(delta);
		allNonNegative = allNonNegative && static_cast<std::int32_t>(ms) >= 0 && ms >= 1u && ms <= 166u;
	}
	Check(allNonNegative, "every delta class (zero, small, edge, above, INT_MAX, INT_MIN, -1) gives 1 <= ms <= 166");
}

void Frozen() {
	Section("F4 (C4): B1 taken - freezeDepth: +0x0C = +0.0, the globals still written, nothing else");
	const time_ns::TimeInfo before = Patterned(1, 0.0f, 0.0f, 1000, 0);
	const Run run = RunOn(before, 1013);
	Check(run.trace.path == update::Path::Frozen, "the freezeDepth path");
	Check(Bits(run.info.frameSeconds) == 0x00000000u, "+0x0C = +0.0 (FLDZ 0x0047D174, FSTP 0x0047D24D)");
	Check(run.globals.currentTickMs == 1013u && run.globals.previousTickMs == 0x11111111u, "0x00B39DB0 = 1013, 0x00B39DB4 = old (claim 5)");
	Check(SameBytes(run.info, before, 0x00, 0x0C), "+0x00..+0x0B untouched (the counter too: claim 8)");
	Check(SameBytes(run.info, before, 0x10, 0x18), "+0x10..+0x17 untouched (claim 8)");
	const time_ns::TimeInfo fixedFrozen = Patterned(0xFF, 16.0f, 0.5f, 1000, 7);
	const Run fixedRun = RunOn(fixedFrozen, 1013, 0.5f);
	Check(Bits(fixedRun.info.frameSeconds) == 0x00000000u, "freezeDepth in fixed mode at multiplier 0.5: still +0.0 (the multiplier is not applied)");
	Check(SameBytes(fixedRun.info, fixedFrozen, 0x00, 0x0C) && SameBytes(fixedRun.info, fixedFrozen, 0x10, 0x18), "and nothing else written");
	Check(fixedRun.globals.currentTickMs == 1006u, "0x00B39DB0 = 1013 - 7");
}

void FixedStep() {
	Section("F5 (C5): fixed, B2 not taken, B3 taken - the tick is ignored for +0x0C and +0x10");
	const time_ns::TimeInfo before = Patterned(0, FromBits(0x4185AAABu), 0.0f, 1000, 0);
	const Run run = RunOn(before, 0x12345678u);
	Check(run.trace.path == update::Path::Fixed, "the fixed path");
	Check(Bits(run.info.frameSeconds) == 0x3C88DFEBu, "+0x0C = 0x3C88DFEB, from the step");
	Check(run.info.clockMs == 1016u, "+0x10 = 1000 + 16");
	Check(Bits(run.info.fixedStepCarryMs) == 0x3F355560u, "+0x08 = 0x3F355560");
	Check(run.globals.currentTickMs == 0x12345678u, "0x00B39DB0 = the tick (claim 12: only the globals see it)");
	Check(SameBytes(run.info, before, 0x00, 0x08), "+0x00..+0x07 untouched (counter, pad, step)");
	Check(SameBytes(run.info, before, 0x14, 0x18), "+0x14 untouched");
}

void FixedCarryCrossing() {
	Section("F19: fixed, B3 taken - the carry crosses into the next whole millisecond");
	// Carry 0x3F2AAA00 is the float nearest the note's 0.6666565f (derived:
	// decimal to float); the measured outputs below agree with it.
	const Run run = RunOn(Patterned(0, FromBits(0x4185AAABu), FromBits(0x3F2AAA00u), 1016, 0), 1);
	Check(run.trace.fixedWholeMs == 17u, "sum 17.37499 truncates to 17");
	Check(run.info.clockMs == 1033u, "+0x10 = 1016 + 17");
	Check(Bits(run.info.fixedStepCarryMs) == 0x3EBFFEC0u, "+0x08 = 0x3EBFFEC0");
	Check(Bits(run.info.frameSeconds) == 0x3C88DFEBu, "+0x0C = 0x3C88DFEB (from the step, not from 17)");
}

void FixedNegativeStep() {
	Section("F15: fixed, B3 not taken - step -5.0: n reads as 2^32 - 5");
	const Run run = RunOn(Patterned(0, -5.0f, 0.0f, 1000, 0), 1);
	Check(run.trace.fixedWholeMs == 0xFFFFFFFBu, "n = 0xFFFFFFFB");
	Check(run.info.clockMs == 995u, "+0x10 = 1000 + 0xFFFFFFFB = 995");
	Check(Bits(run.info.fixedStepCarryMs) == 0xCF800000u, "+0x08 = -2^32 (0xCF800000)");
	Check(Bits(run.info.frameSeconds) == 0xBBA3D70Bu, "+0x0C = 0xBBA3D70B (-0.005)");
	Check(run.globals.currentTickMs == 1u, "0x00B39DB0 = 1");
}

void FixedLargeStep() {
	Section("F17: fixed, B3 not taken - step 3e9: n = 0xB2D05E00 is signed negative, the fix-up reads it right");
	const Run run = RunOn(Patterned(0, 3e9f, 0.0f, 1000, 0), 1);
	Check(run.trace.fixedWholeMs == 0xB2D05E00u, "n = 0xB2D05E00");
	Check(run.info.clockMs == 3000001000u, "+0x10 = 3000001000");
	Check(Bits(run.info.fixedStepCarryMs) == 0x00000000u, "+0x08 = +0.0");
	Check(Bits(run.info.frameSeconds) == 0x4A371B01u, "+0x0C = 0x4A371B01");
}

void FixedNaNStep() {
	Section("F16: fixed, B2 not taken on NaN - FISTP gives the integer indefinite, low dword 0 (B3 taken)");
	const Run run = RunOn(Patterned(0, FromBits(0x7FC00000u), 0.0f, 1000, 0), 1);
	Check(run.trace.path == update::Path::Fixed, "the fixed path");
	Check(run.trace.fixedWholeMs == 0u, "n = 0");
	Check(run.info.clockMs == 1000u, "+0x10 = 1000");
	Check(Bits(run.info.fixedStepCarryMs) == 0x7FC00000u, "+0x08 = 0x7FC00000");
	Check(Bits(run.info.frameSeconds) == 0x7FC00000u, "+0x0C = 0x7FC00000");
}

void Multiplier() {
	Section("F6 (C6): multiplier 0.5 - the scaled store wins");
	const Run run = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 1013, 0.5f);
	Check(Bits(run.trace.unscaledSeconds) == 0x3C54FDF4u, "first store 0x3C54FDF4 (0x0047D241)");
	Check(Bits(run.info.frameSeconds) == 0x3BD4FDF4u, "+0x0C = 0x3BD4FDF4 (0x0047D24D)");
	const Run fixedHalf = RunOn(Patterned(0, FromBits(0x4185AAABu), 0.0f, 1000, 0), 1, 0.5f);
	Check(Bits(fixedHalf.info.frameSeconds) == 0x3C08DFEBu, "fixed step at multiplier 0.5: 0x3C08DFEB (derived: 0x3C88DFEB halved, exact)");
}

void FiftyThreeBitReading() {
	Section("53-bit reading (claim 21): a scaled product in the float subnormal range");
	// 0.013f (0x3C54FDF4) * the float 0x00A7C08F lies in the float subnormal
	// range. At PC 53/64 the product is exact (48 bits) and rounded once by
	// FSTP 0x0047D24D: 0x00022E47. At PC 24 FMUL 0x0047D247 first rounds to a
	// 24-bit significand with the extended exponent, then FSTP rounds again:
	// 0x00022E48. Both derived (exact rational arithmetic in a session-local
	// Python check), not measured in the emulator. The rebuild gives the
	// 53-bit reading.
	const Run run = RunOn(Patterned(0, 0.0f, 0.0f, 1000, 0), 1013, FromBits(0x00A7C08Fu));
	Check(Bits(run.info.frameSeconds) == 0x00022E47u, "+0x0C = 0x00022E47 (53-bit reading; PC 24 would give 0x00022E48)");
}

void BracketDelta() {
	Section("Claim 30 (derived): after the outermost freeze bracket, delta = r - D; D > r reads as 166 ms");
	// start 0, last 1000, freeze at 1010 (last' = 10, start' = 1010),
	// unfreeze at 3010 (last' = 3000, start' = 2000), next update at 3020:
	// r = 20, D = 2000 - this is F8's state.
	const Run run = RunOn(Patterned(0, 0.0f, 0.0f, 3000, 2000), 3020);
	Check(run.trace.frameMs == 166u, "r - D = -1980 reads as 166 ms");
}

void EngineShape() {
	Section("Engine shape: address and calling convention the binding relies on");
	Check(time_ns::kTimeInfoUpdate == 0x0047D170u, "kTimeInfoUpdate is 0x0047D170");
	Check(sizeof(update::engine::UpdateFn) == 4, "the function pointer is 4 bytes (32-bit build)");
	Check(update::engine::kTwoPow32fAddress == 0x00A2FC78u, "kTwoPow32f at 0x00A2FC78");
	Check(update::engine::kMillisecondsToSecondsAddress == 0x00A30E40u, "kMillisecondsToSeconds at 0x00A30E40");
}

} // namespace

int main() {
	Constants();
	Units();
	FixedStepUnits();
	SecondsUnits();
	VariableNormal();
	VariableZero();
	VariableClamp();
	VariableNegative();
	NegativeZeroStep();
	B7Unreachable();
	Frozen();
	FixedStep();
	FixedCarryCrossing();
	FixedNegativeStep();
	FixedLargeStep();
	FixedNaNStep();
	Multiplier();
	FiftyThreeBitReading();
	BracketDelta();
	EngineShape();
	return Failures();
}
