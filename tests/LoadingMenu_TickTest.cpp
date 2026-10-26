// Tests of the rebuild of 0x005AD980 (proposed LoadingMenu_Tick): one tick of
// the loading screen. It advances the loading menu's private TimeInfo
// ([menu+0x54], not g_timeInfo 0x00B33E90), moves the loading bar's target
// step by three time thresholds, counts down the screen timer, pushes the
// shown step to the tile as trait 0xFB1, re-arms the timer and runs the
// interface calls on a refresh, and tail-jumps into 0x0060D0A0 on the main
// thread.
//
// One section per branch of the mapped control flow (re/functions/0x005AD980.md):
//   B1  tile menu for type 0x3EF null        0x005AD995
//   B2  containing menu null                 0x005AD9A4
//   B3  sinceStep > [0x00B14160]             0x005AD9DD
//   B4  targetStep >= slowStepCap            0x005AD9EB
//   B5  sinceStep > [0x00B14168]             0x005AD9F3
//   B6  fast flag, sinceStep > [0x00B14170]  0x005AD9F9 / 0x005ADA01
//   B7  sinceLast negative: unsigned fix-up  0x005ADA16
//   B8  timer expired                        0x005ADA3A
//   B9  forceRefresh                         0x005ADA46
//   B10 step unchanged and no refresh        0x005ADA55 / 0x005ADA5C
//   B11 shownStep >= 100                     0x005ADA65
//   B12 shownStep catches up                 0x005ADA6D
//   B13 step below [0x00A6C4C8]              0x005ADAA2
//   B14 refresh: 0x005AD780, re-arm          0x005ADAA9
//   B15 InterfaceManager null                0x005ADAD0
//   B16 g_ioManager null (first read)        0x005ADADA
//   B17 g_ioManager null (second read)       0x005ADB0B
//   B18 not the main thread                  0x005ADB29
// plus T1 (lastTickMs = now on every path past B2) and the refusal paths the
// original has no guard for: the second containing-menu lookup returning
// null (read [0+0x54] at 0x005AD9B6), a null clock (read [0+0x10] at
// 0x005AD9CA), and a null g_osGlobals (read [0+0x10] at 0x005ADB1B).
//
// The constants 0x00A2FC78 (float), 0x00A2FC70 (double) and 0x00A6C4C8
// (double) are not in the export (unverified values). The tests pass them
// explicitly: 2^32, 1000.0 and 100.0 as the expected reading, and other
// values where a test must show that the constant, not a literal, is used.
//
// The x87 precision control the game runs with at this call is unverified, so
// it is an input of the rebuild (Inputs::precision), not an assumption of the
// tests. The branch tests use timer values whose countdown is exact at 24, 53
// and 64 bits and run with Precision::Double; every test whose expectation
// depends on the precision names the precision it assumes and checks all
// three (B7 and the "x87" sections). The x87 model is checked against the
// host's x87 executing the original's opcodes (D9/0, DB/0, D8/0, DC/6, DE E9,
// D9/3 as at 0x005ADA0B-0x005ADA26) under each control word, so the tests
// need an i386 build.

#include "Check.h"
#include "engine/time/LoadingMenu_Tick.h"
#include "engine/time/LoadingMenu_TickLogic.h"

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
namespace x87 = obng::engine::time::loading_menu_tick::x87;

namespace {

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
constexpr float kScreenPeriod = 7.0f;

constexpr logic::TimerConstants kExpectedTimer{4294967296.0f, 1000.0};
constexpr double kExpectedCeiling = 100.0;
constexpr logic::StepThresholds kThresholds{3000, 1000, 500};

enum class Event {
	TileMenuForType,
	ContainingMenu,
	TickCount,
	UpdateClock,
	ReadClockLastMs,
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

// A baseline menu: no step rule fires (sinceStep 1000 is not above 1000,
// the fast flag is off), the timer goes 5.0 -> 4.5, nothing to redraw.
logic::MenuFields BaselineFields() {
	logic::MenuFields fields;
	fields.slowStepCap = 10;
	fields.screenTimeLeft = 5.0f;
	fields.clock = kClock;
	fields.shownStep = 3;
	fields.targetStep = 3;
	fields.stepChangedMs = 1000u;
	fields.lastTickMs = 1500u;
	fields.fastStepEnabled = 0;
	return fields;
}

constexpr std::uint32_t kBaselineNow = 2000u;

// Records every call the rebuild makes, in order, with its arguments. Each
// lookup the original makes twice has its own sequence of answers, so a test
// can tell the first read from the second.
struct FakePorts {
	logic::MenuFields fields = BaselineFields();
	std::vector<Event> events;

	std::uint32_t tileMenu = kTileMenu;
	std::uint32_t menuTypeAsked = 0;
	std::vector<std::uint32_t> containingMenus{kMenu, kMenu};
	std::size_t containingMenuCalls = 0;
	std::vector<std::uint32_t> containingMenuTiles;
	std::uint32_t fieldsAsked = 0;

	std::optional<std::uint32_t> clockAfterUpdate;  // the update replaces [menu+0x54]
	std::optional<std::pair<std::uint32_t, std::uint32_t>> updated;  // (clock, tick)
	std::vector<std::uint32_t> lastMsReadsFrom;
	std::vector<std::uint32_t> lastMs{kBaselineNow, kBaselineNow};
	std::size_t lastMsCalls = 0;
	logic::StepThresholds thresholds = kThresholds;

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

	std::uint32_t ReadClockLastMs(std::uint32_t clock) {
		events.push_back(Event::ReadClockLastMs);
		lastMsReadsFrom.push_back(clock);
		return lastMs.at(lastMsCalls++);
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

// The branch tests run at 53 bits; their timer values count down exactly at
// every precision, so the choice does not decide them (B7 and the x87
// sections check the precision-dependent cases).
logic::Inputs ExpectedInputs(std::uint8_t forceRefresh = 0, x87::Precision precision = x87::Precision::Double) {
	return logic::Inputs{forceRefresh, kExpectedTimer, kExpectedCeiling, precision};
}

// A menu whose shown step lags the target by one, so the redraw runs.
FakePorts RedrawingPorts() {
	FakePorts ports;
	ports.fields.targetStep = 4;
	return ports;
}

void B1TileMenu() {
	Section("B1 taken: 0x00589B70(0x3EF) returns null - return before anything else");
	FakePorts ports;
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs());
	Check(ports.menuTypeAsked == 0x3EF, "the lookup is asked for menu type 0x3EF (0x005AD984)");
	ports = FakePorts{};
	ports.tileMenu = 0;
	outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::NoTileMenu, "outcome: no tile menu (exit 0x005ADB3A)");
	Check(ports.events == std::vector<Event>{Event::TileMenuForType}, "only the lookup ran");
	Check(ports.fields.lastTickMs == BaselineFields().lastTickMs, "T1 is not reached: +0x6C untouched");
	Check(logic::IsNull(0u) && !logic::IsNull(kTileMenu), "decision: only zero is null");

	Section("B1 not taken: the tile menu goes on as ECX of 0x005898F0");
	ports = FakePorts{};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(!ports.containingMenuTiles.empty() && ports.containingMenuTiles[0] == kTileMenu, "GetContainingMenu on the tile menu");
}

void B2ContainingMenu() {
	Section("B2 taken: the first GetContainingMenu 0x005898F0 returns null - return");
	FakePorts ports;
	ports.containingMenus = {0};
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::NoMenu, "outcome: no menu (exit 0x005ADB3A)");
	Check(ports.events == std::vector<Event>{Event::TileMenuForType, Event::ContainingMenu}, "no clock update, no tick read");
	Check(ports.fields.lastTickMs == BaselineFields().lastTickMs, "T1 is not reached");

	Section("B2 not taken: a second lookup gives the menu; its clock [menu+0x54] is updated with GetTickCount");
	ports = FakePorts{};
	outcome = logic::RunTick(ports, ExpectedInputs());
	Check(ports.Count(Event::ContainingMenu) == 2, "GetContainingMenu is called twice (0x005AD99D, 0x005AD9AF)");
	Check(ports.containingMenuTiles == std::vector<std::uint32_t>{kTileMenu, kTileMenu}, "both on the tile menu");
	Check(ports.fieldsAsked == kMenu, "the menu fields are those of the second lookup's result");
	Check(ports.updated == std::make_pair(kClock, kTick), "0x0047D170(this = [menu+0x54], GetTickCount())");
	Check(ports.lastMsReadsFrom.size() == 1 && ports.lastMsReadsFrom[0] == kClock, "now = clock+0x10, read once on this path");
	Check(outcome == logic::Outcome::TailCalled, "the baseline reaches the tail");

