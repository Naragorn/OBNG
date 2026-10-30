#pragma once

// The decisions of 0x0047DA70 (proposed NiBound::WhichSide; the Gamebryo
// name is unverified, C16 of the mapping), as pure functions over plain
// values: a bounding sphere against one plane. Evidence:
// re/functions/0x0047DA70.md and the export re/export/perf-levers/0x0047DA70/
// (asm.s; no callees, no data references). Names are proposals until the
// perf-levers slice's refutation round; the address stays next to each.
//
// Where pseudo.c and the instructions disagree, the instructions are followed
// (AGENTS.md): pseudo.c sums the dot product as (z + x) + y; the instructions
// sum ((y + x) + z) (0x0047DA74..0x0047DA86, read; C1, C2), and pseudo.c
// hides the two stores through [ESP+4] (C3).
//
// Floating point: the 24-bit reading under the caller's rounding control.
// The game's x87 runs at precision control 24 bits (measured,
// knowledge/frame-clock.md); its rounding control (FCW bits 10-11) is NOT
// one value per process: the shadow pass calls this function's callers with
// FCW 0x0C7F, rounding control = truncate (measured by FXSAVE at the entry of
// 0x007D59E0, artifacts/shadow/obng-range-walk-routed/20261030-131826), while
// other code runs at nearest. So the rounding mode is an input of every
// function here (X87Rounding, from the caller's control word); there is no
// default. Under precision control 24, FMUL, FADDP and FSUB round the
// significand to 24 bits but keep the register's exponent range; FLD, FSTP
// and FCOM are untouched by it (Intel SDM vol. 1 8.1.5.2, quoted in
// knowledge/bone-palettes.md, read). The rounding control applies to every
// one of those roundings and to the store's (SDM vol. 1 4.8.4 / 8.1.5.3;
// measured: tools/difftest/cases/0x0047DA70-rounding.json, the four modes on
// the original bytes). So the original rounds twice where it stores: once to
// a 24-bit significand in the register, once to the float format at FSTP
// float [ESP+4] (0x0047DA88, 0x0047DA93), both under the caller's mode. The
// rebuild models exactly that:
//   - each FMUL is the exact product (a float times a float has at most 48
//     significant bits, exact in double's 53) rounded by RoundToPc24;
//   - each FADDP/FSUB is the exact sum, held as a double and its exact error
//     (TwoSum), rounded by RoundToPc24 - the error carries the sticky bit a
//     directed mode needs when the double sum alone is inexact (1 + 2^-80
//     rounds up to 1 + 2^-23 under Up, while the double sum is 1.0), and it
//     decides the ties of round to nearest exactly;
//   - each FSTP float is StoreSingle (the conversion to float under the mode,
//     overflow and denormal results as with the exception masks of the
//     game's control words, 0x..7F).
// Every value lies between 2^-298 and 2^258 in magnitude or is zero, inside
// double's normal range, so the register's extended exponent is modelled too
// (derived from the float operand ranges), and TwoSum is exact there.
// Plain float arithmetic would differ from the original at the edges: a sum
// beyond FLT_MAX that cancels before the store (E19), and a result below
// FLT_MIN rounded twice (E23).
// NaN payloads are not modelled (SSE's default NaN differs from the x87's
// indefinite); they never reach the result, which only compares (derived).

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

