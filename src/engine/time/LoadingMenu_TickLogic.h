#pragma once

// The decisions of 0x005AD980 (proposed LoadingMenu_Tick), as pure functions
// over plain values, and RunTick, which replays the body's call and access
// order over injected ports so the engine binding and the tests' fakes share
// one sequence. Evidence: re/functions/0x005AD980.md and the export
// re/export/frame-clock/0x005AD980/asm.s. Names are proposals until the
// frame-clock slice's refutation round; the address stays next to each.
//
// Where pseudo.c and the instructions disagree, the instructions are
// followed (AGENTS.md):
//   - pseudo.c converts sinceLast with (float)iVar10; the instructions keep
//     the FILD result on the x87 stack (0x005ADA12), so no float rounding
//     happens there (read).
//   - pseudo.c divides by (float)_DAT_00a2fc70; the instruction is
//     FDIV double ptr [0x00A2FC70] (0x005ADA1E), a double divisor (read).
//   - pseudo.c drops every `this` (claim C12, read).
// pseudo.c's `fVar1 <= 0.0` for B8 agrees with the instructions, NaN
// included: C0 is set on unordered (0x005ADA37 TEST AH,1), so a NaN timer is
// not expired, and `NaN <= 0` is false in C++ as well (derived).
//
// The original has no guard on three reads; the rebuild does not invent one.
// Where the original reads through a null pointer inside this body, RunTick
// stops at that instruction and reports it as an Outcome instead of reading:
//   - 0x005AD9B6: the second GetContainingMenu result is not tested;
//   - 0x005AD9CA: [menu+0x54] null (the update 0x0047D170 is still called with
//     ECX = 0 and faults itself in the game - that is the callee's, so the
//     port is called exactly as the original calls it);
//   - 0x005ADB1B: [0x00B33398] g_osGlobals null.
//
// Floating point (B7, 0x005ADA0B-0x005ADA2E). The countdown is not computed
// in C++ float or double; it runs on the exact x87 model in namespace x87 below,
// instruction by instruction:
//   - FLD float (0x005ADA0B) and FILD dword (0x005ADA12) load exactly; FADD
//     float (0x005ADA18), FDIV double (0x005ADA1E) and FSUBP (0x005ADA24,
//     DE E9: ST1 = ST1 - ST0) each round their result to the precision
//     control's significand width, with the extended exponent range; FSTP
//     float (0x005ADA26) rounds to float (read: the instructions; the rule:
//     Intel SDM Vol. 1 8.1.5.2, "The precision-control bits only affect the
//     results of the following floating-point instructions: FADD, ... FSUBP,
//     ... FDIV ...", and "When reduced precision is specified, the rounding of
//     the significand value clears the unused bits on the right to zeros").
//     The FLD float/FST float pair at 0x005ADA2A/0x005ADA2E moves a float and
//     changes nothing (derived).
//   - The precision control at this call is not known, so it is an input
//     (Inputs::precision), not a choice. The two known sources of the control
//     word disagree: the MSVC runtime start-up sets 53 bits (Microsoft Learn,
//     "/arch (x86)": "the run-time startup code sets the x87 FPU control word
//     precision-control field to 53-bits"), Direct3D 9 sets 24 bits unless the
//     device is created with D3DCREATE_FPU_PRESERVE (Microsoft Learn,
//     "D3DCREATE": "Direct3D defaults to single-precision round-to-nearest
//     mode"). Which flags Oblivion passes, and the control word of the thread
//     that runs this (B18 allows a thread other than the main one), are not in
//     the export (unverified). The choice changes results (measured, host x87
//     with the original's opcodes, tests/LoadingMenu_TickTest.cpp): timer 0.3f
//     and 300 ms store +0.0 at 24 bits, so B8 is taken, but 1.19209291e-08 at
//     53 and 64 bits, so it is not. A rebuild fixed to one precision would be
//     wrong for the other; the earlier double arithmetic equalled 53 bits
//     only (derived: three roundings to 53 bits, then one to float).
//   - Rounding control round-to-nearest and all six exceptions masked are the
//     chosen assumption (unverified for the game). Both sources above give
//     that state (the runtime: Microsoft Learn "/fp": "masks all floating point
//     exceptions, sets the rounding mode to round to nearest"; Direct3D 9:
//     "round-to-nearest", "Portions of Direct3D assume floating-point unit
//     exceptions are masked"), and the only control word change in the
//     export, 0x0047D170, sets truncation around one conversion and restores
//     the caller's word (0x0047D1C0/0x0047D1CD, re/functions/0x0047D170.md,
//     read there). x87::SupportedPrecision makes the assumption a checked
//     precondition: it refuses any other rounding control, an unmasked
//     exception and the reserved precision 01B.
//   - Every intermediate stays within 2^-1300..2^1300 (derived: the operands
//     are an int32, a float and a double, then one division and one
//     subtraction), far inside the extended range 2^-16382..2^16383, so the
//     register results never over- or underflow; only the float store can.
//   - NaN results follow what the host x87 does (measured, the tests'
//     differential check; AMD Ryzen 7 7800X3D): a signaling NaN operand is
//     quieted, of two NaNs the larger significand wins and, on equal
//     significands, the positive one; an invalid operation gives the real
//     indefinite (negative, significand 0xC000000000000000).