	Section("B2: [menu+0x54] is read again after the update (0x005AD9C7)");
	ports = FakePorts{};
	ports.clockAfterUpdate = kOtherClock;
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.updated && ports.updated->first == kClock, "the update ran on the first read");
	Check(ports.lastMsReadsFrom.size() == 1 && ports.lastMsReadsFrom[0] == kOtherClock, "lastMs is read through the second read");

	Section("B2 refusal: the second lookup returns null - the original reads [0+0x54] at 0x005AD9B6");
	ports = FakePorts{};
	ports.containingMenus = {kMenu, 0};
	outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::FaultNullMenuRead, "outcome: the null menu read");
	Check(ports.Count(Event::UpdateClock) == 0 && ports.Count(Event::TickCount) == 0, "nothing past the read runs");

	Section("B2 refusal: null clock - the update still gets ECX = 0, then [0+0x10] at 0x005AD9CA faults");
	ports = FakePorts{};
	ports.fields.clock = 0;
	outcome = logic::RunTick(ports, ExpectedInputs());
	Check(ports.updated == std::make_pair(0u, kTick), "0x0047D170 is called with ECX = 0, no guard");
	Check(outcome == logic::Outcome::FaultNullClockRead, "outcome: the null clock read");
	Check(ports.Count(Event::ReadClockLastMs) == 0, "nothing reads through the null clock");
	Check(ports.fields.lastTickMs == BaselineFields().lastTickMs && ports.fields.screenTimeLeft == 5.0f,
		"no field is written");

	Section("B2: elapsed values are 32-bit differences, signed");
	Check(logic::ElapsedMs(2000u, 1000u) == 1000, "2000 - 1000");
	Check(logic::ElapsedMs(1000u, 2000u) == -1000, "1000 - 2000 is -1000 as signed");
	Check(logic::ElapsedMs(5u, 0xFFFFFFFBu) == 10, "across the 32-bit wrap: 10");
}

void B3AlwaysAdvance() {
	Section("B3 taken: sinceStep > [0x00B14160] advances the target step, without the cap");
	Check(logic::ChooseStepRule(3001, 3, 10, 0, kThresholds) == logic::StepRule::AlwaysAdvance, "3001 > 3000");
	Check(logic::ChooseStepRule(3001, 50, 10, 0, kThresholds) == logic::StepRule::AlwaysAdvance, "target 50 above cap 10: still advances");
	Check(logic::ChooseStepRule(3001, 0, -1, 0, kThresholds) == logic::StepRule::AlwaysAdvance, "the constructor's cap -1 does not block it");
	Check(logic::Advances(logic::StepRule::AlwaysAdvance), "the rule advances");

	Section("B3 not taken: equal, or negative as signed (JLE)");
	Check(logic::ChooseStepRule(3000, 50, 10, 0, kThresholds) == logic::StepRule::AtCap, "3000 is not above 3000: falls to B4");
	Check(logic::ChooseStepRule(-5000, 50, 10, 0, kThresholds) == logic::StepRule::AtCap, "a negative sinceStep is below");

	Section("B3 in the run: +0x64 goes 3 -> 4; the increment wraps at 32 bits");
	FakePorts ports;
	ports.fields.stepChangedMs = 0;
	ports.lastMs = {3001u, 3001u};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.fields.targetStep == 4 && ports.fields.shownStep == 4, "target advanced, shown caught up");
	Check(logic::NextStep(std::numeric_limits<std::int32_t>::max()) == std::numeric_limits<std::int32_t>::min(),
		"ADD [ESI+0x64],1 wraps INT32_MAX to INT32_MIN");
}

void B4Cap() {
	Section("B4 taken: targetStep >= slowStepCap - no increment (JGE)");
	Check(logic::ChooseStepRule(2000, 10, 10, 1, kThresholds) == logic::StepRule::AtCap, "equal to the cap");
	Check(logic::ChooseStepRule(2000, 0, -1, 1, kThresholds) == logic::StepRule::AtCap, "the constructor's cap -1 blocks B5/B6");
	Check(!logic::Advances(logic::StepRule::AtCap), "the rule does not advance");
	FakePorts ports;
	ports.fields.slowStepCap = 3;
	ports.fields.stepChangedMs = 0;  // sinceStep 2000 > 1000, would advance under the cap
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.fields.targetStep == 3, "run: +0x64 stays 3");

	Section("B4 not taken: below the cap, signed (-1 < 0)");
	Check(logic::ChooseStepRule(2000, -2, -1, 0, kThresholds) == logic::StepRule::CappedAdvance, "-2 is below -1");
}

void B5CappedAdvance() {
	Section("B5 taken: under the cap and sinceStep > [0x00B14168] - advance");
	Check(logic::ChooseStepRule(1001, 3, 10, 0, kThresholds) == logic::StepRule::CappedAdvance, "1001 > 1000");
	Check(logic::Advances(logic::StepRule::CappedAdvance), "the rule advances");
	FakePorts ports;
	ports.fields.stepChangedMs = 999u;  // sinceStep 1001
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.fields.targetStep == 4, "run: +0x64 goes 3 -> 4");

	Section("B5 not taken: equal goes to B6 (JG)");
	Check(logic::ChooseStepRule(1000, 3, 10, 0, kThresholds) == logic::StepRule::FastDisabled, "1000 is not above 1000");
}

void B6FastAdvance() {
	Section("B6: fast flag +0x70 zero - no advance, whatever sinceStep");
	Check(logic::ChooseStepRule(900, 3, 10, 0, kThresholds) == logic::StepRule::FastDisabled, "900 > 500 but the flag is off");
	Check(!logic::Advances(logic::StepRule::FastDisabled), "no advance");

	Section("B6: fast flag set and sinceStep > [0x00B14170] - advance");
	Check(logic::ChooseStepRule(501, 3, 10, 1, kThresholds) == logic::StepRule::FastAdvance, "501 > 500");
	Check(logic::ChooseStepRule(501, 3, 10, 0x80, kThresholds) == logic::StepRule::FastAdvance, "any nonzero byte is set (CMP byte,0)");
	Check(logic::Advances(logic::StepRule::FastAdvance), "the rule advances");
	FakePorts ports;
	ports.fields.fastStepEnabled = 1;
	ports.fields.stepChangedMs = 1400u;  // sinceStep 600
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.fields.targetStep == 4, "run: +0x64 goes 3 -> 4");

	Section("B6: fast flag set, sinceStep at or below [0x00B14170] - no advance (JLE)");
	Check(logic::ChooseStepRule(500, 3, 10, 1, kThresholds) == logic::StepRule::NotYet, "500 is not above 500");
	Check(!logic::Advances(logic::StepRule::NotYet), "no advance");
	ports = FakePorts{};
	ports.fields.fastStepEnabled = 1;
	ports.fields.stepChangedMs = 1500u;  // sinceStep 500
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.fields.targetStep == 3, "run: +0x64 stays 3");
}

std::uint32_t FloatBits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

constexpr x87::Precision kSingle = x87::Precision::Single;
constexpr x87::Precision kDouble = x87::Precision::Double;
constexpr x87::Precision kExtended = x87::Precision::Extended;

