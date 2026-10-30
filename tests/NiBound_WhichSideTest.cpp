// Tests of the rebuild of 0x0047DA70 (proposed NiBound::WhichSide, name
// unverified): a bounding sphere (this, ECX) against one plane (the stack
// argument), returning 2 (wholly on the negative side), 1 (wholly on the
// positive side) or 0 (straddling, or a NaN reached a comparison).
//
// The mapped control flow (re/functions/0x0047DA70.md):
//   B1  0x0047DAA7  FCOMP (-r against d) / TEST AH,0x1 / JNZ   not taken: d <= -r, ordered -> 2
//   B2  0x0047DABD  FCOMPP (r against d) / TEST AH,0x41 / JP   not taken: d >= r, ordered  -> 1
//                                                              taken: r > d or unordered   -> 0
// Each section names the branch outcomes it drives.
//
// Precision and rounding. The game's x87 runs at 24-bit precision control
// (measured, knowledge/frame-clock.md) under a rounding control that depends
// on the call site (FCW 0x0C7F, truncate, in the shadow walk; measured). The
// rebuild models that reading exactly where the instructions round
// (NiBound_WhichSideLogic.h), under the caller's mode: every FMUL/FADDP/FSUB
// rounds the significand to 24 bits and keeps the register's exponent range,
// each FSTP float rounds to the float format. The sections "Differential"
// hold the original's results from the emulator (measured,
// tools/difftest/expected/0x0047DA70.json at round to nearest and
// 0x0047DA70-rounding.json under the four modes, Unicorn's x87); the
// sections before them are derived from the instructions or the IEEE 754
// rounding rules and say from which. The helpers are also checked against
// the host's own x87 in tests/X87RoundingHostTest.cpp.

#include "Check.h"
#include "engine/render/NiBound_WhichSide.h"
#include "engine/render/NiBound_WhichSideLogic.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>

namespace ws = obng::engine::render::which_side;
using ws::PlaneSide;

