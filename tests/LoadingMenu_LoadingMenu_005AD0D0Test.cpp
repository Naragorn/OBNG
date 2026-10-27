// Tests of the rebuild of 0x005AD0D0 (proposed LoadingMenu::LoadingMenu), the
// loading menu's constructor: it reads the four [LoadingBar] fPercentageOfBar
// settings, warns when they do not sum to 1.0, and creates and primes the
// menu's private TimeInfo clock at this+0x54.
//
// One section per flow of the mapped control flow (re/functions/0x005AD0D0.md):
//   1  osGlobals hook            0x005AD156 JZ
//   2  setting present           0x005AD179 JNZ
//   3  settings loop             0x005AD1AD JL
//   4  sum side                  0x005AD1BE TEST AH,0x41; JNZ
//   5  sum within tolerance      0x005AD1D7 TEST AH,5; JNP
//   6  warning line per setting  0x005AD204 JZ (the NOT FOUND path faults, C9)
//   7  inlined strlen            0x005AD257, 0x005AD2B9 JNZ
//   8  warning loop              0x005AD281 JL
//   9  allocation                0x005AD307 JZ
//   -  clock prime (unconditional) 0x005AD322-0x005AD327 (null clock: C8)
// plus the x87 handling of the float values (sNaN quieting, NaN choice in the
// sum), the legacy MSVC %.04f text, the read timing of the inputs, and the
// refusal paths (the null name read at 0x005AD22B, the null clock read at
// 0x005AD32F, an unterminated setting line).
//
// The two sum-check constants 0x00A2F928 and 0x00A3B150 are not in the
// export (unverified values). The tests pass them explicitly: 1.0 and 1e-4 as
// the expected reading, and a reference of 0.75 where a test must tell the
// constant apart from the FLD1 of the other side.
//
// Expected warning texts are written out byte for byte, not built from the
// logic's constants, so they pin meta.json's format strings 0x00A6C368,
// 0x00A6C358 and 0x00A6C330 (line end "\n" per meta.json; unverified, see the
// logic header).

#include "Check.h"
#include "engine/time/LoadingMenu_LoadingMenu_005AD0D0.h"
#include "engine/time/LoadingMenu_LoadingMenu_005AD0D0Logic.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace logic = obng::engine::time::loading_menu_ctor;
namespace engine = obng::engine::time::loading_menu_ctor::engine;

namespace {

constexpr logic::SumCheckConstants kExpectedConstants{1.0, 1e-4};
constexpr std::uint32_t kHookObject = 0x0BADF00Du;
constexpr std::uint32_t kClockBlock = 0x12345670u;
constexpr std::uint32_t kFirstTick = 1000u;
constexpr std::uint32_t kSecondTick = 1016u;
constexpr std::uint32_t kPrimedLastMs = 16u;
constexpr std::uint32_t kInitialCountdownBits = 0x40200000u;  // 2.5f

constexpr const char* kName0 = "fPercentageOfBar0:LoadingBar";
constexpr const char* kName1 = "fPercentageOfBar1:LoadingBar";
constexpr const char* kName2 = "fPercentageOfBar2:LoadingBar";
constexpr const char* kName3 = "fPercentageOfBar3:LoadingBar";
constexpr const char* kHeader = "Total value of [LoadingBar] percentages is not equal to 1.0000f in ini file.\n";

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

std::uint64_t Bits(double value) {
	return std::bit_cast<std::uint64_t>(value);
}

// Values with NaN bits are kept as bit patterns throughout: on the 32-bit
// build a float or double return value passes x87 ST0, which quiets a
// signaling NaN on its own (measured), so no helper here returns a float.
std::string Fixed(double value, int precision) {
	return logic::LegacyFixed(Bits(value), precision);
}

enum class Event {
	ConstructBase,
	ReadOsGlobalsHookObject,
	CallOsGlobalsHook,
	ReadSetting,
	LogMissingSection,
	StoreSectionPercentage,
	LogText,
	Allocate,
	TickCount,
	ConstructClock,
	UpdateClock,
	ReadClockLastMs,
	ReadInitialCountdown,
};

using Slots = std::array<logic::SettingSlot, logic::kSectionCount>;

Slots QuarterSlots() {
	return Slots{
		logic::SettingView{Bits(0.25f), kName0},
		logic::SettingView{Bits(0.25f), kName1},
		logic::SettingView{Bits(0.25f), kName2},
		logic::SettingView{Bits(0.25f), kName3},
	};
}

// A write a callee makes to an input, to test when the rebuild reads it.
struct SettingRewrite {
	int index;
	logic::SettingSlot slot;
};

// Records every call the rebuild makes, in order, with its arguments, and
// serves the engine's inputs from plain values.
struct FakePorts {
	// Inputs.
	std::uint32_t hookObjectSource = 0;
	Slots settings = QuarterSlots();
	std::uint32_t countdownSource = kInitialCountdownBits;
	std::uint32_t allocationResult = kClockBlock;
	std::vector<std::uint32_t> ticks{kFirstTick, kSecondTick};
	std::uint32_t clockMs = kPrimedLastMs;

	// Writes the callees make, applied when they run.
	std::optional<std::uint32_t> hookObjectWrittenByBase;
	std::optional<SettingRewrite> settingWrittenByHook;
	std::optional<SettingRewrite> settingWrittenByMissingLog;
	std::optional<std::uint32_t> countdownWrittenByUpdate;

	// Record.
	std::vector<Event> events;
	std::uint32_t hookObject = 0;
	std::vector<int> readIndices;
	std::vector<int> missingIndices;
	std::vector<std::pair<int, std::uint32_t>> storedPercentageBits;
	std::vector<std::string> loggedTexts;
	std::uint32_t allocationSize = 0;
	std::size_t nextTick = 0;
	std::optional<std::pair<std::uint32_t, std::uint32_t>> constructed;  // (clock, tick)
	std::optional<std::pair<std::uint32_t, std::uint32_t>> updated;      // (clock, tick)