void B7UnsignedFixup() {
	Section("B7 not taken: sinceLast >= 0 is used as is (FILD 0x005ADA12, exact)");
	Check(!logic::NeedsUnsignedFixup(0) && !logic::NeedsUnsignedFixup(500), "0 and 500 need no fix-up");
	Check(logic::ElapsedForTimer(500, kExpectedTimer.unsignedFixup, kSingle) == x87::FromInt32(500) &&
			logic::ElapsedForTimer(500, kExpectedTimer.unsignedFixup, kDouble) == x87::FromInt32(500) &&
			logic::ElapsedForTimer(500, kExpectedTimer.unsignedFixup, kExtended) == x87::FromInt32(500),
		"500 ms at 24, 53 and 64 bits: no FADD, nothing rounded");
	Check(logic::CountDown(5.0f, 500, kExpectedTimer, kSingle) == 4.5f &&
			logic::CountDown(5.0f, 500, kExpectedTimer, kDouble) == 4.5f &&
			logic::CountDown(5.0f, 500, kExpectedTimer, kExtended) == 4.5f,
		"5.0 - 500 / 1000 = 4.5 at every precision (0.5 and 4.5 are exact)");
	Check(logic::CountDown(5.0f, 500, logic::TimerConstants{4294967296.0f, 500.0}, kDouble) == 4.0f,
		"the divisor is the constant 0x00A2FC70, not a literal 1000");
	FakePorts ports;
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.fields.screenTimeLeft == 4.5f, "run: +0x40 goes 5.0 -> 4.5");

	Section("B7 taken: sinceLast < 0 as signed gets the float [0x00A2FC78] added (FADD 0x005ADA18, rounded to PC)");
	Check(logic::NeedsUnsignedFixup(-1), "-1 needs the fix-up");
	Check(logic::ElapsedForTimer(-1, kExpectedTimer.unsignedFixup, kDouble) == x87::FromDouble(4294967295.0),
		"PC53: -1 + 2^32 = 0xFFFFFFFF, exact in 53 bits");
	Check(logic::ElapsedForTimer(-1, kExpectedTimer.unsignedFixup, kExtended) == x87::FromDouble(4294967295.0),
		"PC64: exact as well");
	Check(logic::ElapsedForTimer(-1, kExpectedTimer.unsignedFixup, kSingle) == x87::FromDouble(4294967296.0),
		"PC24: 0xFFFFFFFF needs 32 bits; rounded to 24 it is 2^32");
	Check(logic::ElapsedForTimer(-1, 0.0f, kDouble) == x87::FromInt32(-1),
		"the added value is the constant, not a literal 2^32");
	ports = FakePorts{};
	ports.fields.lastTickMs = kBaselineNow + 100u;  // the clock went back: sinceLast -100
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.refreshed.has_value(), "run: the timer falls far below zero and the refresh runs");
	Check(ports.fields.screenTimeLeft == kScreenPeriod, "run: and is re-armed from 0x00B14158");
	// 5 - (2^32 - 100) / 1000 = -4294962.196 (measured on the host x87, see the header).
	Check(FloatBits(logic::CountDown(5.0f, -100, kExpectedTimer, kDouble)) == FloatBits(-4294962.0f) &&
			FloatBits(logic::CountDown(5.0f, -100, kExpectedTimer, kExtended)) == FloatBits(-4294962.0f),
		"PC53 and PC64: stored -4294962.0");
	Check(FloatBits(logic::CountDown(5.0f, -100, kExpectedTimer, kSingle)) == FloatBits(-4294962.5f),
		"PC24: the quotient is rounded to 24 bits first, stored -4294962.5");
}

void B7Precision() {
	Section("B7: the precision control decides the stored timer (a frame of 37 ms)");
	const float timer = std::bit_cast<float>(0x3FFF3CD1u);  // 1.99404347
	Check(FloatBits(logic::CountDown(timer, 37, kExpectedTimer, kDouble)) == 0x3FFA8067u &&
			FloatBits(logic::CountDown(timer, 37, kExpectedTimer, kExtended)) == 0x3FFA8067u,
		"PC53 and PC64: 1.95704353 (0x3FFA8067)");
	Check(FloatBits(logic::CountDown(timer, 37, kExpectedTimer, kSingle)) == 0x3FFA8066u,
		"PC24: 1.95704341 (0x3FFA8066), one ulp lower");

	Section("B7/B8: the precision control decides the expiry (timer 0.3, a frame of 300 ms)");
	Check(logic::TimerQuotient(300, kExpectedTimer, kSingle) == x87::FromFloat(0.3f),
		"PC24: 300 / 1000 rounds to the float nearest 0.3, which is the timer itself");
	Check(logic::TimerQuotient(300, kExpectedTimer, kDouble) == x87::FromDouble(0.3),
		"PC53: 300 / 1000 rounds to the double nearest 0.3");
	Check(logic::TimerQuotient(300, kExpectedTimer, kExtended) != x87::FromDouble(0.3),
		"PC64: 300 / 1000 keeps 64 bits, not the double");
	Check(FloatBits(logic::CountDown(0.3f, 300, kExpectedTimer, kSingle)) == 0u,
		"PC24: 0.3f - 0.3f = +0.0, expired");
	Check(FloatBits(logic::CountDown(0.3f, 300, kExpectedTimer, kDouble)) == 0x324CCCCDu &&
			FloatBits(logic::CountDown(0.3f, 300, kExpectedTimer, kExtended)) == 0x324CCCCDu,
		"PC53 and PC64: 1.19209291e-08 is left, not expired");

	FakePorts atSingle;
	atSingle.fields.screenTimeLeft = 0.3f;
	atSingle.fields.lastTickMs = kBaselineNow - 300u;
	static_cast<void>(logic::RunTick(atSingle, ExpectedInputs(0, kSingle)));
	Check(atSingle.refreshed.has_value() && atSingle.fields.screenTimeLeft == kScreenPeriod,
		"run at PC24: B8 taken, 0x005AD780 runs and the timer is re-armed");
	FakePorts atDouble;
	atDouble.fields.screenTimeLeft = 0.3f;
	atDouble.fields.lastTickMs = kBaselineNow - 300u;
	static_cast<void>(logic::RunTick(atDouble, ExpectedInputs(0, kDouble)));
	Check(!atDouble.refreshed.has_value() && FloatBits(atDouble.fields.screenTimeLeft) == 0x324CCCCDu,
		"run at PC53: B8 not taken, +0x40 keeps 1.19209291e-08");
	FakePorts atExtended;
	atExtended.fields.screenTimeLeft = 0.3f;
	atExtended.fields.lastTickMs = kBaselineNow - 300u;
	static_cast<void>(logic::RunTick(atExtended, ExpectedInputs(0, kExtended)));
	Check(!atExtended.refreshed.has_value() && FloatBits(atExtended.fields.screenTimeLeft) == 0x324CCCCDu,
		"run at PC64: as PC53");
}

// --- The x87 model ---------------------------------------------------------------------

x87::Register Finite(bool negative, std::uint64_t significand, std::int32_t exponent) {
	return x87::Register{x87::Register::Kind::Finite, negative, significand, exponent};
}

x87::Register Nan(bool negative, std::uint64_t significand) {
	return x87::Register{x87::Register::Kind::NaN, negative, significand, 0};
}

constexpr std::uint64_t kIntegerBit = 1ull << 63;
constexpr std::uint64_t kQuietNanBits = 0xC000000000000000ull;

void X87ControlWord() {
	Section("x87: the control word the rebuild models (masks bits 0-5, PC bits 8-9, RC bits 10-11)");
	Check(x87::SupportedPrecision(0x037F) == kExtended, "0x037F (FNINIT): 64 bits");
	Check(x87::SupportedPrecision(0x027F) == kDouble, "0x027F (CRT start-up): 53 bits");
	Check(x87::SupportedPrecision(0x007F) == kSingle, "0x007F (Direct3D 9 without FPU_PRESERVE): 24 bits");
	Check(x87::SupportedPrecision(0x127F) == kDouble, "bit 12 (infinity control) has no effect and is not inspected");

	Section("x87 refusals: a state the rebuild does not model");
	Check(!x87::SupportedPrecision(0x017F).has_value(), "PC 01B is reserved");
	Check(!x87::SupportedPrecision(0x067F).has_value(), "RC 01B: round down");
	Check(!x87::SupportedPrecision(0x0A7F).has_value(), "RC 10B: round up");
	Check(!x87::SupportedPrecision(0x0E7F).has_value(), "RC 11B: truncate (as 0x0047D170 sets it for a moment)");
	Check(!x87::SupportedPrecision(0x027E).has_value(), "invalid-operation exception unmasked");
	Check(!x87::SupportedPrecision(0x025F).has_value(), "precision (inexact) exception unmasked");
}

