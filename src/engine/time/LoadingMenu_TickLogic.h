#pragma once

// The decisions of 0x005AD980 (proposed LoadingMenu::Tick), as pure functions
// over plain values, and RunTick, which replays the body's call and access
// order over injected ports so the engine binding and the tests' fakes share
// one sequence. Evidence: re/functions/0x005AD980.md and the export
// re/export/loading-menu/0x005AD980/ (asm.s, data.json). Names are proposals
// until the loading-menu slice's refutation round; the address stays next to
// each.
//
// Where pseudo.c and the instructions disagree, the instructions are followed
// (AGENTS.md):
//   - pseudo.c converts sinceLast with (float)iVar10; the instructions keep
//     the FILD result on the x87 stack (0x005ADA12), so the integer goes into
//     the division exact (read).
//   - pseudo.c divides by (float)_DAT_00a2fc70; the instruction is FDIV double
//     ptr [0x00A2FC70] (0x005ADA1E). 1000.0 is exact in float, so this one does
//     not change the value (C13, derived).
//   - pseudo.c computes the whole countdown in float; the instructions round
//     at the x87 precision control (below).
//   - pseudo.c drops every `this` (C12, read).
// pseudo.c's `fVar1 <= 0.0` for B8 agrees with the instructions, NaN
// included: C0 is set on unordered (TEST AH,1 0x005ADA37), so a NaN timer is
// not expired, and `NaN <= 0` is false in C++ as well (derived).
//
// The original has no guard on three reads; the rebuild does not invent one.
// Where the original reads through a null pointer inside this body, RunTick
// stops at that instruction and reports it as an Outcome instead of reading:
//   - 0x005AD9B6: the second GetContainingMenu result is not tested;
//   - 0x005AD9CA: [menu+0x54] null (the update 0x0047D170 is still called with
//     ECX = 0 before, as the original calls it; what it does then is the
//     callee's);
//   - 0x005ADB1B: [0x00B33398] g_osGlobals null.
//
// Floating point: the countdown (0x005ADA09..0x005ADA2E) depends on the x87
// precision control (PC), so the precision is an input:
//   - Precision::Single is the game's: the x87 runs at PC 24, round to
//     nearest, all exceptions masked (measured, knowledge/frame-clock.md:
//     _control87 0x000A001F at the first TimeInfo::Update under Proton).
//     At PC 24 exactly three results round to a 24-bit significand: the FADD
//     of 2^32 (0x005ADA18, only when sinceLast < 0), the FDIV by 1000.0
//     (0x005ADA1E) and the FSUBP (0x005ADA24); FLD/FILD/FST do not round for
//     reachable inputs (C17, derived; the tests' host x87 check confirms it).
//     The C++ form of C18 reproduces this: sinceLast exact into a double
//     division, then each of the three results rounded to float. Each double
//     operation here is exact or correctly rounded such that the following
//     cast to float equals one rounding of the exact value to 24 bits (C18,
//     derived), and the float subtraction is one IEEE single rounding, equal
//     to the x87's except for subnormal or overflowing results, which C19
//     excludes.
//   - Precision::Double is the 53-bit reading (PC 53): the same three results
//     rounded to 53 bits, which double arithmetic does, then FSTP float. The
//     x87's extended exponent range does not matter: no intermediate leaves
//     the double range (derived: an int32, 2^32, 1000.0 and a float).
//   - PC 64 and every other rounding control or unmasked exception are not
//     modelled; PrecisionOfControlWord refuses them, and a binding must then
//     let the original run.
// Both readings need C++ float and double evaluated in IEEE single and double
// (no x87 excess precision): the static_assert below. The tests compare both
// against the original's opcodes on the host x87 (300000 inputs each,
// NaN/inf/subnormal timers included) and against the emulator's table.

#include <bit>
#include <cfloat>
#include <cstdint>
#include <optional>