	void ConstructBase() {
		events.push_back(Event::ConstructBase);
		if (hookObjectWrittenByBase.has_value()) {
			hookObjectSource = *hookObjectWrittenByBase;
		}
	}

	std::uint32_t ReadOsGlobalsHookObject() {
		events.push_back(Event::ReadOsGlobalsHookObject);
		return hookObjectSource;
	}

	void CallOsGlobalsHook(std::uint32_t object) {
		events.push_back(Event::CallOsGlobalsHook);
		hookObject = object;
		Apply(settingWrittenByHook);
	}

	logic::SettingSlot ReadSetting(int index) {
		events.push_back(Event::ReadSetting);
		readIndices.push_back(index);
		return settings.at(static_cast<std::size_t>(index));
	}

	void LogMissingSection(int index) {
		events.push_back(Event::LogMissingSection);
		missingIndices.push_back(index);
		Apply(settingWrittenByMissingLog);
	}

	void StoreSectionPercentage(int index, std::uint32_t valueBits) {
		events.push_back(Event::StoreSectionPercentage);
		storedPercentageBits.emplace_back(index, valueBits);
	}

	void LogText(const std::string& text) {
		events.push_back(Event::LogText);
		loggedTexts.push_back(text);
	}

	std::uint32_t Allocate(std::uint32_t size) {
		events.push_back(Event::Allocate);
		allocationSize = size;
		return allocationResult;
	}

	std::uint32_t TickCount() {
		events.push_back(Event::TickCount);
		return ticks.at(nextTick++);
	}

	void ConstructClock(std::uint32_t clock, std::uint32_t tick) {
		events.push_back(Event::ConstructClock);
		constructed = std::make_pair(clock, tick);
	}

	void UpdateClock(std::uint32_t clock, std::uint32_t tick) {
		events.push_back(Event::UpdateClock);
		updated = std::make_pair(clock, tick);
		if (countdownWrittenByUpdate.has_value()) {
			countdownSource = *countdownWrittenByUpdate;
		}
	}

	std::uint32_t ReadClockLastMs(std::uint32_t /*clock*/) {
		events.push_back(Event::ReadClockLastMs);
		return clockMs;
	}

	std::uint32_t ReadInitialCountdown() {
		events.push_back(Event::ReadInitialCountdown);
		return countdownSource;
	}

	int Count(Event event) const {
		int count = 0;
		for (Event recorded : events) {
			count += recorded == event ? 1 : 0;
		}
		return count;
	}

	std::size_t IndexOf(Event event, int occurrence = 0) const {
		for (std::size_t i = 0; i < events.size(); ++i) {
			if (events[i] == event && occurrence-- == 0) {
				return i;
			}
		}
		return events.size();
	}

private:
	void Apply(const std::optional<SettingRewrite>& rewrite) {
		if (rewrite.has_value()) {
			settings.at(static_cast<std::size_t>(rewrite->index)) = rewrite->slot;
		}
	}
};

FakePorts MakePorts(const Slots& slots, std::uint32_t hookObject = 0) {
	FakePorts ports;
	ports.settings = slots;
	ports.hookObjectSource = hookObject;
	return ports;
}

logic::Outcome Run(FakePorts& ports, logic::MenuFields& fields) {
	return logic::RunConstructor(ports, kExpectedConstants, fields);
}

// Fields filled with values the constructor must overwrite.
logic::MenuFields GarbageFields() {
	logic::MenuFields fields{};
	fields.vtable = 0xDDDDDDDDu;
	fields.unknown28 = 0x11;
	fields.sectionValues = {7.0f, 7.0f, 7.0f, 7.0f};
	fields.stepLimit = 0x22;
	fields.countdownBits = Bits(9.0f);
	fields.unknown4C = 0x33;
	fields.unknown50 = 0x44;
	fields.clock = 0x55;
	fields.shownPercent = 0x66;
	fields.step = 0x77;
	fields.lastStepMs = 0x88;
	fields.startMs = 0x99;
	fields.flag70 = true;
	fields.flag71 = false;
	return fields;
}

logic::WarningResult BuildFrom(const Slots& slots, std::uint32_t sumBits) {
	return logic::BuildSumWarning([&slots](int index) { return slots.at(static_cast<std::size_t>(index)); }, sumBits);
}

void OsGlobalsHook() {
	Section("Flow 1: [g_osGlobals 0x00B33398]+0x24 nonzero - 0x006A9B40 runs with it as ECX");
	{
		FakePorts ports = MakePorts(QuarterSlots(), kHookObject);
		logic::MenuFields fields = GarbageFields();
		Check(Run(ports, fields) == logic::Outcome::Completed, "completes");
		Check(ports.Count(Event::CallOsGlobalsHook) == 1, "the hook is called once (0x005AD158)");
		Check(ports.hookObject == kHookObject, "ECX is the value read at 0x005AD14A");
		Check(ports.events.size() >= 3 && ports.events[0] == Event::ConstructBase &&
				ports.events[1] == Event::ReadOsGlobalsHookObject && ports.events[2] == Event::CallOsGlobalsHook,
			"base ctor 0x00584640 (0x005AD116), then the read 0x005AD145, then the hook");
	}
	Section("Flow 1: +0x24 zero - the call is skipped (JZ 0x005AD15D)");
	{
		FakePorts ports = MakePorts(QuarterSlots(), 0);
		logic::MenuFields fields = GarbageFields();
		Check(Run(ports, fields) == logic::Outcome::Completed, "completes");
		Check(ports.Count(Event::CallOsGlobalsHook) == 0, "no hook call");
	}
	Check(logic::ShouldCallOsGlobalsHook(1u), "decision: nonzero calls");
	Check(!logic::ShouldCallOsGlobalsHook(0u), "decision: zero skips");
}

void SettingPresent() {
	Section("Flow 2: entry present - value copied to 0x00B3B3DC[i] and added to the sum");
	logic::SectionStep step = logic::ScanSection(logic::SettingView{Bits(0.25f), kName0}, Bits(0.5f));
	Check(step.copyBits == Bits(0.25f) && step.sumBits == Bits(0.75f), "copy 0.25, sum 0.5 + 0.25");

	Section("Flow 2: entry zero - 'Missing' warning with the index, no copy, sum unchanged");
	step = logic::ScanSection(std::nullopt, Bits(0.5f));
	Check(!step.copyBits.has_value() && step.sumBits == Bits(0.5f), "no copy, sum unchanged");

	Slots slots = QuarterSlots();
	slots[2] = std::nullopt;
	slots[1] = logic::SettingView{Bits(0.5f), kName1};
	FakePorts ports = MakePorts(slots);
	logic::MenuFields fields = GarbageFields();
	Check(Run(ports, fields) == logic::Outcome::Completed, "run with entry 2 missing and sum 1.0 completes");
	Check(ports.missingIndices == std::vector<int>{2}, "0x004A7A60 gets the index as argument (C10: PUSH EDI at 0x005AD17B)");
	Check(ports.storedPercentageBits ==
			std::vector<std::pair<int, std::uint32_t>>{{0, Bits(0.25f)}, {1, Bits(0.5f)}, {3, Bits(0.25f)}},
		"copies for 0, 1, 3 only, in loop order");
}

void SettingsLoop() {
	Section("Flow 3: four passes, each reads entry i, then zeroes this+0x2C+4*i (present or missing)");
	Slots slots = QuarterSlots();
	slots[0] = std::nullopt;
	slots[3] = std::nullopt;
	slots[1] = logic::SettingView{Bits(0.5f), kName1};
	slots[2] = logic::SettingView{Bits(0.5f), kName2};
	FakePorts ports = MakePorts(slots);
	logic::MenuFields fields = GarbageFields();
	Check(Run(ports, fields) == logic::Outcome::Completed, "sum 1.0: completes without a warning");
	Check(fields.sectionValues == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}, "+0x2C..+0x38 are all zero");
	Check(ports.Count(Event::LogMissingSection) + ports.Count(Event::StoreSectionPercentage) == 4,
		"exactly four passes: one log or one copy per entry");
	Check(ports.missingIndices == std::vector<int>{0, 3}, "missing entries reported in order 0, 3");
	Check(ports.readIndices == std::vector<int>{0, 1, 2, 3}, "entries read once each, in order, no warning reads");
}

