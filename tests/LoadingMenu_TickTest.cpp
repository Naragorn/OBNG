// Tests of the rebuild of 0x005AD980 (proposed LoadingMenu::Tick): one tick of
// the loading screen. It advances the loading menu's private TimeInfo
// ([menu+0x54], not g_timeInfo 0x00B33E90), moves the loading bar's target
// step by three time thresholds, counts the screen timer (seconds) down,
// pushes the shown step to the tile as trait 0xFB1, re-arms the timer and
// runs the interface calls on a refresh, and tail-jumps into 0x0060D0A0 on
// the main thread.
//
// One section per branch of the mapped control flow (re/functions/0x005AD980.md):
//   B1  tile menu for type 0x3EF null            0x005AD995
//   B2  containing menu null                     0x005AD9A4
//   B3  sinceStep > [0x00B14160]                 0x005AD9DD
//   B4  targetStep >= slowStepCap                0x005AD9EB
//   B5  sinceStep > [0x00B14168]                 0x005AD9F3
//   B6a fastStepEnabled == 0                     0x005AD9F9
//   B6b sinceStep <= [0x00B14170]                0x005ADA01
//   B7  sinceLast >= 0 (no 2^32 fix-up)          0x005ADA16
//   B8  timer <= 0 (NaN: not expired)            0x005ADA3A
//   B9  forceRefresh == 0                        0x005ADA46
//   B10 step unchanged and no refresh            0x005ADA55 / 0x005ADA5C
//   B11 old shownStep >= 100                     0x005ADA65
//   B12 shownStep == targetStep                  0x005ADA6D
//   B13 !((float)new shownStep < 99.9999...)     0x005ADAA2
//   B14 refresh == 0                             0x005ADAA9
//   B15 InterfaceManager null                    0x005ADAD0
//   B16 g_ioManager null (first read)            0x005ADADA
//   B17 g_ioManager null (second read)           0x005ADB0B
//   B18 not the main thread                      0x005ADB29
// plus T1 (lastTickMs = now on every path past B2) and the refusal paths the
// original has no guard for: the second containing-menu lookup returning
// null (the original reads [0+0x54] at 0x005AD9B6), a null clock (it reads
// [0+0x10] at 0x005AD9CA) and a null g_osGlobals (it reads [0+0x10] at
// 0x005ADB1B); and the control-word gate's refusals.
//
// Precision. The countdown (0x005ADA0B..0x005ADA2E) depends on the x87
// precision control. The game runs at 24 bits (measured, knowledge/frame-clock.md:
// _control87 0x000A001F), so the run defaults to Precision::Single; the 53-bit
// reading is tested next to it. Expected bit patterns marked "emulator" are
// tools/difftest/emulate.py's output on the original bytes (the table in
// re/functions/0x005AD980.md, section "Countdown at PC 24"); the section
// "Host x87" additionally runs the original's opcodes on this machine's x87
// under each control word, so the tests need an i386 build.

#include "Check.h"
#include "engine/time/LoadingMenu_Tick.h"
#include "engine/time/LoadingMenu_TickLogic.h"
#include "engine/time/TimeInfo.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace logic = obng::engine::time::loading_menu_tick;
namespace engine = obng::engine::time::loading_menu_tick::engine;

namespace {

constexpr logic::Precision kSingle = logic::Precision::Single;
constexpr logic::Precision kDouble = logic::Precision::Double;

constexpr std::uint32_t kTileMenu = 0x00001000u;
constexpr std::uint32_t kMenu = 0x00002000u;
constexpr std::uint32_t kClock = 0x00003000u;
constexpr std::uint32_t kOtherClock = 0x00003100u;
constexpr std::uint32_t kOsGlobals = 0x00004000u;
constexpr std::uint32_t kOtherOsGlobals = 0x00004100u;
constexpr std::uint32_t kInterfaceManager = 0x00005000u;
constexpr std::uint32_t kOtherInterfaceManager = 0x00005100u;
constexpr std::uint32_t kIoManager = 0x00006000u;
constexpr std::uint32_t kMainThread = 7u;
constexpr std::uint32_t kOtherThread = 9u;
constexpr std::uint32_t kTick = 123456u;
constexpr float kScreenPeriod = 15.0f;  // the image value of 0x00B14158 (data.json bytes 00 00 70 41)

// The image values of the settings 0x00B14160 / 0x00B14168 / 0x00B14170
// (data.json: 2500, 100, 10); their runtime values are unverified, so the
// rebuild reads them through a port and the tests supply them.
constexpr logic::StepThresholds kImageThresholds{2500, 100, 10};

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

enum class Event {
	TileMenuForType,
	ContainingMenu,
	TickCount,
	UpdateClock,
	ReadClockMs,
	StepThresholds,
	SetTileFloat,
	RefreshScreen,
	ScreenPeriodSetting,
	InterfaceManager,
	IoManager,
	IoManagerBefore,
	InterfaceManagerUpdate,
	InterfacePass,
	InterfacePassEnd,
	IoManagerAfter,
	OsGlobals,
	MainThreadId,
	CurrentThreadId,
	OsGlobalsTail,
};

// The baseline tick: now 10000, sinceStep 50 (no step rule fires: below
// every threshold but the fast one, and the fast flag is off), sinceLast 500
// (the timer goes 5.0 -> 4.5, exact at 24 and 53 bits), shown == target:
// nothing to redraw.
constexpr std::uint32_t kBaselineNow = 10000u;

logic::MenuFields BaselineFields() {
	logic::MenuFields fields;
	fields.slowStepCap = 10;
	fields.screenTimeLeft = 5.0f;
	fields.clock = kClock;
	fields.shownStep = 3;
	fields.targetStep = 3;
	fields.stepChangedMs = kBaselineNow - 50u;
	fields.lastTickMs = kBaselineNow - 500u;
	fields.fastStepEnabled = 0;
	return fields;
}

// Records every call the rebuild makes, in order, with its arguments. Each
// lookup the original makes twice has its own sequence of answers, so a test
// can tell the first read from the second.
struct FakePorts {
	logic::MenuFields fields = BaselineFields();
	std::vector<Event> events;

	std::uint32_t tileMenu = kTileMenu;
	std::optional<std::uint32_t> menuTypeAsked;
	std::vector<std::uint32_t> containingMenus{kMenu, kMenu};
	std::size_t containingMenuCalls = 0;
	std::vector<std::uint32_t> containingMenuTiles;
	std::optional<std::uint32_t> fieldsAsked;

	std::optional<std::uint32_t> clockAfterUpdate;                  // the update replaces [menu+0x54]
	std::optional<std::pair<std::uint32_t, std::uint32_t>> updated;  // (clock, tick)
	std::vector<std::uint32_t> clockReadsFrom;
	std::vector<std::uint32_t> clockMs{kBaselineNow, kBaselineNow};
	std::size_t clockReads = 0;
	logic::StepThresholds thresholds = kImageThresholds;

	std::optional<std::tuple<std::uint32_t, std::uint32_t, float>> tileFloat;  // (tile, trait, value)
	std::optional<std::pair<std::uint32_t, std::uint32_t>> refreshed;          // (menu, tile)
	float screenPeriod = kScreenPeriod;

	std::vector<std::uint32_t> interfaceManagers{kInterfaceManager, kInterfaceManager};
	std::size_t interfaceManagerCalls = 0;
	std::vector<std::uint32_t> ioManagers{kIoManager, kIoManager};
	std::size_t ioManagerCalls = 0;
	std::optional<std::uint32_t> ioBefore;
	std::optional<std::uint32_t> ioAfter;
	std::optional<std::uint32_t> interfaceUpdated;
	std::optional<std::uint32_t> passArgument;