#include <bit>
#include <cstdint>
#include <optional>

namespace obng::engine::time::loading_menu_tick {

// --- An exact model of the x87 arithmetic the countdown uses -------------------------

namespace x87 {

// The precision control field, bits 8-9 of the control word (Intel SDM Vol. 1
// Table 8-2); 01B is reserved and has no enumerator.
enum class Precision : std::uint8_t {
	Single = 0b00,    // 24-bit significand
	Double = 0b10,    // 53-bit significand
	Extended = 0b11,  // 64-bit significand
};

constexpr int SignificandBits(Precision precision) {
	if (precision == Precision::Single) {
		return 24;
	}
	return precision == Precision::Double ? 53 : 64;
}

inline constexpr std::uint16_t kExceptionMasks = 0x003F;  // bits 0-5: IM DM ZM OM UM PM
inline constexpr std::uint16_t kPrecisionField = 0x0300;  // bits 8-9
inline constexpr std::uint16_t kRoundingField = 0x0C00;   // bits 10-11; 00B is round to nearest
inline constexpr int kPrecisionShift = 8;

// The precision of a control word whose state the model covers: all
// exceptions masked and round to nearest. Anything else - another rounding
// control, an unmasked exception, the reserved precision 01B - is not modelled
// and gives no precision. Bits 6-7 and 13-15 are reserved and bit 12 (infinity
// control) "is not meaningful for later version x87 FPU coprocessors" (SDM
// Vol. 1 8.1.6), so they are not inspected.
constexpr std::optional<Precision> SupportedPrecision(std::uint16_t controlWord) {
	if ((controlWord & kExceptionMasks) != kExceptionMasks || (controlWord & kRoundingField) != 0) {
		return std::nullopt;
	}
	switch ((controlWord & kPrecisionField) >> kPrecisionShift) {
	case 0b00:
		return Precision::Single;
	case 0b10:
		return Precision::Double;
	case 0b11:
		return Precision::Extended;
	default:
		return std::nullopt;
	}
}

// One x87 data register. Finite: the value is significand * 2^(exponent - 63)
// with bit 63 of the significand set. NaN: the register's 64 significand bits
// (bit 63 set, bit 62 the quiet bit). Infinity carries bit 63 only and a zero
// nothing, as in the 80-bit image, so equal registers compare equal.
struct Register {
	enum class Kind : std::uint8_t { Zero, Finite, Infinity, NaN };
	Kind kind = Kind::Zero;
	bool negative = false;
	std::uint64_t significand = 0;
	std::int32_t exponent = 0;

