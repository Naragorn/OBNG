#pragma once

// The decisions of 0x005AD0D0 (proposed LoadingMenu::LoadingMenu), as pure
// functions over plain values, and RunConstructor, which replays the body's
// call order over injected ports so the engine binding and the tests' fakes
// share one sequence. Evidence: re/functions/0x005AD0D0.md and the export
// re/export/frame-clock/0x005AD0D0/asm.s. Names are proposals until the
// frame-clock slice's refutation round.
//
// The original has no guard on three paths; the rebuild does not invent one.
// Where the original reads through a null pointer inside this body, the logic
// stops at that instruction and reports it as an Outcome instead of reading:
//   - 0x005AD22B: the NOT FOUND line reads [0+4] (claim C9, read);
//   - 0x005AD32F: after a failed allocation the clock is 0 and [0+0x10] is
//     read (C8, read; the update 0x0047D170 is still called with ECX = 0 and
//     faults itself at 0x0047D176 in the game - that is the callee's, so the
//     port is called exactly as the original calls it).
// The third stop is a deliberate refusal, not a fault of the original: a
// setting line of 0x104 bytes or more is stored by _snprintf without a
// terminator, and the original goes on. Its inlined strlen (0x005AD246-
// 0x005AD259) scans the message buffer at ESP+0x13C, which is always
// terminated (read). The read past the line is __mbsnbcat's (0x005AD270): its
// source is the line at ESP+0x34 (derived: 0x005AD263 LEA EDX,[ESP+0x38] with
// the count pushed), so it reads on into the 4 stack bytes ESP+0x138..0x13B,
// which this body never writes (read: no operand addresses them), and then
// into the message itself, bounded by the count. What it appends depends on
// those 4 undefined bytes, which no input here carries, so the rebuild stops
// there (Outcome::UnterminatedWarningLine) instead of guessing. The original
// would continue: log the text, allocate, and write +0x54, +0x68, +0x6C and
// +0x40. With the binary's two 28-character names (meta.json strings
// 0x00A6C590, 0x00A6C5B0) a line is at most 1 + 28 + 3 + 45 + 1 = 78 bytes
// (derived, kMaxFixed4Length below), so the stop is unreachable unless a name
// changes at run time (unverified that the names are immutable).
//
// Inputs are read through ports at the instruction that reads them in the
// original, not snapshotted up front, because the callees in between were not
// all analysed (0x006A9B40 is open question 5) and may write a Setting or
// 0x00B14158 (unverified either way):
//   - the hook object after the base constructor (0x005AD145, after 0x005AD116);
//   - table entry i and its value at 0x005AD170/0x005AD18B, after the hook
//     (0x005AD158) and after the previous pass's 'Missing' log call;
//   - table entry i, value and name again in the warning loop at 0x005AD200-
//     0x005AD208, after all 'Missing' log calls;
//   - the float at 0x00B14158 at 0x005AD338, after 0x0047D170.
// The value is read twice per scan pass (0x005AD18B, 0x005AD194) with no call
// between, so one read stands for both (derived).

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace obng::engine::time::loading_menu_ctor {

inline constexpr int kSectionCount = 4;                     // loop bound 0x005AD1AA, table end 0x00B14130 (read)
inline constexpr std::size_t kMessageCapacity = 0x400;      // 0x005AD1E9 PUSH 0x400, 0x005AD25B (read)
inline constexpr std::size_t kLineCapacity = 0x104;         // 0x005AD21B PUSH 0x104 (read)
inline constexpr std::uint32_t kClockSize = 0x18;           // 0x005AD2E7 PUSH 0x18 (read)
inline constexpr std::uint32_t kVtable = 0x00A6C40C;        // 0x005AD11F (read; RTTI link unverified, C5)

// The text of the sum warning, as meta.json gives the format strings
// 0x00A6C368 (header), 0x00A6C358 ("\t%s - %.04f\n", one line per setting)
// and 0x00A6C330 ("INI Total = %.04f"). The line format is split into its
// literal parts here and these parts are the only source the code uses; the
// tests pin the resulting bytes literally, so a change here cannot pass
// silently. pseudo.c shows "\r\n" where meta.json shows "\n"; the bytes were
// not read - the line end is unverified.
inline constexpr char kSumHeader[] =
	"Total value of [LoadingBar] percentages is not equal to 1.0000f in ini file.\n";
inline constexpr char kLineIndent[] = "\t";         // "\t%s - %.04f\n" before %s
inline constexpr char kLineSeparator[] = " - ";     // between %s and %.04f
inline constexpr char kLineEnd[] = "\n";            // after %.04f
inline constexpr char kTotalPrefix[] = "INI Total = ";  // "INI Total = %.04f" before %.04f
inline constexpr int kValuePrecision = 4;           // %.04f in both formats

// --- Flow 1: the osGlobals hook (0x005AD156 JZ, read) -------------------------

// hookObject is [[g_osGlobals 0x00B33398]+0x24], loaded at 0x005AD14A.
constexpr bool ShouldCallOsGlobalsHook(std::uint32_t hookObject) {
	return hookObject != 0;
}

// --- x87 handling of the float values -------------------------------------------

// The values on this path are carried as bit patterns, not as float or double.
// On the 32-bit build a float or double return value travels through x87
// ST0, and that load alone quiets a signaling NaN (measured on the build
// machine: a function returning the float 0x7F800001 hands back 0x7FC00001),
// so a float-typed return could not show whether these functions quiet it.

inline constexpr std::uint32_t kFloatSignBit = 0x80000000u;
inline constexpr std::uint32_t kFloatExponentMask = 0x7F800000u;
inline constexpr std::uint32_t kFloatFractionMask = 0x007FFFFFu;
inline constexpr std::uint32_t kFloatQuietBit = 0x00400000u;
inline constexpr std::uint32_t kX87IndefiniteFloat = 0xFFC00000u;

constexpr bool IsNaNBits(std::uint32_t bits) {
	return (bits & kFloatExponentMask) == kFloatExponentMask && (bits & kFloatFractionMask) != 0;
}

constexpr std::uint32_t QuietIfNaN(std::uint32_t bits) {
	return IsNaNBits(bits) ? bits | kFloatQuietBit : bits;
}

// FLD float then FSTP float: the copy to 0x00B3B3DC[i] (0x005AD18B-0x005AD18D)
// and to +0x40 from 0x00B14158 (0x005AD338-0x005AD33E) (read). The load
// quiets a signaling NaN; every other value passes unchanged. Measured on the
// build machine's CPU with masked exceptions (control word 0x037F): 0x7F800001
// is stored as 0x7FC00001, 0xFF800003 as 0xFFC00003. That the game runs with
// the invalid-operation exception masked is unverified.
constexpr std::uint32_t X87FloatThroughStack(std::uint32_t bits) {
	return QuietIfNaN(bits);
}

// FLD float [value]; FADD float [sum]; FSTP float [sum] (0x005AD194-0x005AD19A,
// read). NaN results follow the x87 rule, measured on the build machine's CPU:
// one NaN operand gives that NaN quieted; two give the quieted one with the
// larger fraction, and with equal fractions the positive one; inf + -inf gives
// the indefinite 0xFFC00000. The host's own float add picks the first operand
// on SSE instead, so NaNs never go through it here. For ordered operands the
// sum is rounded to float after every add; whether an x87 intermediate wider
// than float could change that result is not checked (unverified), this
// rebuild adds in float.
constexpr std::uint32_t X87AddFloat(std::uint32_t value, std::uint32_t sum) {
	bool valueIsNaN = IsNaNBits(value);
	bool sumIsNaN = IsNaNBits(sum);
	if (valueIsNaN || sumIsNaN) {
		if (!sumIsNaN) {
			return QuietIfNaN(value);
		}
		if (!valueIsNaN) {
			return QuietIfNaN(sum);
		}
		std::uint32_t quietValue = QuietIfNaN(value);
		std::uint32_t quietSum = QuietIfNaN(sum);
		std::uint32_t valueFraction = quietValue & kFloatFractionMask;
		std::uint32_t sumFraction = quietSum & kFloatFractionMask;
		if (valueFraction != sumFraction) {
			return valueFraction > sumFraction ? quietValue : quietSum;
		}
		return (quietValue & kFloatSignBit) == 0 ? quietValue : quietSum;
	}
	std::uint32_t result = std::bit_cast<std::uint32_t>(std::bit_cast<float>(sum) + std::bit_cast<float>(value));
	return IsNaNBits(result) ? kX87IndefiniteFloat : result;
}

// FLD float then FSTP double: the %.04f arguments (0x005AD206-0x005AD20E,
// 0x005AD287-0x005AD28E, read). A NaN keeps sign and fraction and is quieted,
// measured: 0x7F800001 becomes 0x7FF8000020000000, 0xFFC00000 becomes
// 0xFFF8000000000000. NaNs are built from the bits so no host conversion
// decides them.
constexpr std::uint64_t X87FloatToDouble(std::uint32_t bits) {
	if (!IsNaNBits(bits)) {
		return std::bit_cast<std::uint64_t>(static_cast<double>(std::bit_cast<float>(bits)));
	}
	std::uint32_t quiet = QuietIfNaN(bits);
	return (static_cast<std::uint64_t>(quiet & kFloatSignBit) << 32) | 0x7FF0000000000000ull |
		   (static_cast<std::uint64_t>(quiet & kFloatFractionMask) << 29);
}

// --- Flows 2 and 3: the settings loop ------------------------------------------

// A Setting seen through the table 0x00B14120: +0x00 float value (as its
// bits), +0x04 name.
struct SettingView {
	std::uint32_t valueBits;
	std::string_view name;
};
// std::nullopt is a zero table entry.
using SettingSlot = std::optional<SettingView>;

struct SectionStep {
	std::optional<std::uint32_t> copyBits;  // 0x00B3B3DC[i]; empty when the entry is missing
	std::uint32_t sumBits;                  // [ESP+0x2C] after this pass
};

// Present (0x005AD179 JNZ): copy through the x87 stack, then add to the float
// sum (read, 0x005AD18B-0x005AD19A). Missing: no copy, sum unchanged.
constexpr SectionStep ScanSection(const SettingSlot& slot, std::uint32_t sumBits) {
	if (!slot.has_value()) {
		return SectionStep{std::nullopt, sumBits};
	}
	return SectionStep{X87FloatThroughStack(slot->valueBits), X87AddFloat(slot->valueBits, sumBits)};
}

// --- Flows 4 and 5: the sum check -----------------------------------------------

// The doubles at 0x00A2F928 (subtracted when the sum is above 1.0) and
// 0x00A3B150 (the tolerance). Their values are not in the export (unverified).
struct SumCheckConstants {
	double reference;
	double tolerance;
};

enum class SumSide : std::uint8_t {
	AtOrBelowOne,  // C0 or C3 set by FCOM: below, equal, or unordered (NaN)
	AboveOne,
};

// 0x005AD1B5 FCOM sum vs 1.0, 0x005AD1BB TEST AH,0x41, JNZ (read).
constexpr SumSide SideOf(float sum) {
	return sum > 1.0f ? SumSide::AboveOne : SumSide::AtOrBelowOne;
}

// At or below: FLD1; FSUBRP gives 1.0 - sum (0x005AD1C8). Above: FSUB
// [0x00A2F928] gives sum - reference (0x005AD1C0) (read). The original keeps
// the difference on the x87 stack; its precision control at this point is
// unverified. For a sum in [0.5, 2] and reference 1.0 the subtraction is exact
// in any precision (derived: Sterbenz), so the comparison below does not
// depend on it there.
constexpr double SumDeviation(float sum, const SumCheckConstants& constants) {
	if (SideOf(sum) == SumSide::AtOrBelowOne) {
		return 1.0 - static_cast<double>(sum);
	}
	return static_cast<double>(sum) - constants.reference;
}

// 0x005AD1CC FCOMP [0x00A3B150]; TEST AH,5; JNP skips the warning only when
// C0 alone is set: deviation < tolerance and ordered (read). Equal or NaN warn.
constexpr bool WithinTolerance(double deviation, double tolerance) {
	return deviation < tolerance;
}

constexpr bool NeedsSumWarning(float sum, const SumCheckConstants& constants) {
	return !WithinTolerance(SumDeviation(sum, constants), constants.tolerance);
}

// --- %.04f as the legacy MSVC CRT formats it ----------------------------------------

// __snprintf 0x009833BB is linked into the binary. Its CRT predates Visual
// Studio 2019 16.2 (derived from the binary's age; the exact CRT version is
// unverified), so it formats %f the legacy way, which differs from glibc and
// from today's UCRT (Microsoft Learn, printf_p / Link options,
// legacy_stdio_float_rounding.obj: exactly representable values ending in '5'
// "would always round up"). The steps below follow the Visual Studio 2008 CRT
// sources (github.com/cansou/msvcrt, src/_fptostr.c and src/fcvt.c, read) and
// the Windows-native expectations in Wine's dlls/msvcrt/tests/printf.c (read):
//   1. _fltout2 turns the double into a sign, a decimal point position and a
//      digit string of at most 17 significant digits (Wine native test: _fcvt
//      of 123456789012345678901.0 gives "12345678901234568" then zeros). How
//      $I10_OUTPUT rounds the 17th digit is unverified; this rounds the exact
//      expansion half up there.
//      Special values are the digit strings "1#INF", "1#QNAN", "1#SNAN" and
//      "1#IND" with the decimal point after the "1" (cfout.c assert strings,
//      UCRT successor __acrt_fltout, read). IND is the x86 indefinite: sign set,
//      quiet bit only (Wine native test: the float 0xFFC00000 prints
//      "-1.#IND00"; NAN prints "1.#QNAN0"). That a negative NaN with a nonzero
//      payload prints QNAN, not IND, is derived from Raymond Chen's definition
//      of the indefinite (The Old New Thing, 2013-02-21) - unverified.
//   2. _fptostr copies decimal point + precision characters, pads with '0',
//      and rounds up when the next character is '5' or more, carrying through
//      '9's - over the letters of a special value too (_fptostr.c, read; Wine
//      native tests: "%.2f" of INFINITY is "1.#J", of NAN "1.#R").
//   3. The fixed layout: sign, the integer digits or "0", then the point and
//      the fraction with leading zeros for a negative decimal point (UCRT
//      successor fp_format_f_internal, read; the VS2008 cvt.c was not found -
//      the legacy layout is derived from the Wine native vectors it matches).
// The decimal point character is '.', the "C" locale's; that the game never
// changes LC_NUMERIC is unverified.

struct LegacyDecimal {
	bool negative;
	int decimalPoint;      // digits before the point; may be zero or negative
	std::string digits;    // significant digits, or a special value's "1#..." text
};

inline constexpr std::size_t kLegacySignificantDigits = 17;

// The exact decimal digits of significand * 2^exponent (significand > 0), and
// where the decimal point falls relative to them.
inline LegacyDecimal ExactDecimal(std::uint64_t significand, int exponent) {
	constexpr std::uint32_t kLimbBase = 1000000000u;
	std::vector<std::uint32_t> limbs;  // little-endian, base 10^9
	while (significand != 0) {
		limbs.push_back(static_cast<std::uint32_t>(significand % kLimbBase));
		significand /= kLimbBase;
	}
	auto multiply = [&limbs](std::uint32_t factor) {
		std::uint64_t carry = 0;
		for (std::uint32_t& limb : limbs) {
			std::uint64_t product = static_cast<std::uint64_t>(limb) * factor + carry;
			limb = static_cast<std::uint32_t>(product % kLimbBase);
			carry = product / kLimbBase;
		}
		if (carry != 0) {
			limbs.push_back(static_cast<std::uint32_t>(carry));
		}
	};
	// value = significand * 2^e, or significand * 5^-e / 10^-e for e < 0.
	int fractionDigits = exponent < 0 ? -exponent : 0;
	for (int i = 0; i < (exponent < 0 ? -exponent : exponent); ++i) {
		multiply(exponent < 0 ? 5u : 2u);
	}
	std::string digits = std::to_string(limbs.back());
	for (std::size_t i = limbs.size() - 1; i-- > 0;) {
		std::string limb = std::to_string(limbs[i]);
		digits += std::string(9 - limb.size(), '0') + limb;
	}
	return LegacyDecimal{false, static_cast<int>(digits.size()) - fractionDigits, digits};
}

// Step 1 above.
inline LegacyDecimal LegacyFltout(std::uint64_t bits) {
	bool negative = (bits >> 63) != 0;
	int biasedExponent = static_cast<int>((bits >> 52) & 0x7FF);
	std::uint64_t fraction = bits & 0x000FFFFFFFFFFFFFull;
	constexpr std::uint64_t kDoubleQuietBit = 0x0008000000000000ull;

	if (biasedExponent == 0x7FF) {
		const char* text = "1#SNAN";
		if (fraction == 0) {
			text = "1#INF";
		} else if (negative && fraction == kDoubleQuietBit) {
			text = "1#IND";
		} else if ((fraction & kDoubleQuietBit) != 0) {
			text = "1#QNAN";
		}
		return LegacyDecimal{negative, 1, text};
	}
	if (biasedExponent == 0 && fraction == 0) {
		return LegacyDecimal{negative, 0, "0"};
	}

	std::uint64_t significand = biasedExponent == 0 ? fraction : fraction | 0x0010000000000000ull;
	int exponent = (biasedExponent == 0 ? 1 : biasedExponent) - 1075;
	LegacyDecimal decimal = ExactDecimal(significand, exponent);
	decimal.negative = negative;
	if (decimal.digits.size() > kLegacySignificantDigits) {
		bool roundUp = decimal.digits[kLegacySignificantDigits] >= '5';
		decimal.digits.resize(kLegacySignificantDigits);
		if (roundUp) {
			std::size_t i = decimal.digits.size();
			while (i > 0 && decimal.digits[i - 1] == '9') {
				decimal.digits[--i] = '0';
			}
			if (i == 0) {
				decimal.digits.insert(decimal.digits.begin(), '1');
				decimal.digits.pop_back();
				++decimal.decimalPoint;
			} else {
				++decimal.digits[i - 1];
			}
		}
	}
	return decimal;
}

// Step 2 above (_fptostr): the first `count` characters, rounded on the next
// one; a carry into a new leading digit moves the decimal point.
inline std::string LegacyRoundDigits(LegacyDecimal& decimal, int count) {
	std::string buffer = "0";  // the overflow digit
	std::size_t next = 0;
	for (int i = 0; i < count; ++i) {
		buffer += next < decimal.digits.size() ? decimal.digits[next++] : '0';
	}
	if (count >= 0 && next < decimal.digits.size() && decimal.digits[next] >= '5') {
		std::size_t i = buffer.size() - 1;
		while (buffer[i] == '9') {
			buffer[i--] = '0';
		}
		++buffer[i];
	}
	if (buffer.front() == '1') {
		++decimal.decimalPoint;
	} else {
		buffer.erase(buffer.begin());
	}
	return buffer;
}

// "%.<precision>f" of the double with these bits, steps 1 to 3 above.
inline std::string LegacyFixed(std::uint64_t doubleBits, int precision) {
	LegacyDecimal decimal = LegacyFltout(doubleBits);
	std::string digits = LegacyRoundDigits(decimal, decimal.decimalPoint + precision);
	std::string text = decimal.negative ? "-" : "";
	std::size_t integerDigits = 0;
	if (decimal.decimalPoint <= 0) {
		text += '0';
	} else {
		integerDigits = static_cast<std::size_t>(decimal.decimalPoint);
		text += digits.substr(0, integerDigits);
	}
	if (precision > 0) {
		text += '.';
		if (decimal.decimalPoint < 0) {
			text += std::string(static_cast<std::size_t>(std::min(-decimal.decimalPoint, precision)), '0');
		}
		text += digits.substr(std::min(integerDigits, digits.size()));
	}
	return text;
}

// The longest %.04f text of a float promoted to double: a sign, the 39
// integer digits of FLT_MAX (3.4e38), the point and four digits (derived);
// the special values are shorter ("-1.#QNB").
inline constexpr std::size_t kMaxFixed4Length = 1 + 39 + 1 + kValuePrecision;

// --- Flow 6: one warning line per setting ---------------------------------------

// What MSVC's _snprintf stores for a formatted text of a given length: the
// text up to count bytes, and a terminator only when the length is strictly
// below count (Microsoft Learn, snprintf/_snprintf "Behavior summary").
struct SnprintfStore {
	std::string stored;
	bool terminated;
};

inline SnprintfStore MsvcSnprintfStore(std::string formatted, std::size_t count) {
	bool terminated = formatted.size() < count;
	if (formatted.size() > count) {
		formatted.resize(count);
	}
	return SnprintfStore{std::move(formatted), terminated};
}

struct NullNameRead {};      // 0x005AD22B MOV EAX,[EAX+4] with EAX = 0 (C9)
struct UnterminatedLine {};  // the line filled its 0x104 bytes (see the header comment)
using LineResult = std::variant<std::string, NullNameRead, UnterminatedLine>;

// 0x005AD204 JZ (read): present formats name (+0x04) and value (+0x00, pushed
// as double, 0x005AD20E); zero takes the NOT FOUND path, which faults first.
inline LineResult FormatSettingLine(const SettingSlot& slot) {
	if (!slot.has_value()) {
		return NullNameRead{};
	}
	std::string text = std::string(kLineIndent) + std::string(slot->name) + kLineSeparator +
					   LegacyFixed(X87FloatToDouble(slot->valueBits), kValuePrecision) + kLineEnd;
	SnprintfStore line = MsvcSnprintfStore(std::move(text), kLineCapacity);
	if (!line.terminated) {
		return UnterminatedLine{};
	}
	return std::move(line.stored);
}

// The total line always fits its 0x104 bytes with the terminator (derived), so
// it needs no refusal path.
static_assert(sizeof(kTotalPrefix) - 1 + kMaxFixed4Length < kLineCapacity,
	"\"INI Total = %.04f\" of a float cannot fill the 0x104-byte line");

inline std::string FormatTotalLine(std::uint32_t sumBits) {
	return kTotalPrefix + LegacyFixed(X87FloatToDouble(sumBits), kValuePrecision);
}

// --- Flow 7: the inlined strlen and _mbsnbcat ------------------------------------

// 0x005AD250-0x005AD259 and 0x005AD2B2-0x005AD2BB (read): count bytes up to
// the first zero; a view without one counts to its end.
constexpr std::size_t StrLen(std::string_view buffer) {
	std::size_t length = buffer.find('\0');
	return length == std::string_view::npos ? buffer.size() : length;
}

// 0x005AD25B-0x005AD260 (read): 0x400 - strlen(message).
constexpr std::size_t AppendCount(std::size_t messageLength) {
	return kMessageCapacity - messageLength;
}

// _mbsnbcat(message, line, count): returns at once for count 0, else appends
// at most count bytes of line and then a terminator (VS2008 CRT mbsnbcat.c,
// read: `if (!cnt) return dst`, then strncat for a single-byte code page; the
// appended bytes here are ASCII, so the lead-byte path changes nothing,
// derived). So the message never grows past 0x400 bytes. When a line fills it,
// the terminator lands one past the buffer at ESP+0x53C (C11, derived), in 4
// bytes below the GS cookie at ESP+0x540 that no instruction of this body
// addresses (read: the body's ESP-relative operands). The logged text is then
// exactly the 0x400 bytes, which is what this keeps; the stray byte has no
// counterpart here because nothing reads it.
inline void MbsnbcatAppend(std::string& message, std::string_view line) {
	std::size_t count = AppendCount(StrLen(message));
	message.append(line.substr(0, std::min(StrLen(line), count)));
}

// --- Flow 8: the warning loop ----------------------------------------------------

enum class WarningFaultKind : std::uint8_t {
	NullSettingName,   // 0x005AD22B
	UnterminatedLine,  // a setting line of 0x104 bytes or more
};

struct WarningFault {
	WarningFaultKind kind;
	int index;  // the table entry
};

using WarningResult = std::variant<std::string, WarningFault>;

// 0x005AD1DD-0x005AD2D2 (read): header, one line per table entry while the
// cursor is below 0x00B14130, then "INI Total = %.04f" of the float sum.
// readSlot(i) reads table entry i, its value and name at 0x005AD200-0x005AD208,
// once per pass in loop order.
template <class ReadSlot>
WarningResult BuildSumWarning(ReadSlot&& readSlot, std::uint32_t sumBits) {
	std::string message = MsvcSnprintfStore(kSumHeader, kMessageCapacity).stored;
	for (int i = 0; i < kSectionCount; ++i) {
		LineResult line = FormatSettingLine(readSlot(i));
		if (std::holds_alternative<NullNameRead>(line)) {
			return WarningFault{WarningFaultKind::NullSettingName, i};
		}
		if (std::holds_alternative<UnterminatedLine>(line)) {
			return WarningFault{WarningFaultKind::UnterminatedLine, i};
		}
		MbsnbcatAppend(message, std::get<std::string>(line));
	}
	MbsnbcatAppend(message, FormatTotalLine(sumBits));
	return message;
}

// --- Flow 9 and the clock prime ----------------------------------------------------

// 0x005AD307 JZ (read): only a nonzero block gets the constructor 0x0047D150.
constexpr bool ConstructsClock(std::uint32_t block) {
	return block != 0;
}

// 0x005AD32C-0x005AD32F read [this+0x54] and then [clock+0x10] (read).
constexpr bool ClockReadFaults(std::uint32_t clock) {
	return clock == 0;
}

// --- The run ------------------------------------------------------------------------

// The fields this constructor writes, as plain values (offsets in the engine
// header's LoadingMenuLayout).
struct MenuFields {
	std::uint32_t vtable = 0;                  // +0x00
	std::int32_t unknown28 = 0;                // +0x28
	std::array<float, kSectionCount> sectionValues{};  // +0x2C..+0x38
	std::int32_t stepLimit = 0;                // +0x3C
	std::uint32_t countdownBits = 0;           // +0x40, the float's bits
	std::uint32_t unknown4C = 0;               // +0x4C
	std::uint32_t unknown50 = 0;               // +0x50
	std::uint32_t clock = 0;                   // +0x54
	std::int32_t shownPercent = 0;             // +0x60
	std::int32_t step = 0;                     // +0x64
	std::uint32_t lastStepMs = 0;              // +0x68
	std::uint32_t startMs = 0;                 // +0x6C
	bool flag70 = false;                       // +0x70
	bool flag71 = false;                       // +0x71
};

enum class Outcome : std::uint8_t {
	Completed,                // returns this (0x005AD341)
	FaultNullSettingName,     // the original faults at 0x005AD22B
	UnterminatedWarningLine,  // refusal: __mbsnbcat would read 4 undefined stack bytes
	FaultNullClockRead,       // the original faults at 0x005AD32F
};

// The body's sequence over ports:
//   ConstructBase()                       0x00584640, call 0x005AD116
//   ReadOsGlobalsHookObject() -> object   [[0x00B33398]+0x24], 0x005AD145-0x005AD14A
//   CallOsGlobalsHook(object)             0x006A9B40, call 0x005AD158
//   ReadSetting(index) -> slot            *0x00B14120[index]: 0x005AD170/0x005AD18B, again 0x005AD200-0x005AD208
//   LogMissingSection(index)              0x004A7A60(0x00A6C3B8, index), 0x005AD181
//   StoreSectionPercentage(index, bits)   0x00B3B3DC[index] = the float with these bits, 0x005AD18D
//   LogText(text)                         0x004A7A60(buffer), 0x005AD2DF
//   Allocate(size) -> block               FormHeap_Allocate 0x00401F00, 0x005AD2E9
//   TickCount() -> tick                   GetTickCount via 0x00A280D0
//   ConstructClock(block, tick)           0x0047D150, call 0x005AD30E
//   UpdateClock(clock, tick)              0x0047D170, call 0x005AD327
//   ReadClockLastMs(clock) -> ms          [clock+0x10], 0x005AD32F
//   ReadInitialCountdown() -> bits        the float at 0x00B14158, as bits, 0x005AD338
//
// Exceptions are not modelled. The original registers the EH handler
// 0x009C05A9 (0x005AD0D8), sets state 0 after the base constructor
// (0x005AD14F) and state 1 around 0x0047D150 (0x005AD2FF to 0x005AD317)
// (read). If a callee throws, the original unwinds through that handler, which
// presumably frees the 0x18-byte block in state 1 and destroys the base in
// both states; the handler was not read, so that is unverified, and whether
// any callee here can throw is unverified too. A port that throws leaves
// RunConstructor without any cleanup; the rebuild has no counterpart to the
// handler until it is read.
template <class Ports>
Outcome RunConstructor(Ports& ports, const SumCheckConstants& constants, MenuFields& fields) {
	ports.ConstructBase();
	fields.vtable = kVtable;    // 0x005AD11F
	fields.unknown4C = 0;       // 0x005AD125
	fields.unknown50 = 0;       // 0x005AD128
	fields.countdownBits = 0;   // 0x005AD12B FLDZ; FSTP: +0.0f
	fields.stepLimit = -1;      // 0x005AD131
	fields.unknown28 = -1;      // 0x005AD134
	fields.shownPercent = 0;    // 0x005AD137
	fields.step = 0;            // 0x005AD13A
	fields.flag70 = false;      // 0x005AD13D
	fields.flag71 = true;       // 0x005AD141

	std::uint32_t hookObject = ports.ReadOsGlobalsHookObject();
	if (ShouldCallOsGlobalsHook(hookObject)) {
		ports.CallOsGlobalsHook(hookObject);
	}

	std::uint32_t sumBits = 0;  // 0x005AD15D FLDZ, 0x005AD162 FSTP [ESP+0x2C]: +0.0f
	for (int i = 0; i < kSectionCount; ++i) {
		SectionStep step = ScanSection(ports.ReadSetting(i), sumBits);
		if (step.copyBits.has_value()) {
			ports.StoreSectionPercentage(i, *step.copyBits);
		} else {
			ports.LogMissingSection(i);
		}
		sumBits = step.sumBits;
		fields.sectionValues[static_cast<std::size_t>(i)] = 0.0f;  // 0x005AD19E
	}

	if (NeedsSumWarning(std::bit_cast<float>(sumBits), constants)) {
		WarningResult warning = BuildSumWarning([&ports](int index) { return ports.ReadSetting(index); }, sumBits);
		if (const WarningFault* fault = std::get_if<WarningFault>(&warning)) {
			return fault->kind == WarningFaultKind::NullSettingName ? Outcome::FaultNullSettingName
																	: Outcome::UnterminatedWarningLine;
		}
		ports.LogText(std::get<std::string>(warning));
	}

	std::uint32_t block = ports.Allocate(kClockSize);
	if (ConstructsClock(block)) {
		ports.ConstructClock(block, ports.TickCount());
	}
	fields.clock = block;  // 0x005AD31F
	ports.UpdateClock(fields.clock, ports.TickCount());
	if (ClockReadFaults(fields.clock)) {
		return Outcome::FaultNullClockRead;
	}
	std::uint32_t lastMs = ports.ReadClockLastMs(fields.clock);
	fields.lastStepMs = lastMs;                                             // 0x005AD332
	fields.startMs = lastMs;                                                // 0x005AD335
	fields.countdownBits = X87FloatThroughStack(ports.ReadInitialCountdown());  // 0x005AD338-0x005AD33E
	return Outcome::Completed;
}

} // namespace obng::engine::time::loading_menu_ctor