namespace {

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr ws::X87Rounding kNearest = ws::X87Rounding::Nearest;
constexpr ws::X87Rounding kDown = ws::X87Rounding::Down;
constexpr ws::X87Rounding kUp = ws::X87Rounding::Up;
constexpr ws::X87Rounding kZero = ws::X87Rounding::Zero;
constexpr ws::X87Rounding kModes[] = {kNearest, kDown, kUp, kZero};

double P2(int exponent) {
	return std::ldexp(1.0, exponent);
}

std::uint64_t Bits64(double value) {
	return std::bit_cast<std::uint64_t>(value);
}

void ReturnValues() {
	Section("Return values: MOV EAX,0x2 0x0047DAAB, MOV EAX,0x1 0x0047DABF, XOR EAX,EAX 0x0047DAC7 (read)");
	Check(static_cast<std::int32_t>(PlaneSide::Negative) == 2, "Negative = 2");
	Check(static_cast<std::int32_t>(PlaneSide::Positive) == 1, "Positive = 1");
	Check(static_cast<std::int32_t>(PlaneSide::Straddle) == 0, "Straddle = 0");
}

void BranchB1() {
	Section("B1 0x0047DAA7: not taken (-> 2) exactly when d <= -r and both are ordered (derived: C0 = 1 for -r < d or unordered)");
	Check(ws::IsWhollyNegative(-3.0f, 1.0f), "d < -r: not taken");
	Check(ws::IsWhollyNegative(-1.0f, 1.0f), "d == -r: not taken (equality clears C0)");
	Check(ws::IsWhollyNegative(0.0f, 0.0f), "r = +0, d = +0: -r = -0 == +0, not taken");
	Check(ws::IsWhollyNegative(-0.0f, 0.0f), "r = +0, d = -0: not taken");
	Check(!ws::IsWhollyNegative(0.5f, 1.0f), "-r < d: taken");
	Check(!ws::IsWhollyNegative(kNaN, 1.0f), "d NaN: unordered sets C0, taken");
	Check(!ws::IsWhollyNegative(0.5f, kNaN), "r NaN: unordered, taken");
	Check(ws::IsWhollyNegative(0.5f, -1.0f), "r < 0: d = 0.5 <= -r = 1, not taken");
	Check(!ws::IsWhollyNegative(5.0f, kInf), "r = +inf: -inf < d, taken");
	Check(ws::IsWhollyNegative(-kInf, 1.0f), "d = -inf: not taken");
}

void BranchB2() {
	Section("B2 0x0047DABD: not taken (-> 1) exactly when d >= r and both are ordered (derived: parity of C0|C3)");
	Check(ws::IsWhollyPositive(3.0f, 1.0f), "d > r: C0 = 1, C3 = 0, odd parity, not taken");
	Check(ws::IsWhollyPositive(1.0f, 1.0f), "d == r: C0 = 0, C3 = 1, odd parity, not taken");
	Check(!ws::IsWhollyPositive(0.5f, 1.0f), "r > d: C0 = C3 = 0, even parity, taken");
	Check(!ws::IsWhollyPositive(kNaN, 1.0f), "d NaN: C0 = C3 = 1, even parity, taken");
	Check(!ws::IsWhollyPositive(1.0f, kNaN), "r NaN: taken");
	Check(ws::IsWhollyPositive(kInf, 1.0f), "d = +inf: not taken");
	Check(!ws::IsWhollyPositive(5.0f, kInf), "r = +inf: taken");
}

void Classification() {
	Section("Classify: the flows of the mapping (flows 1..9), B1 first, then B2");
	Check(ws::Classify(-3.0f, 1.0f) == PlaneSide::Negative, "flow 1: d < -r -> 2 (B1 not taken)");
	Check(ws::Classify(-1.0f, 1.0f) == PlaneSide::Negative, "flow 2: d == -r -> 2 (B1 not taken)");
	Check(ws::Classify(0.0f, 0.0f) == PlaneSide::Negative, "flow 2: r = 0, d = +0 -> 2 (the negative test runs first)");
	Check(ws::Classify(-0.0f, 0.0f) == PlaneSide::Negative, "flow 2: r = 0, d = -0 -> 2");
	Check(ws::Classify(0.5f, 1.0f) == PlaneSide::Straddle, "flow 3: -r < d < r -> 0 (B1 taken, B2 taken)");
	Check(ws::Classify(1.0f, 1.0f) == PlaneSide::Positive, "flow 4: d == r -> 1 (B1 taken, B2 not taken)");
	Check(ws::Classify(3.0f, 1.0f) == PlaneSide::Positive, "flow 5: d > r -> 1 (B1 taken, B2 not taken)");
	Check(ws::Classify(kNaN, 1.0f) == PlaneSide::Straddle, "flow 6: d NaN -> 0 (B1 taken, B2 taken)");
	Check(ws::Classify(0.5f, kNaN) == PlaneSide::Straddle, "flow 7: r NaN -> 0");
	Check(ws::Classify(0.5f, -1.0f) == PlaneSide::Negative, "flow 8: r < 0, d <= -r -> 2");
	Check(ws::Classify(2.0f, -1.0f) == PlaneSide::Positive, "flow 8: r < 0, d > -r -> 1 (never 0 for an ordered d)");
	Check(ws::Classify(-0.5f, -1.0f) == PlaneSide::Negative, "flow 8: r < 0, d between r and -r -> 2 (both tests hold, 2 wins)");
	Check(ws::Classify(kNaN, -1.0f) == PlaneSide::Straddle, "flow 8: r < 0, d NaN -> 0");
	Check(ws::Classify(5.0f, kInf) == PlaneSide::Straddle, "flow 9: r = +inf, finite d -> 0");
	Check(ws::Classify(kInf, 1.0f) == PlaneSide::Positive, "flow 9: d = +inf -> 1");
	Check(ws::Classify(-kInf, 1.0f) == PlaneSide::Negative, "flow 9: d = -inf -> 2");
	Check(ws::Classify(kInf, kInf) == PlaneSide::Positive, "d = r = +inf: B1 taken (-inf < inf), B2 not taken (equal) -> 1");
}

void Pc24Rounding() {
	Section("x87 at precision control 24, round to nearest: the significand rounds to 24 bits, the exponent keeps the register's range (SDM 8.1.5.2, read; knowledge/bone-palettes.md)");
	// 1 + 2^-24 is the tie between 1 and 1 + 2^-23: to even.
	Check(ws::RoundToPc24(1.0 + std::ldexp(1.0, -24), kNearest) == 1.0, "1 + 2^-24 (tie) -> 1 (even)");
	Check(ws::RoundToPc24(1.0 + 3.0 * std::ldexp(1.0, -24), kNearest) == 1.0 + std::ldexp(1.0, -22), "1 + 3*2^-24 (tie) -> 1 + 2^-22 (even)");
	Check(ws::RoundToPc24(1.0 + std::ldexp(1.0, -24) + std::ldexp(1.0, -50), kNearest) == 1.0 + std::ldexp(1.0, -23), "just above the tie -> up");
	Check(ws::RoundToPc24(-(1.0 + std::ldexp(1.0, -24) + std::ldexp(1.0, -50)), kNearest) == -(1.0 + std::ldexp(1.0, -23)), "the sign is kept (round to nearest is symmetric)");
	Check(ws::RoundToPc24(2.0 - std::ldexp(1.0, -24), kNearest) == 2.0, "2 - 2^-24 (tie below 2) -> 2: the carry moves into the exponent");
	Check(ws::RoundToPc24(std::ldexp(1.0, 200), kNearest) == std::ldexp(1.0, 200), "2^200 stays finite: no float overflow in the register");
	Check(ws::RoundToPc24(std::ldexp(1.0 + std::ldexp(1.0, -22) + std::ldexp(1.0, -46), -128), kNearest) == std::ldexp(1.0 + std::ldexp(1.0, -22), -128),
	      "(1+2^-23)^2 * 2^-128 -> (1+2^-22) * 2^-128: rounded as a normal value below FLT_MIN");
	Check(ws::RoundToPc24(0.0, kNearest) == 0.0 && !std::signbit(ws::RoundToPc24(0.0, kNearest)), "+0 stays +0");
	Check(std::signbit(ws::RoundToPc24(-0.0, kNearest)), "-0 stays -0");
	Check(std::isinf(ws::RoundToPc24(std::numeric_limits<double>::infinity(), kNearest)), "+inf passes");
	Check(std::isnan(ws::RoundToPc24(std::numeric_limits<double>::quiet_NaN(), kNearest)), "NaN passes");

	Section("FSTP float at 0x0047DA88 / 0x0047DA93: the second rounding, to the float format");
	Check(Bits(ws::StoreSingle(std::ldexp(1.0, 200), kNearest)) == 0x7F800000u, "2^200 -> +inf at the store at nearest (overflow masked: fpcw 0x007F)");
	Check(Bits(ws::StoreSingle(-std::ldexp(1.0, 200), kNearest)) == 0xFF800000u, "-2^200 -> -inf at the store");
	Check(Bits(ws::StoreSingle(std::ldexp(1.0 + std::ldexp(1.0, -22), -128), kNearest)) == 0x00200000u,
	      "(1+2^-22) * 2^-128 is a denormal tie -> 2^-128 (even), bits 0x00200000");
	Check(Bits(ws::StoreSingle(1.0, kNearest)) == 0x3F800000u, "1 stays 1");

	Section("The double rounding of the original (derived from the two roundings above)");
	const double product = ws::MulPc24(FromBits(0x1F800001u), FromBits(0x1F800001u), kNearest);
	Check(Bits(ws::StoreSingle(product, kNearest)) == 0x00200000u,
	      "(1+2^-23)*2^-64 squared: 24-bit register value, then the store -> 2^-128 (a single rounding would give 0x00200001)");
	Check(Bits(FromBits(0x1F800001u) * FromBits(0x1F800001u)) == 0x00200001u, "control: float arithmetic rounds once -> 0x00200001");
}

void ControlWord() {
	Section("RoundingFromControlWord: FCW bits 10-11, 00 nearest, 01 down, 10 up, 11 truncate (SDM vol. 1 8.1.5.3)");
	Check(ws::RoundingFromControlWord(0x007F) == kNearest, "0x007F (the emulator cases' default, PC 24): nearest");
	Check(ws::RoundingFromControlWord(0x047F) == kDown, "0x047F: down");
	Check(ws::RoundingFromControlWord(0x087F) == kUp, "0x087F: up");
	Check(ws::RoundingFromControlWord(0x0C7F) == kZero, "0x0C7F (the shadow walk's, measured): truncate");
	Check(ws::RoundingFromControlWord(0x037F) == kNearest, "0x037F (FNINIT's): nearest - precision bits do not leak in");
	Check(ws::RoundingFromControlWord(0xF3FF) == kNearest && ws::RoundingFromControlWord(0xFFFF) == kZero,
		"only bits 10-11 count: every other bit set changes nothing");
	Check(ws::RoundingFromControlWord(0x0A7F) == kUp, "0x0A7F decodes as up (precision 53) - not a round-to-nearest word");
}

// Every flow of the one rounding decision: 4 modes x 2 signs x 4 discarded
// classes x odd/even last place (IEEE 754 4.3; the x87 follows it, SDM vol. 1
// 4.8.4, measured in X87RoundingHostTest).
void RoundingDecision() {
	Section("RoundsMagnitudeUp: nothing discarded never rounds, in every mode and sign");
	using D = ws::Discarded;
	bool none = true;
	for (const ws::X87Rounding mode : kModes) {
		for (const bool negative : {false, true}) {
			for (const bool odd : {false, true}) {
				none = none && !ws::RoundsMagnitudeUp(mode, negative, odd, D::None);
			}
		}
	}
	Check(none, "Discarded::None: no change, 16 combinations");

	Section("RoundsMagnitudeUp, nearest: above half up, a tie to even, below half down; the sign does not matter");
	for (const bool negative : {false, true}) {
		Check(!ws::RoundsMagnitudeUp(kNearest, negative, false, D::BelowHalf) && !ws::RoundsMagnitudeUp(kNearest, negative, true, D::BelowHalf),
			negative ? "negative, below half: kept" : "positive, below half: kept");
		Check(!ws::RoundsMagnitudeUp(kNearest, negative, false, D::Half), negative ? "negative tie, even last place: kept" : "positive tie, even: kept");
		Check(ws::RoundsMagnitudeUp(kNearest, negative, true, D::Half), negative ? "negative tie, odd last place: up to even" : "positive tie, odd: up to even");
		Check(ws::RoundsMagnitudeUp(kNearest, negative, false, D::AboveHalf) && ws::RoundsMagnitudeUp(kNearest, negative, true, D::AboveHalf),
			negative ? "negative, above half: up" : "positive, above half: up");
	}

	Section("RoundsMagnitudeUp, directed: Up rounds positive magnitudes up, Down negative ones, Zero none - any discarded part, parity ignored");
	for (const D discarded : {D::BelowHalf, D::Half, D::AboveHalf}) {
		for (const bool odd : {false, true}) {
			Check(ws::RoundsMagnitudeUp(kUp, false, odd, discarded) && !ws::RoundsMagnitudeUp(kUp, true, odd, discarded),
				"Up: a positive value away from zero, a negative one toward zero");
			Check(!ws::RoundsMagnitudeUp(kDown, false, odd, discarded) && ws::RoundsMagnitudeUp(kDown, true, odd, discarded),
				"Down: a positive value toward zero, a negative one away from zero");
			Check(!ws::RoundsMagnitudeUp(kZero, false, odd, discarded) && !ws::RoundsMagnitudeUp(kZero, true, odd, discarded),
				"Zero: truncation for both signs");
		}
	}
}

void RoundingModes() {
	Section("RoundToPc24 per mode on exact values: ties, signs, the carry, the 2^200 register range (IEEE 754 4.3)");
	const double tie = 1.0 + P2(-24);           // half way between 1 and 1 + 2^-23
	const double tieOdd = 1.0 + 3.0 * P2(-24);  // half way between 1 + 2^-23 (odd) and 1 + 2^-22
	const double above = 1.0 + P2(-24) + P2(-50);
	Check(ws::RoundToPc24(tie, kNearest) == 1.0 && ws::RoundToPc24(tieOdd, kNearest) == 1.0 + P2(-22), "nearest: ties to even both ways");
	Check(ws::RoundToPc24(tie, kDown) == 1.0 && ws::RoundToPc24(-tie, kDown) == -(1.0 + P2(-23)), "down: +tie truncated, -tie away from zero");
	Check(ws::RoundToPc24(tie, kUp) == 1.0 + P2(-23) && ws::RoundToPc24(-tie, kUp) == -1.0, "up: +tie away from zero, -tie truncated");
	Check(ws::RoundToPc24(tie, kZero) == 1.0 && ws::RoundToPc24(-tie, kZero) == -1.0, "zero: both truncated");
	Check(ws::RoundToPc24(above, kZero) == 1.0 && ws::RoundToPc24(-above, kZero) == -1.0, "zero: just above a tie still truncated");
	Check(ws::RoundToPc24(above, kDown) == 1.0 && ws::RoundToPc24(above, kUp) == 1.0 + P2(-23), "down/up on just above a tie");
	Check(ws::RoundToPc24(1.0 + P2(-40), kUp) == 1.0 + P2(-23) && ws::RoundToPc24(1.0 + P2(-40), kNearest) == 1.0,
		"a tiny discarded part: up rounds away, nearest keeps");
	Check(ws::RoundToPc24(2.0 - P2(-30), kUp) == 2.0 && ws::RoundToPc24(-(2.0 - P2(-30)), kDown) == -2.0,
		"the carry out of the significand raises the exponent under up and down");
	Check(ws::RoundToPc24(2.0 - P2(-30), kZero) == 2.0 - P2(-23), "zero: below 2 stays below 2");
	Check(ws::RoundToPc24(1.5, kUp) == 1.5 && ws::RoundToPc24(-1.5, kDown) == -1.5, "a 24-bit value is exact in every mode");
	Check(ws::RoundToPc24(P2(200) + P2(170), kZero) == P2(200), "2^200 + 2^170 truncates to 2^200: still the register's range");
	for (const ws::X87Rounding mode : kModes) {
		Check(Bits64(ws::RoundToPc24(0.0, mode)) == Bits64(0.0) && Bits64(ws::RoundToPc24(-0.0, mode)) == Bits64(-0.0), "zeros pass in every mode");
	}

	Section("TwoSum: head + tail is the exact sum (Knuth), the tail carries what double dropped");
	{
		const ws::ExactSum a = ws::TwoSum(1.0, P2(-80));
		Check(a.head == 1.0 && a.tail == P2(-80), "1 + 2^-80: head 1, tail 2^-80");
		const ws::ExactSum b = ws::TwoSum(1.0, -P2(-80));
		Check(b.head == 1.0 && b.tail == -P2(-80), "1 - 2^-80: head 1, tail -2^-80");
		const ws::ExactSum c = ws::TwoSum(3.0, 4.0);
		Check(c.head == 7.0 && c.tail == 0.0, "an exact sum: tail 0");
	}

	Section("RoundToPc24 on head + tail: the tail decides where the head alone is on the 24-bit grid or on a tie");
	Check(ws::RoundToPc24(ws::ExactSum{1.0, P2(-80)}, kUp) == 1.0 + P2(-23), "1 + 2^-80 up: 1 + 2^-23 (the double head alone would give 1)");
	Check(ws::RoundToPc24(ws::ExactSum{1.0, P2(-80)}, kNearest) == 1.0 && ws::RoundToPc24(ws::ExactSum{1.0, P2(-80)}, kZero) == 1.0,
		"1 + 2^-80 nearest and zero: 1");
	Check(ws::RoundToPc24(ws::ExactSum{1.0, -P2(-80)}, kZero) == 1.0 - P2(-24) && ws::RoundToPc24(ws::ExactSum{1.0, -P2(-80)}, kDown) == 1.0 - P2(-24),
		"1 - 2^-80 zero/down: the 24-bit value below 1, 1 - 2^-24 (the borrow into the exponent)");
	Check(ws::RoundToPc24(ws::ExactSum{1.0, -P2(-80)}, kUp) == 1.0 && ws::RoundToPc24(ws::ExactSum{1.0, -P2(-80)}, kNearest) == 1.0,
		"1 - 2^-80 up/nearest: 1");
	Check(ws::RoundToPc24(ws::ExactSum{-1.0, P2(-80)}, kZero) == -(1.0 - P2(-24)) && ws::RoundToPc24(ws::ExactSum{-1.0, P2(-80)}, kUp) == -(1.0 - P2(-24)),
		"-1 + 2^-80 zero/up: -(1 - 2^-24)");
	Check(ws::RoundToPc24(ws::ExactSum{-1.0, -P2(-80)}, kDown) == -(1.0 + P2(-23)), "-1 - 2^-80 down: -(1 + 2^-23)");
	Check(ws::RoundToPc24(ws::ExactSum{1.5, -P2(-80)}, kZero) == 1.5 - P2(-23), "1.5 - 2^-80 zero: 1.5 - 2^-23 (not a power of two: same binade)");
	Check(ws::RoundToPc24(ws::ExactSum{tie, P2(-80)}, kNearest) == 1.0 + P2(-23), "a tie in the head pushed above half by the tail: up");
	Check(ws::RoundToPc24(ws::ExactSum{tie, -P2(-80)}, kNearest) == 1.0, "pulled below half: down");
	Check(ws::RoundToPc24(ws::ExactSum{tieOdd, -P2(-80)}, kNearest) == 1.0 + P2(-23), "an odd tie pulled below half: down, not to even");
	Check(ws::RoundToPc24(ws::ExactSum{above, -P2(-80)}, kNearest) == 1.0 + P2(-23), "a head off the tie: the tail cannot move it across");

	Section("AddPc24 / SubPc24 per mode: the exact sum, then one 24-bit rounding");
	Check(ws::AddPc24(1.0, P2(-80), kUp) == 1.0 + P2(-23) && ws::AddPc24(1.0, P2(-80), kDown) == 1.0, "1 + 2^-80 up and down");
	Check(ws::AddPc24(-1.0, -P2(-80), kDown) == -(1.0 + P2(-23)) && ws::AddPc24(-1.0, -P2(-80), kUp) == -1.0, "-1 - 2^-80 down and up");
	Check(ws::SubPc24(1.0, P2(-80), kZero) == 1.0 - P2(-24) && ws::SubPc24(1.0, P2(-80), kNearest) == 1.0, "1 - 2^-80 zero and nearest");
	Check(ws::AddPc24(P2(200), -P2(200), kNearest) == 0.0, "2^200 - 2^200 cancels exactly in the register");

	Section("An exactly zero sum (IEEE 754 6.3): +0 in every mode but down, -0 under down; zeros of one sign keep it");
	for (const ws::X87Rounding mode : kModes) {
		const std::uint64_t expected = Bits64(mode == kDown ? -0.0 : 0.0);
		Check(Bits64(ws::AddPc24(1.5, -1.5, mode)) == expected, mode == kDown ? "down: 1.5 - 1.5 = -0" : "1.5 - 1.5 = +0");
		Check(Bits64(ws::SubPc24(1.5, 1.5, mode)) == expected, mode == kDown ? "down: x - x = -0 (FSUB)" : "x - x = +0 (FSUB)");
		Check(Bits64(ws::AddPc24(0.0, -0.0, mode)) == expected, mode == kDown ? "down: +0 + -0 = -0" : "+0 + -0 = +0");
		Check(Bits64(ws::AddPc24(0.0, 0.0, mode)) == Bits64(0.0) && Bits64(ws::AddPc24(-0.0, -0.0, mode)) == Bits64(-0.0),
			"+0 + +0 = +0 and -0 + -0 = -0 in every mode");
		Check(Bits64(ws::SubPc24(0.0, 0.0, mode)) == expected, mode == kDown ? "down: +0 - +0 = -0" : "+0 - +0 = +0");
	}
	Check(std::isnan(ws::AddPc24(std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), kDown)),
		"inf - inf: NaN, not a signed zero");
	Check(ws::AddPc24(std::numeric_limits<double>::infinity(), 1.0, kZero) == std::numeric_limits<double>::infinity(), "inf + 1: inf");