	friend constexpr bool operator==(const Register&, const Register&) = default;
};

namespace detail {

inline constexpr std::uint64_t kIntegerBit = 1ull << 63;
inline constexpr std::uint64_t kQuietBit = 1ull << 62;

constexpr Register Zero(bool negative) {
	return Register{Register::Kind::Zero, negative, 0, 0};
}

constexpr Register Infinity(bool negative) {
	return Register{Register::Kind::Infinity, negative, kIntegerBit, 0};
}

// The "real indefinite" an invalid operation returns with the invalid
// exception masked.
constexpr Register Indefinite() {
	return Register{Register::Kind::NaN, true, kIntegerBit | kQuietBit, 0};
}

constexpr bool IsNaN(const Register& value) {
	return value.kind == Register::Kind::NaN;
}

// magnitude * 2^exponentOfBit0, magnitude nonzero.
constexpr Register Normalized(bool negative, std::uint64_t magnitude, std::int32_t exponentOfBit0) {
	int shift = std::countl_zero(magnitude);
	return Register{Register::Kind::Finite, negative, magnitude << shift, exponentOfBit0 + 63 - shift};
}

// An IEEE binary format as a register; a signaling NaN arrives quiet.
constexpr Register FromIeee(std::uint64_t bits, int fractionBits, int exponentBits) {
	const std::int32_t exponentMax = (1 << exponentBits) - 1;
	const std::int32_t bias = exponentMax >> 1;
	const bool negative = ((bits >> (fractionBits + exponentBits)) & 1u) != 0;
	const std::uint64_t fraction = bits & ((1ull << fractionBits) - 1u);
	const auto biased = static_cast<std::int32_t>((bits >> fractionBits) & static_cast<std::uint64_t>(exponentMax));
	if (biased == exponentMax) {
		if (fraction == 0) {
			return Infinity(negative);
		}
		return Register{Register::Kind::NaN, negative, kIntegerBit | kQuietBit | (fraction << (63 - fractionBits)), 0};
	}
	if (biased == 0) {
		return fraction == 0 ? Zero(negative) : Normalized(negative, fraction, 1 - bias - fractionBits);
	}
	return Normalized(negative, fraction | (1ull << fractionBits), biased - bias - fractionBits);
}

// Rounds the exact value (high + low * 2^-64 + a sticky part below low, which
// is nonzero when sticky) * 2^(exponent - 63) to `bits` significand bits, to
// nearest, ties to even. Bit 63 of high is set.
constexpr Register RoundToBits(bool negative, std::uint64_t high, std::uint64_t low, bool sticky, std::int32_t exponent,
	int bits) {
	std::uint64_t kept = high;
	std::uint64_t unit = 1;
	bool guard = (low >> 63) != 0;
	bool rest = (low << 1) != 0 || sticky;
	if (bits < 64) {
		const int dropped = 64 - bits;
		unit = 1ull << dropped;
		const std::uint64_t half = unit >> 1;
		kept = high & ~(unit - 1);
		guard = (high & half) != 0;
		rest = (high & (half - 1)) != 0 || low != 0 || sticky;
	}
	if (guard && (rest || (kept & unit) != 0)) {
		kept += unit;
		if (kept == 0) {  // carried out of bit 63
			kept = kIntegerBit;
			++exponent;
		}
	}
	return Register{Register::Kind::Finite, negative, kept, exponent};
}

constexpr Register Round(const Register& value, int bits) {
	return RoundToBits(value.negative, value.significand, 0, false, value.exponent, bits);
}

// Of two operands at least one of which is a NaN: the NaN that comes out.
constexpr Register PropagateNaN(const Register& a, const Register& b) {
	if (!IsNaN(b)) {
		return a;
	}
	if (!IsNaN(a)) {
		return b;
	}
	if (a.significand != b.significand) {
		return a.significand > b.significand ? a : b;
	}
	return a.negative ? b : a;
}

// A significand shifted right by `distance` bits into a 128-bit window
// (high, low) and a sticky flag for the bits that fall out below it.
struct Shifted {
	std::uint64_t high;
	std::uint64_t low;
	bool sticky;
};

constexpr Shifted ShiftRight(std::uint64_t significand, std::uint32_t distance) {
	if (distance == 0) {
		return Shifted{significand, 0, false};
	}
	if (distance < 64) {
		return Shifted{significand >> distance, significand << (64 - distance), false};
	}
	if (distance == 64) {
		return Shifted{0, significand, false};
	}
	if (distance < 128) {
		return Shifted{0, significand >> (distance - 64), (significand << (128 - distance)) != 0};
	}
	return Shifted{0, 0, true};
}

// Two finite nonzero registers.
constexpr Register AddFinite(Register a, Register b, int bits) {
	if (b.exponent > a.exponent || (b.exponent == a.exponent && b.significand > a.significand)) {
		const Register larger = b;
		b = a;
		a = larger;
	}
	const Shifted aligned = ShiftRight(b.significand, static_cast<std::uint32_t>(a.exponent - b.exponent));
	std::int32_t exponent = a.exponent;
	bool sticky = aligned.sticky;

	if (a.negative == b.negative) {
		std::uint64_t low = aligned.low;
		std::uint64_t high = a.significand + aligned.high;
		if (high < a.significand) {  // carry out of bit 63
			sticky = sticky || (low & 1u) != 0;
			low = (low >> 1) | (high << 63);
			high = (high >> 1) | kIntegerBit;
			++exponent;
		}
		return RoundToBits(a.negative, high, low, sticky, exponent, bits);
	}

	// |a| >= |b|: subtract. A sticky part of b takes one more unit off low and
	// stays sticky: the exact difference then lies strictly above (high, low).
	std::uint64_t low = 0 - aligned.low;
	std::uint64_t high = a.significand - aligned.high - (aligned.low != 0 ? 1u : 0u);
	if (sticky) {
		high -= low == 0 ? 1u : 0u;
		low -= 1u;
	}
	if (high == 0 && low == 0 && !sticky) {
		return Zero(false);  // an exact zero difference is +0 when rounding to nearest
	}
	// Cancellation of more than one bit needs distance <= 1, where nothing is
	// sticky; with a sticky part the shift is at most one bit, and the bit it
	// brings in at the bottom only feeds the sticky decision (derived).
	const int shift = high != 0 ? std::countl_zero(high) : 64 + std::countl_zero(low);
	if (shift >= 64) {
		high = low << (shift - 64);
		low = 0;
	} else if (shift > 0) {
		high = (high << shift) | (low >> (64 - shift));
		low <<= shift;
	}
	return RoundToBits(a.negative, high, low, sticky, exponent - shift, bits);
}

// Two finite nonzero registers: a long division to 128 quotient bits and a
// sticky remainder.
constexpr Register DivideFinite(const Register& dividend, const Register& divisor, int bits) {
	std::int32_t exponent = dividend.exponent - divisor.exponent;
	std::uint64_t remainder = dividend.significand;
	bool remainderHigh = false;  // bit 64 of the remainder
	if (dividend.significand < divisor.significand) {
		remainderHigh = true;  // bit 63 of the dividend moves up to bit 64
		remainder = dividend.significand << 1;
		--exponent;
	}
	std::uint64_t high = 0;
	std::uint64_t low = 0;
	for (int i = 0; i < 128; ++i) {
		const bool bit = remainderHigh || remainder >= divisor.significand;
		if (bit) {
			remainder -= divisor.significand;  // wraps back below 2^64 when remainderHigh
		}
		high = (high << 1) | (low >> 63);
		low = (low << 1) | (bit ? 1u : 0u);
		remainderHigh = (remainder >> 63) != 0;
		remainder <<= 1;
	}
	return RoundToBits(dividend.negative != divisor.negative, high, low, remainderHigh || remainder != 0, exponent, bits);
}

// Rounds significand / 2^shift to an integer, to nearest, ties to even.
constexpr std::uint64_t RoundShiftRight(std::uint64_t significand, int shift) {
	if (shift > 64) {
		return 0;
	}
	if (shift == 64) {
		return ((significand >> 63) != 0 && (significand << 1) != 0) ? 1u : 0u;
	}
	const std::uint64_t unit = 1ull << shift;
	const std::uint64_t half = unit >> 1;
	const std::uint64_t kept = significand >> shift;
	const std::uint64_t dropped = significand & (unit - 1);
	const bool up = dropped > half || (dropped == half && (kept & 1u) != 0);
	return kept + (up ? 1u : 0u);
}

inline constexpr std::uint32_t kFloatInfinityBits = 0x7F800000u;

} // namespace detail

// FILD m32int: exact.
constexpr Register FromInt32(std::int32_t value) {
	if (value == 0) {
		return detail::Zero(false);
	}
	const std::int64_t wide = value;
	return detail::Normalized(value < 0, static_cast<std::uint64_t>(wide < 0 ? -wide : wide), 0);
}

// An m32fp operand (FLD, FADD) as a register: exact; a signaling NaN arrives
// quiet (measured, see the header).
constexpr Register FromFloat(float value) {
	return detail::FromIeee(std::bit_cast<std::uint32_t>(value), 23, 8);
}

// An m64fp operand (FDIV) as a register: exact; a signaling NaN arrives quiet.
constexpr Register FromDouble(double value) {
	return detail::FromIeee(std::bit_cast<std::uint64_t>(value), 52, 11);
}

// FADD: a + b, rounded to the precision control.
constexpr Register Add(const Register& a, const Register& b, Precision precision) {
	using Kind = Register::Kind;
	if (detail::IsNaN(a) || detail::IsNaN(b)) {
		return detail::PropagateNaN(a, b);
	}
	if (a.kind == Kind::Infinity) {
		return b.kind == Kind::Infinity && b.negative != a.negative ? detail::Indefinite() : a;
	}
	if (b.kind == Kind::Infinity) {
		return b;
	}
	const int bits = SignificandBits(precision);
	if (a.kind == Kind::Zero) {
		return b.kind == Kind::Zero ? detail::Zero(a.negative && b.negative) : detail::Round(b, bits);
	}
	if (b.kind == Kind::Zero) {
		return detail::Round(a, bits);
	}
	return detail::AddFinite(a, b, bits);
}

// FSUB/FSUBP: minuend - subtrahend, rounded to the precision control. A NaN
// comes out with its own sign.
constexpr Register Subtract(const Register& minuend, const Register& subtrahend, Precision precision) {
	if (detail::IsNaN(minuend) || detail::IsNaN(subtrahend)) {
		return detail::PropagateNaN(minuend, subtrahend);
	}
	Register negated = subtrahend;
	negated.negative = !negated.negative;
	return Add(minuend, negated, precision);
}

// FDIV: dividend / divisor, rounded to the precision control; a zero divisor
// gives an infinity (zero-divide exception masked).
constexpr Register Divide(const Register& dividend, const Register& divisor, Precision precision) {
	using Kind = Register::Kind;
	if (detail::IsNaN(dividend) || detail::IsNaN(divisor)) {
		return detail::PropagateNaN(dividend, divisor);
	}
	const bool negative = dividend.negative != divisor.negative;
	if (dividend.kind == Kind::Infinity) {
		return divisor.kind == Kind::Infinity ? detail::Indefinite() : detail::Infinity(negative);
	}
	if (divisor.kind == Kind::Infinity) {
		return detail::Zero(negative);
	}
	if (dividend.kind == Kind::Zero) {
		return divisor.kind == Kind::Zero ? detail::Indefinite() : detail::Zero(negative);
	}
	if (divisor.kind == Kind::Zero) {
		return detail::Infinity(negative);
	}
	return detail::DivideFinite(dividend, divisor, SignificandBits(precision));
}

// FST/FSTP m32fp: rounded to float to nearest, whatever the precision
// control; overflow gives an infinity and a result below 2^-126 a subnormal
// (overflow and underflow masked); a NaN keeps its top 23 fraction bits.
constexpr float ToFloat(const Register& value) {
	const std::uint32_t sign = value.negative ? 0x80000000u : 0u;
	switch (value.kind) {
	case Register::Kind::Zero:
		return std::bit_cast<float>(sign);
	case Register::Kind::Infinity:
		return std::bit_cast<float>(sign | detail::kFloatInfinityBits);
	case Register::Kind::NaN:
		return std::bit_cast<float>(
			sign | detail::kFloatInfinityBits | static_cast<std::uint32_t>((value.significand >> 40) & 0x7FFFFFu));
	case Register::Kind::Finite:
		break;
	}
	if (value.exponent < -126) {
		// In units of the smallest subnormal 2^-149: significand * 2^(exponent - 63 + 149).
		const auto units = static_cast<std::uint32_t>(detail::RoundShiftRight(value.significand, -86 - value.exponent));
		return std::bit_cast<float>(sign | units);  // 2^23 units is the smallest normal, encoded the same way
	}
	std::uint64_t significand = detail::RoundShiftRight(value.significand, 40);
	std::int32_t exponent = value.exponent;
	if (significand == (1ull << 24)) {
		significand >>= 1;
		++exponent;
	}
	if (exponent > 127) {
		return std::bit_cast<float>(sign | detail::kFloatInfinityBits);
	}
	return std::bit_cast<float>(sign | (static_cast<std::uint32_t>(exponent + 127) << 23) |
		static_cast<std::uint32_t>(significand & 0x7FFFFFu));
}

} // namespace x87

inline constexpr std::uint32_t kLoadingMenuType = 0x3EF;       // 0x005AD984 PUSH (read); kMenuType_Loading (C2, derived)
inline constexpr std::uint32_t kTraitUser3 = 0xFB1;            // 0x005ADA89 PUSH (read); kTileValue_user3 (C4, derived)
inline constexpr std::int32_t kShownStepLimit = 100;           // 0x005ADA62 CMP ECX,0x64 (read)
inline constexpr std::uint32_t kInterfacePassArgument = 0;     // 0x005ADAF4 PUSH 0 (read)

// Zero test for an address held as an integer or as a pointer
// (TEST reg,reg; JZ - 0x005AD993, 0x005AD9A2, 0x005ADACE, 0x005ADAD8, 0x005ADB09).
template <class Address>
constexpr bool IsNull(Address address) {
	return address == Address{};
}

// --- B2: the elapsed values -------------------------------------------------------

// 0x005AD9CD-0x005AD9D4 (read): a 32-bit SUB of two clock+0x10 values, then
// compared and tested as signed (JLE/JG/JGE, TEST ECX,ECX).
constexpr std::int32_t ElapsedMs(std::uint32_t now, std::uint32_t since) {
	return static_cast<std::int32_t>(now - since);
}

// --- B3..B6: the target step ------------------------------------------------------

// The int settings at 0x00B14160, 0x00B14168, 0x00B14170, compared signed
// (C14, read); their names are unverified (open question 2).
struct StepThresholds {
	std::int32_t alwaysMs;  // [0x00B14160]
	std::int32_t cappedMs;  // [0x00B14168]
	std::int32_t fastMs;    // [0x00B14170]
};

enum class StepRule : std::uint8_t {
	AlwaysAdvance,  // B3 taken: sinceStep > alwaysMs, no cap (0x005AD9DF)
	AtCap,          // B4 taken: targetStep >= slowStepCap (0x005AD9EB JGE)
	CappedAdvance,  // B5 taken: sinceStep > cappedMs (0x005AD9F3 JG)
	FastDisabled,   // B6: fastStepEnabled == 0 (0x005AD9F9 JZ)
	FastAdvance,    // B6 taken: sinceStep > fastMs (0x005ADA01 JLE not taken)
	NotYet,         // B6 not taken: sinceStep <= fastMs
};

constexpr StepRule ChooseStepRule(std::int32_t sinceStep, std::int32_t targetStep, std::int32_t slowStepCap,
	std::uint8_t fastStepEnabled, const StepThresholds& thresholds) {
	if (sinceStep > thresholds.alwaysMs) {
		return StepRule::AlwaysAdvance;
	}
	if (targetStep >= slowStepCap) {
		return StepRule::AtCap;
	}
	if (sinceStep > thresholds.cappedMs) {
		return StepRule::CappedAdvance;
	}
	if (fastStepEnabled == 0) {
		return StepRule::FastDisabled;
	}
	return sinceStep > thresholds.fastMs ? StepRule::FastAdvance : StepRule::NotYet;
}

constexpr bool Advances(StepRule rule) {
	return rule == StepRule::AlwaysAdvance || rule == StepRule::CappedAdvance || rule == StepRule::FastAdvance;
}

// ADD dword ptr [ESI+0x64],1 (0x005AD9DF) and ADD EDX,1 (0x005ADA03): 32-bit
// wrapping adds (read).
constexpr std::int32_t NextStep(std::int32_t step) {
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(step) + 1u);
}

