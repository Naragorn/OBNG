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

#include <bit>
#include <cfloat>
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

// Bit tests instead of the <cmath> classification calls: no CRT call can sit in
// the hot path (the 32-bit MS CRT's math routines save, set and restore the x87
// control word - FLDCW, serialising - and run under the caller's word).
constexpr std::uint64_t Bits(double value) {
	return std::bit_cast<std::uint64_t>(value);
}

constexpr bool IsFinite(double value) {
	return ((Bits(value) >> 52) & 0x7FFu) != 0x7FFu;
}

constexpr bool SignBit(double value) {
	return (Bits(value) >> 63) != 0;
}

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
// the mode, the exponent unbounded within double's range - integer work on
// the double's bits only: the 29 low fraction bits are the dropped part, the
// tail's sign the sticky information below them, a carry out of the fraction
// raises the exponent field. Preconditions: the head is zero, infinite, NaN
// or a normal double, and a zero head has a zero tail (true for TwoSum's
// result). A double-subnormal head (below 2^-1022) is excluded, not handled:
// it cannot occur here - a product of two floats is 0 or at least
// 2^-149 * 2^-149 = 2^-298, and a sum of two such 24-bit register values is 0
// or a non-zero multiple of 2^-321 (derived), both far above 2^-1022.
inline double RoundToPc24(ExactSum value, X87Rounding mode) {
	if (!detail::IsFinite(value.head) || value.head == 0.0) {
		return value.head;
	}
	std::uint64_t bits = std::bit_cast<std::uint64_t>(value.head);
	const bool negative = (bits >> 63) != 0;
	const int tailAway = value.tail == 0.0 ? 0 : (detail::SignBit(value.tail) == negative ? 1 : -1);
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
	if (a == 0.0 && b == 0.0 && detail::SignBit(a) == detail::SignBit(b)) {
		return a;
	}
	return mode == X87Rounding::Down ? -0.0 : 0.0;
}

// FADDP / FSUB: two register values with 24-bit significands.
inline double AddPc24(double a, double b, X87Rounding mode) {
	const ExactSum sum = TwoSum(a, b);
	if (sum.head == 0.0 && detail::IsFinite(a) && detail::IsFinite(b)) {
		return ExactZeroSum(a, b, mode);
	}
	return RoundToPc24(sum, mode);
}

inline double SubPc24(double a, double b, X87Rounding mode) {
	return AddPc24(a, -b, mode);  // negation is exact
}