void X87Loads() {
	Section("x87 loads: FILD m32 is exact");
	Check(x87::FromInt32(0) == x87::Register{}, "0 is +0");
	Check(x87::FromInt32(-1) == Finite(true, kIntegerBit, 0), "-1");
	Check(x87::FromInt32(std::numeric_limits<std::int32_t>::min()) == Finite(true, kIntegerBit, 31), "-2^31");
	Check(x87::FromInt32(0x7FFFFFFF) == Finite(false, 0xFFFFFFFE00000000ull, 30), "2^31 - 1");

	Section("x87 loads: m32fp and m64fp convert exactly, a signaling NaN is quieted");
	Check(x87::FromFloat(1.0f) == Finite(false, kIntegerBit, 0), "1.0f");
	Check(x87::FromFloat(-0.0f) == x87::Register{x87::Register::Kind::Zero, true, 0, 0}, "-0.0f");
	Check(x87::FromFloat(std::numeric_limits<float>::denorm_min()) == Finite(false, kIntegerBit, -149),
		"a float subnormal is normalized");
	Check(x87::FromFloat(-std::numeric_limits<float>::infinity()) ==
			x87::Register{x87::Register::Kind::Infinity, true, kIntegerBit, 0},
		"-inf");
	Check(x87::FromFloat(std::bit_cast<float>(0x7FC00001u)) == Nan(false, 0xC000010000000000ull), "a quiet NaN");
	Check(x87::FromFloat(std::bit_cast<float>(0x7F800001u)) == Nan(false, 0xC000010000000000ull),
		"a signaling NaN arrives quiet");
	Check(x87::FromDouble(1000.0) == Finite(false, 0xFA00000000000000ull, 9), "1000.0");
	Check(x87::FromDouble(std::numeric_limits<double>::denorm_min()) == Finite(false, kIntegerBit, -1074),
		"a double subnormal is normalized");
	Check(x87::FromDouble(std::bit_cast<double>(0xFFF0000000000001ull)) == Nan(true, 0xC000000000000800ull),
		"a negative signaling double NaN arrives quiet");
}

void X87Add() {
	Section("x87 FADD: zeros, infinities, NaNs");
	const x87::Register plusZero = x87::FromFloat(0.0f);
	const x87::Register minusZero = x87::FromFloat(-0.0f);
	const x87::Register plusInf = x87::FromFloat(std::numeric_limits<float>::infinity());
	const x87::Register minusInf = x87::FromFloat(-std::numeric_limits<float>::infinity());
	Check(x87::Add(plusZero, minusZero, kDouble) == plusZero, "+0 + -0 = +0");
	Check(x87::Add(minusZero, minusZero, kDouble) == minusZero, "-0 + -0 = -0");
	Check(x87::Add(plusInf, x87::FromInt32(5), kDouble) == plusInf, "inf + 5 = inf");
	Check(x87::Add(x87::FromInt32(5), minusInf, kDouble) == minusInf, "5 + -inf = -inf");
	Check(x87::Add(plusInf, plusInf, kDouble) == plusInf, "inf + inf = inf");
	Check(x87::Add(plusInf, minusInf, kDouble) == Nan(true, kQuietNanBits), "inf + -inf: the real indefinite");
	Check(x87::Add(Nan(false, 0xC000000000000001ull), x87::FromInt32(1), kDouble) == Nan(false, 0xC000000000000001ull),
		"a NaN operand comes through");
	Check(x87::Add(Nan(false, 0xC000000000000001ull), Nan(true, 0xE000000000000000ull), kDouble) ==
			Nan(true, 0xE000000000000000ull),
		"two NaNs: the larger significand");
	Check(x87::Add(Nan(true, 0xC000000000000001ull), Nan(false, 0xC000000000000001ull), kDouble) ==
			Nan(false, 0xC000000000000001ull),
		"two NaNs, equal significands: the positive one (measured on the host x87)");

	Section("x87 FADD: a zero operand still rounds the other one to the precision");
	Check(x87::Add(x87::FromInt32(0x7FFFFFFF), plusZero, kSingle) == x87::FromDouble(2147483648.0),
		"PC24: 2^31 - 1 + 0 = 2^31");
	Check(x87::Add(plusZero, x87::FromInt32(0x7FFFFFFF), kDouble) == x87::FromInt32(0x7FFFFFFF),
		"PC53: 2^31 - 1 kept");

	Section("x87 FADD: same signs, with and without a carry");
	Check(x87::Add(x87::FromFloat(1.5f), x87::FromFloat(1.5f), kSingle) == x87::FromFloat(3.0f), "1.5 + 1.5 = 3");
	Check(x87::Add(x87::FromInt32(-2), x87::FromInt32(-3), kSingle) == x87::FromInt32(-5), "-2 + -3 = -5");

	Section("x87 FADD: round to nearest, ties to even, at each width");
	Check(x87::Add(x87::FromInt32(16777216), x87::FromInt32(1), kSingle) == x87::FromInt32(16777216),
		"PC24: 2^24 + 1 is a tie, down to the even 2^24");
	Check(x87::Add(x87::FromInt32(16777218), x87::FromInt32(1), kSingle) == x87::FromInt32(16777220),
		"PC24: 2^24 + 3 is a tie, up to the even 2^24 + 4");
	Check(x87::Add(x87::FromInt32(16777216), x87::FromFloat(1.5f), kSingle) == x87::FromInt32(16777218),
		"PC24: above the half rounds up");
	Check(x87::Add(x87::FromInt32(16777216), x87::FromInt32(1), kDouble) == x87::FromInt32(16777217),
		"PC53: 2^24 + 1 is exact");
	Check(x87::Add(x87::FromDouble(9007199254740992.0), x87::FromInt32(1), kDouble) ==
			x87::FromDouble(9007199254740992.0),
		"PC53: 2^53 + 1 is a tie, down to 2^53");
	Check(x87::Add(x87::FromDouble(9007199254740992.0), x87::FromInt32(1), kExtended) ==
			Finite(false, kIntegerBit | (1ull << 10), 53),
		"PC64: 2^53 + 1 is exact");
	Check(x87::Add(x87::FromDouble(18446744073709551616.0), x87::FromInt32(1), kExtended) ==
			x87::FromDouble(18446744073709551616.0),
		"PC64: 2^64 + 1 is a tie, down to 2^64");
	Check(x87::Add(x87::FromDouble(18446744073709551616.0), x87::FromInt32(3), kExtended) ==
			Finite(false, kIntegerBit | 2u, 64),
		"PC64: 2^64 + 3 is a tie, up to the even 2^64 + 4");
	Check(x87::Add(x87::FromDouble(18446744073709551616.0), x87::FromFloat(-0.5f), kExtended) ==
			x87::FromDouble(18446744073709551616.0),
		"PC64: 2^64 - 0.5 is a tie between 2^64 - 1 and 2^64; the carry out of bit 63 renormalizes");

	Section("x87 FADD: opposite signs");
	Check(x87::Add(x87::FromInt32(-5), x87::FromInt32(5), kSingle) == plusZero, "-5 + 5 = +0");
	Check(x87::Add(x87::FromDouble(1.0 + 0x1p-52), x87::FromInt32(-1), kDouble) == x87::FromDouble(0x1p-52),
		"(1 + 2^-52) - 1 = 2^-52: full cancellation is exact");
	Check(x87::Add(x87::FromInt32(1), x87::FromDouble(-0x1p-100), kExtended) == x87::FromInt32(1),
		"1 - 2^-100: more than 128 bits apart, the far operand is only a sticky bit");
	Check(x87::Add(x87::FromInt32(1), Finite(true, kIntegerBit, -65), kExtended) == x87::FromInt32(1),
		"PC64: 1 - 2^-65 is a tie between 1 - 2^-64 and 1, to the even 1");
	Check(x87::Add(x87::FromInt32(1), Finite(true, kIntegerBit | 1u, -65), kExtended) ==
			Finite(false, 0xFFFFFFFFFFFFFFFFull, -1),
		"PC64: 1 - (2^-65 + 2^-128): the bit shifted out below breaks the tie, down to 1 - 2^-64");
	Check(x87::Add(x87::FromDouble(1.0 + 0x1p-52), x87::FromDouble(-0x1p-53 - 0x1p-105), kDouble) ==
			x87::FromInt32(1),
		"PC53: (1 + 2^-52) - (2^-53 + 2^-105) is just below the tie, rounds down to 1");
	Check(x87::Subtract(x87::FromInt32(3), x87::FromInt32(5), kSingle) == x87::FromInt32(-2), "FSUB: 3 - 5 = -2");
	Check(x87::Subtract(plusZero, plusZero, kSingle) == plusZero, "FSUB: +0 - +0 = +0");
	Check(x87::Subtract(plusInf, plusInf, kSingle) == Nan(true, kQuietNanBits), "FSUB: inf - inf: the real indefinite");
	Check(x87::Subtract(x87::FromInt32(1), Nan(true, 0xC000000000000001ull), kSingle) == Nan(true, 0xC000000000000001ull),
		"FSUB: a NaN subtrahend keeps its sign");
}