	std::vector<std::uint32_t> osGlobals{kOsGlobals, kOsGlobals};
	std::size_t osGlobalsCalls = 0;
	std::optional<std::uint32_t> mainThreadAskedOf;
	std::uint32_t mainThread = kMainThread;
	std::uint32_t currentThread = kMainThread;
	std::optional<std::uint32_t> tailOn;

	std::uint32_t TileMenuForType(std::uint32_t menuType) {
		events.push_back(Event::TileMenuForType);
		menuTypeAsked = menuType;
		return tileMenu;
	}

	std::uint32_t ContainingMenu(std::uint32_t tile) {
		events.push_back(Event::ContainingMenu);
		containingMenuTiles.push_back(tile);
		return containingMenus.at(containingMenuCalls++);
	}

	logic::MenuFields& Fields(std::uint32_t menu) {
		fieldsAsked = menu;
		return fields;
	}

	std::uint32_t TickCount() {
		events.push_back(Event::TickCount);
		return kTick;
	}

	void UpdateClock(std::uint32_t clock, std::uint32_t tick) {
		events.push_back(Event::UpdateClock);
		updated = std::make_pair(clock, tick);
		if (clockAfterUpdate) {
			fields.clock = *clockAfterUpdate;
		}
	}

	std::uint32_t ReadClockMs(std::uint32_t clock) {
		events.push_back(Event::ReadClockMs);
		clockReadsFrom.push_back(clock);
		return clockMs.at(clockReads++);
	}

	logic::StepThresholds StepThresholds() {
		events.push_back(Event::StepThresholds);
		return thresholds;
	}

	void SetTileFloat(std::uint32_t tile, std::uint32_t trait, float value) {
		events.push_back(Event::SetTileFloat);
		tileFloat = std::make_tuple(tile, trait, value);
	}

	void RefreshScreen(std::uint32_t menu, std::uint32_t tile) {
		events.push_back(Event::RefreshScreen);
		refreshed = std::make_pair(menu, tile);
	}

	float ScreenPeriodSetting() {
		events.push_back(Event::ScreenPeriodSetting);
		return screenPeriod;
	}

	std::uint32_t InterfaceManager() {
		events.push_back(Event::InterfaceManager);
		return interfaceManagers.at(interfaceManagerCalls++);
	}

	std::uint32_t IoManager() {
		events.push_back(Event::IoManager);
		return ioManagers.at(ioManagerCalls++);
	}

	void IoManagerBefore(std::uint32_t io) {
		events.push_back(Event::IoManagerBefore);
		ioBefore = io;
	}

	void InterfaceManagerUpdate(std::uint32_t manager) {
		events.push_back(Event::InterfaceManagerUpdate);
		interfaceUpdated = manager;
	}

	void InterfacePass(std::uint32_t argument) {
		events.push_back(Event::InterfacePass);
		passArgument = argument;
	}

	void InterfacePassEnd() { events.push_back(Event::InterfacePassEnd); }

	void IoManagerAfter(std::uint32_t io) {
		events.push_back(Event::IoManagerAfter);
		ioAfter = io;
	}

	std::uint32_t OsGlobals() {
		events.push_back(Event::OsGlobals);
		return osGlobals.at(osGlobalsCalls++);
	}

	std::uint32_t MainThreadId(std::uint32_t globals) {
		events.push_back(Event::MainThreadId);
		mainThreadAskedOf = globals;
		return mainThread;
	}

	std::uint32_t CurrentThreadId() {
		events.push_back(Event::CurrentThreadId);
		return currentThread;
	}

	void OsGlobalsTail(std::uint32_t globals) {
		events.push_back(Event::OsGlobalsTail);
		tailOn = globals;
	}

	int Count(Event event) const {
		int count = 0;
		for (Event recorded : events) {
			count += recorded == event ? 1 : 0;
		}
		return count;
	}

	// Index of the first occurrence, or -1.
	int IndexOf(Event event) const {
		for (std::size_t i = 0; i < events.size(); ++i) {
			if (events[i] == event) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
};

logic::Inputs GameInputs(std::uint8_t forceRefresh = 0, logic::Precision precision = kSingle) {
	return logic::Inputs{forceRefresh, precision};
}

// A menu whose shown step lags the target by one, so the redraw runs.
FakePorts RedrawingPorts() {
	FakePorts ports;
	ports.fields.targetStep = 4;
	return ports;
}

void Constants() {
	Section("Constants: the image's bytes (data.json of 0x005AD980, read)");
	Check(Bits(logic::kTwoPow32) == 0x4F800000u, "0x00A2FC78 = 4294967296.0f (bytes 00 00 80 4f)");
	Check(std::bit_cast<std::uint64_t>(logic::kMsPerSecond) == 0x408F400000000000ull, "0x00A2FC70 = 1000.0 (bytes 00 00 00 00 00 40 8f 40)");
	Check(std::bit_cast<std::uint64_t>(logic::kStepCeiling) == 0x4058FFFE60000000ull,
	      "0x00A6C4C8 = 99.9999008178711 (bytes 00 00 00 60 fe ff 58 40)");
	Check(logic::kStepCeiling == static_cast<double>(99.9999f), "0x00A6C4C8 is (double)99.9999f (the low 29 mantissa bits are zero)");
	Check(logic::kLoadingMenuType == 0x3EFu, "PUSH 0x3EF 0x005AD984");
	Check(logic::kTraitUser3 == 0xFB1u, "PUSH 0xFB1 0x005ADA89");
	Check(logic::kShownStepLimit == 100, "CMP ECX,0x64 0x005ADA62");
	Check(logic::kInterfacePassArgument == 0u, "PUSH 0 0x005ADAF4");
	Check(engine::kGamePrecision == kSingle, "the game's precision control: 24 bits (measured)");
}

void B1TileMenu() {
	Section("B1 taken: 0x00589B70(0x3EF) returns null - return before anything else");
	FakePorts ports;
	ports.tileMenu = 0;
	logic::Outcome outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::NoTileMenu, "outcome: no tile menu (exit 0x005ADB3A)");
	Check(ports.menuTypeAsked == 0x3EFu, "the lookup is asked for menu type 0x3EF (0x005AD984)");
	Check(ports.events == std::vector<Event>{Event::TileMenuForType}, "only the lookup ran");
	Check(ports.fields.lastTickMs == BaselineFields().lastTickMs, "T1 is not reached: +0x6C untouched");
	Check(logic::IsNull(0u) && !logic::IsNull(kTileMenu), "decision: only zero is null");

	Section("B1 not taken: the tile menu goes on as ECX of 0x005898F0");
	ports = FakePorts{};
	outcome = logic::RunTick(ports, GameInputs());
	Check(!ports.containingMenuTiles.empty() && ports.containingMenuTiles[0] == kTileMenu, "GetContainingMenu on the tile menu");
	Check(outcome == logic::Outcome::TailCalled, "the baseline reaches the tail");
}

void B2ContainingMenu() {
	Section("B2 taken: the first GetContainingMenu 0x005898F0 returns null - return");
	FakePorts ports;
	ports.containingMenus = {0};
	logic::Outcome outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::NoMenu, "outcome: no menu (exit 0x005ADB3A)");
	Check(ports.events == (std::vector<Event>{Event::TileMenuForType, Event::ContainingMenu}), "no clock update, no tick read");
	Check(ports.fields.lastTickMs == BaselineFields().lastTickMs, "T1 is not reached");