void X87Values() {
	Section("x87: FLD float / FSTP float quiets a signaling NaN, nothing else changes (measured)");
	Check(logic::X87FloatThroughStack(0x7F800001u) == 0x7FC00001u, "0x7F800001 -> 0x7FC00001");
	Check(logic::X87FloatThroughStack(0xFF800003u) == 0xFFC00003u, "0xFF800003 -> 0xFFC00003");
	Check(logic::X87FloatThroughStack(0x7FC00000u) == 0x7FC00000u, "a quiet NaN unchanged");
	Check(logic::X87FloatThroughStack(0x7F800000u) == 0x7F800000u, "infinity unchanged");
	Check(logic::X87FloatThroughStack(Bits(0.25f)) == Bits(0.25f), "an ordinary value unchanged");

	Section("x87: FADD NaN choice (measured, value in ST0, sum in memory)");
	struct AddCase {
		std::uint32_t value;
		std::uint32_t sum;
		std::uint32_t expected;
		const char* what;
	};
	const std::array<AddCase, 9> cases{{
		{0xFFC00000u, 0x7FC00005u, 0x7FC00005u, "two NaNs: the larger fraction (sum)"},
		{0x7FC00005u, 0xFFC00000u, 0x7FC00005u, "two NaNs: the larger fraction (value)"},
		{0x7FC00001u, 0xFFC00001u, 0x7FC00001u, "equal fractions: the positive one (value)"},
		{0xFFC00001u, 0x7FC00001u, 0x7FC00001u, "equal fractions: the positive one (sum)"},
		{0x7F800001u, 0x7FC00000u, 0x7FC00001u, "sNaN and qNaN: compared after quieting"},
		{0x7F800005u, 0x7FC00000u, 0x7FC00005u, "sNaN with the larger fraction wins, quieted"},
		{0x7F800001u, 0x3F800000u, 0x7FC00001u, "sNaN value and an ordinary sum: the quieted sNaN"},
		{0x3F800000u, 0x7FC00002u, 0x7FC00002u, "ordinary value and a NaN sum: the NaN"},
		{0x7F800000u, 0xFF800000u, 0xFFC00000u, "inf + -inf: the indefinite"},
	}};
	for (const AddCase& entry : cases) {
		Check(logic::X87AddFloat(entry.value, entry.sum) == entry.expected, entry.what);
	}
	Check(logic::X87AddFloat(Bits(0.25f), Bits(0.5f)) == Bits(0.75f), "ordered operands: the float sum");
	Check(logic::X87AddFloat(Bits(1e-8f), Bits(1.0f)) == Bits(1.0f), "rounded to float after the add");

	Section("x87: FLD float / FSTP double of the %.04f arguments (measured)");
	Check(logic::X87FloatToDouble(0x7F800001u) == 0x7FF8000020000000ull, "sNaN 0x7F800001 -> 0x7FF8000020000000");
	Check(logic::X87FloatToDouble(0xFFC00000u) == 0xFFF8000000000000ull, "indefinite -> 0xFFF8000000000000");
	Check(logic::X87FloatToDouble(0xFF800003u) == 0xFFF8000060000000ull, "negative sNaN keeps sign and payload, quieted");
	Check(logic::X87FloatToDouble(Bits(0.15625f)) == Bits(0.15625), "an ordinary value is exact");

	Section("x87 in the run: a signaling NaN setting and countdown are stored quieted");
	Slots slots = QuarterSlots();
	slots[2] = logic::SettingView{0x7F800001u, kName2};
	FakePorts ports = MakePorts(slots);
	ports.countdownSource = 0x7F800001u;
	logic::MenuFields fields = GarbageFields();
	Check(Run(ports, fields) == logic::Outcome::Completed, "completes (NaN sum warns, the text is logged)");
	Check(ports.storedPercentageBits.size() == 4 && ports.storedPercentageBits[2].second == 0x7FC00001u,
		"0x00B3B3DC[2] gets 0x7FC00001");
	Check(fields.countdownBits == 0x7FC00001u, "+0x40 gets 0x7FC00001 from 0x00B14158");
	Check(ports.loggedTexts.size() == 1 &&
			ports.loggedTexts[0].find("\tfPercentageOfBar2:LoadingBar - 1.#QNB\n") != std::string::npos &&
			ports.loggedTexts[0].ends_with("INI Total = 1.#QNB"),
		"the warning prints the quieted NaN as legacy 1.#QNB, line and total");
}