	Section("MulPc24 per mode: the exact product rounded once (a float times a float fits double)");
	const float a = FromBits(0x3F800001u);  // 1 + 2^-23
	Check(ws::MulPc24(a, a, kNearest) == 1.0 + P2(-22) && ws::MulPc24(a, a, kZero) == 1.0 + P2(-22) && ws::MulPc24(a, a, kDown) == 1.0 + P2(-22),
		"(1+2^-23)^2 = 1 + 2^-22 + 2^-46: nearest, zero, down keep 1 + 2^-22");
	Check(ws::MulPc24(a, a, kUp) == 1.0 + P2(-22) + P2(-23), "up: 1 + 2^-22 + 2^-23");
	Check(ws::MulPc24(-a, a, kDown) == -(1.0 + P2(-22) + P2(-23)) && ws::MulPc24(-a, a, kUp) == -(1.0 + P2(-22)), "negative: down away, up toward zero");
	Check(Bits64(ws::MulPc24(-0.0f, 1.0f, kDown)) == Bits64(-0.0) && Bits64(ws::MulPc24(-0.0f, -1.0f, kNearest)) == Bits64(0.0), "zero signs of products");

	Section("StoreSingle per mode: overflow (IEEE 754 7.4, overflow masked)");
	const double big = P2(200);
	Check(Bits(ws::StoreSingle(big, kNearest)) == 0x7F800000u && Bits(ws::StoreSingle(-big, kNearest)) == 0xFF800000u, "nearest: +-inf");
	Check(Bits(ws::StoreSingle(big, kZero)) == 0x7F7FFFFFu && Bits(ws::StoreSingle(-big, kZero)) == 0xFF7FFFFFu, "zero: +-FLT_MAX");
	Check(Bits(ws::StoreSingle(big, kUp)) == 0x7F800000u && Bits(ws::StoreSingle(-big, kUp)) == 0xFF7FFFFFu, "up: +inf, -FLT_MAX");
	Check(Bits(ws::StoreSingle(big, kDown)) == 0x7F7FFFFFu && Bits(ws::StoreSingle(-big, kDown)) == 0xFF800000u, "down: +FLT_MAX, -inf");
	const double justAbove = static_cast<double>(FLT_MAX) + P2(103);  // half a float ulp above FLT_MAX: a tie toward 2^128
	Check(Bits(ws::StoreSingle(justAbove, kNearest)) == 0x7F800000u && Bits(ws::StoreSingle(justAbove, kZero)) == 0x7F7FFFFFu,
		"FLT_MAX + half an ulp: nearest overflows (to even, 2^128), zero keeps FLT_MAX");
	Check(Bits(ws::StoreSingle(static_cast<double>(FLT_MAX) + P2(80), kUp)) == 0x7F800000u &&
			Bits(ws::StoreSingle(static_cast<double>(FLT_MAX) + P2(80), kNearest)) == 0x7F7FFFFFu,
		"FLT_MAX + a hair: up overflows, nearest stays FLT_MAX");
	Check(Bits(ws::StoreSingle(static_cast<double>(FLT_MAX), kUp)) == 0x7F7FFFFFu, "FLT_MAX itself: exact in every mode");

