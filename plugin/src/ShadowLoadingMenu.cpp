// The shadow detours on LoadingMenu::Tick 0x005AD980 and LoadingMenu's
// constructor 0x005AD0D0. Both rebuilds are written over ports (every
// engine call and memory read behind a method), so the shadow runs them
// with ports that read the game's memory as the original did, supply the
// tick the original was seen to use, and record instead of perform the
// calls that would change the game. What the original left in the menu's
// fields and its private TimeInfo is then compared with the copy, bit for
// bit. The calls themselves (tile update, screen refresh, interface pass)
// are not observable on the original's side and are only counted.

#include "ShadowTimeInfo.h"

#include <windows.h>

#include <bit>
#include <cfloat>  // _control87, _MCW_PC, _PC_53
#include <cstdio>
#include <cstring>

#include "ShadowLogic.h"
#include "ShadowRecorder.h"
#include "engine/time/LoadingMenu_LoadingMenu.h"
#include "engine/time/LoadingMenu_LoadingMenuLogic.h"
#include "engine/time/LoadingMenu_Tick.h"
#include "engine/time/LoadingMenu_TickLogic.h"
#include "engine/time/TimeInfo_TimeInfoLogic.h"
#include "engine/time/TimeInfo_UpdateLogic.h"

namespace obng::plugin {
namespace {

namespace tick = engine::time::loading_menu_tick;
namespace ctor = engine::time::loading_menu_ctor;
using engine::time::TimeInfo;

// The entries (re/export/loading-menu/*/asm.s): whole instructions, nothing relative.
constexpr UInt32 kTick = 0x005AD980;
constexpr UInt8 kTickBytes[] = {0x83, 0xEC, 0x08, 0x53, 0x68, 0xEF, 0x03, 0x00, 0x00};  // sub esp,8; push ebx; push 3EFh
constexpr UInt32 kCtor = 0x005AD0D0;
constexpr UInt8 kCtorBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xC0};  // push ebp; mov ebp,esp; and esp,-40h

constexpr UInt32 kTimeMultiplier = 0x00B06704;
constexpr UInt32 kClockMsGlobal = 0x00B39DB0;
constexpr UInt32 kPreviousClockMsGlobal = 0x00B39DB4;
constexpr UInt32 kTileMenuForType = 0x00589B70;   // cdecl(menuType) -> tile
constexpr UInt32 kContainingMenu = 0x005898F0;    // thiscall(tile) -> menu
constexpr UInt32 kInterfaceManagerGet = 0x00582160;  // cdecl(first, second) -> manager
constexpr UInt32 kIoManager = 0x00B33A10;
constexpr UInt32 kOsGlobals = 0x00B33398;
constexpr UInt32 kScreenTimeSetting = 0x00B14158;
constexpr UInt32 kThresholdAlways = 0x00B14160;
constexpr UInt32 kThresholdCapped = 0x00B14168;
constexpr UInt32 kThresholdFast = 0x00B14170;
constexpr UInt32 kSectionTable = 0x00B14120;
constexpr UInt32 kSectionCopies = 0x00B3B3DC;

using TickFn = UInt32(__cdecl*)(UInt32 forceRefresh);
using CtorFn = UInt32(__fastcall*)(void* self, void* edx);
using TileMenuForTypeFn = void*(__cdecl*)(UInt32 menuType);
using ContainingMenuFn = void*(__fastcall*)(void* tile, void* edx);
using InterfaceManagerGetFn = void*(__cdecl*)(UInt32 first, UInt32 second);

TickFn g_tick = nullptr;
CtorFn g_ctor = nullptr;

template <class T>
T Read(UInt32 address) {
	return *reinterpret_cast<volatile T*>(address);
}

tick::Precision GamePrecision() {
	const unsigned int pc = _control87(0, 0) & _MCW_PC;
	return pc == _PC_53 ? tick::Precision::Double : tick::Precision::Single;
}

engine::time::update::TickGlobals CaptureGlobals() {
	return engine::time::update::TickGlobals{Read<UInt32>(kClockMsGlobal), Read<UInt32>(kPreviousClockMsGlobal)};
}

// The ports of LoadingMenu::Tick: memory and getters as the original has
// them, the clock update on a copy, the changing calls counted.
struct TickPorts {
	tick::MenuFields fields;
	TimeInfo clock;                                // the private TimeInfo, a copy
	engine::time::update::TickGlobals globals;
	UInt32 tileMenu;
	UInt32 menu;
	UInt32 tickUsed;
	float multiplier;
	UInt32 tileFloats = 0;
	UInt32 refreshes = 0;
	UInt32 passes = 0;
	UInt32 tails = 0;