void LegacyFixed() {
	Section("Legacy %f: special values as the native CRT prints them (Wine's native test vectors)");
	constexpr std::uint64_t indefinite = 0xFFF8000000000000ull;
	constexpr std::uint64_t quietNaN = 0x7FF8000000000000ull;
	constexpr std::uint64_t infinity = 0x7FF0000000000000ull;
	constexpr std::uint64_t negativeInfinity = 0xFFF0000000000000ull;
	Check(logic::LegacyFixed(indefinite, 6) == "-1.#IND00", "%f of the indefinite: -1.#IND00");
	Check(logic::LegacyFixed(quietNaN, 6) == "1.#QNAN0", "%f of NAN: 1.#QNAN0");
	Check(logic::LegacyFixed(infinity, 6) == "1.#INF00", "%f of INFINITY: 1.#INF00");
	Check(logic::LegacyFixed(indefinite, 2) == "-1.#J", "%.2f of the indefinite: -1.#J (rounded on 'N')");
	Check(logic::LegacyFixed(quietNaN, 2) == "1.#R", "%.2f of NAN: 1.#R");
	Check(logic::LegacyFixed(infinity, 2) == "1.#J", "%.2f of INFINITY: 1.#J");
	Check(Fixed(-1.5, 0) == "-2", "%.0f of -1.5: -2 (half away, not to even)");
	Check(Fixed(-0.5, 0) == "-1", "%.0f of -0.5: -1");

	Section("Legacy %.04f: the values this body can print");
	Check(logic::LegacyFixed(infinity, 4) == "1.#INF", "infinity: 1.#INF");
	Check(logic::LegacyFixed(negativeInfinity, 4) == "-1.#INF", "-infinity: -1.#INF");
	Check(logic::LegacyFixed(indefinite, 4) == "-1.#IND", "indefinite: -1.#IND");
	Check(logic::LegacyFixed(quietNaN, 4) == "1.#QNB", "quiet NaN: 1.#QNA rounded on 'N' to 1.#QNB");
	Check(logic::LegacyFixed(0xFFF8000020000000ull, 4) == "-1.#QNB",
		"negative quiet NaN with a payload: -1.#QNB, not IND (derived)");
	Check(logic::LegacyFixed(0x7FF0000000000001ull, 4) == "1.#SNB", "signaling NaN: 1.#SNB");
	Check(Fixed(0.15625, 4) == "0.1563", "exact tie 0.15625 rounds up: 0.1563 (glibc: 0.1562)");
	Check(Fixed(0.90625, 4) == "0.9063", "exact tie 0.90625 rounds up: 0.9063");
	Check(Fixed(0.45f, 4) == "0.4500", "0.45f (0.44999998...) rounds to 0.4500");
	Check(Fixed(0.25, 4) == "0.2500", "0.25: zero padded");
	Check(Fixed(-0.25, 4) == "-0.2500", "negative: sign first");
	Check(Fixed(0.0, 4) == "0.0000", "zero");
	Check(Fixed(0.99996, 4) == "1.0000", "carry into a new integer digit");
	Check(Fixed(9.99996, 4) == "10.0000", "carry through the integer digits");
	Check(Fixed(0.00006, 4) == "0.0001", "rounding digit at the point's position: 0.0001");
	Check(Fixed(0.00004, 4) == "0.0000", "below half of the last place: 0.0000");
	Check(Fixed(1e-10, 4) == "0.0000", "the digits start past the precision: 0.0000");
	Check(Fixed(std::numeric_limits<double>::denorm_min(), 4) == "0.0000", "a denormal: 0.0000");
	Check(Fixed(1.2f, 4) == "1.2000", "1.2f");
	Check(Fixed(1e30f, 4) == "1000000015047466200000000000000.0000",
		"17 significant digits, then zeros (exact: 1000000015047466219876688855040)");
	Check(Fixed(123456789012345678901.0, 4) == "123456789012345680000.0000",
		"Wine native _fcvt vector: 12345678901234568 then zeros");
	Check(Fixed(1e98, 4) == "1" + std::string(98, '0') + ".0000",
		"the double 1e98 lies just below 10^98 (99999999999999999|8...): the 17-digit rounding carries into a new digit");
	Check(Fixed(std::numeric_limits<float>::max(), 4).size() == 44,
		"FLT_MAX: 39 integer digits, the point, 4 digits");
	Check(Fixed(-std::numeric_limits<float>::max(), 4).size() == logic::kMaxFixed4Length,
		"-FLT_MAX is the longest float text, kMaxFixed4Length");
}