	Section("StoreSingle per mode: denormal results and underflow (the quantum 2^-149, underflow masked)");
	const double halfSmallest = P2(-150);
	Check(Bits(ws::StoreSingle(halfSmallest, kNearest)) == 0u, "2^-150 nearest: a tie between 0 and 2^-149, to even 0");
	Check(Bits(ws::StoreSingle(halfSmallest, kUp)) == 1u && Bits(ws::StoreSingle(halfSmallest, kDown)) == 0u && Bits(ws::StoreSingle(halfSmallest, kZero)) == 0u,
		"2^-150: up gives the smallest denormal, down and zero +0");
	Check(Bits(ws::StoreSingle(-halfSmallest, kDown)) == 0x80000001u && Bits(ws::StoreSingle(-halfSmallest, kUp)) == 0x80000000u &&
			Bits(ws::StoreSingle(-halfSmallest, kZero)) == 0x80000000u && Bits(ws::StoreSingle(-halfSmallest, kNearest)) == 0x80000000u,
		"-2^-150: down gives -2^-149, the others -0");
	Check(Bits(ws::StoreSingle(P2(-149) * 1.5, kNearest)) == 2u, "1.5 * 2^-149: tie between 1 and 2 units, to even 2");
	Check(Bits(ws::StoreSingle(P2(-149) * 2.5, kNearest)) == 2u, "2.5 units: to even 2");
	Check(Bits(ws::StoreSingle(P2(-149) * 2.5, kUp)) == 3u && Bits(ws::StoreSingle(P2(-149) * 2.5, kZero)) == 2u, "2.5 units: up 3, zero 2");
	Check(Bits(ws::StoreSingle(P2(-300), kUp)) == 1u && Bits(ws::StoreSingle(P2(-300), kNearest)) == 0u, "2^-300 (the register's range): up 2^-149, nearest 0");
	{
		const double tiny = std::numeric_limits<double>::denorm_min();  // a double subnormal: the excluded-input path of StoreSingle
		Check(Bits(ws::StoreSingle(tiny, kUp)) == 1u && Bits(ws::StoreSingle(tiny, kNearest)) == 0u && Bits(ws::StoreSingle(tiny, kZero)) == 0u &&
				Bits(ws::StoreSingle(-tiny, kDown)) == 0x80000001u && Bits(ws::StoreSingle(-tiny, kUp)) == 0x80000000u,
			"a double subnormal: up gives 2^-149, nearest and zero +0; negative: down -2^-149, up -0");
		Check(Bits(ws::StoreSingle(P2(-1000), kUp)) == 1u && Bits(ws::StoreSingle(P2(-1000), kDown)) == 0u,
			"2^-1000 (more than 64 bits dropped): up 2^-149, down +0");
	}
	const double denormalTie = std::ldexp(1.0 + P2(-22), -128);  // (1+2^-22) * 2^-128 = 2^21 + 0.5 units
	Check(Bits(ws::StoreSingle(denormalTie, kNearest)) == 0x00200000u && Bits(ws::StoreSingle(denormalTie, kUp)) == 0x00200001u &&
			Bits(ws::StoreSingle(denormalTie, kDown)) == 0x00200000u && Bits(ws::StoreSingle(denormalTie, kZero)) == 0x00200000u,
		"(1+2^-22) * 2^-128, half a quantum above 2^-128: nearest to even, up one more, down and zero truncate");
	Check(Bits(ws::StoreSingle(P2(-126) - P2(-150), kUp)) == 0x00800000u && Bits(ws::StoreSingle(P2(-126) - P2(-150), kZero)) == 0x007FFFFFu,
		"just below FLT_MIN: up reaches FLT_MIN (a normal), zero keeps the largest denormal");

