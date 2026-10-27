// Tests of the rebuild of 0x005AD0D0 (proposed LoadingMenu::LoadingMenu): the
// constructor of the loading-screen menu. It runs the base constructor
// 0x00584640, writes the vtable 0x00A6C40C and the menu's fields, calls
// 0x006A9B40 on (*g_osGlobals 0x00B33398)+0x24 when that is nonzero, copies
// and sums the four loading-bar shares behind the table 0x00B14120, composes
// a warning when the sum is not 1 within 0.01, allocates and primes the
// menu's private TimeInfo, and loads the setting 0x00B14158 into +0x40.
//
// One section per branch of the mapped control flow (re/functions/0x005AD0D0.md):
//   B1  osGlobals hook object nonzero        0x005AD156
//   B2  table entry present                  0x005AD179
//   B3  settings loop, i < 4 (signed)        0x005AD1AD
//   B4  sum side after FCOM with 1.0         0x005AD1BE
//   B5  difference below the tolerance       0x005AD1D7
//   B6  warning line: entry present          0x005AD204
//   B7  inlined strlen (twice)               0x005AD257, 0x005AD2B9
//   B8  warning loop, cursor < 0x00B14130    0x005AD281
//   B9  allocation nonzero                   0x005AD307
// plus the unconditional clock prime, and the refusal paths the original has
// no guard for: g_osGlobals null (read [0+0x24] at 0x005AD14A), a null entry
// reached by the warning loop (read [0+4] at 0x005AD22B, claim C7), and a
// null clock after the prime (read [0+0x10] at 0x005AD32F, claim C14).
//
// Floating point. The sum (FLD m32, FADD m32, FSTP m32 at 0x005AD194-
// 0x005AD19A), the copy (FLD m32, FSTP m32 at 0x005AD18B/0x005AD18D), the
// widening for %.04f (FLD m32, FSTP m64 at 0x005AD206/0x005AD20E and
// 0x005AD287/0x005AD28E) and the B4/B5 decision (0x005AD1AF-0x005AD1D7) are
// checked against the host's x87 executing the same opcodes under the
// control words 0x007F (24-bit, the game's measured precision), 0x027F
// (53-bit) and 0x037F (64-bit), all round to nearest with every exception
// masked. The tests therefore need an i386 build.

#include "Check.h"
#include "engine/time/LoadingMenu_LoadingMenu.h"
#include "engine/time/LoadingMenu_LoadingMenuLogic.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace logic = obng::engine::time::loading_menu_ctor;
namespace engine = obng::engine::time::loading_menu_ctor::engine;

