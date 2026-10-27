#pragma once

// The decisions of 0x0047D170 (proposed TimeInfo::Update), as pure functions
// over plain values, and RunUpdate, which replays the body's store order on a
// TimeInfo and the two tick globals. Evidence: re/functions/0x0047D170.md
// (revision 3) and the export re/export/time-info/0x0047D170/ (asm.s,
// data.json). Names are proposals until the time-info slice's refutation
// round; the address stays next to each.
//
// Where pseudo.c and the instructions disagree, the instructions are followed
// (AGENTS.md):
//   - pseudo.c rounds the fixed step's sum (ROUND); the instructions set RC to
//     toward zero (OR EAX,0xC00 0x0047D1B8) around FISTP 0x0047D1C3, so the
//     whole milliseconds are truncated (read; claim 23).
//   - pseudo.c recomputes the carry from carry + step; the instructions
//     subtract from the sum already stored as float (FSTP 0x0047D1A5, FLD
//     0x0047D1A9) (read; claim 24).
//   - pseudo.c casts k to float and types the deltas as int; the instructions
//     multiply by the double (DC 0D 0x0047D238) and use the unsigned
//     conversion pattern (read; claim 25). The types here follow the
//     instructions.
//
// Floating point: the 53-bit reading. The rebuild computes what the x87 gives
// with precision control 53 bits, round to nearest and all exceptions masked
// (the precision control at the call is not known - unverified; the only
// control-word change in the body is RC around FISTP, restored at 0x0047D1CD,
// read). Every x87 result of the body is one IEEE operation on values a
// double holds exactly, rounded to 53 bits and then, where stored, to float;
// C++ double arithmetic under FLT_EVAL_METHOD 0 performs the same roundings
// (derived). Where precision control changes the result:
//   - +0x0C: not for products in the float normal range (claim 21: ms or the
//     step times k is exact at 53 bits, float * float likewise); for a product
//     in the float subnormal range PC 24 rounds twice and can differ (the
//     test "53-bit reading" pins one such case).
//   - +0x08 (fixed path, B3 not taken): FADD 0x0047D1DB of n + 2^32 is exact
//     at 53 bits but rounded to 24 bits at PC 24, and so is the FSUBP at
//     0x0047D1E4; whether a reachable input gives a different float there is
//     unverified.
//   - The B5 fix-up (FADD 0x0047D20D) rounds at PC 24 too, but d + 2^32 stays
//     in [2^31, 2^32] at every precision, so B6's outcome does not change
//     (claim 16, derived).
// A NaN result's payload follows the host's SSE arithmetic; for a single NaN
// operand it is the operand quieted, as on the x87 (F16, measured, quiet NaN
// only). Two NaN operands with different payloads (carry and step both NaN)
// are not modelled: the x87 picks the larger significand, SSE the first
// operand (unverified for this body).

#include "engine/time/TimeInfo.h"

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