	Section("B2 not taken: a second lookup gives the menu; its clock [menu+0x54] is updated with GetTickCount");
	ports = FakePorts{};
	outcome = logic::RunTick(ports, GameInputs());
	Check(ports.Count(Event::ContainingMenu) == 2, "GetContainingMenu is called twice (0x005AD99D, 0x005AD9AF)");
	Check(ports.containingMenuTiles == (std::vector<std::uint32_t>{kTileMenu, kTileMenu}), "both on the tile menu");
	Check(ports.fieldsAsked == kMenu, "the menu fields are those of the second lookup's result");
	Check(ports.updated == std::make_pair(kClock, kTick), "0x0047D170(this = [menu+0x54], GetTickCount())");
	Check(ports.IndexOf(Event::TickCount) < ports.IndexOf(Event::UpdateClock), "GetTickCount before the update");
	Check(ports.clockReadsFrom == std::vector<std::uint32_t>{kClock}, "now = clock+0x10, read once on this path");
	Check(outcome == logic::Outcome::TailCalled, "completes");

	Section("B2 not taken: [menu+0x54] is read again after the update (0x005AD9C7)");
	ports = FakePorts{};
	ports.clockAfterUpdate = kOtherClock;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.updated && ports.updated->first == kClock, "the update ran on the first read (0x005AD9B6)");
	Check(ports.clockReadsFrom == std::vector<std::uint32_t>{kOtherClock}, "clockMs is read through the second read");

	Section("B2 refusal: the second lookup returns null - the original reads [0+0x54] at 0x005AD9B6");
	ports = FakePorts{};
	ports.containingMenus = {kMenu, 0};
	outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::FaultNullMenuRead, "outcome: the null menu read");
	Check(ports.Count(Event::UpdateClock) == 0 && ports.Count(Event::TickCount) == 0, "nothing past the read runs");
	Check(!ports.fieldsAsked.has_value(), "no field of a null menu is touched");

	Section("B2 refusal: null clock - the update still gets ECX = 0, then [0+0x10] at 0x005AD9CA faults");
	ports = FakePorts{};
	ports.fields.clock = 0;
	outcome = logic::RunTick(ports, GameInputs());
	Check(ports.updated == std::make_pair(0u, kTick), "0x0047D170 is called with ECX = 0, no guard (as the original)");
	Check(outcome == logic::Outcome::FaultNullClockRead, "outcome: the null clock read");
	Check(ports.Count(Event::ReadClockMs) == 0, "nothing reads through the null clock");
	Check(ports.fields.lastTickMs == BaselineFields().lastTickMs && ports.fields.screenTimeLeft == 5.0f, "no field is written");

	Section("B2: the elapsed values are 32-bit differences, read as signed (0x005AD9CD..0x005AD9D4)");
	Check(logic::ElapsedMs(2000u, 1000u) == 1000, "2000 - 1000");
	Check(logic::ElapsedMs(1000u, 2000u) == -1000, "1000 - 2000 is -1000 as signed");
	Check(logic::ElapsedMs(5u, 0xFFFFFFFBu) == 10, "across the 32-bit wrap: 10");
	Check(logic::ElapsedMs(0x80000000u, 0u) == std::numeric_limits<std::int32_t>::min(), "2^31 reads as INT32_MIN");
}

void B3AlwaysAdvance() {
	Section("B3 taken: sinceStep > [0x00B14160] advances the target step, without the cap");
	Check(logic::ChooseStepRule(2501, 3, 10, 0, kImageThresholds) == logic::StepRule::AlwaysAdvance, "2501 > 2500");
	Check(logic::ChooseStepRule(2501, 50, 10, 0, kImageThresholds) == logic::StepRule::AlwaysAdvance, "target 50 above the cap 10: still advances");
	Check(logic::ChooseStepRule(2501, 0, -1, 0, kImageThresholds) == logic::StepRule::AlwaysAdvance, "the constructor's cap -1 does not block it");
	Check(logic::Advances(logic::StepRule::AlwaysAdvance), "the rule advances");
	FakePorts ports;
	ports.fields.stepChangedMs = kBaselineNow - 2501u;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.targetStep == 4 && ports.fields.shownStep == 4, "run: target 3 -> 4, shown caught up");
	Check(logic::NextStep(std::numeric_limits<std::int32_t>::max()) == std::numeric_limits<std::int32_t>::min(),
	      "ADD dword ptr [ESI+0x64],1 wraps INT32_MAX to INT32_MIN");

	Section("B3 not taken: equal, or negative as signed (JLE)");
	Check(logic::ChooseStepRule(2500, 50, 10, 0, kImageThresholds) == logic::StepRule::AtCap, "2500 is not above 2500: on to B4");
	Check(logic::ChooseStepRule(-5000, 50, 10, 0, kImageThresholds) == logic::StepRule::AtCap, "a negative sinceStep is below");
	Check(logic::ChooseStepRule(std::numeric_limits<std::int32_t>::min(), 50, 10, 0, kImageThresholds) == logic::StepRule::AtCap,
	      "INT32_MIN (a clock 2^31 ms ahead) is below as signed");
}

void B4Cap() {
	Section("B4 taken: targetStep >= slowStepCap - no increment (JGE)");
	Check(logic::ChooseStepRule(200, 10, 10, 1, kImageThresholds) == logic::StepRule::AtCap, "equal to the cap");
	Check(logic::ChooseStepRule(200, 0, -1, 1, kImageThresholds) == logic::StepRule::AtCap, "the constructor's cap -1 blocks B5/B6");
	Check(!logic::Advances(logic::StepRule::AtCap), "the rule does not advance");
	FakePorts ports;
	ports.fields.slowStepCap = 3;
	ports.fields.stepChangedMs = kBaselineNow - 101u;  // would advance under the cap (B5)
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.targetStep == 3, "run: +0x64 stays 3");

	Section("B4 not taken: below the cap, signed (-2 < -1)");
	Check(logic::ChooseStepRule(101, -2, -1, 0, kImageThresholds) == logic::StepRule::CappedAdvance, "-2 is below -1");
}

void B5CappedAdvance() {
	Section("B5 taken: under the cap and sinceStep > [0x00B14168] - advance (ADD EDX,1; MOV [ESI+0x64],EDX)");
	Check(logic::ChooseStepRule(101, 3, 10, 0, kImageThresholds) == logic::StepRule::CappedAdvance, "101 > 100");
	Check(logic::Advances(logic::StepRule::CappedAdvance), "the rule advances");
	FakePorts ports;
	ports.fields.stepChangedMs = kBaselineNow - 101u;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.targetStep == 4, "run: +0x64 goes 3 -> 4");

	Section("B5 not taken: equal goes to B6 (JG)");
	Check(logic::ChooseStepRule(100, 3, 10, 0, kImageThresholds) == logic::StepRule::FastDisabled, "100 is not above 100");
	Check(logic::ChooseStepRule(100, 3, 10, 1, kImageThresholds) == logic::StepRule::FastAdvance, "with the fast flag, B6b decides");
}

void B6aFastFlag() {
	Section("B6a taken: fastStepEnabled +0x70 zero - no advance, whatever sinceStep below [0x00B14168]");
	Check(logic::ChooseStepRule(90, 3, 10, 0, kImageThresholds) == logic::StepRule::FastDisabled, "90 > 10 but the flag is off");
	Check(!logic::Advances(logic::StepRule::FastDisabled), "no advance");
	FakePorts ports;
	ports.fields.stepChangedMs = kBaselineNow - 90u;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.targetStep == 3, "run: +0x64 stays 3");

	Section("B6a not taken: any nonzero byte is set (CMP byte ptr [ESI+0x70],0)");
	Check(logic::ChooseStepRule(11, 3, 10, 1, kImageThresholds) == logic::StepRule::FastAdvance, "flag 1");
	Check(logic::ChooseStepRule(11, 3, 10, 0x80, kImageThresholds) == logic::StepRule::FastAdvance, "flag 0x80");
}