void X87Divide() {
	Section("x87 FDIV: zeros, infinities, NaNs");
	const x87::Register plusZero = x87::FromFloat(0.0f);
	const x87::Register minusZero = x87::FromFloat(-0.0f);
	const x87::Register plusInf = x87::FromFloat(std::numeric_limits<float>::infinity());
	const x87::Register minusInf = x87::FromFloat(-std::numeric_limits<float>::infinity());
	Check(x87::Divide(plusInf, plusInf, kDouble) == Nan(true, kQuietNanBits), "inf / inf: the real indefinite");
	Check(x87::Divide(plusZero, minusZero, kDouble) == Nan(true, kQuietNanBits), "0 / -0: the real indefinite");
	Check(x87::Divide(plusInf, x87::FromInt32(-2), kDouble) == minusInf, "inf / -2 = -inf");
	Check(x87::Divide(x87::FromInt32(-2), plusInf, kDouble) == minusZero, "-2 / inf = -0");
	Check(x87::Divide(plusZero, x87::FromInt32(-2), kDouble) == minusZero, "+0 / -2 = -0");
	Check(x87::Divide(x87::FromInt32(-2), minusZero, kDouble) == plusInf, "-2 / -0 = +inf (zero divide, masked)");
	Check(x87::Divide(x87::FromInt32(1), Nan(false, 0xC000000000000001ull), kDouble) == Nan(false, 0xC000000000000001ull),
		"a NaN divisor comes through");

	Section("x87 FDIV: finite quotients, at each width");
	Check(x87::Divide(x87::FromInt32(6), x87::FromInt32(-3), kSingle) == x87::FromInt32(-2), "6 / -3 = -2, exact");
	Check(x87::Divide(x87::FromInt32(3), x87::FromInt32(2), kSingle) == x87::FromFloat(1.5f),
		"3 / 2: the dividend's significand is the larger one");
	Check(x87::Divide(x87::FromInt32(1), x87::FromInt32(3), kSingle) == x87::FromFloat(1.0f / 3.0f),
		"PC24: 1 / 3 is the float nearest 1/3");
	Check(x87::Divide(x87::FromInt32(1), x87::FromInt32(3), kDouble) == x87::FromDouble(1.0 / 3.0),
		"PC53: 1 / 3 is the double nearest 1/3");
	Check(x87::Divide(x87::FromInt32(1), x87::FromInt32(3), kExtended) == Finite(false, 0xAAAAAAAAAAAAAAABull, -2),
		"PC64: 1 / 3 = 0xAAAAAAAAAAAAAAAB * 2^-65, rounded up");
}

void X87StoreFloat() {
	Section("x87 FSTP m32: specials");
	Check(FloatBits(x87::ToFloat(x87::FromFloat(-0.0f))) == 0x80000000u, "-0");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(-std::numeric_limits<double>::infinity()))) == 0xFF800000u, "-inf");
	Check(FloatBits(x87::ToFloat(Nan(true, kQuietNanBits))) == 0xFFC00000u, "the real indefinite");
	Check(FloatBits(x87::ToFloat(Nan(false, 0xC000010000000FFFull))) == 0x7FC00001u,
		"a NaN keeps the top 23 fraction bits");

	Section("x87 FSTP m32: normal results round to 24 bits");
	Check(x87::ToFloat(x87::FromDouble(1.0 + 0x1p-24)) == 1.0f, "1 + 2^-24 is a tie, down to 1");
	Check(x87::ToFloat(x87::FromDouble(1.0 + 0x1p-23 + 0x1p-24)) == 1.0f + 0x1p-22f, "1 + 3 * 2^-24 is a tie, up");
	Check(x87::ToFloat(x87::FromDouble(2.0 - 0x1p-25)) == 2.0f, "2 - 2^-25 carries into the exponent");
	Check(x87::ToFloat(x87::FromDouble(0x1p127 * (2.0 - 0x1p-24))) == std::numeric_limits<float>::infinity(),
		"the tie above the largest float overflows to inf");
	Check(x87::ToFloat(x87::FromDouble(0x1p128)) == std::numeric_limits<float>::infinity(), "2^128 overflows");
	Check(x87::ToFloat(x87::FromDouble(-0x1p127 * (2.0 - 0x1p-23))) == -std::numeric_limits<float>::max(),
		"the largest float is kept");

	Section("x87 FSTP m32: below 2^-126 the result is a subnormal (underflow masked)");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(0x1p-127))) == 0x00400000u, "2^-127");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(0x1p-126 - 0x1p-150))) == 0x00800000u,
		"a tie below 2^-126 rounds up to the smallest normal");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(0x1p-149))) == 0x00000001u, "2^-149");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(0x1p-150))) == 0x00000000u, "2^-150 is a tie, down to 0");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(-0x1p-150 * 1.5))) == 0x80000001u, "-1.5 * 2^-150 rounds to -2^-149");
	Check(FloatBits(x87::ToFloat(x87::FromDouble(0x1p-151))) == 0x00000000u, "2^-151 is below the half, 0");
}

// --- The host's x87, executing the original's instructions ---------------------------------

#if !defined(__i386__)
#error "The x87 differential check executes x87 instructions: build the tests for i386 (-m32)."
#endif

struct Extended80 {
	std::uint64_t significand;
	std::uint16_t signExponent;
} __attribute__((packed));
static_assert(sizeof(Extended80) == 10, "the m80fp image");

constexpr std::int32_t kExtendedBias = 16383;

x87::Register FromImage(const Extended80& image) {
	bool negative = (image.signExponent >> 15) != 0;
	std::int32_t biased = image.signExponent & 0x7FFF;
	if (biased == 0x7FFF) {
		return (image.significand << 1) == 0 ? x87::Register{x87::Register::Kind::Infinity, negative, kIntegerBit, 0}
											  : Nan(negative, image.significand);
	}
	if (biased == 0 && image.significand == 0) {
		return x87::Register{x87::Register::Kind::Zero, negative, 0, 0};
	}
	// An extended denormal would come back unnormalized here and so not match the model,
	// which says none can arise.
	return Finite(negative, image.significand, biased - kExtendedBias);
}

Extended80 ToImage(const x87::Register& value) {
	std::uint16_t sign = value.negative ? 0x8000 : 0;
	switch (value.kind) {
	case x87::Register::Kind::Zero:
		return Extended80{0, sign};
	case x87::Register::Kind::Finite:
		return Extended80{value.significand, static_cast<std::uint16_t>(sign | (value.exponent + kExtendedBias))};
	case x87::Register::Kind::Infinity:
	case x87::Register::Kind::NaN:
		return Extended80{value.significand, static_cast<std::uint16_t>(sign | 0x7FFF)};
	}
	return Extended80{};
}

std::uint16_t ControlWord(x87::Precision precision) {
	return static_cast<std::uint16_t>(0x007F | (static_cast<std::uint16_t>(precision) << 8));
}

struct HostCountDown {
	Extended80 elapsed;     // after FILD and the optional FADD
	Extended80 quotient;    // after FDIV
	Extended80 difference;  // after FSUBP
	float stored;           // FSTP m32
};

// 0x005ADA0B-0x005ADA26 with the same opcodes: FLD m32 (D9 /0), FILD m32
// (DB /0), FADD m32 (D8 /0) when negative, FDIV m64 (DC /6), FSUBP (DE E9,
// ST1 = ST1 - ST0), FSTP m32 (D9 /3). "fsubrp" is the AT&T spelling of DE E9.
// FLD ST(0) + FSTP m80 copy each intermediate out without rounding.
HostCountDown RunOnHost(float timer, std::int32_t sinceLast, const logic::TimerConstants& constants, std::uint16_t cw) {
	HostCountDown out{};
	std::uint16_t saved = 0;
	__asm__ volatile(
		"fnstcw %[saved]\n\t"
		"fldcw %[cw]\n\t"
		"flds %[timer]\n\t"
		"fildl %[since]\n\t"
		"cmpl $0, %[since]\n\t"
		"jge 1f\n\t"
		"fadds %[fixup]\n"
		"1:\n\t"
		"fld %%st(0)\n\t"
		"fstpt %[elapsed]\n\t"
		"fdivl %[divisor]\n\t"
		"fld %%st(0)\n\t"
		"fstpt %[quotient]\n\t"
		"fsubrp\n\t"
		"fld %%st(0)\n\t"
		"fstpt %[difference]\n\t"
		"fstps %[stored]\n\t"
		"fldcw %[saved]\n\t"
		: [saved] "=m"(saved), [elapsed] "=m"(out.elapsed), [quotient] "=m"(out.quotient),
		[difference] "=m"(out.difference), [stored] "=m"(out.stored)
		: [cw] "m"(cw), [timer] "m"(timer), [since] "m"(sinceLast), [fixup] "m"(constants.unsignedFixup),
		[divisor] "m"(constants.divisor)
		: "cc", "st");
	return out;
}