	Section("StoreSingle per mode: normal values with more than 24 bits, exact values, specials");
	Check(Bits(ws::StoreSingle(1.0 + P2(-30), kUp)) == 0x3F800001u && Bits(ws::StoreSingle(1.0 + P2(-30), kNearest)) == 0x3F800000u,
		"1 + 2^-30: up one ulp, nearest 1");
	Check(Bits(ws::StoreSingle(-(1.0 + P2(-30)), kDown)) == 0xBF800001u && Bits(ws::StoreSingle(-(1.0 + P2(-30)), kZero)) == 0xBF800000u,
		"-(1 + 2^-30): down one ulp away, zero -1");
	for (const ws::X87Rounding mode : kModes) {
		Check(Bits(ws::StoreSingle(1.0, mode)) == 0x3F800000u && Bits(ws::StoreSingle(-0.0, mode)) == 0x80000000u &&
				Bits(ws::StoreSingle(std::numeric_limits<double>::infinity(), mode)) == 0x7F800000u &&
				std::isnan(ws::StoreSingle(std::numeric_limits<double>::quiet_NaN(), mode)),
			"1, -0, +inf, NaN convert exactly in every mode");
	}

	Section("SignedDistance per mode: the rounding reaches d (the R1 inputs: 1 + 2^-24 + 2^-60 - 1)");
	const ws::PlaneValues plane{FromBits(0x33800000u), 1.0f, FromBits(0x30800000u), 1.0f};
	const ws::SphereValues sphere{1.0f, 1.0f, FromBits(0x30800000u), FromBits(0x33800000u)};
	Check(ws::SignedDistance(plane, sphere, kNearest).distanceStored == 0.0f, "nearest: 1 + 2^-24 ties to 1, d 0");
	Check(ws::SignedDistance(plane, sphere, kUp).distanceStored == static_cast<float>(P2(-22)), "up: 1 + 2^-23, then + 2^-60 up again, d 2^-22");
	Check(ws::SignedDistance(plane, sphere, kDown).distanceStored == 0.0f && ws::SignedDistance(plane, sphere, kZero).distanceStored == 0.0f,
		"down and zero: d 0");
	Check(ws::WhichSide(sphere, plane, kUp) == PlaneSide::Positive && ws::WhichSide(sphere, plane, kNearest) == PlaneSide::Straddle,
		"WhichSide: 1 under up, 0 at nearest (emulator R1)");
}

void Distance() {
	Section("SignedDistance at nearest: ((n.y*c.y) + (n.x*c.x)) + n.z*c.z, store, minus the constant, store (C1, C4; read 0x0047DA74..0x0047DA93)");
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{0.0f, 0.0f, 1.0f, 2.0f}, ws::SphereValues{0.0f, 0.0f, 5.0f, 1.0f}, kNearest);
		Check(Bits(d.dotStored) == Bits(5.0f), "first store 0x0047DA88: the dot product 5");
		Check(Bits(d.distanceStored) == Bits(3.0f), "second store 0x0047DA93: 5 - 2 = 3");
	}
	{
		// C2: pseudo.c's (z + x) + y gives 0 here.
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{-1e4f, 1e4f, 1.0f, 0.5f}, ws::SphereValues{1e4f, 1e4f, 1.0f, 0.25f}, kNearest);
		Check(Bits(d.dotStored) == Bits(1.0f), "order: (1e8 + -1e8) + 1 = 1, not pseudo.c's (1 + -1e8) + 1e8 = 0");
		Check(Bits(d.distanceStored) == Bits(0.5f), "minus 0.5 -> 0.5");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{-0x1p100f, 0x1p100f, 1.0f, 0.0f}, ws::SphereValues{0x1p100f, 0x1p100f, 1.0f, 0.5f}, kNearest);
		Check(Bits(d.dotStored) == Bits(1.0f), "2^200 - 2^200 cancels in the register before the store: 1, not NaN");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{0.0f, 0x1p100f, 0.0f, 0.0f}, ws::SphereValues{0.0f, 0x1p100f, 0.0f, 1.0f}, kNearest);
		Check(Bits(d.dotStored) == 0x7F800000u, "2^200 becomes +inf only at the store 0x0047DA88");
		Check(Bits(d.distanceStored) == 0x7F800000u, "inf - 0 = inf");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{0.0f, 0x1p100f, 0.0f, kInf}, ws::SphereValues{0.0f, 0x1p100f, 0.0f, 1.0f}, kNearest);
		Check(std::isnan(d.distanceStored), "stored +inf minus +inf: NaN");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{1.0f, 0.0f, 0.0f, FromBits(0x33000001u)}, ws::SphereValues{1.0f, 0.0f, 0.0f, 0.0f}, kNearest);
		Check(Bits(d.distanceStored) == 0x3F7FFFFFu, "1 - (2^-25 + 2^-48) rounds to 1 - 2^-24 at the FSUB 0x0047DA90");
	}
}

void EngineShape() {
	Section("Engine-facing shape: NiBound / NiPlane (layouts asserted in NiBound_WhichSide.h)");
	const ws::engine::NiBound sphere{{0.0f, 0.0f, 5.0f}, 1.0f};
	const ws::engine::NiPlane plane{{0.0f, 0.0f, 1.0f}, 2.0f};
	const ws::engine::NiPlane flipped{{0.0f, 0.0f, -1.0f}, 2.0f};
	for (const ws::X87Rounding mode : kModes) {
		Check(ws::engine::WhichSide(sphere, plane, mode) == 1, "centre z 5, constant 2, radius 1: d = 3 >= 1 -> 1 (every mode: exact)");
		Check(ws::engine::WhichSide(sphere, flipped, mode) == 2, "normal flipped: d = -7 <= -1 -> 2 (every mode)");
	}
	Check(ws::engine::kWhichSideAddress == 0x0047DA70u, "the original's address");
}