namespace obng::engine::time::loading_menu_tick {

static_assert(FLT_EVAL_METHOD == 0, "the countdown needs float and double arithmetic rounded to their own formats (SSE2, not x87 excess precision)");

// --- Constants (read) -------------------------------------------------------------------

// 0x00A2FC78: 4294967296.0f, the unsigned fix-up (data.json bytes 00 00 80 4f;
// also TimeInfo.h kTwoPow32Address).
inline constexpr float kTwoPow32 = std::bit_cast<float>(std::uint32_t{0x4F800000u});
// 0x00A2FC70: 1000.0, milliseconds per second (data.json bytes 00 00 00 00 00 40 8f 40).
inline constexpr double kMsPerSecond = std::bit_cast<double>(std::uint64_t{0x408F400000000000ull});
// 0x00A6C4C8: 99.9999008178711 = (double)99.9999f, the step ceiling of B13
// (data.json bytes 00 00 00 60 fe ff 58 40).
inline constexpr double kStepCeiling = std::bit_cast<double>(std::uint64_t{0x4058FFFE60000000ull});
static_assert(kStepCeiling == static_cast<double>(99.9999f), "0x00A6C4C8 holds (double)99.9999f");

inline constexpr std::uint32_t kLoadingMenuType = 0x3EF;    // PUSH 0x3EF 0x005AD984 (read); kMenuType_Loading (C2, derived)
inline constexpr std::uint32_t kTraitUser3 = 0xFB1;         // PUSH 0xFB1 0x005ADA89 (read); kTileValue_user3 (C4, derived)
inline constexpr std::int32_t kShownStepLimit = 100;        // CMP ECX,0x64 0x005ADA62 (read)
inline constexpr std::uint32_t kInterfacePassArgument = 0;  // PUSH 0 0x005ADAF4 (read)

// Zero test for an address held as an integer or as a pointer
// (TEST reg,reg; JZ - 0x005AD993, 0x005AD9A2, 0x005ADACE, 0x005ADAD8, 0x005ADB09).
template <class Address>
constexpr bool IsNull(Address address) {
	return address == Address{};
}

// --- Precision -------------------------------------------------------------------------

// The x87 precision control the countdown is computed at (the PC field,
// bits 8-9 of the control word, as the enumerator's value).
enum class Precision : std::uint8_t {
	Single = 0b00,  // 24-bit significand: the game's (measured)
	Double = 0b10,  // 53-bit significand
};

inline constexpr std::uint16_t kExceptionMasks = 0x003F;  // bits 0-5: IM DM ZM OM UM PM
inline constexpr std::uint16_t kPrecisionField = 0x0300;  // bits 8-9
inline constexpr std::uint16_t kRoundingField = 0x0C00;   // bits 10-11; 00B is round to nearest
inline constexpr int kPrecisionShift = 8;

// The precision of a hardware x87 control word (FNSTCW), when the rebuild
// models that state: all six exceptions masked, round to nearest, PC 24 or
// 53. Anything else - PC 64, the reserved PC 01B, another rounding control,
// an unmasked exception - gives none. Bits 6-7 and 13-15 are reserved and
// bit 12 (infinity control) has no effect on later x87 units, so they are not
// inspected.
constexpr std::optional<Precision> PrecisionOfControlWord(std::uint16_t controlWord) {
	if ((controlWord & kExceptionMasks) != kExceptionMasks || (controlWord & kRoundingField) != 0) {
		return std::nullopt;
	}
	switch ((controlWord & kPrecisionField) >> kPrecisionShift) {
	case 0b00:
		return Precision::Single;
	case 0b10:
		return Precision::Double;
	default:
		return std::nullopt;
	}
}

// --- B2: the elapsed values -------------------------------------------------------------

// 0x005AD9CD..0x005AD9D4 (read): a 32-bit SUB of two clock+0x10 values, then
// compared and tested as signed (JLE/JG/JGE, TEST ECX,ECX).
constexpr std::int32_t ElapsedMs(std::uint32_t now, std::uint32_t since) {
	return static_cast<std::int32_t>(now - since);
}

// --- B3..B6: the target step ------------------------------------------------------------

// The int32 settings at 0x00B14160, 0x00B14168, 0x00B14170, compared signed
// (C14, read; image values 2500, 100, 10, runtime values unverified). Their
// names are unverified (open question 1); these say what each one gates.
struct StepThresholds {
	std::int32_t alwaysMs;  // [0x00B14160]  B3
	std::int32_t cappedMs;  // [0x00B14168]  B5
	std::int32_t fastMs;    // [0x00B14170]  B6b
};

enum class StepRule : std::uint8_t {
	AlwaysAdvance,  // B3 taken: sinceStep > alwaysMs, no cap (ADD [ESI+0x64],1 0x005AD9DF)
	AtCap,          // B4 taken: targetStep >= slowStepCap (JGE 0x005AD9EB)
	CappedAdvance,  // B5 taken: sinceStep > cappedMs (JG 0x005AD9F3)
	FastDisabled,   // B6a taken: fastStepEnabled == 0 (JZ 0x005AD9F9)
	FastAdvance,    // B6b not taken: sinceStep > fastMs
	NotYet,         // B6b taken: sinceStep <= fastMs (JLE 0x005ADA01)
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

// --- B7 and the countdown ---------------------------------------------------------------

// TEST ECX,ECX 0x005ADA09 ... JGE 0x005ADA16 (read): the fix-up runs when
// sinceLast is negative as signed.
constexpr bool NeedsUnsignedFixup(std::int32_t sinceLast) {
	return sinceLast < 0;
}

constexpr double RoundToPrecision(double value, Precision precision) {
	return precision == Precision::Single ? static_cast<double>(static_cast<float>(value)) : value;
}

// ST0 after FILD dword (0x005ADA12, exact) and, when negative, FADD float
// [0x00A2FC78] (0x005ADA18, rounded to the precision). sinceLast + 2^32 is an
// integer below 2^33, exact in double, so the float cast is the one rounding
// to 24 bits (C18 (1), derived).
constexpr double ElapsedForTimer(std::int32_t sinceLast, Precision precision) {
	const double exact = static_cast<double>(sinceLast);
	if (!NeedsUnsignedFixup(sinceLast)) {
		return exact;
	}
	return RoundToPrecision(exact + static_cast<double>(kTwoPow32), precision);
}

// ST0 after FDIV double [0x00A2FC70] (0x005ADA1E), rounded to the precision.
// The double quotient never lands on or crosses a 24-bit midpoint the exact
// quotient is not on, so its float cast is the x87's 24-bit rounding (C18 (2),
// derived).
constexpr double TimerQuotient(std::int32_t sinceLast, Precision precision) {
	return RoundToPrecision(ElapsedForTimer(sinceLast, precision) / kMsPerSecond, precision);
}

// The new +0x40: FSUBP (0x005ADA24, DE E9: ST1 = timer - quotient) rounded to
// the precision, then FSTP float (0x005ADA26). At PC 24 the quotient is a
// float already and float - float is one 24-bit rounding (C18 (3), C19); at
// PC 53 the difference is a double rounded once more to float, as FSTP does.
constexpr float CountDown(float timer, std::int32_t sinceLast, Precision precision) {
	const double quotient = TimerQuotient(sinceLast, precision);
	if (precision == Precision::Single) {
		return timer - static_cast<float>(quotient);
	}
	return static_cast<float>(static_cast<double>(timer) - quotient);
}

// --- B8, B9: refresh ----------------------------------------------------------------------

// FLDZ; FCOMPP compares 0 with the stored timer; TEST AH,1 (C0: 0 < timer,
// also set when unordered); JZ means expired (0x005ADA31..0x005ADA3A, read).
constexpr bool TimerExpired(float timer) {
	return timer <= 0.0f;
}

// B8 taken: refresh = 1 (0x005ADA48). B8 not taken: CMP byte ptr [ESP+0x1C],0
// (0x005ADA3C), any nonzero byte forces (read).
constexpr bool Refresh(float timer, std::uint8_t forceRefresh) {
	return TimerExpired(timer) || forceRefresh != 0;
}

// --- B10, B11: whether the bar is touched -------------------------------------------------

enum class RedrawGate : std::uint8_t {
	Unchanged,  // B10 taken: target == shown and no refresh (JZ 0x005ADA5C)
	BarFull,    // B11 taken: the old shown >= 100 (JGE 0x005ADA65)
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

// --- B12, B13 -----------------------------------------------------------------------------

// CMP ECX,EAX; JZ 0x005ADA6B/0x005ADA6D (read): ECX is the old shownStep, EAX
// targetStep; unequal means the shown step catches up.
constexpr bool ShownStepCatchesUp(std::int32_t shownStep, std::int32_t targetStep) {
	return shownStep != targetStep;
}

// FILD [ESI+0x60]; FSTP float [ESP+0x18] (0x005ADA78..0x005ADA7E, read): the
// value pushed to the tile and compared at B13 is the step rounded once to
// float, at any precision control (C23, derived).
constexpr float StepAsFloat(std::int32_t shownStep) {
	return static_cast<float>(shownStep);
}

// FCOMP double ptr [0x00A6C4C8]; TEST AH,5; JP (0x005ADA97..0x005ADAA2, read):
// the jump is taken unless C0 alone is set, so the body goes on only when the
// step is below the ceiling and ordered. For an int step that is step <= 99
// (C22, derived).
constexpr bool BelowCeiling(float stepAsFloat) {
	return static_cast<double>(stepAsFloat) < kStepCeiling;
}

// --- B18 ----------------------------------------------------------------------------------

// CMP EAX,ESI; JNZ 0x005ADB25/0x005ADB29 (read): GetCurrentThreadId against
// [g_osGlobals+0x10] (mainThreadID, xOBSE GameOSDepend.h).
constexpr bool OnMainThread(std::uint32_t currentThreadId, std::uint32_t mainThreadId) {
	return currentThreadId == mainThreadId;
}

// --- The run ------------------------------------------------------------------------------

// The menu fields this function touches, as plain values (offsets in the
// engine header's LoadingMenuLayout, which has the same field names). RunTick
// works on either: it names the fields, the ports supply the reference.
struct MenuFields {
	std::int32_t slowStepCap = -1;     // +0x3C
	float screenTimeLeft = 0.0f;       // +0x40  seconds
	std::uint32_t clock = 0;           // +0x54  TimeInfo* in the engine
	std::int32_t shownStep = 0;        // +0x60
	std::int32_t targetStep = 0;       // +0x64
	std::uint32_t stepChangedMs = 0;   // +0x68
	std::uint32_t lastTickMs = 0;      // +0x6C
	std::uint8_t fastStepEnabled = 0;  // +0x70
};

struct Inputs {
	std::uint8_t forceRefresh;  // the stack byte [entry ESP+4] (0x005ADA3C)
	Precision precision;        // the x87 precision control at the call (the game's: Single, measured)
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
//   ReadClockMs(clock) -> ms               [clock+0x10], 0x005AD9CA and 0x005ADA6F
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
// 0x00432890 and 0x0060D0A0 do is unverified (open question 5); the port
// names say where they are called, not what they do.
//
// The three settings are read through one port after the first clock read;
// the original reads them one by one at B3/B5/B6b, and reading them at once
// changes nothing, as memory reads with no call between them do not affect
// each other (derived).
template <class Ports, class Fields, class Clock>
void RunRedraw(Ports& ports, Fields& fields, std::uint32_t tileMenu, std::uint32_t menu, Clock clock, bool refresh) {
	if (ShownStepCatchesUp(fields.shownStep, fields.targetStep)) {  // B12
		fields.stepChangedMs = ports.ReadClockMs(clock);             // 0x005ADA6F..0x005ADA72, a second read
		fields.shownStep = fields.targetStep;                        // 0x005ADA75
	}
	const float stepAsFloat = StepAsFloat(fields.shownStep);  // the new shownStep (C22)
	ports.SetTileFloat(tileMenu, kTraitUser3, stepAsFloat);
	if (!BelowCeiling(stepAsFloat)) {  // B13
		return;
	}
	if (refresh) {  // B14
		ports.RefreshScreen(menu, tileMenu);
		fields.screenTimeLeft = ports.ScreenPeriodSetting();  // 0x005ADABD..0x005ADABF
	}
	if (IsNull(ports.InterfaceManager())) {  // B15
		return;
	}
	if (const std::uint32_t io = ports.IoManager(); !IsNull(io)) {  // B16
		ports.IoManagerBefore(io);
	}
	ports.InterfaceManagerUpdate(ports.InterfaceManager());  // the second answer, not tested (0x005ADAED)
	ports.InterfacePass(kInterfacePassArgument);
	ports.InterfacePassEnd();
	if (const std::uint32_t io = ports.IoManager(); !IsNull(io)) {  // B17
		ports.IoManagerAfter(io);
	}
}

template <class Ports>
Outcome RunTick(Ports& ports, const Inputs& inputs) {
	const std::uint32_t tileMenu = ports.TileMenuForType(kLoadingMenuType);
	if (IsNull(tileMenu)) {  // B1
		return Outcome::NoTileMenu;
	}
	if (IsNull(ports.ContainingMenu(tileMenu))) {  // B2
		return Outcome::NoMenu;
	}
	const std::uint32_t menu = ports.ContainingMenu(tileMenu);  // 0x005AD9AF, not tested
	if (IsNull(menu)) {
		return Outcome::FaultNullMenuRead;
	}
	auto& fields = ports.Fields(menu);

	const auto clockForUpdate = fields.clock;  // 0x005AD9B6, read before GetTickCount
	ports.UpdateClock(clockForUpdate, ports.TickCount());
	const auto clock = fields.clock;  // 0x005AD9C7, read again
	if (IsNull(clock)) {
		return Outcome::FaultNullClockRead;
	}
	const std::uint32_t now = ports.ReadClockMs(clock);  // EBP for the rest of the body
	const std::int32_t sinceStep = ElapsedMs(now, fields.stepChangedMs);
	const std::int32_t sinceLast = ElapsedMs(now, fields.lastTickMs);

	const StepRule rule =
	    ChooseStepRule(sinceStep, fields.targetStep, fields.slowStepCap, fields.fastStepEnabled, ports.StepThresholds());
	if (Advances(rule)) {  // B3..B6b: +0x64 is written only on an advance
		fields.targetStep = NextStep(fields.targetStep);
	}
	fields.screenTimeLeft = CountDown(fields.screenTimeLeft, sinceLast, inputs.precision);  // B7, 0x005ADA2E
	const bool refresh = Refresh(fields.screenTimeLeft, inputs.forceRefresh);              // B8, B9

	if (GateRedraw(fields.targetStep, fields.shownStep, refresh) == RedrawGate::Proceed) {  // B10, B11
		RunRedraw(ports, fields, tileMenu, menu, clock, refresh);
	}

	fields.lastTickMs = now;  // T1, 0x005ADB12

	const std::uint32_t osGlobals = ports.OsGlobals();
	if (IsNull(osGlobals)) {
		return Outcome::FaultNullOsGlobals;
	}
	const std::uint32_t mainThreadId = ports.MainThreadId(osGlobals);
	if (!OnMainThread(ports.CurrentThreadId(), mainThreadId)) {  // B18
		return Outcome::OffMainThread;
	}
	ports.OsGlobalsTail(ports.OsGlobals());
	return Outcome::TailCalled;
}

} // namespace obng::engine::time::loading_menu_tick