// --- B7: the screen timer ----------------------------------------------------------

// The float at 0x00A2FC78 and the double at 0x00A2FC70; their values are not
// in the export (open question 1; expected 2^32 and 1000.0, unverified).
struct TimerConstants {
	float unsignedFixup;  // FADD float ptr [0x00A2FC78], 0x005ADA18
	double divisor;       // FDIV double ptr [0x00A2FC70], 0x005ADA1E
};

// 0x005ADA09 TEST ECX,ECX ... 0x005ADA16 JGE (read).
constexpr bool NeedsUnsignedFixup(std::int32_t sinceLast) {
	return sinceLast < 0;
}

// ST0 after FILD dword (0x005ADA12, exact) and, when negative, FADD float
// [0x00A2FC78] (0x005ADA18, rounded to the precision control) (read).
constexpr x87::Register ElapsedForTimer(std::int32_t sinceLast, float unsignedFixup, x87::Precision precision) {
	x87::Register elapsed = x87::FromInt32(sinceLast);
	if (NeedsUnsignedFixup(sinceLast)) {
		elapsed = x87::Add(elapsed, x87::FromFloat(unsignedFixup), precision);
	}
	return elapsed;
}

// ST0 after FDIV double [0x00A2FC70] (0x005ADA1E, rounded to the precision
// control) (read).
constexpr x87::Register TimerQuotient(std::int32_t sinceLast, const TimerConstants& constants, x87::Precision precision) {
	return x87::Divide(ElapsedForTimer(sinceLast, constants.unsignedFixup, precision), x87::FromDouble(constants.divisor),
		precision);
}