enum class Operation { Add, Subtract, Divide };

// FADDP / FSUBP / FDIVP ST1, ST0 on two register images (DE C1, DE E9, DE F9).
x87::Register BinaryOnHost(Operation operation, const x87::Register& left, const x87::Register& right, std::uint16_t cw) {
	Extended80 a = ToImage(left);
	Extended80 b = ToImage(right);
	Extended80 result{};
	std::uint16_t saved = 0;
	switch (operation) {
	case Operation::Add:
		__asm__ volatile("fnstcw %[saved]\n\tfldcw %[cw]\n\tfldt %[a]\n\tfldt %[b]\n\tfaddp\n\tfstpt %[r]\n\tfldcw %[saved]"
			: [saved] "=m"(saved), [r] "=m"(result) : [cw] "m"(cw), [a] "m"(a), [b] "m"(b) : "st");
		break;
	case Operation::Subtract:
		__asm__ volatile("fnstcw %[saved]\n\tfldcw %[cw]\n\tfldt %[a]\n\tfldt %[b]\n\tfsubrp\n\tfstpt %[r]\n\tfldcw %[saved]"
			: [saved] "=m"(saved), [r] "=m"(result) : [cw] "m"(cw), [a] "m"(a), [b] "m"(b) : "st");
		break;
	case Operation::Divide:
		__asm__ volatile("fnstcw %[saved]\n\tfldcw %[cw]\n\tfldt %[a]\n\tfldt %[b]\n\tfdivrp\n\tfstpt %[r]\n\tfldcw %[saved]"
			: [saved] "=m"(saved), [r] "=m"(result) : [cw] "m"(cw), [a] "m"(a), [b] "m"(b) : "st");
		break;
	}
	return FromImage(result);
}

float StoreOnHost(const x87::Register& value) {
	Extended80 a = ToImage(value);
	float out = 0.0f;
	__asm__ volatile("fldt %[a]\n\tfstps %[out]" : [out] "=m"(out) : [a] "m"(a) : "st");
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
		return std::bit_cast<float>(static_cast<std::uint32_t>(0x30000000u + random.Next() % 0x11800000u));
	}
	return std::bit_cast<float>(static_cast<std::uint32_t>(random.Next()));  // any pattern: NaN, inf, subnormal, 0
}

std::int32_t RandomSinceLast(Random& random) {
	if (random.OneIn(2)) {
		return static_cast<std::int32_t>(random.Next() % 10001u) - 5000;
	}
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(random.Next()));
}

logic::TimerConstants RandomConstants(Random& random) {
	logic::TimerConstants constants = kExpectedTimer;
	if (random.OneIn(3)) {
		constants.unsignedFixup = std::bit_cast<float>(static_cast<std::uint32_t>(random.Next()));
	}
	if (random.OneIn(3)) {
		constants.divisor = std::bit_cast<double>(random.Next());
	}
	return constants;
}

x87::Register RandomRegister(Random& random, std::int32_t exponent) {
	switch (random.Next() % 16) {
	case 0:
		return x87::Register{x87::Register::Kind::Zero, random.OneIn(2), 0, 0};
	case 1:
		return x87::Register{x87::Register::Kind::Infinity, random.OneIn(2), kIntegerBit, 0};
	case 2:
		return Nan(random.OneIn(2), kQuietNanBits | (random.Next() >> 2));
	default: {
		std::uint64_t significand = kIntegerBit | random.Next();
		if (random.OneIn(2)) {  // short significands make exact results and ties
			significand &= ~((1ull << (random.Next() % 64)) - 1) | kIntegerBit;
		}
		return Finite(random.OneIn(2), significand, exponent);
	}
	}
}

const char* PrecisionName(x87::Precision precision) {
	switch (precision) {
	case x87::Precision::Single:
		return "PC24";
	case x87::Precision::Double:
		return "PC53";
	case x87::Precision::Extended:
		return "PC64";
	}
	return "?";
}

const char* BinaryLabel(Operation operation, x87::Precision precision) {
	static constexpr const char* kLabels[3][3] = {
		{"FADDP PC24", "FADDP PC53", "FADDP PC64"},
		{"FSUBP PC24", "FSUBP PC53", "FSUBP PC64"},
		{"FDIVP PC24", "FDIVP PC53", "FDIVP PC64"},
	};
	const int column = precision == x87::Precision::Single ? 0 : precision == x87::Precision::Double ? 1 : 2;
	return kLabels[static_cast<int>(operation)][column];
}

void X87AgainstHost() {
	constexpr x87::Precision kPrecisions[] = {kSingle, kDouble, kExtended};
	constexpr int kRuns = 100000;

	Section("x87 vs host: 0x005ADA0B-0x005ADA26 at every stage, random inputs, each precision");
	for (x87::Precision precision : kPrecisions) {
		Random random;
		int mismatches = 0;
		for (int run = 0; run < kRuns; ++run) {
			float timer = RandomTimer(random);
			std::int32_t sinceLast = RandomSinceLast(random);
			logic::TimerConstants constants = RandomConstants(random);
			HostCountDown host = RunOnHost(timer, sinceLast, constants, ControlWord(precision));
			bool same = logic::ElapsedForTimer(sinceLast, constants.unsignedFixup, precision) == FromImage(host.elapsed) &&
				logic::TimerQuotient(sinceLast, constants, precision) == FromImage(host.quotient) &&
				logic::TimerDifference(timer, sinceLast, constants, precision) == FromImage(host.difference) &&
				FloatBits(logic::CountDown(timer, sinceLast, constants, precision)) == FloatBits(host.stored);
			if (!same && mismatches++ == 0) {
				std::printf("    first mismatch %s: timer %08X sinceLast %d fixup %08X divisor %016llX\n",
					PrecisionName(precision), FloatBits(timer), sinceLast, FloatBits(constants.unsignedFixup),
					static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(constants.divisor)));
			}
		}
		Check(mismatches == 0, PrecisionName(precision));
	}

	Section("x87 vs host: FADDP, FSUBP, FDIVP on random registers (operands 0..140 binades apart), each precision");
	for (Operation operation : {Operation::Add, Operation::Subtract, Operation::Divide}) {
		for (x87::Precision precision : kPrecisions) {
			Random random;
			int mismatches = 0;
			for (int run = 0; run < kRuns; ++run) {
				std::int32_t exponent = static_cast<std::int32_t>(random.Next() % 601u) - 300;
				std::int32_t distance = static_cast<std::int32_t>(random.Next() % 141u);
				x87::Register left = RandomRegister(random, exponent);
				x87::Register right = RandomRegister(random, random.OneIn(2) ? exponent - distance : exponent + distance);
				x87::Register model = operation == Operation::Add ? x87::Add(left, right, precision)
					: operation == Operation::Subtract           ? x87::Subtract(left, right, precision)
																 : x87::Divide(left, right, precision);
				x87::Register host = BinaryOnHost(operation, left, right, ControlWord(precision));
				if (model != host && mismatches++ == 0) {
					std::printf("    first mismatch op %d %s: %016llX e%d vs %016llX e%d\n", static_cast<int>(operation),
						PrecisionName(precision), static_cast<unsigned long long>(left.significand), left.exponent,
						static_cast<unsigned long long>(right.significand), right.exponent);
				}
			}
			Check(mismatches == 0, BinaryLabel(operation, precision));
		}
	}

	Section("x87 vs host: FSTP m32 of random registers around the float range");
	Random random;
	int mismatches = 0;
	for (int run = 0; run < kRuns; ++run) {
		std::int32_t exponent = static_cast<std::int32_t>(random.Next() % 321u) - 170;  // 2^-170 .. 2^150
		x87::Register value = RandomRegister(random, exponent);
		if (FloatBits(x87::ToFloat(value)) != FloatBits(StoreOnHost(value)) && mismatches++ == 0) {
			std::printf("    first mismatch: %016llX e%d\n", static_cast<unsigned long long>(value.significand),
				value.exponent);
		}
	}
	Check(mismatches == 0, "FSTP m32");
}

