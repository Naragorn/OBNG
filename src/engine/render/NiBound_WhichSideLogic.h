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
// Floating point: the 24-bit reading. The game's x87 runs at precision
// control 24 bits, round to nearest (measured, knowledge/frame-clock.md;
// the emulator cases run at fpcw 0x007F). Under that setting FMUL, FADDP and
// FSUB round the significand to 24 bits but keep the register's exponent
// range; FLD, FSTP and FCOM are untouched by it (Intel SDM vol. 1 8.1.5.2,
// quoted in knowledge/bone-palettes.md, read). So the original rounds twice
// where it stores: once to a 24-bit significand in the register, once to the
// float format at FSTP float [ESP+4] (0x0047DA88, 0x0047DA93). The rebuild
// models exactly that:
//   - each FMUL/FADDP/FSUB is computed in double and rounded by RoundToPc24
//     (24-bit significand, ties to even, double's exponent range);
//   - each FSTP float is StoreSingle (the IEEE conversion to float, round to
//     nearest; overflow to +-inf and denormal results as with the exception
//     masks of fpcw 0x007F).
// Why double is wide enough (derived): a float times a float has at most 48
// significant bits, exact in double's 53; every sum here adds two values
// with 24-bit significands, and its double result rounded to 24 bits equals
// the exact sum rounded to 24 bits (53 >= 2*24 + 2: when the double sum is
// inexact the smaller operand lies below 2^-29 of the result, far from a
// 24-bit tie; the general theorem, Figueroa 1995, was not fetched - unverified
// as a citation, the emulator cases E17, E19, E23 and E24 agree). Every value
// lies between 2^-298 and 2^258 in magnitude or is zero, inside double's
// normal range, so the register's extended exponent is modelled too
// (derived from the float operand ranges).
// Plain float arithmetic would differ from the original at the edges: a sum
// beyond FLT_MAX that cancels before the store (E19), and a result below
// FLT_MIN rounded twice (E23).
// NaN payloads are not modelled (SSE's default NaN differs from the x87's
// indefinite); they never reach the result, which only compares (derived).

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>

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

// One x87 arithmetic result at precision control 24: the significand rounded
// to 24 bits, ties to even, the exponent unbounded within double's range.
// Precondition: value is zero, infinite, NaN, or a normal double (true for
// every value of this body, see above).
inline double RoundToPc24(double value) {
	if (!std::isfinite(value)) {
		return value;
	}
	constexpr std::uint64_t kDroppedMask = (std::uint64_t{1} << 29) - 1;  // 52 - 23 fraction bits dropped
	constexpr std::uint64_t kHalf = std::uint64_t{1} << 28;
	constexpr std::uint64_t kKeptLowBit = std::uint64_t{1} << 29;
	std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
	const std::uint64_t dropped = bits & kDroppedMask;
	bits &= ~kDroppedMask;
	if (dropped > kHalf || (dropped == kHalf && (bits & kKeptLowBit) != 0)) {
		bits += kKeptLowBit;  // a carry out of the fraction raises the exponent
	}
	return std::bit_cast<double>(bits);
}

// FMUL float: a float in the register times a float operand.
inline double MulPc24(float a, float b) {
	return RoundToPc24(static_cast<double>(a) * static_cast<double>(b));
}

// FADDP / FSUB: two register values with 24-bit significands.
inline double AddPc24(double a, double b) {
	return RoundToPc24(a + b);
}

inline double SubPc24(double a, double b) {
	return RoundToPc24(a - b);
}

// FSTP float [ESP+4]: the register value rounded to the float format.
inline float StoreSingle(double value) {
	return static_cast<float>(value);
}

// What the two stores through [ESP+4] hold.
struct DistanceStores {
	float dotStored;       // 0x0047DA88: dot(normal, centre)
	float distanceStored;  // 0x0047DA93: that minus the plane constant, d
};

// 0x0047DA74..0x0047DA97 (read): FLD n.y, FMUL c.y; FLD n.x, FMUL c.x; FADDP;
// FLD n.z, FMUL c.z; FADDP; FSTP/FLD [ESP+4]; FSUB constant; FSTP/FLD [ESP+4].
inline DistanceStores SignedDistance(PlaneValues plane, SphereValues sphere) {
	const double yTerm = MulPc24(plane.normalY, sphere.centerY);
	const double xTerm = MulPc24(plane.normalX, sphere.centerX);
	const double yx = AddPc24(yTerm, xTerm);
	const double zTerm = MulPc24(plane.normalZ, sphere.centerZ);
	const double dot = AddPc24(yx, zTerm);
	const float dotStored = StoreSingle(dot);
	const float distanceStored = StoreSingle(SubPc24(dotStored, plane.constant));
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
// wins (C6, derived).
inline PlaneSide Classify(float distance, float radius) {
	if (IsWhollyNegative(distance, radius)) {
		return PlaneSide::Negative;
	}
	if (IsWhollyPositive(distance, radius)) {
		return PlaneSide::Positive;
	}
	return PlaneSide::Straddle;
}

// 0x0047DA70 as a whole: the radius is read after the stores (0x0047DA9B,
// 0x0047DAB3), from the same [ECX+0xC] both times.
inline PlaneSide WhichSide(SphereValues sphere, PlaneValues plane) {
	return Classify(SignedDistance(plane, sphere).distanceStored, sphere.radius);
}

} // namespace obng::engine::render::which_side