void SumSide() {
	Section("Flow 4: sum <= 1.0 or unordered takes 1.0 - sum; sum > 1.0 takes sum - [0x00A2F928]");
	Check(logic::SideOf(0.5f) == logic::SumSide::AtOrBelowOne, "0.5: C0 set");
	Check(logic::SideOf(1.0f) == logic::SumSide::AtOrBelowOne, "1.0: C3 set");
	Check(logic::SideOf(1.5f) == logic::SumSide::AboveOne, "1.5: neither C0 nor C3");
	Check(logic::SideOf(std::numeric_limits<float>::quiet_NaN()) == logic::SumSide::AtOrBelowOne,
		"NaN: unordered sets C0 and C3 -> the 1.0 - sum side");
	constexpr logic::SumCheckConstants reference075{0.75, 1e-4};
	Check(logic::SumDeviation(0.5f, reference075) == 0.5, "below: FLD1; FSUBRP gives 1.0 - 0.5, the constant unused");
	Check(logic::SumDeviation(1.5f, reference075) == 0.75, "above: 1.5 - [0x00A2F928] (0.75 here), not 1.0");
	Check(logic::SumDeviation(1.5f, kExpectedConstants) == 0.5, "above with the expected 1.0: 0.5");
}

void SumWithinTolerance() {
	Section("Flow 5: deviation < tolerance skips the warning; >=, or NaN, builds it");
	Check(logic::WithinTolerance(0.00005, 1e-4), "below tolerance: C0 only, odd parity, JNP taken");
	Check(!logic::WithinTolerance(1e-4, 1e-4), "equal: C3 only, masked to 0, even parity -> warn");
	Check(!logic::WithinTolerance(0.5, 1e-4), "above: nothing set -> warn");
	Check(!logic::WithinTolerance(std::numeric_limits<double>::quiet_NaN(), 1e-4), "NaN: C0 and C2 -> even parity -> warn");
	Check(!logic::NeedsSumWarning(1.0f, kExpectedConstants), "sum exactly 1.0: no warning");
	Check(logic::NeedsSumWarning(0.9f, kExpectedConstants), "sum 0.9: warning");
	Check(logic::NeedsSumWarning(1.2f, kExpectedConstants), "sum 1.2: warning");

	FakePorts within = MakePorts(QuarterSlots());
	logic::MenuFields fields = GarbageFields();
	static_cast<void>(Run(within, fields));
	Check(within.Count(Event::LogText) == 0, "run with sum 1.0: 0x004A7A60 is not called with a text");

	Slots off = QuarterSlots();
	off[1] = logic::SettingView{Bits(0.45f), kName1};
	FakePorts outside = MakePorts(off);
	fields = GarbageFields();
	Check(Run(outside, fields) == logic::Outcome::Completed, "run with sum 1.2: completes");
	Check(outside.Count(Event::LogText) == 1, "run with sum 1.2: one warning text");
}

void WarningLinePerSetting() {
	Section("Flow 6: the format strings' literal parts are meta.json's bytes");
	Check(std::string(logic::kLineIndent) + "%s" + logic::kLineSeparator + "%.04f" + logic::kLineEnd == "\t%s - %.04f\n",
		"0x00A6C358 \"\\t%s - %.04f\\n\"");
	Check(std::string(logic::kTotalPrefix) + "%.04f" == "INI Total = %.04f", "0x00A6C330 \"INI Total = %.04f\"");
	Check(std::string(logic::kSumHeader) == kHeader, "0x00A6C368, the header");
	Check(logic::kValuePrecision == 4, "%.04f: four fraction digits");

	Section("Flow 6: entry present - '\\t%s - %.04f' with the name at +0x04 and the value at +0x00");
	logic::LineResult line = logic::FormatSettingLine(logic::SettingView{Bits(0.25f), kName0});
	const std::string* text = std::get_if<std::string>(&line);
	Check(text != nullptr && *text == "\tfPercentageOfBar0:LoadingBar - 0.2500\n", "the line text");
	line = logic::FormatSettingLine(logic::SettingView{Bits(0.15625f), kName0});
	text = std::get_if<std::string>(&line);
	Check(text != nullptr && *text == "\tfPercentageOfBar0:LoadingBar - 0.1563\n", "an exact tie rounds up as the legacy CRT does");

	Section("Flow 6: entry zero - the NOT FOUND path reads [0+4] (C9): refusal, not a line");
	line = logic::FormatSettingLine(std::nullopt);
	Check(std::holds_alternative<logic::NullNameRead>(line), "reported as the null read at 0x005AD22B");

	Section("Flow 6 refusal: a line of 0x104 bytes or more is stored unterminated by _snprintf");
	// The line is 1 + name + 3 + 6 ("0.2500") + 1 bytes.
	std::string name248(248, 'x');
	line = logic::FormatSettingLine(logic::SettingView{Bits(0.25f), name248});
	text = std::get_if<std::string>(&line);
	Check(text != nullptr && text->size() == 0x103, "0x103 bytes fit with their terminator");
	std::string name249(249, 'x');
	line = logic::FormatSettingLine(logic::SettingView{Bits(0.25f), name249});
	Check(std::holds_alternative<logic::UnterminatedLine>(line), "0x104 bytes: refused, __mbsnbcat would read undefined bytes");
	logic::SnprintfStore store = logic::MsvcSnprintfStore(std::string(0x105, 'y'), logic::kLineCapacity);
	Check(!store.terminated && store.stored.size() == 0x104, "longer: _snprintf stores 0x104 bytes, no terminator");

	Section("Flow 6 in the run: missing entry, rest sums to 1.0 - no warning, no fault");
	Slots slots = QuarterSlots();
	slots[3] = std::nullopt;
	slots[2] = logic::SettingView{Bits(0.5f), kName2};
	FakePorts quiet = MakePorts(slots);
	logic::MenuFields fields = GarbageFields();
	Check(Run(quiet, fields) == logic::Outcome::Completed, "completes");
	Check(quiet.Count(Event::LogText) == 0, "no warning text");

	Section("Flow 6 in the run: missing entry and the sum off - the warning faults at 0x005AD22B");
	slots = QuarterSlots();
	slots[3] = std::nullopt;
	FakePorts faulting = MakePorts(slots);
	fields = GarbageFields();
	Check(Run(faulting, fields) == logic::Outcome::FaultNullSettingName, "outcome: the null name read");
	Check(faulting.Count(Event::LogText) == 0, "the text is never handed to 0x004A7A60");
	Check(faulting.Count(Event::Allocate) == 0, "the clock is never allocated");
	Check(fields.clock == 0x55u, "+0x54 is not written");

	Section("Flow 6 refusal in the run: a long name and the sum off - UnterminatedWarningLine");
	slots = QuarterSlots();
	slots[1] = logic::SettingView{Bits(0.45f), std::string_view(name249)};
	logic::WarningResult warning = BuildFrom(slots, Bits(1.2f));
	const logic::WarningFault* fault = std::get_if<logic::WarningFault>(&warning);
	Check(fault != nullptr && fault->kind == logic::WarningFaultKind::UnterminatedLine && fault->index == 1,
		"BuildSumWarning: unterminated line at entry 1");
	FakePorts refused = MakePorts(slots);
	fields = GarbageFields();
	Check(Run(refused, fields) == logic::Outcome::UnterminatedWarningLine, "outcome: UnterminatedWarningLine");
	Check(refused.Count(Event::LogText) == 0, "no text is logged");
	Check(refused.Count(Event::Allocate) == 0, "the clock is never allocated");
	Check(fields.clock == 0x55u && fields.lastStepMs == 0x88u && fields.startMs == 0x99u && fields.countdownBits == 0u,
		"+0x54, +0x68, +0x6C keep their values, +0x40 keeps the 0.0 from 0x005AD12B");
}