void B8TimerExpired() {
	Section("B8 taken: !(0 < timer) and ordered - expired, refresh = 1");
	Check(logic::TimerExpired(0.0f), "0.0 is expired");
	Check(logic::TimerExpired(-0.0f), "-0.0 compares equal to 0: expired");
	Check(logic::TimerExpired(-1.0f), "negative is expired");
	FakePorts ports = FakePorts{};
	ports.fields.screenTimeLeft = 0.5f;  // 0.5 - 0.5 = 0
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.refreshed.has_value(), "run: target == shown, but the refresh forces the redraw and 0x005AD780");

	Section("B8 not taken: positive, or NaN (C0 set on unordered) - not expired");
	Check(!logic::TimerExpired(0.5f), "0.5 is not expired");
	Check(!logic::TimerExpired(std::numeric_limits<float>::quiet_NaN()), "NaN is not expired");
	Check(!logic::Refresh(std::numeric_limits<float>::quiet_NaN(), 0), "NaN without force: no refresh");
}

void B9ForceRefresh() {
	Section("B9: not expired - refresh is the stack byte forceRefresh");
	Check(!logic::Refresh(1.0f, 0), "force 0: refresh = 0");
	Check(logic::Refresh(1.0f, 1), "force 1: refresh = 1");
	Check(logic::Refresh(1.0f, 0x80), "any nonzero byte forces (CMP byte ptr [ESP+0x1C],0)");
	Check(logic::Refresh(0.0f, 0), "expired wins over force 0");

	FakePorts forced;
	logic::Outcome outcome = logic::RunTick(forced, ExpectedInputs(1));
	Check(outcome == logic::Outcome::TailCalled, "run with force: completes");
	Check(forced.refreshed == std::make_pair(kMenu, kTileMenu), "run with force: 0x005AD780(this = menu, tileMenu)");
	Check(forced.fields.screenTimeLeft == kScreenPeriod, "run with force: the timer is re-armed although 4.5 > 0");

	FakePorts plain;
	static_cast<void>(logic::RunTick(plain, ExpectedInputs(0)));
	Check(!plain.refreshed.has_value(), "run without force: no 0x005AD780");
}

void B10Unchanged() {
	Section("B10 taken: targetStep == shownStep and no refresh - straight to T1");
	Check(logic::GateRedraw(3, 3, false) == logic::RedrawGate::Unchanged, "decision: unchanged");
	FakePorts ports;
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::TailCalled, "the tail still runs");
	Check(!ports.tileFloat.has_value() && ports.Count(Event::InterfaceManager) == 0, "no tile update, no interface calls");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1: +0x6C = now");

	Section("B10 not taken: the step differs, or a refresh is due");
	Check(logic::GateRedraw(4, 3, false) == logic::RedrawGate::Proceed, "target ahead");
	Check(logic::GateRedraw(3, 3, true) == logic::RedrawGate::Proceed, "refresh with an unchanged step");
}

void B11BarFull() {
	Section("B11 taken: shownStep >= 100 (signed) - straight to T1");
	Check(logic::GateRedraw(150, 100, false) == logic::RedrawGate::BarFull, "100 is full");
	Check(logic::GateRedraw(100, 100, true) == logic::RedrawGate::BarFull, "full beats a due refresh");
	FakePorts ports;
	ports.fields.shownStep = 100;
	ports.fields.targetStep = 101;
	ports.fields.screenTimeLeft = 0.5f;  // expired after this tick
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(!ports.tileFloat.has_value() && !ports.refreshed.has_value(), "no tile update, no 0x005AD780");
	Check(ports.fields.screenTimeLeft == 0.0f, "the expired timer is not re-armed");
	Check(ports.fields.shownStep == 100 && ports.fields.lastTickMs == kBaselineNow, "shown kept; T1 written");

	Section("B11 not taken: 99, and negative values, go on");
	Check(logic::GateRedraw(150, 99, false) == logic::RedrawGate::Proceed, "99 < 100");
	Check(logic::GateRedraw(0, -5, false) == logic::RedrawGate::Proceed, "-5 < 100 as signed");
}

void B12CatchUp() {
	Section("B12 taken: shownStep != targetStep - stepChangedMs = clock+0x10 (read again), shownStep = target");
	Check(logic::ShownStepCatchesUp(3, 4), "decision: 3 != 4");
	FakePorts ports = RedrawingPorts();
	ports.lastMs = {kBaselineNow, 2222u};  // the second read is a separate read (0x005ADA6F)
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.lastMsReadsFrom == std::vector<std::uint32_t>{kClock, kClock}, "clock+0x10 read twice, same clock");
	Check(ports.fields.stepChangedMs == 2222u, "+0x68 = the second read");
	Check(ports.fields.shownStep == 4, "+0x60 = 4");
	Check(ports.tileFloat == std::make_tuple(kTileMenu, 0xFB1u, 4.0f), "Tile::UpdateFloat(tileMenu, 0xFB1, 4.0)");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 uses the first read (EBP), not the second");

	Section("B12 not taken: equal (the refresh path) - +0x68 and +0x60 kept");
	Check(!logic::ShownStepCatchesUp(3, 3), "decision: equal");
	ports = FakePorts{};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs(1)));
	Check(ports.Count(Event::ReadClockLastMs) == 1, "no second clock read");
	Check(ports.fields.stepChangedMs == 1000u && ports.fields.shownStep == 3, "+0x68 and +0x60 unchanged");
	Check(ports.tileFloat == std::make_tuple(kTileMenu, 0xFB1u, 3.0f), "the tile still gets 3.0");
	Check(logic::StepAsFloat(16777217) == 16777216.0f, "FILD then FSTP float: 2^24 + 1 rounds to 2^24");
}

void B13Ceiling() {
	Section("B13 not taken: (float)shownStep < [0x00A6C4C8] - go on");
	Check(logic::BelowCeiling(99.0f, kExpectedCeiling), "99 < 100");
	FakePorts ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.Count(Event::InterfaceManager) == 2, "the interface calls run");

	Section("B13 taken: equal, greater or unordered - straight to T1 after the tile update");
	Check(!logic::BelowCeiling(100.0f, kExpectedCeiling), "100 is not below 100");
	Check(!logic::BelowCeiling(150.0f, kExpectedCeiling), "150 is not below 100");
	Check(!logic::BelowCeiling(1.0f, std::numeric_limits<double>::quiet_NaN()), "a NaN ceiling: JP taken");
	Check(!logic::BelowCeiling(50.0f, 40.0), "the ceiling is the constant, not a literal 100");
	ports = FakePorts{};
	ports.fields.shownStep = 99;
	ports.fields.targetStep = 100;
	ports.fields.screenTimeLeft = 0.5f;  // expired after this tick
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.tileFloat == std::make_tuple(kTileMenu, 0xFB1u, 100.0f), "the full bar (100.0) is pushed to the tile");
	Check(!ports.refreshed.has_value() && ports.Count(Event::ScreenPeriodSetting) == 0, "no 0x005AD780, no re-arm");
	Check(ports.fields.screenTimeLeft == 0.0f, "the expired timer stays expired");
	Check(ports.Count(Event::InterfaceManager) == 0, "no interface calls");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 written");
}

void B14Refresh() {
	Section("B14 taken: 0x005AD780(this = menu, tileMenu), then +0x40 = *0x00403C00(this = 0x00B14158)");
	FakePorts ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, ExpectedInputs(1)));
	Check(ports.refreshed == std::make_pair(kMenu, kTileMenu), "0x005AD780 on the menu with the tile menu");
	Check(ports.IndexOf(Event::RefreshScreen) < ports.IndexOf(Event::ScreenPeriodSetting), "0x005AD780 before the setting read");
	Check(ports.IndexOf(Event::SetTileFloat) < ports.IndexOf(Event::RefreshScreen), "after the tile update");
	Check(ports.IndexOf(Event::ScreenPeriodSetting) < ports.IndexOf(Event::InterfaceManager), "before the interface calls");
	Check(ports.fields.screenTimeLeft == kScreenPeriod, "+0x40 = the setting");

	Section("B14 not taken: no refresh - neither call, the counted-down timer stays");
	ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, ExpectedInputs(0)));
	Check(!ports.refreshed.has_value() && ports.Count(Event::ScreenPeriodSetting) == 0, "no 0x005AD780, no setting read");
	Check(ports.fields.screenTimeLeft == 4.5f, "+0x40 = 4.5");
	Check(ports.Count(Event::InterfaceManager) == 2, "the interface calls still run");
}

