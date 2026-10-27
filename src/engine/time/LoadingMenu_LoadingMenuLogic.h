#pragma once

// The decisions of 0x005AD0D0 (proposed LoadingMenu::LoadingMenu), as pure
// functions over plain values, and RunConstructor, which replays the body's
// call and access order over injected ports so an engine binding and the
// tests' fakes share one sequence. Evidence: re/functions/0x005AD0D0.md and
// the export re/export/loading-menu/0x005AD0D0/ (asm.s, data.json, meta.json).
// Names are proposals until the loading-menu slice's refutation round; the
// address stays next to each.
//
// Where pseudo.c and the instructions disagree, the instructions are
// followed (AGENTS.md); each is read in asm.s:
//   - pseudo.c tests the sum with `(float)_DAT_00a3b150 <= fVar4`, false for
//     NaN; the instructions warn on NaN (FCOMP, TEST AH,5, JNP at
//     0x005AD1CC-0x005AD1D7: unordered sets C0 C2 C3, AH&5 = 5, even parity,
//     no jump). WithinTolerance follows the instructions.
//   - pseudo.c drops the index argument of the Missing warning (PUSH EDI at
//     0x005AD17B), the name argument of the NOT FOUND line (MOV EAX,[EAX+4]
//     at 0x005AD22B, which hides the null read of claim C7), ECX of
//     0x006A9B40, the 0x18 of FormHeap_Allocate and the ticks and ECX of
//     Init/Update. The ports take every one of them.
//   - pseudo.c passes the cookie copy to 0x00584640 and declares the function
//     void __fastcall; the instructions pass ECX only (0x005AD116) and return
//     this in EAX (0x005AD341).
//   - pseudo.c stores `(float)iVar8` into the sum's slot; 0x005AD2F3 is a
//     dword store of the allocated pointer into the reused slot.
//
// The original has no guard on three reads; the rebuild does not invent one.
// Where the original reads through a null pointer inside this body,
// RunConstructor stops at that instruction and reports it as an Outcome
// instead of reading:
//   - 0x005AD14A: [g_osGlobals 0x00B33398] null, read [0+0x24];
//   - 0x005AD22B: a null table entry reached by the warning loop, read [0+4]
//     (C7) - so the NOT FOUND line (0x00A6C344) is never formatted by the
//     rebuild; whether address 4 faults in the game is unverified;
//   - 0x005AD32F: [this+0x54] null after the prime, read [0+0x10] (C14). The
//     update 0x0047D170 before it is still called with ECX = 0, exactly as
//     the original calls it; in the game that call reads [0+0x14] first
//     (re/functions/0x0047D170.md) - that is the callee's, not this body's.
//
// Not modelled (compiler artefacts with no effect on the result): the /GS
// cookie (0x005AD0EA-0x005AD0F1, check at 0x005AD35E), the C++ EH frame and
// its state byte (0x005AD0D6-0x005AD10A, 0x005AD14F, 0x005AD2FF, 0x005AD317),
// the 64-byte stack alignment (0x005AD0D3). The original's two stack buffers
// are uninitialised; every read of them follows a format into them, so the
// rebuild zero-initialises its own (derived: a difference could only show if
// _snprintf truncated without terminating, which needs a line or message
// longer than its buffer - with the strings here a setting name over ~0xF0
// bytes; unverified for the game's names, not reachable with the image ones).
//
// Floating point. Every float operation of the body is modelled on the bit
// pattern, not with C++ float arithmetic, so the result does not depend on
// the host's SSE/MXCSR state:
//   - LoadStore: FLD m32 / FSTP m32 (0x005AD18B/0x005AD18D, 0x005AD338/
//     0x005AD33E) is a bit copy, except that FLD quiets a signaling NaN
//     (sets fraction bit 22) with the invalid exception masked.
//   - SumStep: FLD m32, FADD m32, FSTP m32 (0x005AD194-0x005AD19A). The
//     result is the IEEE single sum with round to nearest, and it does not
//     depend on the x87 precision control (derived): at PC=24 the FADD
//     rounds to a 24-bit significand with the extended exponent range and
//     FSTP m32 then changes nothing for a normal result; at PC=53/64 the
//     second rounding is innocuous because 53 >= 2*24+2 (double rounding of
//     a sum is harmless when the wider format has at least 2p+2 bits); a
//     result in the float denormal range is exact at any precision, as both
//     operands are multiples of 2^-149 and the sum is below 2^-126. Checked
//     against the host x87 at 24, 53 and 64 bits
//     (tests/LoadingMenu_LoadingMenuTest.cpp, measured on the test host).
//   - NaN choice (Intel SDM Vol. 1 Table 4-7 as the tests measure it on the
//     host x87): the loaded operand is already quiet; of two NaNs of the same
//     kind the one with the larger significand wins, on equal significands
//     the positive one; a QNaN wins over an SNaN (the same rule, as the quiet
//     bit is the fraction's top bit); inf + -inf gives the real indefinite
//     0xFFC00000.
//   - DifferenceFromOne / WithinTolerance (B4/B5, 0x005AD1AF-0x005AD1D7):
//     computed in double, the 53-bit reading. At PC=24 the x87 rounds the
//     difference to 24 bits; the decision is the same at every precision
//     (derived, claim C8): for a sum in [0.5, 2] the difference from 1.0 is
//     exact (Sterbenz), outside it the difference is at least 0.5, far above
//     the tolerance (double)0.01f, at any rounding. The tests check the
//     decision against the host x87 running the original's bytes at 24, 53
//     and 64 bits. The double operations here also cannot be changed by
//     MXCSR flush-to-zero or denormals-are-zero in a way that changes the
//     decision (derived: a denormal sum is below 0.5 and warns either way).
//   - WidenToDouble: FLD m32 / FSTP m64 (0x005AD206/0x005AD20E,
//     0x005AD287/0x005AD28E), exact, with the sNaN quieted.