void B6bFastThreshold() {
	Section("B6b not taken: flag set and sinceStep > [0x00B14170] - advance");
	Check(logic::ChooseStepRule(11, 3, 10, 1, kImageThresholds) == logic::StepRule::FastAdvance, "11 > 10");
	Check(logic::Advances(logic::StepRule::FastAdvance), "the rule advances");
	FakePorts ports;
	ports.fields.fastStepEnabled = 1;
	ports.fields.stepChangedMs = kBaselineNow - 11u;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.targetStep == 4, "run: +0x64 goes 3 -> 4");

	Section("B6b taken: flag set, sinceStep at or below [0x00B14170] - no advance (JLE)");
	Check(logic::ChooseStepRule(10, 3, 10, 1, kImageThresholds) == logic::StepRule::NotYet, "10 is not above 10");
	Check(logic::ChooseStepRule(-1, 3, 10, 1, kImageThresholds) == logic::StepRule::NotYet, "-1 is below, signed");
	Check(!logic::Advances(logic::StepRule::NotYet), "no advance");
	ports = FakePorts{};
	ports.fields.fastStepEnabled = 1;
	ports.fields.stepChangedMs = kBaselineNow - 10u;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.targetStep == 3, "run: +0x64 stays 3");
}

void B7UnsignedFixup() {
	Section("B7 taken (JGE): sinceLast >= 0 goes into the division exact (FILD 0x005ADA12)");
	Check(!logic::NeedsUnsignedFixup(0) && !logic::NeedsUnsignedFixup(500), "0 and 500 need no fix-up");
	Check(logic::ElapsedForTimer(500, kSingle) == 500.0 && logic::ElapsedForTimer(500, kDouble) == 500.0, "500 at 24 and 53 bits");
	Check(logic::ElapsedForTimer(16777217, kSingle) == 16777217.0,
	      "2^24 + 1 is kept exact at PC 24 (FILD does not round; the float-first trap would give 2^24)");
	Check(logic::ElapsedForTimer(std::numeric_limits<std::int32_t>::max(), kSingle) == 2147483647.0, "INT32_MAX exact");
	FakePorts ports;
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.screenTimeLeft == 4.5f, "run: +0x40 goes 5.0 -> 4.5");

	Section("B7 not taken: sinceLast < 0 gets 2^32 added (FADD float [0x00A2FC78] 0x005ADA18, rounded to PC)");
	Check(logic::NeedsUnsignedFixup(-1), "-1 needs the fix-up");
	Check(logic::ElapsedForTimer(-1, kDouble) == 4294967295.0, "PC 53: -1 + 2^32 = 0xFFFFFFFF, exact");
	Check(logic::ElapsedForTimer(-1, kSingle) == 4294967296.0, "PC 24: 0xFFFFFFFF rounded to 24 bits is 2^32 (derived, C17)");
	Check(logic::ElapsedForTimer(-100, kSingle) == 4294967296.0, "PC 24: 4294967196 rounds to 2^32 (the review's input)");
	Check(logic::ElapsedForTimer(std::numeric_limits<std::int32_t>::min(), kSingle) == 2147483648.0, "INT32_MIN + 2^32 = 2^31, exact");
	ports = FakePorts{};
	ports.fields.lastTickMs = kBaselineNow + 100u;  // the clock went back: sinceLast -100
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.refreshed.has_value(), "run: the timer falls far below zero, B8 expires it and 0x005AD780 runs");
	Check(ports.fields.screenTimeLeft == kScreenPeriod, "run: and +0x40 is re-armed from 0x00B14158");
}

struct CountdownRow {
	float timer;
	std::int32_t sinceLast;
	std::uint32_t pc24;
	std::optional<std::uint32_t> pc53;
	const char* what;
};

void Countdown() {
	// The emulator's stored timer (the write at 0x005ADA2E) at fpcw 0x007F
	// (PC 24) and 0x027F (PC 53), re/functions/0x005AD980.md.
	const CountdownRow rows[] = {
	    {5.0f, -100, 0xCA831265u, 0xCA831264u, "(5.0, -100): the review's input"},
	    {FromBits(0x3A83126Fu), 1, 0x00000000u, 0x2E50E560u, "(0.001f, 1): PC 24 reaches exactly 0"},
	    {FromBits(0x3C83126Fu), 16, 0x00000000u, 0x3050E560u, "(0.016f, 16): PC 24 reaches exactly 0"},
	    {5.0f, 100, 0x409CCCCDu, 0x409CCCCDu, "(5.0, 100)"},
	    {15.0f, 13, 0x416FCAC1u, 0x416FCAC1u, "(15.0, 13): a 75 Hz frame"},
	    {15.0f, 16, 0x416FBE77u, 0x416FBE77u, "(15.0, 16)"},
	    {1.0f, 16777217, 0xC683106Fu, 0xC683106Fu, "(1.0, 2^24 + 1)"},
	    {1.0f, -16777217, 0xCA828F5Au, 0xCA828F5Au, "(1.0, -(2^24 + 1))"},
	    {1.0f, 2147483647, 0xCA03126Bu, 0xCA03126Bu, "(1.0, INT32_MAX)"},
	    {1.0f, std::numeric_limits<std::int32_t>::min(), 0xCA03126Bu, 0xCA03126Bu, "(1.0, INT32_MIN)"},
	    {3.3f, -1, 0xCA831268u, 0xCA831268u, "(3.3f, -1)"},
	    {0.0f, 0, 0x00000000u, 0x00000000u, "(0.0, 0)"},
	    {1e-30f, 1, 0xBA83126Fu, 0xBA83126Fu, "(1e-30f, 1)"},
	    {7.7f, 123457, 0xC2E78396u, 0xC2E78396u, "(7.7f, 123457)"},
	    {15.0f, -12345679, 0xCA82B1DDu, 0xCA82B1DDu, "(15.0, -12345679)"},
	    {-337518.46875f, -337518454, 0xCA83126Fu, std::nullopt, "(-337518.46875, -337518454): float-first trap gives ...6E"},
	    {5.0f, -1121445008, 0xCA41B235u, std::nullopt, "(5.0, -1121445008): float-first trap gives ...36"},
	    {static_cast<float>(8.932059288024902), 1136951100, 0xC98AC972u, std::nullopt,
	     "(8.932059288024902, 1136951100): float-first trap gives ...71"},
	};

	Section("Countdown at PC 24 (the game's): the emulator's stored bit patterns");
	for (const CountdownRow& row : rows) {
		Check(Bits(logic::CountDown(row.timer, row.sinceLast, kSingle)) == row.pc24, row.what);
	}

	Section("Countdown, the 53-bit reading: the emulator's stored bit patterns");
	for (const CountdownRow& row : rows) {
		if (row.pc53) {
			Check(Bits(logic::CountDown(row.timer, row.sinceLast, kDouble)) == *row.pc53, row.what);
		}
	}

	Section("Countdown: the three roundings at PC 24 (FADD 0x005ADA18, FDIV 0x005ADA1E, FSUBP 0x005ADA24)");
	Check(logic::TimerQuotient(-100, kSingle) == 4294967.5, "(2^32) / 1000 = 4294967.296 rounds to 4294967.5 at 24 bits");
	Check(logic::TimerQuotient(-100, kDouble) == 4294967.196, "PC 53: (2^32 - 100) / 1000, the double nearest 4294967.196");
	Check(logic::TimerQuotient(1, kSingle) == static_cast<double>(0.001f), "PC 24: 1 / 1000 is the float nearest 0.001");
	Check(logic::TimerQuotient(1, kDouble) == 0.001, "PC 53: 1 / 1000 is the double nearest 0.001");
	Check(Bits(logic::CountDown(-0.0f, 0, kSingle)) == 0x80000000u && Bits(logic::CountDown(-0.0f, 0, kDouble)) == 0x80000000u,
	      "-0.0 - (+0 / 1000) = -0.0 at both precisions");
	Check(Bits(logic::CountDown(std::numeric_limits<float>::infinity(), 7, kSingle)) == 0x7F800000u, "+inf stays +inf");
	Check(Bits(logic::CountDown(FromBits(0x7FC00123u), 7, kSingle)) == 0x7FC00123u, "a quiet NaN timer comes through (B8: not expired)");
	Check(Bits(logic::CountDown(FromBits(0x00000001u), 0, kSingle)) == 0x00000001u, "a subnormal timer with sinceLast 0 is kept");
}