void InlinedStrlen() {
	Section("Flow 7: the inlined strlen stops at the first zero byte");
	Check(logic::StrLen(std::string_view("abc\0def", 7)) == 3, "stops at the embedded zero");
	Check(logic::StrLen(std::string_view("", 1)) == 0, "zero at once: loop body runs once, length 0");
	Check(logic::StrLen(std::string_view("abcd")) == 4, "no zero inside the view: its whole length");
	Check(logic::AppendCount(0x50) == 0x400 - 0x50, "_mbsnbcat count is 0x400 - strlen(dst)");

	Section("Flow 7 / C11: _mbsnbcat appends at most count bytes, never past 0x400");
	std::string message(0x3F0, 'm');
	logic::MbsnbcatAppend(message, std::string(8, 'a'));
	Check(message.size() == 0x3F8, "8 of 16 free bytes: all appended");
	logic::MbsnbcatAppend(message, std::string(20, 'b'));
	Check(message.size() == 0x400 && message.substr(0x3F8) == std::string(8, 'b'), "the append is cut at the buffer end");
	logic::MbsnbcatAppend(message, std::string(4, 'c'));
	Check(message.size() == 0x400 && message.find('c') == std::string::npos, "a full buffer: count 0, nothing appended");
	std::string shortMessage = "ab";
	logic::MbsnbcatAppend(shortMessage, std::string_view("cd\0ef", 5));
	Check(shortMessage == "abcd", "the line's own terminator ends the copy");
}