void B15InterfaceManager() {
	Section("B15 taken: InterfaceManager_GetSingleton(0,1) 0x00582160 null - straight to T1");
	FakePorts ports = RedrawingPorts();
	ports.interfaceManagers = {0};
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs());
	Check(ports.Count(Event::InterfaceManager) == 1, "asked once");
	Check(ports.Count(Event::IoManager) == 0 && ports.Count(Event::InterfacePass) == 0, "no g_ioManager read, no pass");
	Check(ports.fields.lastTickMs == kBaselineNow && outcome == logic::Outcome::TailCalled, "T1 written, tail reached");

	Section("B15 not taken: asked again; the second answer is this of 0x00583F40, unchecked");
	ports = RedrawingPorts();
	ports.interfaceManagers = {kInterfaceManager, kOtherInterfaceManager};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.Count(Event::InterfaceManager) == 2, "asked twice (0x005ADAC6, 0x005ADAE5)");
	Check(ports.interfaceUpdated == kOtherInterfaceManager, "0x00583F40 on the second answer");
	ports = RedrawingPorts();
	ports.interfaceManagers = {kInterfaceManager, 0};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.interfaceUpdated == 0u, "a null second answer is passed on as the original does (no guard)");
	Check(ports.passArgument == 0u && ports.Count(Event::InterfacePassEnd) == 1,
		"0x00579260(0) and 0x005792B0 run unconditionally past B15");
}

void B16IoManagerBefore() {
	Section("B16 not taken: g_ioManager 0x00B33A10 set - 0x00432860 on it, before 0x00583F40");
	FakePorts ports = RedrawingPorts();
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.ioBefore == kIoManager, "0x00432860(this = g_ioManager)");
	Check(ports.IndexOf(Event::IoManagerBefore) < ports.IndexOf(Event::InterfaceManagerUpdate), "before 0x00583F40");

	Section("B16 taken: g_ioManager null - 0x00432860 skipped, the rest runs");
	ports = RedrawingPorts();
	ports.ioManagers = {0, 0};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(!ports.ioBefore.has_value(), "no 0x00432860");
	Check(ports.interfaceUpdated == kInterfaceManager && ports.passArgument == 0u, "0x00583F40 and 0x00579260(0) run");
}

void B17IoManagerAfter() {
	Section("B17 not taken: g_ioManager read again, set - 0x00432890 on it, after 0x005792B0");
	FakePorts ports = RedrawingPorts();
	ports.ioManagers = {0, kIoManager};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.Count(Event::IoManager) == 2, "g_ioManager is read twice (0x005ADAD2, 0x005ADB03)");
	Check(!ports.ioBefore.has_value() && ports.ioAfter == kIoManager, "the second read decides alone");
	Check(ports.IndexOf(Event::InterfacePassEnd) < ports.IndexOf(Event::IoManagerAfter), "after 0x005792B0");

	Section("B17 taken: the second read null - 0x00432890 skipped");
	ports = RedrawingPorts();
	ports.ioManagers = {kIoManager, 0};
	static_cast<void>(logic::RunTick(ports, ExpectedInputs()));
	Check(ports.ioBefore == kIoManager && !ports.ioAfter.has_value(), "before ran, after skipped");
}

void T1AndFullOrder() {
	Section("T1 and the longest path: the whole call order");
	FakePorts ports = RedrawingPorts();
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs(1));
	Check(outcome == logic::Outcome::TailCalled, "completes into the tail");
	std::vector<Event> expected{
		Event::TileMenuForType, Event::ContainingMenu, Event::ContainingMenu, Event::TickCount, Event::UpdateClock,
		Event::ReadClockLastMs, Event::StepThresholds, Event::ReadClockLastMs, Event::SetTileFloat,
		Event::RefreshScreen, Event::ScreenPeriodSetting, Event::InterfaceManager, Event::IoManager,
		Event::IoManagerBefore, Event::InterfaceManager, Event::InterfaceManagerUpdate, Event::InterfacePass,
		Event::InterfacePassEnd, Event::IoManager, Event::IoManagerAfter, Event::OsGlobals, Event::MainThreadId,
		Event::CurrentThreadId, Event::OsGlobals, Event::OsGlobalsTail,
	};
	Check(ports.events == expected, "the order of 0x005AD980's calls and reads");
	Check(ports.fields.lastTickMs == kBaselineNow, "+0x6C = now");
}

void B18MainThread() {
	Section("B18 not taken: GetCurrentThreadId() == [g_osGlobals+0x10] - tail into 0x0060D0A0");
	FakePorts ports;
	ports.osGlobals = {kOsGlobals, kOtherOsGlobals};
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::TailCalled, "outcome: the tail");
	Check(ports.mainThreadAskedOf == kOsGlobals, "mainThreadID read from the first g_osGlobals read (0x005ADB15)");
	Check(ports.tailOn == kOtherOsGlobals, "the tail's ECX is g_osGlobals read again (0x005ADB2B)");
	Check(ports.IndexOf(Event::MainThreadId) < ports.IndexOf(Event::CurrentThreadId),
		"[g_osGlobals+0x10] is read before GetCurrentThreadId");
	Check(logic::OnMainThread(kMainThread, kMainThread), "decision: equal ids");

	Section("B18 taken: another thread - plain return, no tail");
	ports = FakePorts{};
	ports.currentThread = kOtherThread;
	outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::OffMainThread, "outcome: returned off the main thread");
	Check(ports.Count(Event::OsGlobalsTail) == 0 && ports.Count(Event::OsGlobals) == 1, "no tail, no second g_osGlobals read");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 written before the check");
	Check(!logic::OnMainThread(kOtherThread, kMainThread), "decision: different ids");

	Section("B18 refusal: g_osGlobals null - the original reads [0+0x10] at 0x005ADB1B");
	ports = FakePorts{};
	ports.osGlobals = {0};
	outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::FaultNullOsGlobals, "outcome: the null g_osGlobals read");
	Check(ports.Count(Event::MainThreadId) == 0 && ports.Count(Event::CurrentThreadId) == 0, "nothing past the read");
	Check(ports.fields.lastTickMs == kBaselineNow, "T1 was written before it");
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
	void UpdateClock(obng::engine::time::TimeInfo* self, std::uint32_t tick) { self->lastMs = tick - self->startTick; }
	std::uint32_t ReadClockLastMs(obng::engine::time::TimeInfo* self) { return self->lastMs; }
	logic::StepThresholds StepThresholds() { return kThresholds; }
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
	ports.clock.startTick = kTick - 5000u;  // lastMs after the update: 5000
	ports.menu.clock = &ports.clock;
	ports.menu.slowStepCap = 10;
	ports.menu.screenTimeLeft = 3.0f;
	ports.menu.shownStep = 3;
	ports.menu.targetStep = 3;
	ports.menu.stepChangedMs = 1000u;  // sinceStep 4000 > 3000: advance
	ports.menu.lastTickMs = 4000u;     // sinceLast 1000: timer 3.0 -> 2.0
	logic::Outcome outcome = logic::RunTick(ports, ExpectedInputs());
	Check(outcome == logic::Outcome::OffMainThread, "outcome: off the main thread (the fake's ids differ)");
	Check(ReadAt<std::int32_t>(&ports.menu, 0x64) == 4, "+0x64 targetStep = 4");
	Check(ReadAt<std::int32_t>(&ports.menu, 0x60) == 4, "+0x60 shownStep = 4");
	Check(ReadAt<std::uint32_t>(&ports.menu, 0x68) == 5000u, "+0x68 stepChangedMs = clock+0x10");
	Check(ReadAt<std::uint32_t>(&ports.menu, 0x6C) == 5000u, "+0x6C lastTickMs = now");
	Check(ReadAt<float>(&ports.menu, 0x40) == 2.0f, "+0x40 screenTimeLeft = 2.0");
	Check(ReadAt<std::uint32_t>(&ports.clock, 0x10) == 5000u, "TimeInfo+0x10 lastMs");
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
	B1TileMenu();
	B2ContainingMenu();
	B3AlwaysAdvance();
	B4Cap();
	B5CappedAdvance();
	B6FastAdvance();
	B7UnsignedFixup();
	B7Precision();
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
	EngineShape();
	X87ControlWord();
	X87Loads();
	X87Add();
	X87Divide();
	X87StoreFloat();
	X87AgainstHost();
	return Failures();
}