#include <array>
#include <bit>
#include <cstdint>
#include <type_traits>

namespace obng::engine::time::loading_menu_ctor {

// --- Immediate operands of the body (read): values the rebuild must reproduce ----------
//
// These are operands of the instructions, the values stored or passed - not
// declarations of what lives at those addresses (AGENTS.md's two-source rule
// governs the engine header's addresses, LoadingMenu_LoadingMenu.h).

inline constexpr std::uint32_t kVtable = 0x00A6C40C;             // MOV [ESI],0xA6C40C at 0x005AD11F
inline constexpr std::int32_t kSectionCount = 4;                 // CMP EDI,4 at 0x005AD1AA
inline constexpr std::uint32_t kSectionTableAddress = 0x00B14120;  // MOV EDI,0xB14120 at 0x005AD1F7
inline constexpr std::uint32_t kSectionTableEnd = 0x00B14130;    // CMP EDI,0xB14130 at 0x005AD27B
inline constexpr std::uint32_t kSlotBytes = 4;                   // ADD EDI,4 at 0x005AD275
inline constexpr std::uint32_t kMessageBytes = 0x400;            // PUSH 0x400 at 0x005AD1E9, MOV ECX,0x400 at 0x005AD25B
inline constexpr std::uint32_t kLineBytes = 0x104;               // PUSH 0x104 at 0x005AD21B, 0x005AD238, 0x005AD29A
inline constexpr std::uint32_t kClockBytes = 0x18;               // PUSH 0x18 at 0x005AD2E7 (sizeof TimeInfo, TimeInfo.h)

// data.json 0x00A2F928 (f64 1.0) and 0x00A3B150 (bytes 00 00 00 40 e1 7a 84 3f,
// = (double)0.01f = 0.009999999776482582), read.
inline constexpr double kOne = std::bit_cast<double>(std::uint64_t{0x3FF0000000000000});
inline constexpr double kTolerance = std::bit_cast<double>(std::uint64_t{0x3F847AE140000000});

// A format string of the image: its address (what the original pushes) and
// its exact text (meta.json `strings`, data.json bytes and `defined.length`;
// CR LF kept). An engine binding passes the address; the text is the record
// and what a test fake formats with.
struct FormatString {
	std::uint32_t address;
	const char* text;
	std::uint32_t bytes;  // with the NUL
};

// PUSH 0xA6C3B8 at 0x005AD17C; 78 bytes, ends in '.' NUL, no line end (C5).
inline constexpr FormatString kMissingFormat{
	0x00A6C3B8, "Missing [LoadingBar] 'fPercentageOfBar%d' in LoadingMenu::pSectionPercentage.", 78};
// PUSH 0xA6C368 at 0x005AD1DD; 79 bytes, ends in CR LF (C6).
inline constexpr FormatString kHeaderFormat{
	0x00A6C368, "Total value of [LoadingBar] percentages is not equal to 1.0000f in ini file.\r\n", 79};
// PUSH 0xA6C358 at 0x005AD212; 14 bytes, ends in CR LF.
inline constexpr FormatString kFoundLineFormat{0x00A6C358, "\t%s - %.04f\r\n", 14};
// PUSH 0xA6C344 at 0x005AD22F; 18 bytes, ends in CR LF. Reached only through
// the null read of C7, so the rebuild never formats it (see the header).
inline constexpr FormatString kNotFoundLineFormat{0x00A6C344, "\t%s - NOT FOUND\r\n", 18};
// PUSH 0xA6C330 at 0x005AD291; 18 bytes, no line end.
inline constexpr FormatString kTotalFormat{0x00A6C330, "INI Total = %.04f", 18};

// Zero test for an address held as an integer or as a pointer
// (TEST/CMP reg,reg; JZ - 0x005AD14D, 0x005AD177, 0x005AD202, 0x005AD2F7).
template <class Address>
constexpr bool IsNull(Address address) {
	return address == Address{};
}

// --- The float model ------------------------------------------------------------------

inline constexpr std::uint32_t kSignBit = 0x80000000;
inline constexpr std::uint32_t kExponentMask = 0x7F800000;
inline constexpr std::uint32_t kFractionMask = 0x007FFFFF;
inline constexpr std::uint32_t kQuietBit = 0x00400000;
inline constexpr std::uint32_t kRealIndefinite = 0xFFC00000;  // x87 default NaN: negative, quiet, fraction 0

constexpr bool IsNaN(std::uint32_t bits) {
	return (bits & kExponentMask) == kExponentMask && (bits & kFractionMask) != 0;
}

constexpr bool IsSignaling(std::uint32_t bits) {
	return IsNaN(bits) && (bits & kQuietBit) == 0;
}

constexpr bool IsInfinity(std::uint32_t bits) {
	return (bits & ~kSignBit) == kExponentMask;
}

constexpr std::uint32_t Quiet(std::uint32_t bits) {
	return IsSignaling(bits) ? bits | kQuietBit : bits;
}

// FLD m32 then FSTP m32: a bit copy, the sNaN quieted (0x005AD18B/0x005AD18D,
// 0x005AD338/0x005AD33E).
constexpr std::uint32_t LoadStore(std::uint32_t bits) {
	return Quiet(bits);
}

// The NaN an x87 FADD returns for ST0 = first and the memory operand second,
// at least one of them a NaN.
constexpr std::uint32_t ChooseNaN(std::uint32_t first, std::uint32_t second) {
	if (!IsNaN(first)) {
		return Quiet(second);
	}
	if (!IsNaN(second)) {
		return Quiet(first);
	}
	// The larger significand wins. A QNaN against an SNaN needs no rule of its
	// own: the quiet bit is the fraction's top bit, so the QNaN's fraction is
	// always the larger (derived), which is the SDM's "QNaN source operand".
	std::uint32_t firstFraction = first & kFractionMask;
	std::uint32_t secondFraction = second & kFractionMask;
	if (firstFraction != secondFraction) {
		return Quiet(firstFraction > secondFraction ? first : second);
	}
	return Quiet((first & kSignBit) == 0 ? first : second);
}

// x + y for two finite floats, rounded to nearest even, as an IEEE single
// addition; exact on the integer significands.
constexpr std::uint32_t AddFinite(std::uint32_t x, std::uint32_t y) {
	struct Operand {
		std::uint32_t bits;
		bool negative;
		std::int32_t exponent;     // biased; 1 for denormals and zero
		std::uint64_t significand;  // value = significand * 2^(exponent - 150)
	};
	auto unpack = [](std::uint32_t bits) {
		std::int32_t field = static_cast<std::int32_t>((bits & kExponentMask) >> 23);
		std::uint64_t fraction = bits & kFractionMask;
		return Operand{bits, (bits & kSignBit) != 0, field == 0 ? 1 : field, field == 0 ? fraction : fraction | 0x00800000};
	};
	Operand large = unpack(x);
	Operand small = unpack(y);
	if (large.significand == 0 && small.significand == 0) {
		return large.negative && small.negative ? kSignBit : 0u;  // -0 only from -0 + -0
	}
	if (small.significand == 0) {
		return large.bits;
	}
	if (large.significand == 0) {
		return small.bits;
	}
	if (large.exponent < small.exponent) {
		Operand swap = large;
		large = small;
		small = swap;
	}
	std::int32_t gap = large.exponent - small.exponent;
	// From a gap of 26 on, |small| < ulp(large) / 4 (a quarter of the
	// smaller ulp below a power of two included), so the sum rounds to large.
	if (gap >= 26) {
		return large.bits;
	}
	std::int64_t a = static_cast<std::int64_t>(large.significand << gap);
	std::int64_t b = static_cast<std::int64_t>(small.significand);
	std::int64_t sum = (large.negative ? -a : a) + (small.negative ? -b : b);
	if (sum == 0) {
		return 0u;  // exact cancellation gives +0 under round to nearest
	}
	std::uint32_t sign = sum < 0 ? kSignBit : 0u;
	std::uint64_t magnitude = static_cast<std::uint64_t>(sum < 0 ? -sum : sum);  // value = magnitude * 2^(small.exponent - 150)
	std::int32_t length = static_cast<std::int32_t>(std::bit_width(magnitude));
	std::int32_t exponent = small.exponent + length - 24;
	if (exponent < 1) {
		// Denormal: exact, magnitude * 2^(small.exponent - 1) < 2^23.
		return sign | static_cast<std::uint32_t>(magnitude << (small.exponent - 1));
	}
	std::uint64_t significand = 0;
	if (length > 24) {
		std::int32_t shift = length - 24;
		significand = magnitude >> shift;
		std::uint64_t rest = magnitude & ((std::uint64_t{1} << shift) - 1);
		std::uint64_t half = std::uint64_t{1} << (shift - 1);
		if (rest > half || (rest == half && (significand & 1) != 0)) {
			++significand;
		}
		if (significand == (std::uint64_t{1} << 24)) {
			significand >>= 1;
			++exponent;
		}
	} else {
		significand = magnitude << (24 - length);
	}
	if (exponent >= 255) {
		return sign | kExponentMask;  // overflow rounds to infinity
	}
	return sign | (static_cast<std::uint32_t>(exponent) << 23) | (static_cast<std::uint32_t>(significand) & kFractionMask);
}

// One pass of the sum: FLD m32 value (ST0), FADD m32 sum, FSTP m32 sum
// (0x005AD194-0x005AD19A). Takes and returns float bit patterns.
constexpr std::uint32_t SumStep(std::uint32_t sum, std::uint32_t value) {
	std::uint32_t loaded = Quiet(value);  // FLD m32 quiets an sNaN
	if (IsNaN(loaded) || IsNaN(sum)) {
		return ChooseNaN(loaded, sum);
	}
	if (IsInfinity(loaded) || IsInfinity(sum)) {
		if (IsInfinity(loaded) && IsInfinity(sum) && loaded != sum) {
			return kRealIndefinite;  // inf + -inf, invalid operation masked
		}
		return IsInfinity(loaded) ? loaded : sum;
	}
	return AddFinite(loaded, sum);
}

// FLD m32 then FSTP m64: the double bit pattern of a float, exact, the sNaN
// quieted (0x005AD206/0x005AD20E for a setting, 0x005AD287/0x005AD28E for
// the sum). What the original pushes for %.04f.
constexpr std::uint64_t WidenToDouble(std::uint32_t bits) {
	std::uint64_t sign = static_cast<std::uint64_t>(bits & kSignBit) << 32;
	std::uint32_t field = (bits & kExponentMask) >> 23;
	std::uint64_t fraction = bits & kFractionMask;
	if (field == 0xFF) {
		std::uint64_t payload = fraction == 0 ? 0 : (fraction | kQuietBit) << 29;
		return sign | (std::uint64_t{0x7FF} << 52) | payload;
	}
	if (field == 0) {
		if (fraction == 0) {
			return sign;
		}
		std::int32_t shift = 24 - static_cast<std::int32_t>(std::bit_width(fraction));  // normalise the denormal
		std::uint64_t exponent = static_cast<std::uint64_t>(1 - shift - 127 + 1023);
		return sign | (exponent << 52) | (((fraction << shift) & kFractionMask) << 29);
	}
	return sign | (static_cast<std::uint64_t>(field - 127 + 1023) << 52) | (fraction << 29);
}

// --- B3, B8: the two loops -------------------------------------------------------------

// B3: ADD EDI,1; CMP EDI,4; JL (0x005AD1A4-0x005AD1AD), signed.
constexpr bool SettingsLoopContinues(std::int32_t nextIndex) {
	return nextIndex < kSectionCount;
}

// B8: ADD EDI,4; CMP EDI,0xB14130; JL (0x005AD275-0x005AD281), a signed
// compare of addresses.
constexpr bool WarningLoopContinues(std::uint32_t nextCursor) {
	return static_cast<std::int32_t>(nextCursor) < static_cast<std::int32_t>(kSectionTableEnd);
}

// --- B4, B5: is the sum 1 within the tolerance? -------------------------------------

enum class SumSide : std::uint8_t {
	AboveOne,  // FCOM: C0 = C3 = 0, JNZ not taken: FSUB m64 [0x00A2F928] (0x005AD1C0)
	NotAbove,  // C0 or C3 set - less, equal or unordered: FLD1; FSUBRP (0x005AD1C8)
};

// B4: FLD1; FLD sum; FCOM; TEST AH,0x41; JNZ (0x005AD1AF-0x005AD1BE).
constexpr SumSide SideOf(std::uint32_t sumBits) {
	double sum = std::bit_cast<double>(WidenToDouble(sumBits));
	return sum > kOne ? SumSide::AboveOne : SumSide::NotAbove;
}

// B4's two arms: sum - 1.0 (0x005AD1C0) or 1.0 - sum (0x005AD1C8-0x005AD1CA,
// DE E1 = FSUBRP ST1,ST0: ST1 = ST0 - ST1 = 1 - sum). The 53-bit reading;
// see the header for why the decision is the same at 24 bits.
constexpr double DifferenceFromOne(std::uint32_t sumBits) {
	double sum = std::bit_cast<double>(WidenToDouble(sumBits));
	return SideOf(sumBits) == SumSide::AboveOne ? sum - kOne : kOne - sum;
}

// B5: FCOMP m64 [0x00A3B150]; TEST AH,5; JNP (0x005AD1CC-0x005AD1D7). The
// jump past the warning is taken for C0 alone (difference below the
// tolerance, ordered); equal (C3) and unordered (C0 C2 C3) fall through.
constexpr bool WithinTolerance(double difference) {
	return difference < kTolerance;
}

constexpr bool SumWarns(std::uint32_t sumBits) {
	return !WithinTolerance(DifferenceFromOne(sumBits));
}

// --- B7: the inlined strlen and the append budget ------------------------------------

// MOV CL,[EAX]; ADD EAX,1; TEST CL,CL; JNZ; SUB EAX,EDX
// (0x005AD24D-0x005AD259 and 0x005AD2AF-0x005AD2BB).
constexpr std::uint32_t InlinedLength(const char* text) {
	std::uint32_t length = 0;
	while (text[length] != '\0') {
		++length;
	}
	return length;
}

// MOV ECX,0x400; SUB ECX,EAX (0x005AD25B-0x005AD260, 0x005AD2BD-0x005AD2C2):
// a 32-bit subtraction, pushed as _mbsnbcat's count.
constexpr std::uint32_t AppendBudget(std::uint32_t length) {
	return kMessageBytes - length;
}

// --- The run -----------------------------------------------------------------------------

// The menu fields this function writes, as plain values; the names follow
// re/functions/0x005AD980.md where that function reads the same offset, and
// the offsets are the engine header's LoadingMenuLayout. RunConstructor works
// on either this or the layout itself.
struct MenuFields {
	std::uint32_t vtable = 0;                   // +0x00
	std::int32_t unknown28 = 0;                 // +0x28
	std::array<std::uint32_t, 4> sectionValues{};  // +0x2C..+0x38
	std::int32_t slowStepCap = 0;               // +0x3C
	float screenTimeLeft = 0.0f;                // +0x40
	std::uint32_t unknown4C = 0;                // +0x4C
	std::uint32_t unknown50 = 0;                // +0x50
	std::uint32_t clock = 0;                    // +0x54  TimeInfo* in the engine
	std::int32_t shownStep = 0;                 // +0x60
	std::int32_t targetStep = 0;                // +0x64
	std::int32_t stepChangedMs = 0;             // +0x68
	std::int32_t lastTickMs = 0;                // +0x6C
	std::uint8_t fastStepEnabled = 0;           // +0x70
	std::uint8_t unknown71 = 0;                 // +0x71
};

enum class Outcome : std::uint8_t {
	Constructed,           // returns this at 0x005AD341
	FaultNullOsGlobals,    // the original reads [0+0x24] at 0x005AD14A
	FaultNullSettingName,  // the original reads [0+4] at 0x005AD22B (C7)
	FaultNullClockRead,    // the original reads [0+0x10] at 0x005AD32F (C14)
};

template <class Field>
Field FromAddress(std::uint32_t address) {
	if constexpr (std::is_pointer_v<Field>) {
		return reinterpret_cast<Field>(static_cast<std::uintptr_t>(address));
	} else {
		return static_cast<Field>(address);
	}
}

template <class Field>
std::uint32_t ToAddress(Field field) {
	if constexpr (std::is_pointer_v<Field>) {
		return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(field));
	} else {
		return static_cast<std::uint32_t>(field);
	}
}