void WarningLoop() {
	Section("Flow 8: the warning has the header, one line per entry in order, and the total");
	Slots slots = QuarterSlots();
	slots[1] = logic::SettingView{Bits(0.45f), kName1};
	logic::WarningResult warning = BuildFrom(slots, Bits(1.2f));
	const std::string* built = std::get_if<std::string>(&warning);
	std::string expected = std::string(kHeader) +
						   "\tfPercentageOfBar0:LoadingBar - 0.2500\n"
						   "\tfPercentageOfBar1:LoadingBar - 0.4500\n"
						   "\tfPercentageOfBar2:LoadingBar - 0.2500\n"
						   "\tfPercentageOfBar3:LoadingBar - 0.2500\n"
						   "INI Total = 1.2000";
	Check(built != nullptr && *built == expected, "the text matches, entries 0..3");

	Section("Flow 8: exact ties in the lines and the total round up (legacy CRT)");
	Slots ties = QuarterSlots();
	ties[0] = logic::SettingView{Bits(0.15625f), kName0};
	FakePorts tiePorts = MakePorts(ties);
	logic::MenuFields fields = GarbageFields();
	Check(Run(tiePorts, fields) == logic::Outcome::Completed, "sum 0.90625 warns and completes");
	std::string tieText = std::string(kHeader) +
						  "\tfPercentageOfBar0:LoadingBar - 0.1563\n"
						  "\tfPercentageOfBar1:LoadingBar - 0.2500\n"
						  "\tfPercentageOfBar2:LoadingBar - 0.2500\n"
						  "\tfPercentageOfBar3:LoadingBar - 0.2500\n"
						  "INI Total = 0.9063";
	Check(tiePorts.loggedTexts.size() == 1 && tiePorts.loggedTexts[0] == tieText, "'- 0.1563' and 'INI Total = 0.9063'");

	Section("Flow 8: infinite and indefinite values print as the legacy CRT spells them");
	Slots specials = QuarterSlots();
	specials[2] = logic::SettingView{Bits(std::numeric_limits<float>::infinity()), kName2};
	warning = BuildFrom(specials, logic::X87AddFloat(Bits(std::numeric_limits<float>::infinity()), Bits(0.5f)));
	built = std::get_if<std::string>(&warning);
	Check(built != nullptr && built->find("\tfPercentageOfBar2:LoadingBar - 1.#INF\n") != std::string::npos &&
			built->ends_with("INI Total = 1.#INF"),
		"infinity: '- 1.#INF' and 'INI Total = 1.#INF'");
	specials[2] = logic::SettingView{0xFFC00000u, kName2};
	warning = BuildFrom(specials, 0xFFC00000u);
	built = std::get_if<std::string>(&warning);
	Check(built != nullptr && built->find("\tfPercentageOfBar2:LoadingBar - -1.#IND\n") != std::string::npos &&
			built->ends_with("INI Total = -1.#IND"),
		"indefinite: '- -1.#IND' and 'INI Total = -1.#IND'");

	Section("Flow 8: long lines fill the 0x400-byte buffer; the rest is cut");
	std::string longName(240, 'n');  // 1 + 240 + 3 + 6 + 1 = 251 bytes per line
	Slots full{
		logic::SettingView{Bits(0.25f), longName},
		logic::SettingView{Bits(0.25f), longName},
		logic::SettingView{Bits(0.25f), longName},
		logic::SettingView{Bits(0.45f), longName},
	};
	warning = BuildFrom(full, Bits(1.2f));
	built = std::get_if<std::string>(&warning);
	std::string oneLine = "\t" + longName + " - 0.2500\n";
	Check(built != nullptr && built->size() == 0x400, "the message stops at 0x400 bytes");
	Check(built != nullptr && built->starts_with(std::string(kHeader) + oneLine + oneLine + oneLine) &&
			built->find("INI Total") == std::string::npos,
		"three full lines, the fourth cut, no room for the total");

	Section("Flow 8: the loop stops at the first missing entry (fault), after the earlier lines");
	slots[2] = std::nullopt;
	warning = BuildFrom(slots, Bits(1.2f));
	const logic::WarningFault* fault = std::get_if<logic::WarningFault>(&warning);
	Check(fault != nullptr && fault->kind == logic::WarningFaultKind::NullSettingName && fault->index == 2,
		"null name read at entry 2");

	Slots off = QuarterSlots();
	off[1] = logic::SettingView{Bits(0.45f), kName1};
	FakePorts ports = MakePorts(off);
	fields = GarbageFields();
	static_cast<void>(Run(ports, fields));
	Check(ports.loggedTexts.size() == 1 && ports.loggedTexts[0] == expected, "the run hands that text to 0x004A7A60");
	Check(ports.readIndices == std::vector<int>{0, 1, 2, 3, 0, 1, 2, 3}, "the warning loop reads entries 0..3 again");
}

void ReadTiming() {
	Section("Read timing: the hook object is read after the base constructor");
	{
		FakePorts ports = MakePorts(QuarterSlots(), 0);
		ports.hookObjectWrittenByBase = kHookObject;
		logic::MenuFields fields = GarbageFields();
		static_cast<void>(Run(ports, fields));
		Check(ports.hookObject == kHookObject, "the hook sees what 0x00584640 left at +0x24");
	}
	Section("Read timing: entries are read after the hook (0x005AD170 follows 0x005AD158)");
	{
		FakePorts ports = MakePorts(QuarterSlots(), kHookObject);
		ports.settingWrittenByHook = SettingRewrite{0, logic::SettingView{Bits(0.5f), kName0}};
		logic::MenuFields fields = GarbageFields();
		static_cast<void>(Run(ports, fields));
		Check(!ports.storedPercentageBits.empty() && ports.storedPercentageBits[0].second == Bits(0.5f),
			"0x00B3B3DC[0] gets the value the hook wrote");
	}
	Section("Read timing: entry i+1 is read after the 'Missing' call for entry i");
	{
		Slots slots = QuarterSlots();
		slots[0] = std::nullopt;
		FakePorts ports = MakePorts(slots);
		ports.settingWrittenByMissingLog = SettingRewrite{1, logic::SettingView{Bits(0.5f), kName1}};
		logic::MenuFields fields = GarbageFields();
		Check(Run(ports, fields) == logic::Outcome::Completed, "sum 0.5 + 0.25 + 0.25 = 1.0: no warning");
		Check(ports.storedPercentageBits.size() == 3 && ports.storedPercentageBits[0] == std::make_pair(1, Bits(0.5f)),
			"0x00B3B3DC[1] gets the value written during the 'Missing' log");
	}
	Section("Read timing: the warning loop reads the entries again (0x005AD200), after all 'Missing' calls");
	{
		Slots slots = QuarterSlots();
		slots[0] = std::nullopt;
		FakePorts ports = MakePorts(slots);
		ports.settingWrittenByMissingLog = SettingRewrite{0, logic::SettingView{Bits(0.125f), kName0}};
		logic::MenuFields fields = GarbageFields();
		Check(Run(ports, fields) == logic::Outcome::Completed, "entry 0 present again by then: no null read");
		Check(ports.loggedTexts.size() == 1 &&
				ports.loggedTexts[0].starts_with(std::string(kHeader) + "\tfPercentageOfBar0:LoadingBar - 0.1250\n") &&
				ports.loggedTexts[0].ends_with("INI Total = 0.7500"),
			"the line shows the rewritten entry, the total the sum of the scan");
		Check(ports.IndexOf(Event::ReadSetting, 4) > ports.IndexOf(Event::LogMissingSection),
			"the fifth read follows the 'Missing' call");
	}
	Section("Read timing: 0x00B14158 is read after the update 0x0047D170");
	{
		FakePorts ports = MakePorts(QuarterSlots());
		ports.countdownWrittenByUpdate = Bits(7.5f);
		logic::MenuFields fields = GarbageFields();
		Check(Run(ports, fields) == logic::Outcome::Completed, "completes");
		Check(fields.countdownBits == Bits(7.5f), "+0x40 gets the value the update left");
		Check(ports.events.back() == Event::ReadInitialCountdown, "the last read of the body");
	}
}