namespace obng::engine::render::which_side {

static_assert(FLT_EVAL_METHOD == 0, "the 24-bit reading needs double arithmetic rounded to double (SSE2, not x87 excess precision)");

// The return value in EAX: MOV EAX,0x2 0x0047DAAB, MOV EAX,0x1 0x0047DABF,
// XOR EAX,EAX 0x0047DAC7 (read). Names derived from the comparisons;
// Gamebryo's NiPlane side names are unverified (C16).
enum class PlaneSide : std::int32_t {
	Straddle = 0,  // neither test held: the sphere crosses the plane, or a NaN
	Positive = 1,  // d >= r: wholly on the side the normal points to
	Negative = 2,  // d <= -r: wholly on the other side
};

// Plain values of the two 16-byte inputs (layouts in NiBound_WhichSide.h).
struct SphereValues {
	float centerX;  // [ECX+0x0]
	float centerY;  // [ECX+0x4]
	float centerZ;  // [ECX+0x8]
	float radius;   // [ECX+0xC]
};

struct PlaneValues {
	float normalX;   // [arg+0x0]
	float normalY;   // [arg+0x4]
	float normalZ;   // [arg+0x8]
	float constant;  // [arg+0xC], subtracted (FSUB 0x0047DA90)
};

// ---- The x87 rounding control ------------------------------------------------

// FCW bits 10-11 (Intel SDM vol. 1 8.1.5.3, Table 4-8; the encoding the
// emulator and the host x87 honour, measured: tools/difftest/expected/
// 0x0047DA70-rounding.json, tests/X87RoundingHostTest.cpp): 00 round to nearest
// (ties to even), 01 round down (toward -inf), 10 round up (toward +inf),
// 11 round toward zero (truncate).
enum class X87Rounding : std::uint8_t {
	Nearest = 0,
	Down = 1,
	Up = 2,
	Zero = 3,
};

constexpr X87Rounding RoundingFromControlWord(std::uint16_t controlWord) {
	switch ((controlWord >> 10) & 3u) {
	case 0:
		return X87Rounding::Nearest;
	case 1:
		return X87Rounding::Down;
	case 2:
		return X87Rounding::Up;
	default:
		return X87Rounding::Zero;
	}
}

// Where the part a rounding discards lies, in units of the kept last place.
enum class Discarded : std::uint8_t {
	None,       // exactly 0: the value is representable, nothing rounds
	BelowHalf,  // in (0, 1/2)
	Half,       // exactly 1/2: a tie
	AboveHalf,  // in (1/2, 1)
};

// The one decision every rounding here makes: does the magnitude kept by
// truncation go up one unit of its last place? Round to nearest: above half,
// or a tie with an odd last place (ties to even). Toward zero: never.
// Up (toward +inf): any discarded part of a positive value. Down (toward
// -inf): any discarded part of a negative value (IEEE 754 rounding
// directions; the x87 implements them, SDM vol. 1 4.8.4).
constexpr bool RoundsMagnitudeUp(X87Rounding mode, bool negative, bool keptLastPlaceOdd, Discarded discarded) {
	if (discarded == Discarded::None) {
		return false;
	}
	switch (mode) {
	case X87Rounding::Nearest:
		return discarded == Discarded::AboveHalf || (discarded == Discarded::Half && keptLastPlaceOdd);
	case X87Rounding::Down:
		return negative;
	case X87Rounding::Up:
		return !negative;
	case X87Rounding::Zero:
		return false;
	}
	return false;
}

// ---- One x87 arithmetic result at precision control 24 ------------------------

// An exact real value held as a double and the double's exact error,
// value = head + tail with |tail| <= half an ulp of head (TwoSum).
struct ExactSum {
	double head;
	double tail;
};

// Knuth's TwoSum: head = a + b rounded to double (SSE, round to nearest),
// tail = the exact rest. Exact for finite operands whose sum does not
// overflow double (true for every value here, see above). FLT_EVAL_METHOD 0
// and no multiplication (nothing to contract into an FMA) keep each step one
// IEEE double operation.
inline ExactSum TwoSum(double a, double b) {
	const double head = a + b;
	const double bVirtual = head - a;
	const double aVirtual = head - bVirtual;
	const double tail = (a - aVirtual) + (b - bVirtual);
	return ExactSum{head, tail};
}

namespace detail {

inline constexpr std::uint64_t kDroppedMask = (std::uint64_t{1} << 29) - 1;  // 52 - 23 fraction bits dropped
inline constexpr std::uint64_t kHalf = std::uint64_t{1} << 28;
inline constexpr std::uint64_t kKeptLowBit = std::uint64_t{1} << 29;

// The dropped bits of the head, corrected by the sign of the tail relative
// to the head's magnitude (+1: the exact value lies farther from zero, -1:
// nearer, 0: the head is exact). The tail is below one unit of the head's
// last double place, so it only matters where the head's dropped part is 0
// or exactly half (derived: it cannot move a value across another boundary).
constexpr Discarded ClassifyDropped(std::uint64_t dropped, int tailAway) {
	if (dropped == 0) {
		return tailAway > 0 ? Discarded::BelowHalf : Discarded::None;  // tailAway < 0 is handled by the caller
	}
	if (dropped < kHalf) {
		return Discarded::BelowHalf;
	}
	if (dropped > kHalf) {
		return Discarded::AboveHalf;
	}
	if (tailAway > 0) {
		return Discarded::AboveHalf;
	}
	return tailAway < 0 ? Discarded::BelowHalf : Discarded::Half;
}

} // namespace detail

// The exact value head + tail with its significand rounded to 24 bits under
// the mode, the exponent unbounded within double's range. Preconditions: the
// head is zero, infinite, NaN or a normal double, and a zero head has a zero
// tail (true for TwoSum's result and every value of this body, see above).
inline double RoundToPc24(ExactSum value, X87Rounding mode) {
	if (!std::isfinite(value.head) || value.head == 0.0) {
		return value.head;
	}
	std::uint64_t bits = std::bit_cast<std::uint64_t>(value.head);
	const bool negative = (bits >> 63) != 0;
	const int tailAway = value.tail == 0.0 ? 0 : (std::signbit(value.tail) == negative ? 1 : -1);
	const std::uint64_t dropped = bits & detail::kDroppedMask;
	bits &= ~detail::kDroppedMask;
	Discarded discarded = detail::ClassifyDropped(dropped, tailAway);
	if (dropped == 0 && tailAway < 0) {
		// The head is a 24-bit value and the exact value lies just inside it:
		// truncation keeps the next 24-bit value toward zero (one unit less in
		// the sign-magnitude bits; at a power of two the borrow moves into the
		// exponent, where the units are half as large), and the part discarded
		// is all but the tail of a unit.
		bits -= detail::kKeptLowBit;
		discarded = Discarded::AboveHalf;
	}
	if (RoundsMagnitudeUp(mode, negative, (bits & detail::kKeptLowBit) != 0, discarded)) {
		bits += detail::kKeptLowBit;  // a carry out of the fraction raises the exponent
	}
	return std::bit_cast<double>(bits);
}

// An exact value (a product of two floats, a float, a 24-bit register value).
inline double RoundToPc24(double value, X87Rounding mode) {
	return RoundToPc24(ExactSum{value, 0.0}, mode);
}

// FMUL float: a float in the register times a float operand.
inline double MulPc24(float a, float b, X87Rounding mode) {
	return RoundToPc24(static_cast<double>(a) * static_cast<double>(b), mode);
}

// An exactly zero sum: x + (-x), or two zeros. IEEE 754 6.3, which the x87
// follows (measured on the host's x87 under the four modes,
// tests/X87RoundingHostTest.cpp): +0 in every mode but Down, -0 under Down;
// two zeros of the same sign keep it (x + x keeps x's sign).
inline double ExactZeroSum(double a, double b, X87Rounding mode) {
	if (a == 0.0 && b == 0.0 && std::signbit(a) == std::signbit(b)) {
		return a;
	}
	return mode == X87Rounding::Down ? -0.0 : 0.0;
}

// FADDP / FSUB: two register values with 24-bit significands.
inline double AddPc24(double a, double b, X87Rounding mode) {
	const ExactSum sum = TwoSum(a, b);
	if (sum.head == 0.0 && std::isfinite(a) && std::isfinite(b)) {
		return ExactZeroSum(a, b, mode);
	}
	return RoundToPc24(sum, mode);
}

inline double SubPc24(double a, double b, X87Rounding mode) {
	return AddPc24(a, -b, mode);  // negation is exact
}

// ---- FSTP float: the register value rounded to the float format ------------------

inline constexpr int kFloatMinQuantumExponent = -149;  // the smallest denormal, 2^-149
inline constexpr int kFloatSignificandBits = 24;

// The result of an overflowing store, by mode (IEEE 754 7.4 with the
// overflow exception masked, as in every control word the game was measured
// with; the x87's masked response, SDM vol. 1 4.9.1.4): nearest gives
// infinity; toward zero the largest finite value; Up gives +inf for a
// positive value and -FLT_MAX for a negative one; Down the mirror.
inline float OverflowResult(bool negative, X87Rounding mode) {
	constexpr float kInf = std::numeric_limits<float>::infinity();
	bool infinite = false;
	switch (mode) {
	case X87Rounding::Nearest:
		infinite = true;
		break;
	case X87Rounding::Down:
		infinite = negative;
		break;
	case X87Rounding::Up:
		infinite = !negative;
		break;
	case X87Rounding::Zero:
		infinite = false;
		break;
	}
	const float magnitude = infinite ? kInf : FLT_MAX;
	return negative ? -magnitude : magnitude;
}

// The conversion of any double to float under the mode, with the float
// format's bounds: the significand rounded to 24 bits for a normal result,
// to the fixed quantum 2^-149 for a denormal one (underflow masked: the
// denormal result rounded under the mode, SDM vol. 1 4.9.1.5), overflow per
// OverflowResult. Zeros, infinities and NaNs convert exactly.
inline float StoreSingle(double value, X87Rounding mode) {
	if (!std::isfinite(value) || value == 0.0) {
		return static_cast<float>(value);
	}
	const bool negative = std::signbit(value);
	const double magnitude = std::fabs(value);
	int exponent = 0;
	std::frexp(magnitude, &exponent);  // magnitude = f * 2^exponent, f in [0.5, 1)
	const int quantumExponent = std::max(exponent - kFloatSignificandBits, kFloatMinQuantumExponent);
	// The magnitude in units of the quantum: below 2^24, exact (a power-of-two
	// scaling of a double that neither overflows nor underflows).
	const double units = std::ldexp(magnitude, -quantumExponent);
	const double kept = std::floor(units);
	const double rest = units - kept;  // exact: both are doubles of the same binade or below
	const Discarded discarded = rest == 0.0 ? Discarded::None
		: rest < 0.5                       ? Discarded::BelowHalf
		: rest == 0.5                      ? Discarded::Half
		                                   : Discarded::AboveHalf;
	const bool odd = std::fmod(kept, 2.0) != 0.0;
	const double rounded = RoundsMagnitudeUp(mode, negative, odd, discarded) ? kept + 1.0 : kept;
	const double result = std::ldexp(rounded, quantumExponent);  // exact: at most 2^24 units of a float quantum
	if (result > static_cast<double>(FLT_MAX)) {
		return OverflowResult(negative, mode);
	}
	const float single = static_cast<float>(result);  // exact: the value is a float
	return negative ? -single : single;
}

// What the two stores through [ESP+4] hold.
struct DistanceStores {
	float dotStored;       // 0x0047DA88: dot(normal, centre)
	float distanceStored;  // 0x0047DA93: that minus the plane constant, d
};

// 0x0047DA74..0x0047DA97 (read): FLD n.y, FMUL c.y; FLD n.x, FMUL c.x; FADDP;
// FLD n.z, FMUL c.z; FADDP; FSTP/FLD [ESP+4]; FSUB constant; FSTP/FLD [ESP+4].
// Every rounding under the caller's mode.
inline DistanceStores SignedDistance(PlaneValues plane, SphereValues sphere, X87Rounding mode) {
	const double yTerm = MulPc24(plane.normalY, sphere.centerY, mode);
	const double xTerm = MulPc24(plane.normalX, sphere.centerX, mode);
	const double yx = AddPc24(yTerm, xTerm, mode);
	const double zTerm = MulPc24(plane.normalZ, sphere.centerZ, mode);
	const double dot = AddPc24(yx, zTerm, mode);
	const float dotStored = StoreSingle(dot, mode);
	const float distanceStored = StoreSingle(SubPc24(dotStored, plane.constant, mode), mode);
	return DistanceStores{dotStored, distanceStored};
}

// B1, FLD r / FCHS / FCOMP / TEST AH,0x1 / JNZ 0x0047DAA7: C0 is set for
// -r < d and for unordered, so the jump is not taken exactly when d <= -r
// with both ordered (read; C7 derived). FCHS only flips the sign and FCOM
// compares exactly (C5). The C++ <= is false on unordered (derived).
inline bool IsWhollyNegative(float distance, float radius) {
	return distance <= -radius;
}

// B2, FLD r / FCOMPP / TEST AH,0x41 / JP 0x0047DABD: not taken exactly when
// one of C0 (r < d) and C3 (r == d) is set, i.e. d >= r with both ordered;
// taken for r > d (none set) and unordered (both set) (read; C7 derived).
inline bool IsWhollyPositive(float distance, float radius) {
	return distance >= radius;
}

// 0x0047DA9B..0x0047DAC9: B1 first, so with r < 0 both tests can hold and 2
// wins (C6, derived). No mode: nothing here rounds - FLD of a float is
// exact, FCHS flips the sign bit, FCOMP/FCOMPP compare exactly (C5) - so the
// classification of two stored floats is the same under every rounding
// control (derived).
inline PlaneSide Classify(float distance, float radius) {
	if (IsWhollyNegative(distance, radius)) {
		return PlaneSide::Negative;
	}
	if (IsWhollyPositive(distance, radius)) {
		return PlaneSide::Positive;
	}
	return PlaneSide::Straddle;
}

// 0x0047DA70 as a whole, under the caller's rounding mode: the radius is read
// after the stores (0x0047DA9B, 0x0047DAB3), from the same [ECX+0xC] both
// times.
inline PlaneSide WhichSide(SphereValues sphere, PlaneValues plane, X87Rounding mode) {
	return Classify(SignedDistance(plane, sphere, mode).distanceStored, sphere.radius);
}

} // namespace obng::engine::render::which_side