// The original's results, transcribed from tools/difftest/expected/0x0047DA70.json
// (emulator run 2026-10-30 on re/export/perf-levers/0x0047DA70, cases
// tools/difftest/cases/0x0047DA70.json; measured in Unicorn's x87, not in the
// game). Every case returned to its sentinel with stack_delta +4 (the RET 0x4
// pops the one argument: balanced for this callee-cleans function), no
// emulator error, ECX = 0x00B00000 and EDX = 0x5A5A5A5A unchanged, no write
// outside the emulator's stack (the two FSTP to [ESP+4] are stack writes the
// emulator does not record), no call, and the 32 input bytes read back
// unchanged.
struct EmulatorCase {
	const char* name;
	std::uint32_t sphere[4];  // centre x, y, z, radius at ECX+0x00..+0x0C
	std::uint32_t plane[4];   // normal x, y, z, constant at [ESP+4]+0x00..+0x0C
	std::uint16_t fpcw;
	std::uint32_t eax;        // the original's EAX at return
	int writes;               // recorded writes outside the stack
	int calls;                // traced calls
};

constexpr EmulatorCase kCases[] = {
	{"E1 d -3 < -r -1", {0xC0400000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E2 d -1 == -r", {0xBF800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E3 r +0, d +0", {0, 0, 0, 0}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E4 straddle d 0.5, r 1", {0x3F000000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E5 d == r 1", {0x3F800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E6 d 3 > r 1", {0x40400000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E7 d NaN", {0x7FC00000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E8 r NaN", {0x3F000000u, 0, 0, 0x7FC00000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E9 r -1, d 0.5", {0x3F000000u, 0, 0, 0xBF800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E10 r -1, d 2", {0x40000000u, 0, 0, 0xBF800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E11 r -1, d NaN", {0x7FC00000u, 0, 0, 0xBF800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E12 r +inf, d 5", {0x40A00000u, 0, 0, 0x7F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E13 d +inf", {0x7F800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E14 d -inf", {0xFF800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E15 constant 4.5, centre x 5, r 0.25", {0x40A00000u, 0, 0, 0x3E800000u}, {0x3F800000u, 0, 0, 0x40900000u}, 0x007F, 1, 0, 0},
	{"E16 summation order", {0x461C4000u, 0x461C4000u, 0x3F800000u, 0x3E800000u}, {0xC61C4000u, 0x461C4000u, 0x3F800000u, 0x3F000000u}, 0x007F, 1, 0, 0},
	{"E17 PC 24 tie at the first FADDP", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x007F, 0, 0, 0},
	{"E19 extended exponent in the register", {0x71800000u, 0x71800000u, 0x3F800000u, 0x3F000000u}, {0xF1800000u, 0x71800000u, 0x3F800000u, 0}, 0x007F, 1, 0, 0},
	{"E20 +inf at the store", {0, 0x71800000u, 0, 0x3F800000u}, {0, 0x71800000u, 0, 0}, 0x007F, 1, 0, 0},
	{"E21 -inf at the store", {0, 0x71800000u, 0, 0x3F800000u}, {0, 0xF1800000u, 0, 0}, 0x007F, 2, 0, 0},
	{"E22 +inf minus constant +inf", {0, 0x71800000u, 0, 0x3F800000u}, {0, 0x71800000u, 0, 0x7F800000u}, 0x007F, 0, 0, 0},
	{"E23 double rounding to a denormal at the store", {0x1F800001u, 0, 0, 0x00000001u}, {0x1F800001u, 0, 0, 0x00200000u}, 0x007F, 0, 0, 0},
	{"E24 the FSUB rounds below a tie", {0x3F800000u, 0, 0, 0x3F7FFFFFu}, {0x3F800000u, 0, 0, 0x33000001u}, 0x007F, 1, 0, 0},
};

// E18: E17's inputs at fpcw 0x037F (64-bit precision). The original returned
// 1 there (1 + 2^-24 + 2^-60 stays exact in the register, the store rounds it
// up). The game runs at 24 bits (measured, knowledge/frame-clock.md) and the
// rebuild models only that, so it gives E17's 0 for these inputs: the
// difference is the documented precision dependence (C11), and E17/E18
// together show that the emulator honours the precision control.
constexpr EmulatorCase kPc64Case =
	{"E18 E17 at PC 64", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x037F, 1, 0, 0};

// The rebuild under the case's rounding control (fpcw bits 10-11); E18's
// precision control 64 is not modelled (see kPc64Case).
std::int32_t RunRebuild(const EmulatorCase& c) {
	const ws::engine::NiBound sphere{{FromBits(c.sphere[0]), FromBits(c.sphere[1]), FromBits(c.sphere[2])}, FromBits(c.sphere[3])};
	const ws::engine::NiPlane plane{{FromBits(c.plane[0]), FromBits(c.plane[1]), FromBits(c.plane[2])}, FromBits(c.plane[3])};
	return ws::engine::WhichSide(sphere, plane, ws::RoundingFromControlWord(c.fpcw));
}

void Differential() {
	Section("Differential (emulator gate, layer 1): the rebuild against the original's EAX, case by case (measured)");
	char line[160];
	for (const EmulatorCase& c : kCases) {
		const std::int32_t rebuilt = RunRebuild(c);
		std::snprintf(line, sizeof(line), "%s: original EAX %u, rebuild %d", c.name, static_cast<unsigned>(c.eax), static_cast<int>(rebuilt));
		Check(c.fpcw == 0x007F && static_cast<std::uint32_t>(rebuilt) == c.eax, line);
		std::snprintf(line, sizeof(line), "%s: no write outside the stack, no call (the rebuild has no ports)", c.name);
		Check(c.writes == 0 && c.calls == 0, line);
	}
	Check(std::size(kCases) == 23, "23 cases at fpcw 0x007F, E18 apart");

	Section("E18: the original at PC 64 differs from the PC 24 reading the rebuild models");
	Check(kPc64Case.eax == 1, "the original at fpcw 0x037F returned 1");
	Check(RunRebuild(kPc64Case) == 0, "the rebuild (PC 24, nearest from 0x037F's RC bits) returns E17's 0 for the same inputs");
}

// The original under the four rounding controls, transcribed from
// tools/difftest/expected/0x0047DA70-rounding.json (emulator run 2026-10-30
// on re/export/perf-levers/0x0047DA70, cases tools/difftest/cases/
// 0x0047DA70-rounding.json from generators/0x0047DA70-rounding.py; measured
// in Unicorn's x87). E1..E24 (E18 apart) at fpcw 0x047F, 0x087F, 0x0C7F,
// then R1..R15 at all four. Every case returned to its sentinel with
// stack_delta +4, no emulator error, ECX and EDX unchanged, no write outside
// the stack, no call, and the 32 input bytes read back unchanged. The same
// 129 inputs gave the same EAX on the host's own x87 running the original's
// 92 bytes (scratch program, 2026-10-30; not part of this test).
// clang-format off
constexpr EmulatorCase kRoundingCases[] = {
	{"E1 B1 not taken", {0xC0400000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"E2 B1 not taken at equality", {0xBF800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"E3 B1 not taken", {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"E4 B1 taken, B2 taken (r > d)", {0x3F000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"E5 B1 taken, B2 not taken at equality", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 1, 0, 0},
	{"E6 B1 taken, B2 not taken", {0x40400000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 1, 0, 0},
	{"E7 d NaN (centre x NaN)", {0x7FC00000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"E8 r NaN", {0x3F000000u, 0x00000000u, 0x00000000u, 0x7FC00000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"E9 r = -1, d = 0.5 <= -r", {0x3F000000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"E10 r = -1, d = 2 > -r", {0x40000000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 1, 0, 0},
	{"E11 r = -1, d NaN -> 0", {0x7FC00000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"E12 r = +inf, d = 5 -> 0", {0x40A00000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"E13 d = +inf (centre x +inf), r = 1 -> 1", {0x7F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 1, 0, 0},
	{"E14 d = -inf (centre x -inf), r = 1 -> 2", {0xFF800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"E15 the plane constant", {0x40A00000u, 0x00000000u, 0x00000000u, 0x3E800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x40900000u}, 0x047F, 1, 0, 0},
	{"E16 summation order (C1/C2)", {0x461C4000u, 0x461C4000u, 0x3F800000u, 0x3E800000u}, {0xC61C4000u, 0x461C4000u, 0x3F800000u, 0x3F000000u}, 0x047F, 1, 0, 0},
	{"E17 PC 24 tie", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x047F, 0, 0, 0},
	{"E19 extended exponent in the register", {0x71800000u, 0x71800000u, 0x3F800000u, 0x3F000000u}, {0xF1800000u, 0x71800000u, 0x3F800000u, 0x00000000u}, 0x047F, 1, 0, 0},
	{"E20 overflow at the store 0x0047DA88", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x00000000u}, 0x047F, 1, 0, 0},
	{"E21 overflow at the store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"E22 stored +inf minus constant +inf", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F800000u}, 0x047F, 2, 0, 0},
	{"E23 double rounding at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x047F, 0, 0, 0},
	{"E24 the subtraction 0x0047DA90 rounds to 24 bits", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F7FFFFFu}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x047F, 1, 0, 0},
	{"E1 B1 not taken", {0xC0400000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"E2 B1 not taken at equality", {0xBF800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"E3 B1 not taken", {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"E4 B1 taken, B2 taken (r > d)", {0x3F000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"E5 B1 taken, B2 not taken at equality", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"E6 B1 taken, B2 not taken", {0x40400000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"E7 d NaN (centre x NaN)", {0x7FC00000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"E8 r NaN", {0x3F000000u, 0x00000000u, 0x00000000u, 0x7FC00000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"E9 r = -1, d = 0.5 <= -r", {0x3F000000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"E10 r = -1, d = 2 > -r", {0x40000000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"E11 r = -1, d NaN -> 0", {0x7FC00000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"E12 r = +inf, d = 5 -> 0", {0x40A00000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"E13 d = +inf (centre x +inf), r = 1 -> 1", {0x7F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"E14 d = -inf (centre x -inf), r = 1 -> 2", {0xFF800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"E15 the plane constant", {0x40A00000u, 0x00000000u, 0x00000000u, 0x3E800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x40900000u}, 0x087F, 1, 0, 0},
	{"E16 summation order (C1/C2)", {0x461C4000u, 0x461C4000u, 0x3F800000u, 0x3E800000u}, {0xC61C4000u, 0x461C4000u, 0x3F800000u, 0x3F000000u}, 0x087F, 1, 0, 0},
	{"E17 PC 24 tie", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x087F, 1, 0, 0},
	{"E19 extended exponent in the register", {0x71800000u, 0x71800000u, 0x3F800000u, 0x3F000000u}, {0xF1800000u, 0x71800000u, 0x3F800000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"E20 overflow at the store 0x0047DA88", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"E21 overflow at the store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"E22 stored +inf minus constant +inf", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F800000u}, 0x087F, 0, 0, 0},
	{"E23 double rounding at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x087F, 1, 0, 0},
	{"E24 the subtraction 0x0047DA90 rounds to 24 bits", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F7FFFFFu}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x087F, 1, 0, 0},
	{"E1 B1 not taken", {0xC0400000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
	{"E2 B1 not taken at equality", {0xBF800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
	{"E3 B1 not taken", {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
	{"E4 B1 taken, B2 taken (r > d)", {0x3F000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"E5 B1 taken, B2 not taken at equality", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 1, 0, 0},
	{"E6 B1 taken, B2 not taken", {0x40400000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 1, 0, 0},
	{"E7 d NaN (centre x NaN)", {0x7FC00000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"E8 r NaN", {0x3F000000u, 0x00000000u, 0x00000000u, 0x7FC00000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"E9 r = -1, d = 0.5 <= -r", {0x3F000000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
	{"E10 r = -1, d = 2 > -r", {0x40000000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 1, 0, 0},
	{"E11 r = -1, d NaN -> 0", {0x7FC00000u, 0x00000000u, 0x00000000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"E12 r = +inf, d = 5 -> 0", {0x40A00000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"E13 d = +inf (centre x +inf), r = 1 -> 1", {0x7F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 1, 0, 0},
	{"E14 d = -inf (centre x -inf), r = 1 -> 2", {0xFF800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
	{"E15 the plane constant", {0x40A00000u, 0x00000000u, 0x00000000u, 0x3E800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x40900000u}, 0x0C7F, 1, 0, 0},
	{"E16 summation order (C1/C2)", {0x461C4000u, 0x461C4000u, 0x3F800000u, 0x3E800000u}, {0xC61C4000u, 0x461C4000u, 0x3F800000u, 0x3F000000u}, 0x0C7F, 1, 0, 0},
	{"E17 PC 24 tie", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x0C7F, 0, 0, 0},
	{"E19 extended exponent in the register", {0x71800000u, 0x71800000u, 0x3F800000u, 0x3F000000u}, {0xF1800000u, 0x71800000u, 0x3F800000u, 0x00000000u}, 0x0C7F, 1, 0, 0},
	{"E20 overflow at the store 0x0047DA88", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x00000000u}, 0x0C7F, 1, 0, 0},
	{"E21 overflow at the store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
	{"E22 stored +inf minus constant +inf", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F800000u}, 0x0C7F, 2, 0, 0},
	{"E23 double rounding at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x0C7F, 0, 0, 0},
	{"E24 the subtraction 0x0047DA90 rounds to 24 bits", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F7FFFFFu}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x0C7F, 1, 0, 0},
	{"R1 tie at the first FADDP", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x007F, 0, 0, 0},
	{"R1 tie at the first FADDP", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x047F, 0, 0, 0},
	{"R1 tie at the first FADDP", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x087F, 1, 0, 0},
	{"R1 tie at the first FADDP", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x0C7F, 0, 0, 0},
	{"R2 R1 mirrored", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, 0x007F, 0, 0, 0},
	{"R2 R1 mirrored", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, 0x047F, 2, 0, 0},
	{"R2 R1 mirrored", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, 0x087F, 0, 0, 0},
	{"R2 R1 mirrored", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, 0x0C7F, 0, 0, 0},
	{"R3 sticky below double", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0x2B800000u, 0x3F800000u, 0x00000000u, 0x3F800000u}, 0x007F, 0, 0, 0},
	{"R3 sticky below double", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0x2B800000u, 0x3F800000u, 0x00000000u, 0x3F800000u}, 0x047F, 0, 0, 0},
	{"R3 sticky below double", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0x2B800000u, 0x3F800000u, 0x00000000u, 0x3F800000u}, 0x087F, 1, 0, 0},
	{"R3 sticky below double", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0x2B800000u, 0x3F800000u, 0x00000000u, 0x3F800000u}, 0x0C7F, 0, 0, 0},
	{"R4 R3 mirrored", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0xAB800000u, 0xBF800000u, 0x00000000u, 0xBF800000u}, 0x007F, 0, 0, 0},
	{"R4 R3 mirrored", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0xAB800000u, 0xBF800000u, 0x00000000u, 0xBF800000u}, 0x047F, 2, 0, 0},
	{"R4 R3 mirrored", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0xAB800000u, 0xBF800000u, 0x00000000u, 0xBF800000u}, 0x087F, 0, 0, 0},
	{"R4 R3 mirrored", {0x2B800000u, 0x3F800000u, 0x00000000u, 0x33800000u}, {0xAB800000u, 0xBF800000u, 0x00000000u, 0xBF800000u}, 0x0C7F, 0, 0, 0},
	{"R5 overflow at the first store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F7FFFFFu}, 0x007F, 1, 0, 0},
	{"R5 overflow at the first store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F7FFFFFu}, 0x047F, 0, 0, 0},
	{"R5 overflow at the first store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F7FFFFFu}, 0x087F, 1, 0, 0},
	{"R5 overflow at the first store", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0x71800000u, 0x00000000u, 0x7F7FFFFFu}, 0x0C7F, 0, 0, 0},
	{"R6 R5 mirrored", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0xFF7FFFFFu}, 0x007F, 2, 0, 0},
	{"R6 R5 mirrored", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0xFF7FFFFFu}, 0x047F, 2, 0, 0},
	{"R6 R5 mirrored", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0xFF7FFFFFu}, 0x087F, 0, 0, 0},
	{"R6 R5 mirrored", {0x00000000u, 0x71800000u, 0x00000000u, 0x3F800000u}, {0x00000000u, 0xF1800000u, 0x00000000u, 0xFF7FFFFFu}, 0x0C7F, 0, 0, 0},
	{"R7 overflow at the second store", {0x7F7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0xFF7FFFFFu}, 0x007F, 1, 0, 0},
	{"R7 overflow at the second store", {0x7F7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0xFF7FFFFFu}, 0x047F, 0, 0, 0},
	{"R7 overflow at the second store", {0x7F7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0xFF7FFFFFu}, 0x087F, 1, 0, 0},
	{"R7 overflow at the second store", {0x7F7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0xFF7FFFFFu}, 0x0C7F, 0, 0, 0},
	{"R8 R7 mirrored", {0xFF7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x7F7FFFFFu}, 0x007F, 2, 0, 0},
	{"R8 R7 mirrored", {0xFF7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x7F7FFFFFu}, 0x047F, 2, 0, 0},
	{"R8 R7 mirrored", {0xFF7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x7F7FFFFFu}, 0x087F, 0, 0, 0},
	{"R8 R7 mirrored", {0xFF7FFFFFu, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x7F7FFFFFu}, 0x0C7F, 0, 0, 0},
	{"R9 denormal at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x007F, 0, 0, 0},
	{"R9 denormal at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x047F, 0, 0, 0},
	{"R9 denormal at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x087F, 1, 0, 0},
	{"R9 denormal at the store", {0x1F800001u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1F800001u, 0x00000000u, 0x00000000u, 0x00200000u}, 0x0C7F, 0, 0, 0},
	{"R10 a denormal below the smallest", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x007F, 0, 0, 0},
	{"R10 a denormal below the smallest", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"R10 a denormal below the smallest", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"R10 a denormal below the smallest", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"R11 R10 negative", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x9A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x007F, 0, 0, 0},
	{"R11 R10 negative", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x9A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"R11 R10 negative", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x9A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"R11 R10 negative", {0x1A000000u, 0x00000000u, 0x00000000u, 0x00000001u}, {0x9A000000u, 0x00000000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"R12 the FSUB rounds", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x007F, 0, 0, 0},
	{"R12 the FSUB rounds", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x047F, 0, 0, 0},
	{"R12 the FSUB rounds", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x087F, 1, 0, 0},
	{"R12 the FSUB rounds", {0x3F800000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x33000001u}, 0x0C7F, 0, 0, 0},
	{"R13 a negative tie", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0xB3800000u, 0xBF800000u, 0x00000000u, 0x00000000u}, 0x007F, 0, 0, 0},
	{"R13 a negative tie", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0xB3800000u, 0xBF800000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"R13 a negative tie", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0xB3800000u, 0xBF800000u, 0x00000000u, 0x00000000u}, 0x087F, 0, 0, 0},
	{"R13 a negative tie", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0xB3800000u, 0xBF800000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"R14 R13 positive", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0x33800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x007F, 0, 0, 0},
	{"R14 R13 positive", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0x33800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x047F, 0, 0, 0},
	{"R14 R13 positive", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0x33800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x087F, 1, 0, 0},
	{"R14 R13 positive", {0x40400000u, 0x3F800001u, 0x00000000u, 0x3F800003u}, {0x33800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x0C7F, 0, 0, 0},
	{"R15 exact cancellation", {0x3F800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, {0xBF800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x007F, 2, 0, 0},
	{"R15 exact cancellation", {0x3F800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, {0xBF800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x047F, 2, 0, 0},
	{"R15 exact cancellation", {0x3F800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, {0xBF800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x087F, 2, 0, 0},
	{"R15 exact cancellation", {0x3F800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, {0xBF800000u, 0x3F800000u, 0x00000000u, 0x00000000u}, 0x0C7F, 2, 0, 0},
};
// clang-format on

void DifferentialRounding() {
	Section("Differential under the four rounding controls (emulator gate, layer 1): the rebuild against the original's EAX (measured)");
	char line[200];
	int perMode[4] = {0, 0, 0, 0};
	int modesDiffer = 0;
	for (const EmulatorCase& c : kRoundingCases) {
		const ws::X87Rounding mode = ws::RoundingFromControlWord(c.fpcw);
		++perMode[static_cast<unsigned>(mode)];
		const std::int32_t rebuilt = RunRebuild(c);
		std::snprintf(line, sizeof(line), "%s at fpcw %04X: original EAX %u, rebuild %d", c.name, static_cast<unsigned>(c.fpcw),
			static_cast<unsigned>(c.eax), static_cast<int>(rebuilt));
		Check((c.fpcw & 0xF3FFu) == 0x007Fu && static_cast<std::uint32_t>(rebuilt) == c.eax, line);
		EmulatorCase nearest = c;
		nearest.fpcw = 0x007F;
		modesDiffer += RunRebuild(nearest) != rebuilt ? 1 : 0;
	}
	Check(std::size(kRoundingCases) == 129, "129 cases: 23 E cases at three control words, 15 R cases at four");
	Check(perMode[0] == 15 && perMode[1] == 38 && perMode[2] == 38 && perMode[3] == 38, "15 at nearest, 38 each at down, up, truncate");
	std::snprintf(line, sizeof(line), "the mode decides the answer in %d of them, 22 expected (a nearest-only rebuild fails those)", modesDiffer);
	Check(modesDiffer == 22, line);
}

} // namespace

int main() {
	ReturnValues();
	BranchB1();
	BranchB2();
	Classification();
	Pc24Rounding();
	ControlWord();
	RoundingDecision();
	RoundingModes();
	Distance();
	EngineShape();
	Differential();
	DifferentialRounding();
	return Failures();
}