void B8TimerExpired() {
	Section("B8 taken (JZ, C0 = 0): the stored timer <= 0 - refresh = 1");
	Check(logic::TimerExpired(0.0f), "0.0 is expired");
	Check(logic::TimerExpired(-0.0f), "-0.0 compares equal to 0: expired");
	Check(logic::TimerExpired(-1.0f), "negative is expired");
	Check(logic::Refresh(0.0f, 0), "expired gives refresh without the force byte (the CMP at 0x005ADA3C is skipped)");
	FakePorts ports;
	ports.fields.screenTimeLeft = 0.5f;  // 0.5 - 0.5 = +0
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.refreshed == std::make_pair(kMenu, kTileMenu), "run: target == shown, but the refresh redraws and calls 0x005AD780");
	Check(ports.fields.screenTimeLeft == kScreenPeriod, "run: +0x40 re-armed");

	Section("B8 not taken: positive, or NaN (C0 set on unordered) - not expired");
	Check(!logic::TimerExpired(0.5f), "0.5 is not expired");
	Check(!logic::TimerExpired(FromBits(0x00000001u)), "the smallest subnormal is not expired");
	Check(!logic::TimerExpired(std::numeric_limits<float>::quiet_NaN()), "NaN is not expired (pseudo.c's <= agrees for NaN)");
	Check(!logic::Refresh(std::numeric_limits<float>::quiet_NaN(), 0), "NaN without force: no refresh");

	Section("B8 at PC 24 against PC 53: (0.001f, 1) expires only at 24 bits (C20, emulator)");
	FakePorts atSingle;
	atSingle.fields.screenTimeLeft = FromBits(0x3A83126Fu);
	atSingle.fields.lastTickMs = kBaselineNow - 1u;
	static_cast<void>(logic::RunTick(atSingle, GameInputs(0, kSingle)));
	Check(atSingle.refreshed.has_value() && Bits(atSingle.fields.screenTimeLeft) == 0x41700000u,
	      "PC 24: stored 0, B8 taken, 0x005AD780 runs and +0x40 = 15.0 (0x41700000, the emulator's re-arm write)");
	FakePorts atDouble;
	atDouble.fields.screenTimeLeft = FromBits(0x3A83126Fu);
	atDouble.fields.lastTickMs = kBaselineNow - 1u;
	static_cast<void>(logic::RunTick(atDouble, GameInputs(0, kDouble)));
	Check(!atDouble.refreshed.has_value() && Bits(atDouble.fields.screenTimeLeft) == 0x2E50E560u,
	      "PC 53: 4.75e-11 is left, B8 not taken, no re-arm");
}

void B9ForceRefresh() {
	Section("B9 taken: not expired and forceRefresh == 0 - refresh = 0");
	Check(!logic::Refresh(1.0f, 0), "force 0: no refresh");
	FakePorts plain = RedrawingPorts();
	static_cast<void>(logic::RunTick(plain, GameInputs(0)));
	Check(!plain.refreshed.has_value() && plain.fields.screenTimeLeft == 4.5f, "run: no 0x005AD780, +0x40 counted down only");

	Section("B9 not taken: forceRefresh nonzero - refresh = 1 although the timer runs");
	Check(logic::Refresh(1.0f, 1), "force 1");
	Check(logic::Refresh(1.0f, 0x80), "any nonzero byte forces (CMP byte ptr [ESP+0x1C],0)");
	FakePorts forced;
	logic::Outcome outcome = logic::RunTick(forced, GameInputs(1));
	Check(outcome == logic::Outcome::TailCalled, "run: completes");
	Check(forced.refreshed == std::make_pair(kMenu, kTileMenu), "run: 0x005AD780(this = menu, tileMenu)");
	Check(forced.fields.screenTimeLeft == kScreenPeriod, "run: the timer is re-armed although 4.5 > 0");
}

void B10Unchanged() {
	Section("B10 taken: targetStep == shownStep and no refresh - straight to T1");
	Check(logic::GateRedraw(3, 3, false) == logic::RedrawGate::Unchanged, "decision: unchanged");
	FakePorts ports;
	logic::Outcome outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::TailCalled, "the tail still runs");
	Check(!ports.tileFloat.has_value() && ports.Count(Event::InterfaceManager) == 0, "no tile update, no interface calls");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1: +0x6C = now");

	Section("B10 not taken: the step differs (JNZ 0x005ADA55), or a refresh is due (0x005ADA5C)");
	Check(logic::GateRedraw(4, 3, false) == logic::RedrawGate::Proceed, "target ahead");
	Check(logic::GateRedraw(2, 3, false) == logic::RedrawGate::Proceed, "target behind: also a difference");
	Check(logic::GateRedraw(3, 3, true) == logic::RedrawGate::Proceed, "refresh with an unchanged step");
}

void B11BarFull() {
	Section("B11 taken: the old shownStep >= 100 (signed) - straight to T1");
	Check(logic::GateRedraw(150, 100, false) == logic::RedrawGate::BarFull, "100 is full");
	Check(logic::GateRedraw(100, 100, true) == logic::RedrawGate::BarFull, "full beats a due refresh");
	FakePorts ports;
	ports.fields.shownStep = 100;
	ports.fields.targetStep = 101;
	ports.fields.screenTimeLeft = 0.5f;  // expired after this tick
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(!ports.tileFloat.has_value() && !ports.refreshed.has_value(), "no tile update, no 0x005AD780");
	Check(ports.fields.screenTimeLeft == 0.0f, "the expired timer is not re-armed");
	Check(ports.fields.shownStep == 100 && ports.fields.lastTickMs == kBaselineNow, "shown kept; T1 written");

	Section("B11 not taken: 99, and negative values, go on");
	Check(logic::GateRedraw(150, 99, false) == logic::RedrawGate::Proceed, "99 < 100");
	Check(logic::GateRedraw(0, -5, false) == logic::RedrawGate::Proceed, "-5 < 100 as signed");
	Check(logic::GateRedraw(0, std::numeric_limits<std::int32_t>::min(), false) == logic::RedrawGate::Proceed, "INT32_MIN < 100");
}