// ST1 = ST1 - ST0 by FSUBP (0x005ADA24, DE E9), ST1 being the timer loaded by
// FLD float at 0x005ADA0B; rounded to the precision control (read).
constexpr x87::Register TimerDifference(float timer, std::int32_t sinceLast, const TimerConstants& constants,
	x87::Precision precision) {
	return x87::Subtract(x87::FromFloat(timer), TimerQuotient(sinceLast, constants, precision), precision);
}

// The new +0x40: FSTP float (0x005ADA26) of the difference (read).
constexpr float CountDown(float timer, std::int32_t sinceLast, const TimerConstants& constants, x87::Precision precision) {
	return x87::ToFloat(TimerDifference(timer, sinceLast, constants, precision));
}

// --- B8, B9: refresh -----------------------------------------------------------------

// FLDZ; FCOMPP compares 0 with the timer; TEST AH,1 (C0: 0 < timer, also set
// when unordered); JZ means expired (0x005ADA31-0x005ADA3A, read).
constexpr bool TimerExpired(float timer) {
	return timer <= 0.0f;
}

// 0x005ADA3C CMP byte ptr [ESP+0x1C],0: any nonzero byte forces (read).
constexpr bool Refresh(float timer, std::uint8_t forceRefresh) {
	return TimerExpired(timer) || forceRefresh != 0;
}