// ---- FSTP float: the register value rounded to the float format ------------------


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
// OverflowResult. Zeros, infinities and NaNs convert exactly. Integer work on
// the double's bits only (no CRT call): the 53-bit significand is shifted
// right by the number of bits the float drops (29 for a normal result, more
// for a denormal one), the shifted-out bits classified against half, the kept
// units rounded per RoundsMagnitudeUp, and the float's bits built by adding
// the units to the exponent field (so a carry to 2^24 units raises the
// exponent, and a denormal rounding up to 2^23 units is FLT_MIN's pattern).
// A double-subnormal input is handled too: it lies far below half the
// smallest float denormal, so it rounds to 0 or 2^-149 by mode.
inline float StoreSingle(double value, X87Rounding mode) {
	const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
	const bool negative = (bits >> 63) != 0;
	const auto biased = static_cast<int>((bits >> 52) & 0x7FFu);
	const std::uint64_t fraction = bits & ((std::uint64_t{1} << 52) - 1);
	if (biased == 0x7FF || (biased == 0 && fraction == 0)) {
		return static_cast<float>(value);  // inf, NaN, +-0: exact (CVTSD2SS, no CRT call)
	}
	constexpr int kFloatBias = 127;
	constexpr int kFloatMinExponent = -126;
	std::uint64_t kept = 0;
	Discarded discarded = Discarded::BelowHalf;  // the double-subnormal case: everything dropped, far below half
	int exponent = kFloatMinExponent;            // of the result's leading unit, for the exponent field
	if (biased != 0) {
		const std::uint64_t significand = fraction | (std::uint64_t{1} << 52);
		exponent = biased - 1023;
		// Bits dropped: 29 for a normal float result, one more per binade below FLT_MIN.
		const int shift = 29 + (exponent < kFloatMinExponent ? kFloatMinExponent - exponent : 0);
		if (shift < 64) {
			kept = significand >> shift;
			const std::uint64_t dropped = significand & ((std::uint64_t{1} << shift) - 1);
			const std::uint64_t half = std::uint64_t{1} << (shift - 1);
			discarded = dropped == 0 ? Discarded::None
				: dropped < half     ? Discarded::BelowHalf
				: dropped == half    ? Discarded::Half
				                     : Discarded::AboveHalf;
		}  // else: shift >= 64 - the value is below 2^-150 * 2^-11, below half a quantum
	}
	const std::uint64_t rounded = kept + (RoundsMagnitudeUp(mode, negative, (kept & 1u) != 0, discarded) ? 1u : 0u);
	// Normal: field (exponent + 127) holds the leading unit 2^23 of `rounded`,
	// so the pattern is ((exponent + 126) << 23) + rounded. Denormal: the
	// pattern is the unit count itself (exponent clamped to -126 gives 0 << 23).
	const std::int64_t field = exponent < kFloatMinExponent ? 0 : exponent + kFloatBias - 1;
	const std::int64_t pattern = (field << 23) + static_cast<std::int64_t>(rounded);
	if (pattern >= 0x7F800000) {
		return OverflowResult(negative, mode);
	}
	const auto magnitude = static_cast<std::uint32_t>(pattern);
	return std::bit_cast<float>(negative ? (magnitude | 0x80000000u) : magnitude);
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

// ---- The native reading: IEEE single on SSE under the same rounding control ----
//
// Measured 2026-10-30 (artifacts/shadow/obng-walkers-routed/20261030-152849,
// OBVR's sampler): the software rounding above costs about five times the
// engine's own walk, 0.46 ms a scene pass. The hardware does the same
// rounding for free: an x87 operation at precision control 24 and an SSE
// single-precision operation both round the exact result of one operation
// to a 24-bit significand under the rounding control they run with (SDM
// vol. 1 4.8.4; both implement IEEE 754's four directions, the x87's
// measured in tests/X87RoundingHostTest.cpp). They differ in the exponent
// range alone: the x87 register keeps 15 exponent bits until the store, the
// single format 8. So the two part only where an operation's result lies
// beyond the single's normal range - above its largest finite value
// (overflow), or below its smallest normal one (underflow: the single rounds
// at the fixed denormal quantum, the register at 24 bits; a denormal operand
// of a later operation is such a value), or when the operation is invalid
// (inf - inf, 0 * inf: both produce a default NaN, whose payload is not
// modelled here; a quiet NaN operand is not invalid, it propagates silently
// in both, with a payload that is not modelled either and reaches only the
// comparisons, which are false for any NaN) - and nowhere else (derived;
// measured against the host's
// own x87 on random edge-biased inputs in tests/NiBound_WhichSideNativeTest.cpp).
// SSE reports exactly these cases in MXCSR's sticky flags while its
// exceptions are masked (SDM vol. 1 11.5.2): IE for the invalid operation,
// DE for a denormal operand, OE for the overflow, UE for a tiny inexact
// result (a tiny exact one raises nothing and is the x87's value too: the
// next operation on it raises DE, a stored final value is exact in both).
// ZE cannot be raised here (no division); PE (inexact) is the ordinary case.
//
// The reading: MXCSR set to the x87 word's rounding control with every
// exception masked, flush-to-zero and denormals-are-zero off and the flags
// cleared (NativeControl); the plain float arithmetic in the original's
// order; then the flags read back. None of the four raised: the result is
// the x87's bit for bit. Any raised: the exact path above recomputes it.
// MXCSR is a port (Mxcsr.h MxcsrPort; the game's own MXCSR at the walk's
// entry was 0x00001FBF, nearest with every flag already set, measured
// 2026-10-30 13:18, so both the control and the flags must be written) and is
// put back as found, so nothing the walk calls between two batches runs
// under a changed register. The pragma keeps the compiler from moving the
// arithmetic across the port's accesses or assuming the default rounding
// mode: clang lowers the operations to constrained intrinsics with dynamic
// rounding and strict exceptions (measured in the generated code of
// clang++ -m32 and clang-cl, 2026-10-30: the STMXCSR / LDMXCSR pair stays
// around the MULSS / ADDSS / SUBSS); MSVC's fenv_access pragma disables the
// code motion, folding and contraction that assume the default environment
// (learn.microsoft.com/cpp/preprocessor/fenv-access; the MSVC build is not
// run here, unverified).
//
// MXCSR layout (read in clang's xmmintrin.h, the _MM_* macros): flags in
// bits 0-5 (IE 0x01, DE 0x02, ZE 0x04, OE 0x08, UE 0x10, PE 0x20), DAZ bit
// 6, the six masks in bits 7-12 (0x1F80), the rounding control in bits
// 13-14 with the x87's encoding (nearest 0, down 1, up 2, toward zero 3),
// FTZ bit 15.

#if defined(__clang__)
#define OBNG_FENV_ACCESS_ON _Pragma("STDC FENV_ACCESS ON") _Pragma("clang fp contract(off)")
#elif defined(_MSC_VER)
#define OBNG_FENV_ACCESS_ON __pragma(fenv_access(on))
#else
#error "the native reading needs a compiler whose floating-point environment access is known"
#endif

inline constexpr std::uint32_t kMxcsrExceptionMasks = 0x1F80u;
inline constexpr unsigned kMxcsrRoundingShift = 13;
inline constexpr std::uint32_t kMxcsrInvalid = 0x01u;
inline constexpr std::uint32_t kMxcsrDenormal = 0x02u;
inline constexpr std::uint32_t kMxcsrDivideByZero = 0x04u;
inline constexpr std::uint32_t kMxcsrOverflow = 0x08u;
inline constexpr std::uint32_t kMxcsrUnderflow = 0x10u;
inline constexpr std::uint32_t kMxcsrInexact = 0x20u;
inline constexpr std::uint32_t kMxcsrFallbackFlags = kMxcsrInvalid | kMxcsrDenormal | kMxcsrOverflow | kMxcsrUnderflow;

// The MXCSR the native arithmetic runs under: the mode's rounding control,
// every exception masked, FTZ and DAZ off, every flag clear.
constexpr std::uint32_t NativeControl(X87Rounding mode) {
	return kMxcsrExceptionMasks | (static_cast<std::uint32_t>(mode) << kMxcsrRoundingShift);
}

// Whether the native results are the x87's, from the MXCSR read after them.
constexpr bool NativeResultExact(std::uint32_t mxcsrAfter) {
	return (mxcsrAfter & kMxcsrFallbackFlags) == 0;
}

// 0x0047DA74..0x0047DA97 in single precision: the same operations in the
// same order, each rounded by the hardware; the FSTP/FLD pairs through
// [ESP+4] are the identity for a value in the single's normal range, which
// the flags guarantee when the result is kept. Valid only between a write of
// NativeControl(mode) to MXCSR and the flag check that follows.
inline float SignedDistanceNative(PlaneValues plane, SphereValues sphere) {
	OBNG_FENV_ACCESS_ON
	const float yTerm = plane.normalY * sphere.centerY;
	const float xTerm = plane.normalX * sphere.centerX;
	const float yx = yTerm + xTerm;
	const float zTerm = plane.normalZ * sphere.centerZ;
	const float dot = yx + zTerm;
	return dot - plane.constant;
}

// The distances of up to six planes against one sphere, natively, as one
// MXCSR scope: the register saved, set, the arithmetic, the flags read, the
// register put back. Returns whether the results are the x87's; the caller
// recomputes them on the exact path when not. The lanes are the planes'
// indices with a set bit; other entries of `out` are left alone.
template <class Planes, class Mxcsr>
bool NativeDistances(const Planes& planes, std::uint32_t lanes, SphereValues sphere, X87Rounding mode, Mxcsr& mxcsr,
	float* out) {
	OBNG_FENV_ACCESS_ON
	const std::uint32_t saved = mxcsr.Read();
	mxcsr.Write(NativeControl(mode));
	for (unsigned plane = 0; plane < 6; ++plane) {
		if ((lanes & (std::uint32_t{1} << plane)) != 0) {
			out[plane] = SignedDistanceNative(planes[plane], sphere);
		}
	}
	const std::uint32_t after = mxcsr.Read();
	mxcsr.Write(saved);
	return NativeResultExact(after);
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