void B12CatchUp() {
	Section("B12 not taken (JZ): shownStep != targetStep - stepChangedMs = clock+0x10 (read again), shownStep = target");
	Check(logic::ShownStepCatchesUp(3, 4), "decision: 3 != 4");
	FakePorts ports = RedrawingPorts();
	ports.clockMs = {kBaselineNow, 2222u};  // the second read is a separate read (0x005ADA6F)
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.clockReadsFrom == (std::vector<std::uint32_t>{kClock, kClock}), "clock+0x10 read twice, same clock (EDI)");
	Check(ports.fields.stepChangedMs == 2222u, "+0x68 = the second read");
	Check(ports.fields.shownStep == 4, "+0x60 = 4");
	Check(ports.tileFloat == std::make_tuple(kTileMenu, 0xFB1u, 4.0f), "Tile::UpdateFloat(tileMenu, 0xFB1, 4.0)");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 uses the first read (EBP), not the second");

	Section("B12 taken: equal (the refresh path) - +0x68 and +0x60 kept");
	Check(!logic::ShownStepCatchesUp(3, 3), "decision: equal");
	ports = FakePorts{};
	static_cast<void>(logic::RunTick(ports, GameInputs(1)));
	Check(ports.Count(Event::ReadClockMs) == 1, "no second clock read");
	Check(ports.fields.stepChangedMs == BaselineFields().stepChangedMs && ports.fields.shownStep == 3, "+0x68 and +0x60 unchanged");
	Check(ports.tileFloat == std::make_tuple(kTileMenu, 0xFB1u, 3.0f), "the tile still gets 3.0");
	Check(logic::StepAsFloat(16777217) == 16777216.0f, "FILD then FSTP float: 2^24 + 1 rounds to 2^24 (C23)");
}

void B13Ceiling() {
	Section("B13 not taken: (float)new shownStep < [0x00A6C4C8] = 99.9999008 - go on");
	Check(logic::BelowCeiling(99.0f), "99 < 99.9999");
	Check(logic::BelowCeiling(-5.0f), "negative steps are below");
	FakePorts ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.Count(Event::InterfaceManager) == 2, "the interface calls run");

	Section("B13 taken: equal or greater (JP on C0 = C2 = 0) - straight to T1 after the tile update");
	Check(!logic::BelowCeiling(100.0f), "100 is not below 99.9999");
	Check(!logic::BelowCeiling(99.9999f), "(float)99.9999 equals the constant: JP taken on equal");
	Check(!logic::BelowCeiling(150.0f), "150 is not below");
	Check(!logic::BelowCeiling(std::numeric_limits<float>::quiet_NaN()), "unordered: JP taken (a float of an int is never NaN)");

	Section("B13 taken through B12: old shown 99, target 100 - the new shownStep is tested (C22)");
	ports = FakePorts{};
	ports.fields.shownStep = 99;
	ports.fields.targetStep = 100;
	ports.fields.screenTimeLeft = 0.5f;  // expired after this tick
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.fields.shownStep == 100, "B11 passed on the old 99; +0x60 = 100 (the emulator's write at 0x005ADA75)");
	Check(ports.tileFloat == std::make_tuple(kTileMenu, 0xFB1u, 100.0f), "the full bar (100.0) is pushed to the tile");
	Check(!ports.refreshed.has_value() && ports.Count(Event::ScreenPeriodSetting) == 0, "no 0x005AD780, no re-arm");
	Check(ports.fields.screenTimeLeft == 0.0f, "the expired timer stays expired");
	Check(ports.Count(Event::InterfaceManager) == 0, "no interface calls");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 written");
}

void B14Refresh() {
	Section("B14 not taken: refresh - 0x005AD780(this = menu, tileMenu), then +0x40 = *0x00403C00(this = 0x00B14158)");
	FakePorts ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, GameInputs(1)));
	Check(ports.refreshed == std::make_pair(kMenu, kTileMenu), "0x005AD780 on the menu with the tile menu");
	Check(ports.IndexOf(Event::SetTileFloat) < ports.IndexOf(Event::RefreshScreen), "after the tile update");
	Check(ports.IndexOf(Event::RefreshScreen) < ports.IndexOf(Event::ScreenPeriodSetting), "0x005AD780 before the setting read");
	Check(ports.IndexOf(Event::ScreenPeriodSetting) < ports.IndexOf(Event::InterfaceManager), "before the interface calls");
	Check(ports.fields.screenTimeLeft == kScreenPeriod, "+0x40 = the setting");

	Section("B14 taken: no refresh - neither call, the counted-down timer stays");
	ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, GameInputs(0)));
	Check(!ports.refreshed.has_value() && ports.Count(Event::ScreenPeriodSetting) == 0, "no 0x005AD780, no setting read");
	Check(ports.fields.screenTimeLeft == 4.5f, "+0x40 = 4.5");
	Check(ports.Count(Event::InterfaceManager) == 2, "the interface calls still run");
}

void B15InterfaceManager() {
	Section("B15 taken: InterfaceManager_GetSingleton(0,1) 0x00582160 null - straight to T1");
	FakePorts ports = RedrawingPorts();
	ports.interfaceManagers = {0};
	logic::Outcome outcome = logic::RunTick(ports, GameInputs());
	Check(ports.Count(Event::InterfaceManager) == 1, "asked once");
	Check(ports.Count(Event::IoManager) == 0 && ports.Count(Event::InterfacePass) == 0, "no g_ioManager read, no pass");
	Check(ports.fields.lastTickMs == kBaselineNow && outcome == logic::Outcome::TailCalled, "T1 written, tail reached");

	Section("B15 not taken: asked again; the second answer is this of 0x00583F40, unchecked");
	ports = RedrawingPorts();
	ports.interfaceManagers = {kInterfaceManager, kOtherInterfaceManager};
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.Count(Event::InterfaceManager) == 2, "asked twice (0x005ADAC6, 0x005ADAE5)");
	Check(ports.interfaceUpdated == kOtherInterfaceManager, "0x00583F40 on the second answer");
	ports = RedrawingPorts();
	ports.interfaceManagers = {kInterfaceManager, 0};
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.interfaceUpdated == 0u, "a null second answer is passed on as the original does (no guard)");
	Check(ports.passArgument == 0u && ports.Count(Event::InterfacePassEnd) == 1, "0x00579260(0) and 0x005792B0 run past B15");
}

void B16IoManagerBefore() {
	Section("B16 not taken: g_ioManager 0x00B33A10 set - 0x00432860 on it, before the second singleton call");
	FakePorts ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.ioBefore == kIoManager, "0x00432860(this = g_ioManager)");
	Check(ports.IndexOf(Event::IoManagerBefore) < ports.IndexOf(Event::InterfaceManagerUpdate), "before 0x00583F40");

	Section("B16 taken: g_ioManager null - 0x00432860 skipped, the rest runs");
	ports = RedrawingPorts();
	ports.ioManagers = {0, 0};
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(!ports.ioBefore.has_value(), "no 0x00432860");
	Check(ports.interfaceUpdated == kInterfaceManager && ports.passArgument == 0u, "0x00583F40 and 0x00579260(0) run");
}

void B17IoManagerAfter() {
	Section("B17 not taken: g_ioManager read again, set - 0x00432890 on it, after 0x005792B0");
	FakePorts ports = RedrawingPorts();
	ports.ioManagers = {0, kIoManager};
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.Count(Event::IoManager) == 2, "g_ioManager is read twice (0x005ADAD2, 0x005ADB03)");
	Check(!ports.ioBefore.has_value() && ports.ioAfter == kIoManager, "the second read decides alone");
	Check(ports.IndexOf(Event::InterfacePassEnd) < ports.IndexOf(Event::IoManagerAfter), "after 0x005792B0");

	Section("B17 taken: the second read null - 0x00432890 skipped");
	ports = RedrawingPorts();
	ports.ioManagers = {kIoManager, 0};
	static_cast<void>(logic::RunTick(ports, GameInputs()));
	Check(ports.ioBefore == kIoManager && !ports.ioAfter.has_value(), "before ran, after skipped");
}