// The body's sequence over ports:
//   BaseConstruct(menu)                         0x00584640, ECX = this, 0x005AD116
//   Fields(menu) -> fields&                     the menu's memory (no call)
//   OsGlobals() -> globals                      [0x00B33398], 0x005AD145
//   OsGlobalsHookObject(globals) -> object      [globals+0x24], 0x005AD14A
//   OsGlobalsHook(object)                       0x006A9B40, ECX = object, 0x005AD158
//   SectionSetting(index) -> setting            [0x00B14120 + 4*index], 0x005AD170 and 0x005AD200
//   SettingValue(setting) -> float bits         [setting+0], 0x005AD18B, 0x005AD194, 0x005AD206
//   SettingName(setting) -> name pointer        [setting+4], 0x005AD208
//   StoreSectionCopy(index, float bits)         [0x00B3B3DC + 4*index], 0x005AD18D
//   WarnMissing(format, index)                  0x004A7A60(0x00A6C3B8, i), 0x005AD181
//   FormatHeader(buffer, 0x400, format)         _snprintf 0x009833BB, 0x005AD1EF
//   FormatFoundLine(line, 0x104, format, name, value as double bits)   0x009833BB, 0x005AD221
//   FormatTotalLine(line, 0x104, format, sum as double bits)           0x009833BB, 0x005AD2A0
//   Concatenate(message, line, count)           _mbsnbcat 0x009872B3, 0x005AD270 and 0x005AD2D2
//   WarnMessage(message)                        0x004A7A60(buffer), 0x005AD2DF
//   Allocate(0x18) -> block                     FormHeap_Allocate 0x00401F00, 0x005AD2E9
//   TickCount() -> tick                         GetTickCount via 0x00A280D0, 0x005AD309 and 0x005AD322
//   InitClock(block, tick)                      TimeInfo::Init 0x0047D150, 0x005AD30E
//   UpdateClock(block, tick)                    TimeInfo::Update 0x0047D170, 0x005AD327
//   ReadClockMs(clock) -> ms                    [clock+0x10] (clockMs), 0x005AD32F
//   ScreenTimeSetting() -> float bits           [0x00B14158], 0x005AD338
// What 0x00584640, 0x006A9B40 and 0x004A7A60 do is unverified (open
// questions 2 and 4 of the mapping); the port names say where they are
// called, not what they do. The formatting and the concatenation stay the
// game's CRT (ports), as its %.04f and NaN/infinity output is the result.
template <class Ports>
Outcome RunConstructor(Ports& ports, std::uint32_t menu) {
	ports.BaseConstruct(menu);
	auto& fields = ports.Fields(menu);
	fields.vtable = FromAddress<decltype(fields.vtable)>(kVtable);  // 0x005AD11F
	fields.unknown4C = 0;                                          // 0x005AD125
	fields.unknown50 = 0;                                          // 0x005AD128
	fields.screenTimeLeft = 0.0f;                                  // FLDZ; FSTP 0x005AD12B (+0.0f)
	fields.slowStepCap = -1;                                       // 0x005AD131
	fields.unknown28 = -1;                                         // 0x005AD134
	fields.shownStep = 0;                                          // 0x005AD137
	fields.targetStep = 0;                                         // 0x005AD13A
	fields.fastStepEnabled = 0;                                    // 0x005AD13D
	fields.unknown71 = 1;                                          // 0x005AD141

	std::uint32_t osGlobals = ports.OsGlobals();
	if (IsNull(osGlobals)) {
		return Outcome::FaultNullOsGlobals;
	}
	if (std::uint32_t hookObject = ports.OsGlobalsHookObject(osGlobals); !IsNull(hookObject)) {  // B1
		ports.OsGlobalsHook(hookObject);
	}

	std::uint32_t sum = 0;  // FLDZ; FSTP float [ESP+0x2C] (0x005AD15D-0x005AD162): +0.0f
	std::int32_t index = 0;
	do {  // B3, entered at 0x005AD170 by the JMP at 0x005AD166
		std::uint32_t setting = ports.SectionSetting(static_cast<std::uint32_t>(index));
		if (IsNull(setting)) {  // B2
			ports.WarnMissing(kMissingFormat, index);
		} else {
			ports.StoreSectionCopy(static_cast<std::uint32_t>(index), LoadStore(ports.SettingValue(setting)));
			sum = SumStep(sum, ports.SettingValue(setting));
		}
		fields.sectionValues[index] = 0;  // 0x005AD19E, both arms
		++index;
	} while (SettingsLoopContinues(index));

	if (SumWarns(sum)) {  // B4, B5
		char message[kMessageBytes] = {};
		char line[kLineBytes] = {};
		ports.FormatHeader(message, kMessageBytes, kHeaderFormat);
		std::uint32_t cursor = kSectionTableAddress;
		do {  // B8
			std::uint32_t setting = ports.SectionSetting((cursor - kSectionTableAddress) / kSlotBytes);
			if (IsNull(setting)) {  // B6
				return Outcome::FaultNullSettingName;
			}
			std::uint64_t value = WidenToDouble(ports.SettingValue(setting));  // 0x005AD206, before the name
			ports.FormatFoundLine(line, kLineBytes, kFoundLineFormat, ports.SettingName(setting), value);
			ports.Concatenate(message, line, AppendBudget(InlinedLength(message)));  // B7
			cursor += kSlotBytes;
		} while (WarningLoopContinues(cursor));
		ports.FormatTotalLine(line, kLineBytes, kTotalFormat, WidenToDouble(sum));
		ports.Concatenate(message, line, AppendBudget(InlinedLength(message)));  // B7
		ports.WarnMessage(message);
	}

	std::uint32_t block = ports.Allocate(kClockBytes);
	if (!IsNull(block)) {  // B9
		ports.InitClock(block, ports.TickCount());  // Init's EAX is not used (C13)
	}
	fields.clock = FromAddress<decltype(fields.clock)>(block);  // 0x005AD31F (EDI = 0 on the null arm)
	ports.UpdateClock(block, ports.TickCount());                // EDI, not the field (0x005AD325)
	std::uint32_t clock = ToAddress(fields.clock);              // re-read at 0x005AD32C
	if (IsNull(clock)) {
		return Outcome::FaultNullClockRead;
	}
	std::uint32_t clockMs = ports.ReadClockMs(clock);
	fields.stepChangedMs = static_cast<std::int32_t>(clockMs);  // 0x005AD332, a 32-bit copy
	fields.lastTickMs = static_cast<std::int32_t>(clockMs);     // 0x005AD335
	fields.screenTimeLeft = std::bit_cast<float>(LoadStore(ports.ScreenTimeSetting()));  // 0x005AD338-0x005AD33E
	return Outcome::Constructed;
}

} // namespace obng::engine::time::loading_menu_ctor