namespace obng::engine::time::update {

static_assert(FLT_EVAL_METHOD == 0, "the 53-bit reading needs double arithmetic rounded to double (SSE2, not x87 excess precision)");

// 0x00A2FC78: 4294967296.0f, the unsigned-to-float fix-up (read, data.json
// bytes 00 00 80 4f).
inline constexpr float kTwoPow32 = std::bit_cast<float>(std::uint32_t{0x4F800000u});
// 0x00A3D14C: 166.66667f, the frame ceiling (read, data.json bytes ab aa 26 43).
inline constexpr float kFrameCeilingMs = std::bit_cast<float>(std::uint32_t{0x4326AAABu});
// 0x00A30E40: the double (double)0.001f = 0.0010000000474974513 (read,
// data.json bytes 00 00 00 e0 4d 62 50 3f).
inline constexpr double kMillisecondsToSeconds = std::bit_cast<double>(std::uint64_t{0x3F50624DE0000000ull});
static_assert(kMillisecondsToSeconds == static_cast<double>(0.001f), "0x00A30E40 holds (double)0.001f");
// MOV ESI,0xA6 0x0047D220: the clamped frame (read).
inline constexpr std::uint32_t kCeilingMs = 0xA6u;
// MOV ESI,0x1 0x0047D1F8: the zero frame read as one millisecond (read).
inline constexpr std::uint32_t kZeroDeltaMs = 1u;

// 0x00B39DB0 (proposed g_currentTickMs) and 0x00B39DB4 (g_previousTickMs):
// shared by every TimeInfo instance (claim 6, derived).
struct TickGlobals {
	std::uint32_t currentTickMs;   // 0x00B39DB0: tick - startTick of the instance updated last (claim 3)
	std::uint32_t previousTickMs;  // 0x00B39DB4: the previous value of 0x00B39DB0 (claim 4)
};

// 0x0047D176 SUB EDX,[ECX+0x14]: 32-bit, wraps modulo 2^32 (read).
constexpr std::uint32_t RelativeTick(std::uint32_t tick, std::uint32_t startTick) {
	return tick - startTick;
}

// 0x0047D179..0x0047D186: the old current to previous, the relative tick to
// current; on every call, before B1 (read; claim 5).
constexpr TickGlobals NextTickGlobals(TickGlobals old, std::uint32_t relativeTick) {
	return TickGlobals{relativeTick, old.currentTickMs};
}

// B1, CMP byte ptr [ECX],0x0 0x0047D18C / JNZ 0x0047D18F: any nonzero depth
// is freezeDepth (read).
constexpr bool IsFrozen(std::uint8_t freezeDepth) {
	return freezeDepth != 0;
}

// B2, FCOMP 0.0,[ECX+0x4] 0x0047D195 / TEST AH,0x44 / JNP 0x0047D19D: taken
// (variable) only on ordered equal, so -0.0 is variable and NaN is fixed
// (read; claim 10). The C++ == has the same truth table (derived: IEEE
// equality is false on unordered, -0.0 == +0.0).
constexpr bool IsVariableMode(float fixedStepMs) {
	return fixedStepMs == 0.0f;
}

// The pattern TEST r,r / FILD dword / JGE +6 / FADD [0x00A2FC78] at
// 0x0047D1CB..0x0047D1DB (B3), 0x0047D201..0x0047D20D (B5) and
// 0x0047D225..0x0047D231 (B7): the dword as UInt32 (read; claim 16). FILD is
// exact; the FADD is exact at 53 bits (the result is an integer below 2^32).
constexpr double ReadAsUnsigned(std::uint32_t value) {
	const std::int32_t asSigned = static_cast<std::int32_t>(value);
	double read = static_cast<double>(asSigned);  // FILD dword
	if (asSigned < 0) {                           // JGE not taken
		read += static_cast<double>(kTwoPow32);   // FADD float [0x00A2FC78]
	}
	return read;
}

// 0x0047D1F1 MOV ESI,EDX / 0x0047D1F3 SUB ESI,[ECX+0x10]: 32-bit (read).
constexpr std::uint32_t VariableDelta(std::uint32_t relativeTick, std::uint32_t clockMs) {
	return relativeTick - clockMs;
}

// B6, FCOMP fdelta,[0x00A3D14C] 0x0047D213 / TEST AH,0x41 / JNZ 0x0047D21E:
// not taken (clamp) only when fdelta > 166.66667f; unordered cannot occur
// (FILD of an integer, a finite constant) (read; claim 17).
constexpr bool ExceedsCeiling(std::uint32_t delta) {
	return ReadAsUnsigned(delta) > static_cast<double>(kFrameCeilingMs);
}

// B4..B6: the frame's milliseconds on the variable path (read; claim 15).
constexpr std::uint32_t FrameMs(std::uint32_t delta) {
	if (delta == 0) {  // B4 not taken: 0x0047D1F6 falls through
		return kZeroDeltaMs;
	}
	if (ExceedsCeiling(delta)) {  // B6 not taken: 0x0047D220
		return kCeilingMs;
	}
	return delta;  // B6 taken: 0x0047D225
}

// FISTP qword 0x0047D1C3 under RC = toward zero: the truncated value, or the
// integer indefinite 0x8000000000000000 for NaN, infinities and values
// outside the int64 range (invalid operation masked - assumed, unverified for
// the game; F16 measured the indefinite's low dword 0 for a NaN).
inline std::int64_t TruncateToInt64(float sum) {
	constexpr double kTwoPow63 = 9223372036854775808.0;
	const double value = static_cast<double>(sum);
	if (std::isnan(value) || value >= kTwoPow63 || value < -kTwoPow63) {
		return std::numeric_limits<std::int64_t>::min();
	}
	return static_cast<std::int64_t>(value);  // C++ conversion truncates toward zero
}

// The fixed path's whole milliseconds and new carry.
struct FixedStep {
	std::uint32_t wholeMs;  // n: the low dword of the FISTP result, added to +0x10
	float carryMs;          // the new +0x08
};

// 0x0047D19F..0x0047D1E8 (read; claim 11):
//   sum   = (float)(carry + step)           FLD/FADD, FSTP float 0x0047D1A5
//   n     = low dword of trunc(sum)         FISTP qword 0x0047D1C3, MOV EAX,[ESP]
//   carry = (float)(sum - ReadAsUnsigned(n)) FSUBP 0x0047D1E4 (DE E9: ST1 - ST0), FSTP 0x0047D1E8
// The sum is rounded to 53 bits and then to float, as at PC 53 (FADD, then
// FSTP float). That this equals one rounding to float (claim 22) rests on the
// known 2p+2 rule for double rounding of a sum - recalled, not verified here;
// the measured C5 and F19 carries agree with it.
inline FixedStep AdvanceFixedStep(float carryMs, float stepMs) {
	const float sum = static_cast<float>(static_cast<double>(carryMs) + static_cast<double>(stepMs));
	const std::uint32_t wholeMs = static_cast<std::uint32_t>(static_cast<std::uint64_t>(TruncateToInt64(sum)));
	const float carry = static_cast<float>(static_cast<double>(sum) - ReadAsUnsigned(wholeMs));
	return FixedStep{wholeMs, carry};
}

// The two stores to +0x0C.
struct Seconds {
	float unscaled;  // FMUL double [0x00A30E40] 0x0047D238, FSTP 0x0047D241
	float scaled;    // FLD 0x0047D244, FMUL float [0x00B06704] 0x0047D247, FSTP 0x0047D24D
};

// 0x0047D238..0x0047D24D: milliseconds (exact in a double: ReadAsUnsigned of
// ms, or the float step) times k, rounded to float; then times the multiplier,
// rounded to float (read; claim 19). Both products exact at 53 bits (claim 21).
inline Seconds FrameSecondsFrom(double milliseconds, float timeMultiplier) {
	const float unscaled = static_cast<float>(milliseconds * kMillisecondsToSeconds);
	const float scaled = static_cast<float>(static_cast<double>(unscaled) * static_cast<double>(timeMultiplier));
	return Seconds{unscaled, scaled};
}

enum class Path : std::uint8_t {
	Frozen,    // B1 taken
	Fixed,     // B1 not taken, B2 not taken
	Variable,  // B1 not taken, B2 taken
};

// What the run decided, for tests and a differential check; the engine state
// is what RunUpdate wrote.
struct UpdateTrace {
	Path path;
	std::uint32_t frameMs;       // variable path: the milliseconds multiplied (B4..B6); else 0
	std::uint32_t fixedWholeMs;  // fixed path: n; else 0
	float unscaledSeconds;       // the store at 0x0047D241; freezeDepth path: 0.0f (no such store)
};

// The body's store order on a TimeInfo, the tick globals and the multiplier's
// value (0x00B06704, read at 0x0047D247; passed by value - the body does not
// write it). `self` cannot be null here; the original faults at 0x0047D176
// when called with ECX = 0 (claim 28), which this reference makes the
// caller's concern.
inline UpdateTrace RunUpdate(TimeInfo& self, std::uint32_t tick, TickGlobals& globals, float timeMultiplier) {
	const std::uint32_t relativeTick = RelativeTick(tick, self.startTick);  // 0x0047D176
	globals = NextTickGlobals(globals, relativeTick);                       // 0x0047D181, 0x0047D186

	if (IsFrozen(self.freezeDepth)) {  // B1 taken
		self.frameSeconds = 0.0f;  // FLDZ 0x0047D174, FSTP 0x0047D24D
		return UpdateTrace{Path::Frozen, 0, 0, 0.0f};
	}

	if (!IsVariableMode(self.fixedStepMs)) {  // B2 not taken: fixed step
		const FixedStep step = AdvanceFixedStep(self.fixedStepCarryMs, self.fixedStepMs);
		self.fixedStepCarryMs = step.carryMs;  // 0x0047D1E8
		const Seconds seconds = FrameSecondsFrom(static_cast<double>(self.fixedStepMs), timeMultiplier);  // FLD 0x0047D1EB
		self.clockMs += step.wholeMs;           // ADD EDX,EAX 0x0047D1E6, stored 0x0047D23E
		self.frameSeconds = seconds.unscaled;  // 0x0047D241
		self.frameSeconds = seconds.scaled;    // 0x0047D24D
		return UpdateTrace{Path::Fixed, 0, step.wholeMs, seconds.unscaled};
	}

	const std::uint32_t ms = FrameMs(VariableDelta(relativeTick, self.clockMs));  // B4..B6
	const Seconds seconds = FrameSecondsFrom(ReadAsUnsigned(ms), timeMultiplier);  // B7 (always taken, claim 17)
	self.clockMs = relativeTick;            // 0x0047D23E: the real tick, not last + ms (claim 18)
	self.frameSeconds = seconds.unscaled;  // 0x0047D241
	self.frameSeconds = seconds.scaled;    // 0x0047D24D
	return UpdateTrace{Path::Variable, ms, 0, seconds.unscaled};
}

} // namespace obng::engine::time::update