void T1AndFullOrder() {
	Section("T1 and the longest path: the whole call order");
	FakePorts ports = RedrawingPorts();
	logic::Outcome outcome = logic::RunTick(ports, GameInputs(1));
	Check(outcome == logic::Outcome::TailCalled, "completes into the tail");
	const std::vector<Event> expected{
	    Event::TileMenuForType, Event::ContainingMenu,   Event::ContainingMenu,         Event::TickCount,
	    Event::UpdateClock,     Event::ReadClockMs,      Event::StepThresholds,         Event::ReadClockMs,
	    Event::SetTileFloat,    Event::RefreshScreen,    Event::ScreenPeriodSetting,    Event::InterfaceManager,
	    Event::IoManager,       Event::IoManagerBefore,  Event::InterfaceManager,       Event::InterfaceManagerUpdate,
	    Event::InterfacePass,   Event::InterfacePassEnd, Event::IoManager,              Event::IoManagerAfter,
	    Event::OsGlobals,       Event::MainThreadId,     Event::CurrentThreadId,        Event::OsGlobals,
	    Event::OsGlobalsTail,
	};
	Check(ports.events == expected, "the order of 0x005AD980's calls and reads");
	Check(ports.fields.lastTickMs == kBaselineNow, "+0x6C = now");
}

void B18MainThread() {
	Section("B18 not taken: GetCurrentThreadId() == [g_osGlobals+0x10] - tail into 0x0060D0A0");
	FakePorts ports;
	ports.osGlobals = {kOsGlobals, kOtherOsGlobals};
	logic::Outcome outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::TailCalled, "outcome: the tail");
	Check(ports.mainThreadAskedOf == kOsGlobals, "mainThreadID read from the first g_osGlobals read (0x005ADB15)");
	Check(ports.tailOn == kOtherOsGlobals, "the tail's ECX is g_osGlobals read again (0x005ADB2B)");
	Check(ports.IndexOf(Event::MainThreadId) < ports.IndexOf(Event::CurrentThreadId), "[g_osGlobals+0x10] is read before GetCurrentThreadId");
	Check(logic::OnMainThread(kMainThread, kMainThread), "decision: equal ids");

	Section("B18 taken: another thread - plain return, no tail");
	ports = FakePorts{};
	ports.currentThread = kOtherThread;
	outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::OffMainThread, "outcome: returned off the main thread");
	Check(ports.Count(Event::OsGlobalsTail) == 0 && ports.Count(Event::OsGlobals) == 1, "no tail, no second g_osGlobals read");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 written before the check");
	Check(!logic::OnMainThread(kOtherThread, kMainThread), "decision: different ids");

	Section("B18 refusal: g_osGlobals null - the original reads [0+0x10] at 0x005ADB1B");
	ports = FakePorts{};
	ports.osGlobals = {0};
	outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::FaultNullOsGlobals, "outcome: the null g_osGlobals read");
	Check(ports.Count(Event::MainThreadId) == 0 && ports.Count(Event::CurrentThreadId) == 0, "nothing past the read");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 was written before it");
}

void ControlWordGate() {
	Section("Control word: the x87 states the rebuild models (masks bits 0-5, PC bits 8-9, RC bits 10-11)");
	Check(logic::PrecisionOfControlWord(0x007F) == kSingle, "0x007F: PC 24, RC nearest, all masked (the game's, measured as _control87 0x000A001F)");
	Check(logic::PrecisionOfControlWord(0x027F) == kDouble, "0x027F: PC 53 (the MSVC runtime's start-up word)");
	Check(logic::PrecisionOfControlWord(0x107F) == kSingle, "bit 12 (infinity control) is not inspected");

	Section("Control word refusals: states the rebuild does not model - the original has to run");
	Check(!logic::PrecisionOfControlWord(0x037F).has_value(), "PC 64 (FNINIT's 0x037F): not modelled");
	Check(!logic::PrecisionOfControlWord(0x017F).has_value(), "PC 01B is reserved");
	Check(!logic::PrecisionOfControlWord(0x047F).has_value(), "RC 01B: round down");
	Check(!logic::PrecisionOfControlWord(0x087F).has_value(), "RC 10B: round up");
	Check(!logic::PrecisionOfControlWord(0x0C7F).has_value(), "RC 11B: truncate");
	Check(!logic::PrecisionOfControlWord(0x007E).has_value(), "invalid-operation exception unmasked");
	Check(!logic::PrecisionOfControlWord(0x005F).has_value(), "precision (inexact) exception unmasked");
}

// --- The host's x87, executing the original's instructions --------------------------------

#if !defined(__i386__)
#error "The x87 differential check executes x87 instructions: build the tests for i386 (-m32)."
#endif

struct HostCountDown {
	double elapsed;   // ST0 after FILD and the optional FADD (FST m64: exact for a 24- or 53-bit value)
	double quotient;  // ST0 after FDIV
	float stored;     // FSTP m32 after FSUBP
};

// 0x005ADA09..0x005ADA26 with the same opcodes: FLD m32 (D9 /0), FILD m32
// (DB /0), FADD m32 (D8 /0) when negative, FDIV m64 (DC /6), FSUBP (DE E9,
// ST1 = ST1 - ST0), FSTP m32 (D9 /3). GNU as spells DE E9 "fsubrp" in AT&T
// syntax (the SysV mnemonic swap); the disassembly check below confirms the
// bytes. FST m64 copies the intermediates out without changing them.
HostCountDown RunOnHost(float timer, std::int32_t sinceLast, std::uint16_t controlWord) {
	HostCountDown out{};
	std::uint16_t saved = 0;
	const float fixup = logic::kTwoPow32;
	const double divisor = logic::kMsPerSecond;
	__asm__ volatile(
	    "fnstcw %[saved]\n\t"
	    "fldcw %[cw]\n\t"
	    "flds %[timer]\n\t"
	    "fildl %[since]\n\t"
	    "cmpl $0, %[since]\n\t"
	    "jge 1f\n\t"
	    "fadds %[fixup]\n"
	    "1:\n\t"
	    "fstl %[elapsed]\n\t"
	    "fdivl %[divisor]\n\t"
	    "fstl %[quotient]\n\t"
	    "fsubrp\n\t"
	    "fstps %[stored]\n\t"
	    "fldcw %[saved]\n\t"
	    : [saved] "=m"(saved), [elapsed] "=m"(out.elapsed), [quotient] "=m"(out.quotient), [stored] "=m"(out.stored)
	    : [cw] "m"(controlWord), [timer] "m"(timer), [since] "m"(sinceLast), [fixup] "m"(fixup), [divisor] "m"(divisor)
	    : "cc", "st");
	return out;
}

// xorshift64, fixed seed: the same inputs on every run.
struct Random {
	std::uint64_t state = 0x9E3779B97F4A7C15ull;
	std::uint64_t Next() {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		return state;
	}
	bool OneIn(std::uint64_t n) { return Next() % n == 0; }
};

float RandomTimer(Random& random) {
	if (random.OneIn(2)) {  // 2^-31 .. 2^4: the seconds a loading screen counts
		return FromBits(static_cast<std::uint32_t>(0x30000000u + random.Next() % 0x11800000u));
	}
	return FromBits(static_cast<std::uint32_t>(random.Next()));  // any pattern: NaN, inf, subnormal, 0
}

std::int32_t RandomSinceLast(Random& random) {
	switch (random.Next() % 3) {
	case 0:
		return static_cast<std::int32_t>(random.Next() % 10001u) - 5000;
	case 1:
		return static_cast<std::int32_t>(random.Next() % (1u << 26)) - (1 << 25);
	default:
		return static_cast<std::int32_t>(static_cast<std::uint32_t>(random.Next()));
	}
}