// --- B10, B11: whether the bar is touched ------------------------------------------

enum class RedrawGate : std::uint8_t {
	Unchanged,  // B10 taken: target == shown and no refresh (0x005ADA5C)
	BarFull,    // B11 taken: shown >= 100 (0x005ADA65 JGE)
	Proceed,
};

constexpr RedrawGate GateRedraw(std::int32_t targetStep, std::int32_t shownStep, bool refresh) {
	if (targetStep == shownStep && !refresh) {
		return RedrawGate::Unchanged;
	}
	if (shownStep >= kShownStepLimit) {
		return RedrawGate::BarFull;
	}
	return RedrawGate::Proceed;
}

// --- B12, B13 --------------------------------------------------------------------------

// 0x005ADA6B CMP ECX,EAX; JZ (read): ECX is the old shownStep, EAX targetStep.
constexpr bool ShownStepCatchesUp(std::int32_t shownStep, std::int32_t targetStep) {
	return shownStep != targetStep;
}

// FILD [ESI+0x60]; FSTP float [ESP+0x18] (0x005ADA78-0x005ADA7E, read): the
// value pushed to the tile and compared at B13 is the float-rounded step.
constexpr float StepAsFloat(std::int32_t shownStep) {
	return static_cast<float>(shownStep);
}