	UInt32 TileMenuForType(UInt32) { return tileMenu; }
	UInt32 ContainingMenu(UInt32) { return menu; }
	tick::MenuFields& Fields(UInt32) { return fields; }
	UInt32 TickCount() { return tickUsed; }
	void UpdateClock(UInt32 clockAddress, UInt32 tickValue) {
		if (clockAddress != 0) {
			engine::time::update::RunUpdate(clock, tickValue, globals, multiplier);
		}
	}
	UInt32 ReadClockMs(UInt32) { return clock.clockMs; }
	tick::StepThresholds StepThresholds() {
		return tick::StepThresholds{Read<std::int32_t>(kThresholdAlways), Read<std::int32_t>(kThresholdCapped),
		                            Read<std::int32_t>(kThresholdFast)};
	}
	void SetTileFloat(UInt32, UInt32, float) { ++tileFloats; }
	void RefreshScreen(UInt32, UInt32) { ++refreshes; }
	float ScreenPeriodSetting() { return Read<float>(kScreenTimeSetting); }
	UInt32 InterfaceManager() {
		return reinterpret_cast<UInt32>(reinterpret_cast<InterfaceManagerGetFn>(kInterfaceManagerGet)(0, 1));
	}
	UInt32 IoManager() { return Read<UInt32>(kIoManager); }
	void IoManagerBefore(UInt32) {}
	void InterfaceManagerUpdate(UInt32) {}
	void InterfacePass(UInt32) { ++passes; }
	void InterfacePassEnd() {}
	void IoManagerAfter(UInt32) {}
	UInt32 OsGlobals() { return Read<UInt32>(kOsGlobals); }
	UInt32 MainThreadId(UInt32 globals) { return Read<UInt32>(globals + 0x10); }
	UInt32 CurrentThreadId() { return GetCurrentThreadId(); }
	void OsGlobalsTail(UInt32) { ++tails; }
};

tick::MenuFields TickFieldsOf(const tick::engine::LoadingMenuLayout* menu) {
	tick::MenuFields f;
	f.slowStepCap = menu->slowStepCap;
	f.screenTimeLeft = menu->screenTimeLeft;
	f.clock = reinterpret_cast<UInt32>(menu->clock);
	f.shownStep = menu->shownStep;
	f.targetStep = menu->targetStep;
	f.stepChangedMs = menu->stepChangedMs;
	f.lastTickMs = menu->lastTickMs;
	f.fastStepEnabled = menu->fastStepEnabled;
	return f;
}

int DescribeTickFields(const tick::MenuFields& original, const tick::MenuFields& rebuild, char* out, UInt32 capacity) {
	int at = 0;
	auto field = [&](const char* name, UInt32 a, UInt32 b) {
		if (a != b && at + 80 < static_cast<int>(capacity)) {
			at += std::snprintf(out + at, capacity - at, " %s original %08X rebuild %08X;", name, a, b);
		}
	};
	field("slowStepCap", static_cast<UInt32>(original.slowStepCap), static_cast<UInt32>(rebuild.slowStepCap));
	field("screenTimeLeft", std::bit_cast<UInt32>(original.screenTimeLeft), std::bit_cast<UInt32>(rebuild.screenTimeLeft));
	field("clock", original.clock, rebuild.clock);
	field("shownStep", static_cast<UInt32>(original.shownStep), static_cast<UInt32>(rebuild.shownStep));
	field("targetStep", static_cast<UInt32>(original.targetStep), static_cast<UInt32>(rebuild.targetStep));
	field("stepChangedMs", original.stepChangedMs, rebuild.stepChangedMs);
	field("lastTickMs", original.lastTickMs, rebuild.lastTickMs);
	field("fastStepEnabled", original.fastStepEnabled, rebuild.fastStepEnabled);
	return at;
}

UInt32 __cdecl ShadowTick(UInt32 forceRefreshArg) {
	const UInt8 forceRefresh = static_cast<UInt8>(forceRefreshArg);
	// The lookups the original makes first, made here first as well: getters.
	void* tile = reinterpret_cast<TileMenuForTypeFn>(kTileMenuForType)(tick::kLoadingMenuType);
	auto* menu = tile == nullptr ? nullptr
	                             : static_cast<tick::engine::LoadingMenuLayout*>(
	                                   reinterpret_cast<ContainingMenuFn>(kContainingMenu)(tile, nullptr));
	if (menu == nullptr || menu->clock == nullptr) {
		const UInt32 eax = g_tick(forceRefreshArg);
		RecordSkipped(Shadowed::LoadingMenuTick, menu == nullptr ? "no loading menu (the original returns at once)" : "the menu's clock is null");
		return eax;
	}
	const tick::MenuFields before = TickFieldsOf(menu);
	TimeInfo clockBefore{};
	std::memcpy(&clockBefore, menu->clock, sizeof(TimeInfo));
	const engine::time::update::TickGlobals globalsBefore = CaptureGlobals();
	const float multiplier = Read<float>(kTimeMultiplier);

	const UInt32 eax = g_tick(forceRefreshArg);

	const tick::MenuFields original = TickFieldsOf(menu);
	TimeInfo clockAfter{};
	std::memcpy(&clockAfter, menu->clock, sizeof(TimeInfo));
	if (clockBefore.freezeDepth != 0 || clockBefore.fixedStepMs != 0.0f) {
		RecordSkipped(Shadowed::LoadingMenuTick, "the clock was frozen or in fixed-step mode, its tick is not recoverable");
		return eax;
	}
	// The update stored tick - startTick: the original's GetTickCount reading recovered.
	const UInt32 tickUsed = clockAfter.clockMs + clockAfter.startTick;

	TickPorts ports{before, clockBefore, globalsBefore, reinterpret_cast<UInt32>(tile), reinterpret_cast<UInt32>(menu),
	                tickUsed, multiplier};
	const tick::Outcome outcome = tick::RunTick(ports, tick::Inputs{forceRefresh, GamePrecision()});

	shadow::State originalState{clockAfter, Read<UInt32>(kClockMsGlobal), Read<UInt32>(kPreviousClockMsGlobal)};
	shadow::State rebuildState{ports.clock, ports.globals.currentTickMs, ports.globals.previousTickMs};
	// The globals were also written by the original's inner Update, which our
	// Update shadow saw; here they are compared once more through the Tick.
	const UInt32 clockMask = shadow::Differences(originalState, rebuildState);

	char detail[640] = {};
	int at = DescribeTickFields(original, ports.fields, detail, sizeof(detail));
	if (clockMask != 0) {
		shadow::State beforeState{clockBefore, globalsBefore.currentTickMs, globalsBefore.previousTickMs};
		DescribeStateDifferences(beforeState, originalState, rebuildState, clockMask, detail + at, sizeof(detail) - at);
	}
	const bool mismatch = at != 0 || clockMask != 0;
	char inputs[160];
	std::snprintf(inputs, sizeof(inputs), "menu %08X force %u tick %08X outcome %u tileFloats %u refreshes %u passes %u tails %u",
	              reinterpret_cast<UInt32>(menu), forceRefresh, tickUsed, static_cast<unsigned>(outcome), ports.tileFloats,
	              ports.refreshes, ports.passes, ports.tails);
	RecordCall(Shadowed::LoadingMenuTick, mismatch, detail, inputs);
	return eax;
}

// The ports of the constructor: the settings table as the game has it, the
// clock on a scratch block, the CRT formatting done with ours (the text is
// only logged), the warnings counted.
struct CtorPorts {
	ctor::MenuFields fields{};
	TimeInfo scratch{};
	engine::time::update::TickGlobals globals;
	float multiplier;
	UInt32 ticks[2];
	UInt32 tickCalls = 0;
	std::uint32_t copies[4] = {0, 0, 0, 0};
	UInt32 copiesStored = 0;
	UInt32 missing = 0;
	UInt32 warnings = 0;
	char lastWarning[200] = {};