void HostDifferential(logic::Precision precision, std::uint16_t controlWord, const char* label) {
	constexpr int kRuns = 300000;
	Random random;
	int mismatches = 0;
	for (int run = 0; run < kRuns; ++run) {
		const float timer = RandomTimer(random);
		const std::int32_t sinceLast = RandomSinceLast(random);
		const HostCountDown host = RunOnHost(timer, sinceLast, controlWord);
		const bool same = std::bit_cast<std::uint64_t>(logic::ElapsedForTimer(sinceLast, precision)) ==
		                      std::bit_cast<std::uint64_t>(host.elapsed) &&
		                  std::bit_cast<std::uint64_t>(logic::TimerQuotient(sinceLast, precision)) ==
		                      std::bit_cast<std::uint64_t>(host.quotient) &&
		                  Bits(logic::CountDown(timer, sinceLast, precision)) == Bits(host.stored);
		if (!same && mismatches++ == 0) {
			std::printf("    first mismatch %s: timer %08X sinceLast %d: host %08X, rebuild %08X\n", label, Bits(timer),
			            static_cast<int>(sinceLast), Bits(host.stored), Bits(logic::CountDown(timer, sinceLast, precision)));
		}
	}
	Check(mismatches == 0, label);
}

void HostX87() {
	Section("Host x87: the original's opcodes reproduce the emulator's rows (the harness is sound)");
	Check(Bits(RunOnHost(5.0f, -100, 0x007F).stored) == 0xCA831265u, "(5.0, -100) at 0x007F: 0xCA831265");
	Check(Bits(RunOnHost(5.0f, -100, 0x027F).stored) == 0xCA831264u, "(5.0, -100) at 0x027F: 0xCA831264");
	Check(Bits(RunOnHost(FromBits(0x3A83126Fu), 1, 0x007F).stored) == 0x00000000u, "(0.001f, 1) at 0x007F: 0");
	Check(Bits(RunOnHost(5.0f, -1121445008, 0x007F).stored) == 0xCA41B235u, "(5.0, -1121445008) at 0x007F: 0xCA41B235");

	Section("Host x87: the rebuild against the original's opcodes, 300000 random inputs each (timers include NaN, inf, subnormals)");
	HostDifferential(kSingle, 0x007F, "PC 24 (0x007F): elapsed, quotient and stored timer bit-equal");
	HostDifferential(kDouble, 0x027F, "PC 53 (0x027F): elapsed, quotient and stored timer bit-equal");
}

// The run on the engine layout itself: the same template writes the
// LoadingMenuLayout and reads a TimeInfo, so the field names and offsets the
// logic uses are the ones static_asserted in the engine header.
struct LayoutPorts {
	engine::LoadingMenuLayout menu{};
	obng::engine::time::TimeInfo clock{};
	std::optional<float> tileValue;

	std::uint32_t TileMenuForType(std::uint32_t) { return kTileMenu; }
	std::uint32_t ContainingMenu(std::uint32_t) { return kMenu; }
	engine::LoadingMenuLayout& Fields(std::uint32_t) { return menu; }
	std::uint32_t TickCount() { return kTick; }
	// A stand-in for 0x0047D170: only its +0x10 result matters here.
	void UpdateClock(obng::engine::time::TimeInfo* self, std::uint32_t tick) { self->clockMs = tick - self->startTick; }
	std::uint32_t ReadClockMs(obng::engine::time::TimeInfo* self) { return self->clockMs; }
	logic::StepThresholds StepThresholds() { return kImageThresholds; }
	void SetTileFloat(std::uint32_t, std::uint32_t, float value) { tileValue = value; }
	void RefreshScreen(std::uint32_t, std::uint32_t) {}
	float ScreenPeriodSetting() { return kScreenPeriod; }
	std::uint32_t InterfaceManager() { return 0; }
	std::uint32_t IoManager() { return 0; }
	void IoManagerBefore(std::uint32_t) {}
	void InterfaceManagerUpdate(std::uint32_t) {}
	void InterfacePass(std::uint32_t) {}
	void InterfacePassEnd() {}
	void IoManagerAfter(std::uint32_t) {}
	std::uint32_t OsGlobals() { return kOsGlobals; }
	std::uint32_t MainThreadId(std::uint32_t) { return kOtherThread; }
	std::uint32_t CurrentThreadId() { return kMainThread; }
	void OsGlobalsTail(std::uint32_t) {}
};

template <class T>
T ReadAt(const void* base, std::size_t offset) {
	T value;
	std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof(T));
	return value;
}

void EngineShape() {
	Section("Engine shape: the run writes the layout at the read offsets");
	LayoutPorts ports;
	ports.clock.startTick = kTick - 5000u;  // clockMs after the update: 5000
	ports.menu.clock = &ports.clock;
	ports.menu.slowStepCap = 10;
	ports.menu.screenTimeLeft = 3.0f;
	ports.menu.shownStep = 3;
	ports.menu.targetStep = 3;
	ports.menu.stepChangedMs = 2000u;  // sinceStep 3000 > 2500: advance
	ports.menu.lastTickMs = 4000u;     // sinceLast 1000: timer 3.0 -> 2.0
	logic::Outcome outcome = logic::RunTick(ports, GameInputs());
	Check(outcome == logic::Outcome::OffMainThread, "outcome: off the main thread (the fake's ids differ)");
	Check(ReadAt<std::int32_t>(&ports.menu, 0x64) == 4, "+0x64 targetStep = 4");
	Check(ReadAt<std::int32_t>(&ports.menu, 0x60) == 4, "+0x60 shownStep = 4");
	Check(ReadAt<std::uint32_t>(&ports.menu, 0x68) == 5000u, "+0x68 stepChangedMs = clock+0x10");
	Check(ReadAt<std::uint32_t>(&ports.menu, 0x6C) == 5000u, "+0x6C lastTickMs = now");
	Check(ReadAt<float>(&ports.menu, 0x40) == 2.0f, "+0x40 screenTimeLeft = 2.0");
	Check(ReadAt<std::uint32_t>(&ports.clock, 0x10) == 5000u, "TimeInfo+0x10 clockMs");
	Check(ports.tileValue == 4.0f, "the tile got 4.0");

	Section("Engine shape: addresses with two sources, and 32-bit function pointers");
	Check(engine::kInterfaceManagerGetSingletonAddress == 0x00582160u, "InterfaceManager_GetSingleton");
	Check(engine::kTileUpdateFloatAddress == 0x0058CEB0u, "Tile::UpdateFloat");
	Check(engine::kOsGlobalsAddress == 0x00B33398u, "g_osGlobals");
	Check(engine::kIoManagerAddress == 0x00B33A10u, "g_ioManager");
	Check(sizeof(engine::TickFn) == 4 && sizeof(engine::TileUpdateFloatFn) == 4 && sizeof(engine::RefreshScreenFn) == 4,
	      "function pointers are 32-bit");
	Check(offsetof(engine::OsGlobalsView, mainThreadId) == 0x10, "g_osGlobals+0x10 mainThreadID");
}

} // namespace

int main() {
	Constants();
	B1TileMenu();
	B2ContainingMenu();
	B3AlwaysAdvance();
	B4Cap();
	B5CappedAdvance();
	B6aFastFlag();
	B6bFastThreshold();
	B7UnsignedFixup();
	Countdown();
	B8TimerExpired();
	B9ForceRefresh();
	B10Unchanged();
	B11BarFull();
	B12CatchUp();
	B13Ceiling();
	B14Refresh();
	B15InterfaceManager();
	B16IoManagerBefore();
	B17IoManagerAfter();
	T1AndFullOrder();
	B18MainThread();
	ControlWordGate();
	HostX87();
	EngineShape();
	return Failures();
}