// FCOMP double ptr [0x00A6C4C8]; TEST AH,5; JP (0x005ADA97-0x005ADAA2, read):
// the jump is taken unless C0 alone is set, i.e. go on only when the step is
// below the constant and ordered. The constant's value is unverified (open
// question 1; expected 100.0).
constexpr bool BelowCeiling(float stepAsFloat, double ceiling) {
	return static_cast<double>(stepAsFloat) < ceiling;
}

// --- B18 -------------------------------------------------------------------------------

// 0x005ADB25 CMP EAX,ESI; JNZ (read): GetCurrentThreadId against
// [g_osGlobals+0x10] (mainThreadID, xOBSE GameOSDepend.h:351).
constexpr bool OnMainThread(std::uint32_t currentThreadId, std::uint32_t mainThreadId) {
	return currentThreadId == mainThreadId;
}

// --- The run -----------------------------------------------------------------------------

// The menu fields this function touches, as plain values (offsets in the
// engine header's LoadingMenuLayout). RunTick works on either this or the
// layout itself: it names the fields, the ports supply the reference.
struct MenuFields {
	std::int32_t slowStepCap = -1;     // +0x3C
	float screenTimeLeft = 0.0f;       // +0x40
	std::uint32_t clock = 0;           // +0x54  TimeInfo* in the engine
	std::int32_t shownStep = 0;        // +0x60
	std::int32_t targetStep = 0;       // +0x64
	std::uint32_t stepChangedMs = 0;   // +0x68
	std::uint32_t lastTickMs = 0;      // +0x6C
	std::uint8_t fastStepEnabled = 0;  // +0x70
};

struct Inputs {
	std::uint8_t forceRefresh;  // the stack byte [entry ESP+4] (0x005ADA3C)
	TimerConstants timer;       // 0x00A2FC78, 0x00A2FC70
	double stepCeiling;         // 0x00A6C4C8
	x87::Precision precision;   // the x87 control word's PC field at the call (unverified; see the header)
};

enum class Outcome : std::uint8_t {
	NoTileMenu,          // B1 taken, return at 0x005ADB3A
	NoMenu,              // B2 taken, return at 0x005ADB3A
	FaultNullMenuRead,   // the original reads [0+0x54] at 0x005AD9B6
	FaultNullClockRead,  // the original reads [0+0x10] at 0x005AD9CA
	FaultNullOsGlobals,  // the original reads [0+0x10] at 0x005ADB1B
	OffMainThread,       // B18 taken, return at 0x005ADB3A
	TailCalled,          // B18 not taken, JMP 0x0060D0A0 at 0x005ADB35
};