	void BaseConstruct(UInt32) {}
	ctor::MenuFields& Fields(UInt32) { return fields; }
	UInt32 OsGlobals() { return Read<UInt32>(kOsGlobals); }
	UInt32 OsGlobalsHookObject(UInt32 globalsAddress) { return Read<UInt32>(globalsAddress + 0x24); }
	void OsGlobalsHook(UInt32) {}
	UInt32 SectionSetting(UInt32 index) { return Read<UInt32>(kSectionTable + 4 * index); }
	UInt32 SettingValue(UInt32 setting) { return Read<UInt32>(setting); }
	UInt32 SettingName(UInt32 setting) { return Read<UInt32>(setting + 4); }
	void StoreSectionCopy(UInt32 index, UInt32 bits) {
		if (index < 4) {
			copies[index] = bits;
			copiesStored |= 1u << index;
		}
	}
	void WarnMissing(const ctor::FormatString&, std::int32_t) { ++missing; }
	void WarnMessage(const char* message) {
		++warnings;
		std::snprintf(lastWarning, sizeof(lastWarning), "%s", message);
	}
	void FormatHeader(char* buffer, UInt32 size, const ctor::FormatString& format) {
		std::snprintf(buffer, size, "%s", format.text);
	}
	void FormatFoundLine(char* buffer, UInt32 size, const ctor::FormatString& format, UInt32 name, std::uint64_t valueBits) {
		std::snprintf(buffer, size, format.text, reinterpret_cast<const char*>(name), std::bit_cast<double>(valueBits));
	}
	void FormatTotalLine(char* buffer, UInt32 size, const ctor::FormatString& format, std::uint64_t totalBits) {
		std::snprintf(buffer, size, format.text, std::bit_cast<double>(totalBits));
	}
	void Concatenate(char* message, const char* line, UInt32 count) { std::strncat(message, line, count); }
	UInt32 Allocate(UInt32) { return reinterpret_cast<UInt32>(&scratch); }
	UInt32 TickCount() { return ticks[tickCalls < 2 ? tickCalls++ : 1]; }
	void InitClock(UInt32, UInt32 tickValue) {
		engine::time::timeinfo_ctor::Apply(scratch, engine::time::timeinfo_ctor::InitialValuesFor(tickValue));
	}
	void UpdateClock(UInt32 block, UInt32 tickValue) {
		if (block != 0) {
			engine::time::update::RunUpdate(scratch, tickValue, globals, multiplier);
		}
	}
	UInt32 ReadClockMs(UInt32) { return scratch.clockMs; }
	UInt32 ScreenTimeSetting() { return Read<UInt32>(kScreenTimeSetting); }
};

ctor::MenuFields CtorFieldsOf(const ctor::engine::LoadingMenuLayout* menu) {
	ctor::MenuFields f;
	f.vtable = menu->vtable;
	f.unknown28 = menu->unknown28;
	f.sectionValues = menu->sectionValues;
	f.slowStepCap = menu->slowStepCap;
	f.screenTimeLeft = menu->screenTimeLeft;
	f.unknown4C = menu->unknown4C;
	f.unknown50 = menu->unknown50;
	f.clock = reinterpret_cast<UInt32>(menu->clock);
	f.shownStep = menu->shownStep;
	f.targetStep = menu->targetStep;
	f.stepChangedMs = menu->stepChangedMs;
	f.lastTickMs = menu->lastTickMs;
	f.fastStepEnabled = menu->fastStepEnabled;
	f.unknown71 = menu->unknown71;
	return f;
}

int DescribeCtorFields(const ctor::MenuFields& original, const ctor::MenuFields& rebuild, char* out, UInt32 capacity) {
	int at = 0;
	auto field = [&](const char* name, UInt32 a, UInt32 b) {
		if (a != b && at + 80 < static_cast<int>(capacity)) {
			at += std::snprintf(out + at, capacity - at, " %s original %08X rebuild %08X;", name, a, b);
		}
	};
	field("vtable", original.vtable, rebuild.vtable);
	field("unknown28", static_cast<UInt32>(original.unknown28), static_cast<UInt32>(rebuild.unknown28));
	for (int i = 0; i < 4; ++i) {
		char name[24];
		std::snprintf(name, sizeof(name), "sectionValues[%d]", i);
		field(name, original.sectionValues[i], rebuild.sectionValues[i]);
	}
	field("slowStepCap", static_cast<UInt32>(original.slowStepCap), static_cast<UInt32>(rebuild.slowStepCap));
	field("screenTimeLeft", std::bit_cast<UInt32>(original.screenTimeLeft), std::bit_cast<UInt32>(rebuild.screenTimeLeft));
	field("unknown4C", original.unknown4C, rebuild.unknown4C);
	field("unknown50", original.unknown50, rebuild.unknown50);
	// The clock is a heap pointer in the original and the scratch block in the
	// rebuild: only null-ness is comparable.
	field("clock (null-ness)", original.clock == 0 ? 0 : 1, rebuild.clock == 0 ? 0 : 1);
	field("shownStep", static_cast<UInt32>(original.shownStep), static_cast<UInt32>(rebuild.shownStep));
	field("targetStep", static_cast<UInt32>(original.targetStep), static_cast<UInt32>(rebuild.targetStep));
	field("stepChangedMs", static_cast<UInt32>(original.stepChangedMs), static_cast<UInt32>(rebuild.stepChangedMs));
	field("lastTickMs", static_cast<UInt32>(original.lastTickMs), static_cast<UInt32>(rebuild.lastTickMs));
	field("fastStepEnabled", original.fastStepEnabled, rebuild.fastStepEnabled);
	field("unknown71", original.unknown71, rebuild.unknown71);
	return at;
}

UInt32 __fastcall ShadowCtor(void* self, void* /*edx*/) {
	const engine::time::update::TickGlobals globalsBefore = CaptureGlobals();
	const float multiplier = Read<float>(kTimeMultiplier);
	UInt32 copiesBefore[4];
	for (int i = 0; i < 4; ++i) copiesBefore[i] = Read<UInt32>(kSectionCopies + 4 * i);

	const UInt32 eax = g_ctor(self, nullptr);

	const auto* menu = static_cast<const ctor::engine::LoadingMenuLayout*>(self);
	const ctor::MenuFields original = CtorFieldsOf(menu);
	if (menu->clock == nullptr) {
		RecordSkipped(Shadowed::LoadingMenuCtor, "the constructor's clock allocation failed, no ticks to recover");
		return eax;
	}
	TimeInfo clockAfter{};
	std::memcpy(&clockAfter, menu->clock, sizeof(TimeInfo));
	// Init stored the first reading as startTick; Update stored second - first.
	const UInt32 firstTick = clockAfter.startTick;
	const UInt32 secondTick = clockAfter.clockMs + clockAfter.startTick;

	CtorPorts ports{};
	ports.globals = globalsBefore;
	ports.multiplier = multiplier;
	ports.ticks[0] = firstTick;
	ports.ticks[1] = secondTick;
	const ctor::Outcome outcome = ctor::RunConstructor(ports, reinterpret_cast<UInt32>(self));

	char detail[800] = {};
	int at = DescribeCtorFields(original, ports.fields, detail, sizeof(detail));
	shadow::State originalState{clockAfter, Read<UInt32>(kClockMsGlobal), Read<UInt32>(kPreviousClockMsGlobal)};
	shadow::State rebuildState{ports.scratch, ports.globals.currentTickMs, ports.globals.previousTickMs};
	const UInt32 clockMask = shadow::Differences(originalState, rebuildState);
	if (clockMask != 0) {
		shadow::State beforeState{};
		beforeState.clockMs = globalsBefore.currentTickMs;
		beforeState.previousClockMs = globalsBefore.previousTickMs;
		at += std::snprintf(detail + at, sizeof(detail) - at, " [clock]");
		DescribeStateDifferences(beforeState, originalState, rebuildState, clockMask, detail + at, sizeof(detail) - at);
		at = static_cast<int>(std::strlen(detail));
	}
	for (int i = 0; i < 4 && at + 90 < static_cast<int>(sizeof(detail)); ++i) {
		const UInt32 real = Read<UInt32>(kSectionCopies + 4 * i);
		const bool stored = (ports.copiesStored & (1u << i)) != 0;
		const UInt32 expected = stored ? ports.copies[i] : copiesBefore[i];
		if (real != expected) {
			at += std::snprintf(detail + at, sizeof(detail) - at, " sectionCopy[%d] original %08X rebuild %08X;", i, real, expected);
		}
	}
	const bool mismatch = at != 0 || clockMask != 0;
	char inputs[200];
	std::snprintf(inputs, sizeof(inputs), "this %08X outcome %u ticks %08X/%08X missing %u warnings %u%s%.60s",
	              reinterpret_cast<UInt32>(self), static_cast<unsigned>(outcome), firstTick, secondTick, ports.missing,
	              ports.warnings, ports.warnings ? " last: " : "", ports.warnings ? ports.lastWarning : "");
	RecordCall(Shadowed::LoadingMenuCtor, mismatch, detail, inputs);
	return eax;
}

}  // namespace

UInt32 InstallShadowLoadingMenu(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	UInt32 made = 0;
	if (settings.loadingMenuTick) {
		made += InstallEntryDetour(ShadowName(Shadowed::LoadingMenuTick), kTick, kTickBytes, sizeof(kTickBytes),
		                           reinterpret_cast<const void*>(&ShadowTick), reinterpret_cast<void**>(&g_tick), log) ? 1 : 0;
	}
	if (settings.loadingMenuCtor) {
		made += InstallEntryDetour(ShadowName(Shadowed::LoadingMenuCtor), kCtor, kCtorBytes, sizeof(kCtorBytes),
		                           reinterpret_cast<const void*>(&ShadowCtor), reinterpret_cast<void**>(&g_ctor), log) ? 1 : 0;
	}
	return made;
}

}  // namespace obng::plugin