namespace {

constexpr std::uint32_t kMenu = 0x00002000u;
constexpr std::uint32_t kOsGlobals = 0x00004000u;
constexpr std::uint32_t kHookObject = 0x00004400u;
constexpr std::uint32_t kBlock = 0x00003000u;
constexpr std::uint32_t kOtherBlock = 0x00003100u;
constexpr std::uint32_t kFirstTick = 1000u;
constexpr std::uint32_t kSecondTick = 1016u;
constexpr std::uint32_t kClockMs = 16u;
constexpr std::uint32_t kBaseVtable = 0x00A00000u;  // what the fake base constructor writes

constexpr std::array<std::uint32_t, 4> kSettings{0x00B14138u, 0x00B14140u, 0x00B14148u, 0x00B14150u};
constexpr std::array<std::uint32_t, 4> kNamePointers{0x00A6C590u, 0x00A6C5B0u, 0x00A6C5D0u, 0x00A6C5F0u};

// The image values (data.json 0x00B14138.. and C4): 0.06f, 0.35f, 0.44f, 0.15f.
constexpr std::array<std::uint32_t, 4> kImageShares{0x3D75C28Fu, 0x3EB33333u, 0x3EE147AEu, 0x3E19999Au};
constexpr std::uint32_t kImageScreenTime = 0x41700000u;  // 15.0f, data.json 0x00B14158

constexpr std::uint32_t kPlusZero = 0x00000000u;
constexpr std::uint32_t kMinusZero = 0x80000000u;
constexpr std::uint32_t kOne = 0x3F800000u;
constexpr std::uint32_t kPlusInfinity = 0x7F800000u;
constexpr std::uint32_t kMinusInfinity = 0xFF800000u;
constexpr std::uint32_t kQuietNaN = 0x7FC00000u;
constexpr std::uint32_t kSignalingNaN = 0x7F800001u;
constexpr std::uint32_t kIndefinite = 0xFFC00000u;

std::uint32_t BitsOf(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

// Records every port call in order as text, so a test can check the whole
// sequence and each argument.
template <class FieldsT>
struct FakePorts {
	FieldsT fields{};
	std::vector<std::string> log;

	std::uint32_t osGlobals = kOsGlobals;
	std::uint32_t hookObject = 0;
	std::array<std::uint32_t, 4> table = kSettings;
	std::map<std::uint32_t, std::uint32_t> values;  // setting -> float bits
	std::map<std::uint32_t, std::uint32_t> names;   // setting -> name pointer
	std::map<std::uint32_t, std::string> nameText{
		{kNamePointers[0], "fPercentageOfBar0:LoadingBar"},
		{kNamePointers[1], "fPercentageOfBar1:LoadingBar"},
		{kNamePointers[2], "fPercentageOfBar2:LoadingBar"},
		{kNamePointers[3], "fPercentageOfBar3:LoadingBar"},
	};
	std::array<std::uint32_t, 4> copies{0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu};
	std::vector<std::uint32_t> missingIndices;
	std::vector<std::uint32_t> missingFormats;
	std::vector<std::string> messages;
	std::vector<std::uint32_t> budgets;
	std::vector<std::uint32_t> lengthsBeforeAppend;
	std::uint32_t allocation = kBlock;
	std::vector<std::uint32_t> ticks{kFirstTick, kSecondTick};
	std::size_t tickCalls = 0;
	std::uint32_t clockMs = kClockMs;
	std::uint32_t screenTime = kImageScreenTime;
	std::uint32_t vtableSeenByBase = 0;
	std::function<void(FakePorts&)> onHeader;
	std::function<void(FakePorts&)> onUpdate;

	FakePorts() {
		for (std::size_t i = 0; i < 4; ++i) {
			values[kSettings[i]] = kImageShares[i];
			names[kSettings[i]] = kNamePointers[i];
		}
	}

	void Note(const std::string& text) { log.push_back(text); }

	void BaseConstruct(std::uint32_t menu) {
		Note("BaseConstruct(" + std::to_string(menu) + ")");
		vtableSeenByBase = fields.vtable;
		fields.vtable = kBaseVtable;
	}
	FieldsT& Fields(std::uint32_t menu) {
		Note("Fields(" + std::to_string(menu) + ")");
		return fields;
	}

	std::uint32_t OsGlobals() {
		Note("OsGlobals");
		return osGlobals;
	}
	std::uint32_t OsGlobalsHookObject(std::uint32_t globals) {
		Note("OsGlobalsHookObject(" + std::to_string(globals) + ")");
		return hookObject;
	}
	void OsGlobalsHook(std::uint32_t object) { Note("OsGlobalsHook(" + std::to_string(object) + ")"); }

	std::uint32_t SectionSetting(std::uint32_t index) {
		Note("SectionSetting(" + std::to_string(index) + ")");
		return table.at(index);
	}
	std::uint32_t SettingValue(std::uint32_t setting) {
		Note("SettingValue(" + std::to_string(setting) + ")");
		return values.at(setting);
	}
	std::uint32_t SettingName(std::uint32_t setting) {
		Note("SettingName(" + std::to_string(setting) + ")");
		return names.at(setting);
	}
	void StoreSectionCopy(std::uint32_t index, std::uint32_t bits) {
		Note("StoreSectionCopy(" + std::to_string(index) + ")");
		copies.at(index) = bits;
	}
	void WarnMissing(const logic::FormatString& format, std::int32_t index) {
		Note("WarnMissing(" + std::to_string(index) + ")");
		missingFormats.push_back(format.address);
		missingIndices.push_back(static_cast<std::uint32_t>(index));
	}
	void WarnMessage(const char* message) {
		Note("WarnMessage");
		messages.emplace_back(message);
	}
	void FormatHeader(char* buffer, std::uint32_t size, const logic::FormatString& format) {
		Note("FormatHeader(" + std::to_string(size) + "," + std::to_string(format.address) + ")");
		std::snprintf(buffer, size, "%s", format.text);
		if (onHeader) {
			onHeader(*this);
		}
	}
	void FormatFoundLine(char* buffer, std::uint32_t size, const logic::FormatString& format, std::uint32_t name,
		std::uint64_t valueBits) {
		Note("FormatFoundLine(" + std::to_string(size) + "," + std::to_string(format.address) + "," +
			std::to_string(name) + ")");
		std::snprintf(buffer, size, format.text, nameText.at(name).c_str(), std::bit_cast<double>(valueBits));
	}
	void FormatTotalLine(char* buffer, std::uint32_t size, const logic::FormatString& format, std::uint64_t totalBits) {
		Note("FormatTotalLine(" + std::to_string(size) + "," + std::to_string(format.address) + ")");
		std::snprintf(buffer, size, format.text, std::bit_cast<double>(totalBits));
	}
	// _mbsnbcat over single-byte text: append at most count bytes, then NUL.
	void Concatenate(char* message, const char* line, std::uint32_t count) {
		Note("Concatenate");
		lengthsBeforeAppend.push_back(static_cast<std::uint32_t>(std::strlen(message)));
		budgets.push_back(count);
		std::strncat(message, line, count);
	}
	std::uint32_t Allocate(std::uint32_t size) {
		Note("Allocate(" + std::to_string(size) + ")");
		return allocation;
	}
	std::uint32_t TickCount() {
		Note("TickCount");
		return ticks.at(tickCalls++);
	}
	void InitClock(std::uint32_t clock, std::uint32_t tick) {
		Note("InitClock(" + std::to_string(clock) + "," + std::to_string(tick) + ")");
	}
	void UpdateClock(std::uint32_t clock, std::uint32_t tick) {
		Note("UpdateClock(" + std::to_string(clock) + "," + std::to_string(tick) + ")");
		if (onUpdate) {
			onUpdate(*this);
		}
	}
	std::uint32_t ReadClockMs(std::uint32_t clock) {
		Note("ReadClockMs(" + std::to_string(clock) + ")");
		return clockMs;
	}
	std::uint32_t ScreenTimeSetting() {
		Note("ScreenTimeSetting");
		return screenTime;
	}

	std::size_t Count(const std::string& entry) const {
		std::size_t n = 0;
		for (const std::string& line : log) {
			n += line == entry ? 1 : 0;
		}
		return n;
	}
	std::size_t CountPrefix(const std::string& prefix) const {
		std::size_t n = 0;
		for (const std::string& line : log) {
			n += line.rfind(prefix, 0) == 0 ? 1 : 0;
		}
		return n;
	}
	bool Has(const std::string& entry) const { return Count(entry) > 0; }
};

using Ports = FakePorts<logic::MenuFields>;

void SetShares(Ports& ports, std::array<std::uint32_t, 4> shares) {
	for (std::size_t i = 0; i < 4; ++i) {
		ports.values[kSettings[i]] = shares[i];
	}
}

// --- The host x87, running the original's opcodes --------------------------------------

constexpr std::array<std::uint16_t, 3> kControlWords{0x007F, 0x027F, 0x037F};  // PC 24, 53, 64; RC nearest; all masked
constexpr std::array<const char*, 3> kPrecisionNames{"24-bit", "53-bit", "64-bit"};

// FLD m32 (D9 /0), FADD m32 (D8 /0), FSTP m32 (D9 /3), as 0x005AD194-0x005AD19A.
std::uint32_t SumStepOnHost(std::uint32_t sum, std::uint32_t value, std::uint16_t cw) {
	std::uint32_t out = 0;
	std::uint16_t saved = 0;
	__asm__ volatile(
		"fnstcw %[saved]\n\t"
		"fldcw %[cw]\n\t"
		"flds %[value]\n\t"
		"fadds %[sum]\n\t"
		"fstps %[out]\n\t"
		"fnclex\n\t"
		"fldcw %[saved]\n\t"
		: [saved] "=m"(saved), [out] "=m"(out)
		: [cw] "m"(cw), [value] "m"(value), [sum] "m"(sum)
		: "st");
	return out;
}

// FLD m32 (D9 /0), FSTP m32 (D9 /3), as 0x005AD18B/0x005AD18D and 0x005AD338/0x005AD33E.
std::uint32_t LoadStoreOnHost(std::uint32_t value, std::uint16_t cw) {
	std::uint32_t out = 0;
	std::uint16_t saved = 0;
	__asm__ volatile("fnstcw %[saved]\n\tfldcw %[cw]\n\tflds %[value]\n\tfstps %[out]\n\tfnclex\n\tfldcw %[saved]"
		: [saved] "=m"(saved), [out] "=m"(out)
		: [cw] "m"(cw), [value] "m"(value)
		: "st");
	return out;
}

// FLD m32 (D9 /0), FSTP m64 (DD /3), as 0x005AD206/0x005AD20E and 0x005AD287/0x005AD28E.
std::uint64_t WidenOnHost(std::uint32_t value, std::uint16_t cw) {
	std::uint64_t out = 0;
	std::uint16_t saved = 0;
	__asm__ volatile("fnstcw %[saved]\n\tfldcw %[cw]\n\tflds %[value]\n\tfstpl %[out]\n\tfnclex\n\tfldcw %[saved]"
		: [saved] "=m"(saved), [out] "=m"(out)
		: [cw] "m"(cw), [value] "m"(value)
		: "st");
	return out;
}

// 0x005AD1AF-0x005AD1D7 byte for byte where the operands are registers:
// FLD1 (D9 E8), FLD m32 sum, FCOM ST1 (D8 D1), FNSTSW AX, FSTP ST1 (DD D9),
// TEST AH,0x41, JNZ; FSUB m64 [1.0] (DC /4) or FLD1; FSUBRP ST1,ST0 (DE E1);
// FCOMP m64 [tolerance] (DC /3), FNSTSW AX, TEST AH,5, and the parity the
// JNP reads. Returns true where the original falls through to the warning.
bool WarnsOnHost(std::uint32_t sum, std::uint16_t cw) {
	const std::uint64_t one = 0x3FF0000000000000ull;        // data.json 0x00A2F928
	const std::uint64_t tolerance = 0x3F847AE140000000ull;  // data.json 0x00A3B150
	std::uint8_t warns = 0;
	std::uint16_t saved = 0;
	__asm__ volatile(
		"fnstcw %[saved]\n\t"
		"fldcw %[cw]\n\t"
		".byte 0xd9, 0xe8\n\t"
		"flds %[sum]\n\t"
		".byte 0xd8, 0xd1\n\t"
		"fnstsw %%ax\n\t"
		".byte 0xdd, 0xd9\n\t"
		"testb $0x41, %%ah\n\t"
		"jnz 1f\n\t"
		"fsubl %[one]\n\t"
		"jmp 2f\n"
		"1:\n\t"
		".byte 0xd9, 0xe8\n\t"
		".byte 0xde, 0xe1\n"
		"2:\n\t"
		"fcompl %[tolerance]\n\t"
		"fnstsw %%ax\n\t"
		"testb $5, %%ah\n\t"
		"setp %[warns]\n\t"
		"fnclex\n\t"
		"fldcw %[saved]\n\t"
		: [saved] "=m"(saved), [warns] "=q"(warns)
		: [cw] "m"(cw), [sum] "m"(sum), [one] "m"(one), [tolerance] "m"(tolerance)
		: "eax", "cc", "st", "st(1)");
	return warns != 0;
}

// xorshift32 with a fixed seed: the same inputs on every run.
struct Random {
	std::uint32_t state = 0x2545F491u;
	std::uint32_t Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
};

// A float with an interesting exponent: near 1, near the denormal edge, near
// the top, or any bits; NaNs of both kinds and infinities come out of the
// last group.
std::uint32_t RandomFloat(Random& random) {
	std::uint32_t bits = random.Next();
	std::uint32_t sign = bits & 0x80000000u;
	std::uint32_t fraction = random.Next() & 0x007FFFFFu;
	switch (random.Next() % 6) {
	case 0: return sign | (static_cast<std::uint32_t>(120 + random.Next() % 10) << 23) | fraction;
	case 1: return sign | (static_cast<std::uint32_t>(random.Next() % 4) << 23) | fraction;
	case 2: return sign | (static_cast<std::uint32_t>(250 + random.Next() % 6) << 23) | fraction;
	case 3: return sign | fraction;  // zero exponent: denormals and zeros
	case 4: return sign | 0x7F800000u | (random.Next() % 3 == 0 ? 0u : fraction);
	default: return bits;
	}
}

const std::array<std::uint32_t, 22> kEdgeFloats{
	kPlusZero, kMinusZero, kOne, 0xBF800000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x00800000u,
	0x7F7FFFFFu, 0xFF7FFFFFu, kPlusInfinity, kMinusInfinity, kQuietNaN, 0xFFC00000u, kSignalingNaN,
	0xFF800001u, 0x7FBFFFFFu, 0x7FFFFFFFu, 0x3F8147AEu, 0x3F7D70A4u, 0x33800000u, 0x4B800000u,
};

// --- Sections -------------------------------------------------------------------------

void TestFormatStrings() {
	Section("Format strings: the exact bytes of 0x00A6C3B8, 0x00A6C368, 0x00A6C358, 0x00A6C344, 0x00A6C330");
	Check(logic::kMissingFormat.address == 0x00A6C3B8u, "Missing at 0x00A6C3B8");
	Check(std::string(logic::kMissingFormat.text) ==
			"Missing [LoadingBar] 'fPercentageOfBar%d' in LoadingMenu::pSectionPercentage.",
		"Missing text, ends in '.' with no line end");
	Check(logic::kMissingFormat.bytes == 78, "Missing is 78 bytes with the NUL (data.json defined.length)");
	Check(logic::kHeaderFormat.address == 0x00A6C368u, "header at 0x00A6C368");
	Check(std::string(logic::kHeaderFormat.text) ==
			"Total value of [LoadingBar] percentages is not equal to 1.0000f in ini file.\r\n",
		"header text ends in CR LF");
	Check(logic::kHeaderFormat.bytes == 79, "header is 79 bytes with the NUL");
	Check(logic::kFoundLineFormat.address == 0x00A6C358u && std::string(logic::kFoundLineFormat.text) == "\t%s - %.04f\r\n" &&
			logic::kFoundLineFormat.bytes == 14,
		"found line: tab, %s - %.04f, CR LF, 14 bytes");
	Check(logic::kNotFoundLineFormat.address == 0x00A6C344u &&
			std::string(logic::kNotFoundLineFormat.text) == "\t%s - NOT FOUND\r\n" && logic::kNotFoundLineFormat.bytes == 18,
		"NOT FOUND line: CR LF, 18 bytes (unreachable without the C7 fault)");
	Check(logic::kTotalFormat.address == 0x00A6C330u && std::string(logic::kTotalFormat.text) == "INI Total = %.04f" &&
			logic::kTotalFormat.bytes == 18,
		"total line: no line end, 18 bytes");
	for (const logic::FormatString* format : {&logic::kMissingFormat, &logic::kHeaderFormat, &logic::kFoundLineFormat,
			 &logic::kNotFoundLineFormat, &logic::kTotalFormat}) {
		Check(std::strlen(format->text) + 1 == format->bytes, "the text's length agrees with its byte count");
	}
}

void TestConstructedFields() {
	Section("Fields: base constructor first, then the vtable and the field values (0x005AD116-0x005AD141)");
	Ports ports;
	ports.fields.vtable = 0x11111111u;
	ports.fields.unknown28 = 5;
	ports.fields.sectionValues = {7u, 7u, 7u, 7u};
	ports.fields.slowStepCap = 5;
	ports.fields.unknown4C = 9u;
	ports.fields.unknown50 = 9u;
	ports.fields.shownStep = 9;
	ports.fields.targetStep = 9;
	ports.fields.fastStepEnabled = 9;
	ports.fields.unknown71 = 9;
	logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
	Check(outcome == logic::Outcome::Constructed, "constructed");
	Check(ports.log.front() == "BaseConstruct(" + std::to_string(kMenu) + ")", "the base constructor runs first, on this");
	Check(ports.vtableSeenByBase == 0x11111111u, "the vtable is written after the base constructor");
	Check(ports.fields.vtable == 0x00A6C40Cu, "+0x00 vtable 0x00A6C40C");
	Check(ports.fields.unknown28 == -1, "+0x28 = -1");
	Check(ports.fields.sectionValues == std::array<std::uint32_t, 4>{0u, 0u, 0u, 0u}, "+0x2C..+0x38 = 0");
	Check(ports.fields.slowStepCap == -1, "+0x3C = -1");
	Check(ports.fields.unknown4C == 0u && ports.fields.unknown50 == 0u, "+0x4C, +0x50 = 0");
	Check(ports.fields.shownStep == 0 && ports.fields.targetStep == 0, "+0x60, +0x64 = 0");
	Check(ports.fields.fastStepEnabled == 0 && ports.fields.unknown71 == 1, "+0x70 = 0, +0x71 = 1");
	Check(BitsOf(ports.fields.screenTimeLeft) == kImageScreenTime, "+0x40 ends with the setting 0x00B14158 (15.0f)");
	Check(ports.fields.clock == kBlock, "+0x54 = the allocated block");
	Check(ports.fields.stepChangedMs == static_cast<std::int32_t>(kClockMs) &&
			ports.fields.lastTickMs == static_cast<std::int32_t>(kClockMs),
		"+0x68 and +0x6C = the clock's clockMs");
}

void TestB1() {
	Section("B1 osGlobals hook (0x005AD156): object zero -> 0x006A9B40 not called");
	{
		Ports ports;
		ports.hookObject = 0;
		logic::RunConstructor(ports, kMenu);
		Check(ports.Has("OsGlobalsHookObject(" + std::to_string(kOsGlobals) + ")"), "[g_osGlobals]+0x24 read");
		Check(ports.CountPrefix("OsGlobalsHook(") == 0, "no hook call");
	}
	Section("B1 osGlobals hook: object nonzero -> 0x006A9B40 with ECX = the object, before the settings loop");
	{
		Ports ports;
		ports.hookObject = kHookObject;
		logic::RunConstructor(ports, kMenu);
		Check(ports.Count("OsGlobalsHook(" + std::to_string(kHookObject) + ")") == 1, "one hook call on the object");
		Check(ports.log[4] == "OsGlobalsHook(" + std::to_string(kHookObject) + ")" && ports.log[5] == "SectionSetting(0)",
			"order: base, fields, OsGlobals, +0x24, hook, then the table");
	}
	Section("Refusal: g_osGlobals null - the original reads [0+0x24] at 0x005AD14A");
	{
		Ports ports;
		ports.osGlobals = 0;
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(outcome == logic::Outcome::FaultNullOsGlobals, "reported, not read");
		Check(ports.log.size() == 3 && ports.log.back() == "OsGlobals", "nothing after the global's read");
		Check(ports.fields.vtable == 0x00A6C40Cu && ports.fields.unknown71 == 1, "the fields before it are written");
	}
}

void TestB2B3() {
	Section("B2 entry present (0x005AD179): the copy and the sum take the setting's value");
	{
		Ports ports;
		logic::RunConstructor(ports, kMenu);
		Check(ports.copies == kImageShares, "0x00B3B3DC[i] = the four image values, bit for bit");
		Check(ports.missingIndices.empty(), "no Missing warning");
		Check(ports.messages.empty(), "image sum is exactly 1.0f (C4): no sum warning");
		Check(ports.Count("SettingValue(" + std::to_string(kSettings[0]) + ")") == 2,
			"the value is read twice, for the copy (0x005AD18B) and the sum (0x005AD194)");
	}
	Section("B2 entry null: Missing format 0x00A6C3B8 with the index, copy slot untouched, sum unchanged");
	{
		Ports ports;
		ports.table[2] = 0;
		SetShares(ports, {0x3E800000u, 0x3F000000u, 0x3F800000u, 0x3E800000u});  // 0.25 + 0.5 + (missing) + 0.25
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(outcome == logic::Outcome::Constructed, "constructed: the other three sum to 1.0, no warning loop");
		Check(ports.missingIndices == std::vector<std::uint32_t>{2u}, "one Missing warning, index 2");
		Check(ports.missingFormats == std::vector<std::uint32_t>{0x00A6C3B8u}, "with the format at 0x00A6C3B8");
		Check(ports.copies[2] == 0xDEADBEEFu, "slot 2 of 0x00B3B3DC untouched");
		Check(ports.messages.empty(), "sum 1.0 without the missing entry: no sum warning");
		Check(ports.fields.sectionValues[2] == 0u, "+0x34 zeroed in the null arm too");
	}
	Section("B3 settings loop (0x005AD1AD): four passes in order, i compared signed");
	{
		Ports ports;
		ports.table = {0u, kSettings[1], 0u, kSettings[3]};
		SetShares(ports, {kOne, 0x3F000000u, kOne, 0x3F000000u});
		logic::RunConstructor(ports, kMenu);
		Check(ports.missingIndices == std::vector<std::uint32_t>{0u, 2u}, "Missing for 0 and 2, in order");
		Check(ports.Count("SectionSetting(0)") == 1 && ports.Count("SectionSetting(3)") == 1 &&
				ports.CountPrefix("SectionSetting(") == 4,
			"each slot read once by the loop (sum within tolerance, no second loop)");
		Check(ports.fields.sectionValues == std::array<std::uint32_t, 4>{0u, 0u, 0u, 0u}, "all four fields zeroed");
		Check(logic::SettingsLoopContinues(3) && !logic::SettingsLoopContinues(4), "3 continues, 4 stops");
		Check(logic::SettingsLoopContinues(-1), "signed: -1 continues (JL)");
	}
}

void TestB4B5() {
	Section("B4 sum side (0x005AD1BE): above 1 subtracts 1.0 from the sum, otherwise the sum from 1");
	Check(logic::SideOf(0x3F800001u) == logic::SumSide::AboveOne, "1 + 2^-23 is above");
	Check(logic::SideOf(kOne) == logic::SumSide::NotAbove, "1.0 itself (C3) takes 1 - sum");
	Check(logic::SideOf(0x3F7FFFFFu) == logic::SumSide::NotAbove, "just below (C0)");
	Check(logic::SideOf(kQuietNaN) == logic::SumSide::NotAbove, "NaN (unordered: C0 C2 C3) takes 1 - sum");
	Check(logic::DifferenceFromOne(0x3F8147AEu) == 1.0099999904632568 - 1.0, "above: sum - 1.0");
	Check(logic::DifferenceFromOne(0x3F7D70A4u) == 1.0 - 0.99000000953674316, "below: 1.0 - sum");
	Check(logic::DifferenceFromOne(0x40400000u) == 2.0, "3.0 -> 2.0");
	Check(logic::DifferenceFromOne(0xC0000000u) == 3.0, "-2.0 -> 3.0");

	Section("B5 tolerance (0x005AD1D7): warn unless the difference is below (double)0.01f; C9 boundary sums");
	Check(!logic::SumWarns(0x3F8147AEu), "1.0099999905 quiet");
	Check(!logic::SumWarns(0x3F7D70A4u), "0.9900000095 quiet");
	Check(logic::SumWarns(0x3F8147AFu), "1.0100001097 warns");
	Check(logic::SumWarns(0x3F7D70A3u), "0.9899999499 warns");
	Check(!logic::SumWarns(kOne), "exactly 1.0 quiet");
	Check(logic::SumWarns(kQuietNaN) && logic::SumWarns(kIndefinite), "NaN warns (unordered, AH&5 = 5)");
	Check(logic::SumWarns(kPlusInfinity) && logic::SumWarns(kMinusInfinity), "infinities warn");
	Check(logic::SumWarns(kPlusZero), "0 warns");
	const double tolerance = std::bit_cast<double>(0x3F847AE140000000ull);
	Check(!logic::WithinTolerance(tolerance), "difference equal to the tolerance warns (C3 alone, even parity)");
	Check(logic::WithinTolerance(std::nextafter(tolerance, 0.0)), "one double below it is quiet");
	Check(!logic::WithinTolerance(std::bit_cast<double>(0x7FF8000000000000ull)), "NaN difference warns");

	Section("B4/B5 against the host x87 running 0x005AD1AF-0x005AD1D7 (24-, 53- and 64-bit precision)");
	Random random;
	int mismatches[3] = {0, 0, 0};
	auto compare = [&](std::uint32_t sum) {
		for (std::size_t p = 0; p < 3; ++p) {
			mismatches[p] += WarnsOnHost(sum, kControlWords[p]) != logic::SumWarns(sum) ? 1 : 0;
		}
	};
	for (std::uint32_t edge : kEdgeFloats) {
		compare(edge);
	}
	for (std::uint32_t sum : {0x3F8147AEu, 0x3F7D70A4u, 0x3F8147AFu, 0x3F7D70A3u, 0x3F000000u, 0x3EFFFFFFu, 0x40000000u,
			 0x40000001u}) {
		compare(sum);
	}
	for (int n = 0; n < 200000; ++n) {
		compare(n % 2 == 0 ? RandomFloat(random) : (0x3F700000u + random.Next() % 0x00200000u));
	}
	for (std::size_t p = 0; p < 3; ++p) {
		std::printf("    %s: %d mismatches\n", kPrecisionNames[p], mismatches[p]);
		Check(mismatches[p] == 0, kPrecisionNames[p]);
	}
	Check(WarnsOnHost(0x3F8147AFu, 0x027F) && !WarnsOnHost(0x3F8147AEu, 0x027F),
		"the 53-bit reading: host gives the C9 boundary as the rebuild does");
}

std::string ExpectedMessage(const std::vector<std::string>& lines, const char* total) {
	std::string message = "Total value of [LoadingBar] percentages is not equal to 1.0000f in ini file.\r\n";
	for (const std::string& line : lines) {
		message += line;
	}
	return message + total;
}

void TestWarning() {
	Section("Sum off: header, one line per entry (B6, B8), the total line, then 0x004A7A60(buffer)");
	{
		Ports ports;
		SetShares(ports, {0x3E800000u, 0x3E800000u, 0x3E800000u, 0x3E99999Au});  // 0.25 0.25 0.25 0.3
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(outcome == logic::Outcome::Constructed, "constructed");
		Check(ports.messages.size() == 1, "one sum warning");
		std::string expected = ExpectedMessage(
			{
				"\tfPercentageOfBar0:LoadingBar - 0.2500\r\n",
				"\tfPercentageOfBar1:LoadingBar - 0.2500\r\n",
				"\tfPercentageOfBar2:LoadingBar - 0.2500\r\n",
				"\tfPercentageOfBar3:LoadingBar - 0.3000\r\n",
			},
			"INI Total = 1.0500");
		Check(!ports.messages.empty() && ports.messages[0] == expected, "the message, CR LF kept, no line end after the total");
		Check(ports.Count("FormatHeader(1024," + std::to_string(0x00A6C368u) + ")") == 1, "header into 0x400 bytes");
		Check(ports.CountPrefix("FormatFoundLine(260," + std::to_string(0x00A6C358u) + ",") == 4, "four lines into 0x104 bytes");
		Check(ports.Count("FormatTotalLine(260," + std::to_string(0x00A6C330u) + ")") == 1, "the total line");
		Check(ports.Count("Concatenate") == 5, "five appends");
		Check(ports.log[ports.log.size() - 8] == "WarnMessage", "the warning before the allocation");
	}
	Section("B6 entry present: name pointer from +4, value re-read from the setting (0x005AD206), not the copy");
	{
		Ports ports;
		SetShares(ports, {0x3F000000u, kSignalingNaN, kPlusZero, kPlusZero});
		ports.onHeader = [](Ports& self) { self.values[kSettings[0]] = 0x3F400000u; };  // 0.5 -> 0.75 after the sum
		logic::RunConstructor(ports, kMenu);
		Check(ports.copies[0] == 0x3F000000u, "the copy keeps 0.5");
		Check(ports.messages.size() == 1 &&
				ports.messages[0].find("\tfPercentageOfBar0:LoadingBar - 0.7500\r\n") != std::string::npos,
			"the line prints the current value 0.75");
		Check(ports.copies[1] == 0x7FC00001u, "an sNaN share is quieted in the copy (FLD m32 / FSTP m32)");
		Check(ports.Has("FormatFoundLine(260," + std::to_string(0x00A6C358u) + "," + std::to_string(kNamePointers[1]) + ")"),
			"the name pointer of setting 1");
	}
	Section("Refusal: B6 entry null in the warning loop - the original reads [0+4] at 0x005AD22B (C7)");
	{
		Ports ports;
		ports.table[1] = 0;
		SetShares(ports, {0x3E800000u, kOne, 0x3E800000u, 0x3E800000u});  // 0.75 without entry 1
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(outcome == logic::Outcome::FaultNullSettingName, "reported, not read");
		Check(ports.missingIndices == std::vector<std::uint32_t>{1u}, "the Missing warning came first");
		Check(ports.CountPrefix("FormatFoundLine(") == 1 && ports.Count("Concatenate") == 1,
			"entry 0's line was appended before the fault");
		Check(ports.log.back() == "SectionSetting(1)", "stops at the null entry's read");
		Check(ports.messages.empty() && !ports.Has("Allocate(24)"), "no message, no allocation");
	}
	Section("Refusal: every entry null - the sum 0 warns and the first warning line faults");
	{
		Ports ports;
		ports.table = {0u, 0u, 0u, 0u};
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(outcome == logic::Outcome::FaultNullSettingName, "fault at index 0");
		Check(ports.missingIndices.size() == 4, "four Missing warnings first");
		Check(ports.CountPrefix("FormatHeader(") == 1 && ports.Count("Concatenate") == 0, "header only");
	}
}

void TestB7B8() {
	Section("B7 inlined strlen (0x005AD250-0x005AD259): stops at the first NUL, bytes >= 0x80 count");
	Check(logic::InlinedLength("") == 0u, "empty: 0");
	Check(logic::InlinedLength("a") == 1u, "one byte");
	Check(logic::InlinedLength("\x80\xff\x01") == 3u, "high bytes are nonzero (TEST CL,CL)");
	Check(logic::InlinedLength("ab\0cd") == 2u, "stops at the NUL");
	Check(logic::AppendBudget(0u) == 0x400u && logic::AppendBudget(78u) == 0x400u - 78u, "budget 0x400 - length");
	Check(logic::AppendBudget(0x401u) == 0xFFFFFFFFu, "32-bit wrap past the buffer (SUB ECX,EAX)");
	{
		Ports ports;
		SetShares(ports, {0x3E800000u, 0x3E800000u, 0x3E800000u, 0x3E99999Au});
		logic::RunConstructor(ports, kMenu);
		bool budgets = ports.budgets.size() == 5;
		for (std::size_t i = 0; budgets && i < 5; ++i) {
			budgets = ports.budgets[i] == 0x400u - ports.lengthsBeforeAppend[i];
		}
		Check(budgets, "each append gets 0x400 - strlen(message) at that moment");
		Check(!ports.lengthsBeforeAppend.empty() && ports.lengthsBeforeAppend[0] == 78u, "first append after the 78-char header");
	}
	Section("B8 warning loop (0x005AD281): cursor 0x00B14120..0x00B1412C, stops at 0x00B14130, signed");
	Check(logic::WarningLoopContinues(0x00B14124u) && logic::WarningLoopContinues(0x00B14130u - 4u), "inside continues");
	Check(!logic::WarningLoopContinues(0x00B14130u) && !logic::WarningLoopContinues(0x00B14134u), "end stops");
	Check(logic::WarningLoopContinues(0x80000000u), "signed JL: an address with the top bit set reads as below");
	{
		Ports ports;
		SetShares(ports, {kOne, kOne, kOne, kOne});
		logic::RunConstructor(ports, kMenu);
		std::vector<std::string> order;
		bool afterHeader = false;
		for (const std::string& line : ports.log) {
			afterHeader = afterHeader || line.rfind("FormatHeader(", 0) == 0;
			if (afterHeader && line.rfind("SectionSetting(", 0) == 0) {
				order.push_back(line);
			}
		}
		Check(order == std::vector<std::string>{"SectionSetting(0)", "SectionSetting(1)", "SectionSetting(2)", "SectionSetting(3)"},
			"four passes, slot order, table re-read (0x005AD200)");
		Check(!ports.messages.empty() && ports.messages[0].find("INI Total = 4.0000") != std::string::npos, "total 4.0");
	}
}

void TestB9() {
	Section("B9 allocation nonzero (0x005AD307): Init(block, tick 1), +0x54, Update(block, tick 2), +0x68/+0x6C");
	{
		Ports ports;
		ports.clockMs = 0x80000001u;
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(outcome == logic::Outcome::Constructed, "constructed");
		std::vector<std::string> tail(ports.log.end() - 7, ports.log.end());
		Check(tail == std::vector<std::string>{"Allocate(24)", "TickCount",
						  "InitClock(" + std::to_string(kBlock) + "," + std::to_string(kFirstTick) + ")", "TickCount",
						  "UpdateClock(" + std::to_string(kBlock) + "," + std::to_string(kSecondTick) + ")",
						  "ReadClockMs(" + std::to_string(kBlock) + ")", "ScreenTimeSetting"},
			"allocate 0x18, two separate ticks, Init then Update, read +0x10, then 0x00B14158");
		Check(ports.fields.stepChangedMs == static_cast<std::int32_t>(0x80000001u) &&
				ports.fields.lastTickMs == static_cast<std::int32_t>(0x80000001u),
			"a 32-bit copy, bit for bit (signedness is the readers')");
	}
	Section("B9 allocation zero: no Init and no first tick, +0x54 = 0, Update(0, tick) still called");
	{
		Ports ports;
		ports.allocation = 0;
		ports.ticks = {kSecondTick};
		ports.fields.clock = 0x55555555u;
		logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
		Check(ports.CountPrefix("InitClock(") == 0 && ports.Count("TickCount") == 1, "one tick only");
		Check(ports.fields.clock == 0u, "+0x54 = 0 (XOR EDI,EDI at 0x005AD315)");
		Check(ports.Has("UpdateClock(0," + std::to_string(kSecondTick) + ")"), "the update is called with ECX = 0 (C14)");
		Check(outcome == logic::Outcome::FaultNullClockRead, "then the read [0+0x10] at 0x005AD32F is reported, not made");
		Check(ports.log.back() == "UpdateClock(0," + std::to_string(kSecondTick) + ")", "nothing after the update");
	}
	Section("Clock prime: +0x54 is re-read after the update (0x005AD32C)");
	{
		Ports ports;
		ports.onUpdate = [](Ports& self) { self.fields.clock = kOtherBlock; };
		logic::RunConstructor(ports, kMenu);
		Check(ports.Has("UpdateClock(" + std::to_string(kBlock) + "," + std::to_string(kSecondTick) + ")"),
			"update on the allocated block (EDI)");
		Check(ports.Has("ReadClockMs(" + std::to_string(kOtherBlock) + ")"), "clockMs read through the re-read field");
	}
	Section("+0x40 := [0x00B14158] through FLD m32 / FSTP m32: an sNaN is quieted");
	{
		Ports ports;
		ports.screenTime = kSignalingNaN;
		logic::RunConstructor(ports, kMenu);
		Check(BitsOf(ports.fields.screenTimeLeft) == 0x7FC00001u, "0x7F800001 -> 0x7FC00001");
	}
}

void TestFloatModel() {
	Section("Sum (C4): the image shares 0.06f + 0.35f + 0.44f + 0.15f in instruction order give exactly 1.0f");
	{
		std::uint32_t sum = kPlusZero;
		for (std::uint32_t share : kImageShares) {
			sum = logic::SumStep(sum, share);
		}
		Check(sum == kOne, "1.0f");
	}
	Section("SumStep: IEEE single addition with the x87's NaN choice");
	Check(logic::SumStep(kPlusZero, kMinusZero) == kPlusZero, "+0 + -0 = +0");
	Check(logic::SumStep(kMinusZero, kMinusZero) == kMinusZero, "-0 + -0 = -0");
	Check(logic::SumStep(kPlusInfinity, kMinusInfinity) == kIndefinite, "inf - inf = the real indefinite");
	Check(logic::SumStep(0x7F7FFFFFu, 0x7F7FFFFFu) == kPlusInfinity, "overflow rounds to +inf");
	Check(logic::SumStep(0x00000001u, 0x00000001u) == 0x00000002u, "denormals add exactly");
	Check(logic::SumStep(kOne, 0x33800000u) == kOne, "1 + 2^-24: the tie rounds to even");
	Check(logic::SumStep(kOne, 0x33800001u) == 0x3F800001u, "1 + just above half an ulp rounds up");
	Check(logic::SumStep(kPlusZero, kSignalingNaN) == 0x7FC00001u, "the loaded sNaN is quieted");

	Section("SumStep, LoadStore and WidenToDouble against the host x87 (24-, 53- and 64-bit precision)");
	Random random;
	int sumMismatches[3] = {0, 0, 0};
	int copyMismatches[3] = {0, 0, 0};
	int widenMismatches[3] = {0, 0, 0};
	auto compare = [&](std::uint32_t sum, std::uint32_t value) {
		for (std::size_t p = 0; p < 3; ++p) {
			sumMismatches[p] += SumStepOnHost(sum, value, kControlWords[p]) != logic::SumStep(sum, value) ? 1 : 0;
			copyMismatches[p] += LoadStoreOnHost(value, kControlWords[p]) != logic::LoadStore(value) ? 1 : 0;
			widenMismatches[p] += WidenOnHost(value, kControlWords[p]) != logic::WidenToDouble(value) ? 1 : 0;
		}
	};
	for (std::uint32_t a : kEdgeFloats) {
		for (std::uint32_t b : kEdgeFloats) {
			compare(a, b);
		}
	}
	for (int n = 0; n < 300000; ++n) {
		std::uint32_t a = RandomFloat(random);
		std::uint32_t b = RandomFloat(random);
		if (n % 3 == 0) {
			// Same binade, opposite signs: cancellation.
			b = (a ^ 0x80000000u) + (random.Next() % 64) - 32;
		} else if (n % 3 == 1) {
			// Exponent gaps around the rounding horizon.
			std::uint32_t exponent = (a >> 23) & 0xFFu;
			std::uint32_t gap = random.Next() % 30;
			if (exponent > gap && exponent < 255) {
				b = (random.Next() & 0x80000000u) | ((exponent - gap) << 23) | (random.Next() & 0x007FFFFFu);
			}
		}
		compare(a, b);
	}
	for (std::size_t p = 0; p < 3; ++p) {
		std::printf("    %s: sum %d, copy %d, widen %d mismatches\n", kPrecisionNames[p], sumMismatches[p], copyMismatches[p],
			widenMismatches[p]);
		Check(sumMismatches[p] == 0 && copyMismatches[p] == 0 && widenMismatches[p] == 0, kPrecisionNames[p]);
	}
}

void TestOnLayout() {
	Section("The same run on the engine layout: the fields land at their offsets");
	FakePorts<engine::LoadingMenuLayout> ports;
	std::memset(&ports.fields, 0xCC, sizeof(ports.fields));
	ports.hookObject = kHookObject;
	logic::Outcome outcome = logic::RunConstructor(ports, kMenu);
	Check(outcome == logic::Outcome::Constructed, "constructed");
	const auto* bytes = reinterpret_cast<const std::uint8_t*>(&ports.fields);
	auto dword = [&](std::size_t offset) {
		std::uint32_t value = 0;
		std::memcpy(&value, bytes + offset, 4);
		return value;
	};
	Check(dword(0x00) == 0x00A6C40Cu, "+0x00 vtable");
	Check(dword(0x28) == 0xFFFFFFFFu && dword(0x3C) == 0xFFFFFFFFu, "+0x28, +0x3C = -1");
	Check(dword(0x2C) == 0u && dword(0x30) == 0u && dword(0x34) == 0u && dword(0x38) == 0u, "+0x2C..+0x38 = 0");
	Check(dword(0x40) == kImageScreenTime, "+0x40 = 15.0f");
	Check(dword(0x44) == 0xCCCCCCCCu && dword(0x48) == 0xCCCCCCCCu, "+0x44..+0x4B untouched");
	Check(dword(0x4C) == 0u && dword(0x50) == 0u, "+0x4C, +0x50 = 0");
	Check(dword(0x54) == kBlock, "+0x54 = the block");
	Check(dword(0x58) == 0xCCCCCCCCu && dword(0x5C) == 0xCCCCCCCCu, "+0x58..+0x5F untouched");
	Check(dword(0x60) == 0u && dword(0x64) == 0u, "+0x60, +0x64 = 0");
	Check(dword(0x68) == kClockMs && dword(0x6C) == kClockMs, "+0x68, +0x6C = clockMs");
	Check(bytes[0x70] == 0 && bytes[0x71] == 1, "+0x70 = 0, +0x71 = 1");
	Check(dword(0x04) == 0xCCCCCCCCu, "+0x04 (base) untouched by the rebuild");
}

} // namespace

int main() {
	TestFormatStrings();
	TestConstructedFields();
	TestB1();
	TestB2B3();
	TestB4B5();
	TestWarning();
	TestB7B8();
	TestB9();
	TestFloatModel();
	TestOnLayout();
	return Failures();
}