// The body's sequence over ports:
//   TileMenuForType(type) -> tile          0x00589B70(0x3EF), 0x005AD989
//   ContainingMenu(tile) -> menu           0x005898F0, 0x005AD99D and 0x005AD9AF
//   Fields(menu) -> fields&                the menu's memory (no call)
//   TickCount() -> tick                    GetTickCount via 0x00A280D0, 0x005AD9B9
//   UpdateClock(clock, tick)               0x0047D170, 0x005AD9C2
//   ReadClockLastMs(clock) -> ms           [clock+0x10], 0x005AD9CA and 0x005ADA6F
//   StepThresholds() -> thresholds         [0x00B14160/68/70], from 0x005AD9D7
//   SetTileFloat(tile, trait, value)       Tile::UpdateFloat 0x0058CEB0, 0x005ADA8E
//   RefreshScreen(menu, tile)              0x005AD780, 0x005ADAAE
//   ScreenPeriodSetting() -> float         *0x00403C00(this = 0x00B14158), 0x005ADAB8
//   InterfaceManager() -> manager          0x00582160(0, 1), 0x005ADAC6 and 0x005ADAE5
//   IoManager() -> io                      [0x00B33A10], 0x005ADAD2 and 0x005ADB03
//   IoManagerBefore(io)                    0x00432860, 0x005ADADC
//   InterfaceManagerUpdate(manager)        0x00583F40, 0x005ADAEF
//   InterfacePass(argument)                0x00579260, 0x005ADAF6
//   InterfacePassEnd()                     0x005792B0, 0x005ADAFE
//   IoManagerAfter(io)                     0x00432890, 0x005ADB0D
//   OsGlobals() -> globals                 [0x00B33398], 0x005ADB15 and 0x005ADB2B
//   MainThreadId(globals) -> id            [globals+0x10], 0x005ADB1B
//   CurrentThreadId() -> id                GetCurrentThreadId via 0x00A2808C, 0x005ADB1E
//   OsGlobalsTail(globals)                 JMP 0x0060D0A0, 0x005ADB35
// What 0x005AD780, 0x00583F40, 0x00579260, 0x005792B0, 0x00432860,
// 0x00432890 and 0x0060D0A0 do is unverified (open question 4); the port
// names say where they are called, not what they do.
//
// The settings are read through one port after the first clock read; reading
// them at once instead of one by one at B3/B5/B6 changes nothing, as memory
// reads with no call between them have no effect on each other (derived).
template <class Ports, class Fields>
void RunRedraw(Ports& ports, const Inputs& inputs, Fields& fields, std::uint32_t tileMenu, std::uint32_t menu,
	decltype(Fields::clock) clock, bool refresh) {
	if (ShownStepCatchesUp(fields.shownStep, fields.targetStep)) {  // B12
		fields.stepChangedMs = ports.ReadClockLastMs(clock);          // 0x005ADA6F-0x005ADA72
		fields.shownStep = fields.targetStep;                         // 0x005ADA75
	}
	float stepAsFloat = StepAsFloat(fields.shownStep);
	ports.SetTileFloat(tileMenu, kTraitUser3, stepAsFloat);
	if (!BelowCeiling(stepAsFloat, inputs.stepCeiling)) {  // B13
		return;
	}
	if (refresh) {  // B14
		ports.RefreshScreen(menu, tileMenu);
		fields.screenTimeLeft = ports.ScreenPeriodSetting();  // 0x005ADABD-0x005ADABF
	}
	if (IsNull(ports.InterfaceManager())) {  // B15
		return;
	}
	if (std::uint32_t io = ports.IoManager(); !IsNull(io)) {  // B16
		ports.IoManagerBefore(io);
	}
	ports.InterfaceManagerUpdate(ports.InterfaceManager());  // the second answer, not tested (0x005ADAED)
	ports.InterfacePass(kInterfacePassArgument);
	ports.InterfacePassEnd();
	if (std::uint32_t io = ports.IoManager(); !IsNull(io)) {  // B17
		ports.IoManagerAfter(io);
	}
}

template <class Ports>
Outcome RunTick(Ports& ports, const Inputs& inputs) {
	std::uint32_t tileMenu = ports.TileMenuForType(kLoadingMenuType);
	if (IsNull(tileMenu)) {  // B1
		return Outcome::NoTileMenu;
	}
	if (IsNull(ports.ContainingMenu(tileMenu))) {  // B2
		return Outcome::NoMenu;
	}
	std::uint32_t menu = ports.ContainingMenu(tileMenu);
	if (IsNull(menu)) {
		return Outcome::FaultNullMenuRead;
	}
	auto& fields = ports.Fields(menu);

	auto clockForUpdate = fields.clock;  // 0x005AD9B6, read before GetTickCount
	ports.UpdateClock(clockForUpdate, ports.TickCount());
	auto clock = fields.clock;  // 0x005AD9C7, read again
	if (IsNull(clock)) {
		return Outcome::FaultNullClockRead;
	}
	std::uint32_t now = ports.ReadClockLastMs(clock);  // EBP for the rest of the body
	std::int32_t sinceStep = ElapsedMs(now, fields.stepChangedMs);
	std::int32_t sinceLast = ElapsedMs(now, fields.lastTickMs);

	StepRule rule = ChooseStepRule(sinceStep, fields.targetStep, fields.slowStepCap, fields.fastStepEnabled,
		ports.StepThresholds());
	if (Advances(rule)) {  // B3..B6: written only on an advance
		fields.targetStep = NextStep(fields.targetStep);
	}
	fields.screenTimeLeft = CountDown(fields.screenTimeLeft, sinceLast, inputs.timer, inputs.precision);  // B7, 0x005ADA2E
	bool refresh = Refresh(fields.screenTimeLeft, inputs.forceRefresh);                // B8, B9

	if (GateRedraw(fields.targetStep, fields.shownStep, refresh) == RedrawGate::Proceed) {  // B10, B11
		RunRedraw(ports, inputs, fields, tileMenu, menu, clock, refresh);
	}

	fields.lastTickMs = now;  // T1, 0x005ADB12

	std::uint32_t osGlobals = ports.OsGlobals();
	if (IsNull(osGlobals)) {
		return Outcome::FaultNullOsGlobals;
	}
	std::uint32_t mainThreadId = ports.MainThreadId(osGlobals);
	if (!OnMainThread(ports.CurrentThreadId(), mainThreadId)) {  // B18
		return Outcome::OffMainThread;
	}
	ports.OsGlobalsTail(ports.OsGlobals());
	return Outcome::TailCalled;
}

} // namespace obng::engine::time::loading_menu_tick