void Allocation() {
	Section("Flow 9: allocation succeeds - 0x0047D150(GetTickCount) on the block");
	FakePorts ports = MakePorts(QuarterSlots());
	logic::MenuFields fields = GarbageFields();
	Check(Run(ports, fields) == logic::Outcome::Completed, "completes");
	Check(ports.allocationSize == 0x18, "FormHeap_Allocate(0x18) (0x005AD2E7)");
	Check(ports.constructed == std::make_pair(kClockBlock, kFirstTick), "ctor on the block with the first tick (0x005AD309)");
	Check(ports.updated == std::make_pair(kClockBlock, kSecondTick), "update with a second, separate tick (C12, 0x005AD322)");
	Check(fields.clock == kClockBlock, "+0x54 is the block");
	std::vector<Event> tail(ports.events.end() - 7, ports.events.end());
	Check(tail == std::vector<Event>{Event::Allocate, Event::TickCount, Event::ConstructClock, Event::TickCount,
					Event::UpdateClock, Event::ReadClockLastMs, Event::ReadInitialCountdown},
		"allocate, tick, construct, tick, update, read +0x10, read 0x00B14158");

	Section("Flow 9: allocation fails - clock 0, ctor and its tick skipped");
	FakePorts failed = MakePorts(QuarterSlots());
	failed.allocationResult = 0;
	failed.ticks = {kSecondTick};
	fields = GarbageFields();
	static_cast<void>(Run(failed, fields));
	Check(!failed.constructed.has_value(), "0x0047D150 not called");
	Check(failed.Count(Event::TickCount) == 1, "only the update's tick is read");
	Check(fields.clock == 0, "+0x54 = 0 (XOR EDI,EDI at 0x005AD315)");
	Check(logic::ConstructsClock(kClockBlock) && !logic::ConstructsClock(0), "decision: nonzero constructs");
}

void ClockPrime() {
	Section("Clock prime: unconditional - baselines +0x68 = +0x6C = clock+0x10, +0x40 from 0x00B14158");
	FakePorts ports = MakePorts(QuarterSlots(), kHookObject);
	logic::MenuFields fields = GarbageFields();
	Check(Run(ports, fields) == logic::Outcome::Completed, "completes");
	Check(fields.lastStepMs == kPrimedLastMs && fields.startMs == kPrimedLastMs, "both baselines are the primed +0x10");
	Check(fields.countdownBits == kInitialCountdownBits && Bits(2.5f) == kInitialCountdownBits, "+0x40 = the float at 0x00B14158");
	Check(fields.vtable == logic::kVtable, "+0x00 = 0x00A6C40C");
	Check(fields.unknown28 == -1 && fields.stepLimit == -1, "+0x28 = +0x3C = -1");
	Check(fields.unknown4C == 0 && fields.unknown50 == 0 && fields.shownPercent == 0 && fields.step == 0,
		"+0x4C, +0x50, +0x60, +0x64 = 0");
	Check(!fields.flag70 && fields.flag71, "+0x70 = 0, +0x71 = 1");

	Section("Clock prime refusal: null clock - update still called with 0 (C8), then the read at 0x005AD32F faults");
	FakePorts failed = MakePorts(QuarterSlots());
	failed.allocationResult = 0;
	failed.ticks = {kSecondTick};
	fields = GarbageFields();
	logic::Outcome outcome = Run(failed, fields);
	Check(failed.updated == std::make_pair(0u, kSecondTick), "0x0047D170 is called with ECX = 0, no guard");
	Check(outcome == logic::Outcome::FaultNullClockRead, "outcome: the null read of [0+0x10]");
	Check(failed.Count(Event::ReadClockLastMs) == 0, "nothing reads through the null clock");
	Check(failed.Count(Event::ReadInitialCountdown) == 0, "0x00B14158 is not read");
	Check(fields.lastStepMs == 0x88u && fields.startMs == 0x99u, "+0x68 and +0x6C are not written");
	Check(fields.countdownBits == 0u, "+0x40 keeps the 0.0 from 0x005AD12B");
	Check(logic::ClockReadFaults(0) && !logic::ClockReadFaults(kClockBlock), "decision: only the null clock faults");
}

// The declared calling conventions are part of the function types: a build
// that silently dropped __thiscall or __stdcall would make these types equal.
static_assert(!std::is_same_v<engine::ConstructorFn, engine::LoadingMenuLayout* (__cdecl*)(engine::LoadingMenuLayout*)>,
	"0x005AD0D0 is __thiscall, not __cdecl");
static_assert(!std::is_same_v<engine::TimeInfoUpdateFn,
				  void(__cdecl*)(obng::engine::time::TimeInfo*, std::uint32_t)>,
	"0x0047D170 is __thiscall, not __cdecl");
static_assert(!std::is_same_v<engine::GetTickCountFn, std::uint32_t(__cdecl*)()>, "GetTickCount is __stdcall");

void EngineShape() {
	Section("Engine-facing shape");
	Check(sizeof(engine::SettingLayout) == 8, "Setting: value +0x00, name +0x04");
	Check(sizeof(obng::engine::time::TimeInfo) == logic::kClockSize, "the allocation size is sizeof(TimeInfo)");
}

} // namespace

int main() {
	OsGlobalsHook();
	SettingPresent();
	SettingsLoop();
	X87Values();
	LegacyFixed();
	SumSide();
	SumWithinTolerance();
	WarningLinePerSetting();
	InlinedStrlen();
	WarningLoop();
	ReadTiming();
	Allocation();
	ClockPrime();
	EngineShape();
	return Failures();
}
