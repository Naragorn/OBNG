// The shadow detours on the four TimeInfo functions. Each replacement runs
// the original first, on the game's own state, so the game never sees the
// rebuild's result; then it runs the rebuilt logic on a copy of the state
// as it was before, and compares what the two left, bit for bit. A
// difference is counted and, within the log budget, written with every
// field of both sides.

#include "ShadowTimeInfo.h"

#include <windows.h>

#include <cfloat>
#include <cstdio>
#include <cstring>

#include "EntryDetour.h"
#include "Memory.h"
#include "ShadowLogic.h"
#include "engine/time/TimeInfo_FreezeLogic.h"
#include "engine/time/TimeInfo_TimeInfoLogic.h"
#include "engine/time/TimeInfo_UnfreezeLogic.h"
#include "engine/time/TimeInfo_UpdateLogic.h"

namespace obng::plugin {
namespace {

using engine::time::TimeInfo;
using shadow::State;

// The entries as read in Oblivion.exe 1.2.0.416 (re/export/time-info/*/asm.s):
// whole instructions with nothing relative in them.
constexpr UInt32 kUpdate = 0x0047D170;
constexpr UInt8 kUpdateBytes[] = {0x8B, 0x54, 0x24, 0x04, 0xD9, 0xEE, 0x2B, 0x51, 0x14};  // mov edx,[esp+4]; fldz; sub edx,[ecx+14h]
constexpr UInt32 kInit = 0x0047D150;
constexpr UInt8 kInitBytes[] = {0xD9, 0xEE, 0x8B, 0x44, 0x24, 0x04};  // fldz; mov eax,[esp+4]
constexpr UInt32 kFreeze = 0x0047D120;
constexpr UInt8 kFreezeBytes[] = {0x56, 0x8B, 0xF1, 0x8A, 0x06};  // push esi; mov esi,ecx; mov al,[esi]
constexpr UInt32 kUnfreeze = 0x0047D0F0;
constexpr UInt8 kUnfreezeBytes[] = {0x56, 0x8B, 0xF1, 0x8A, 0x06};  // the same three instructions

constexpr UInt32 kClockMs = 0x00B39DB0;
constexpr UInt32 kPreviousClockMs = 0x00B39DB4;
constexpr UInt32 kTimeMultiplier = 0x00B06704;

// The Havok planner's outputs (re/functions/0x00889810.md, proposals).
constexpr UInt32 kHavokUpdateType = 0x00BA7918;
constexpr UInt32 kHavokStepCount = 0x00BA7914;
constexpr UInt32 kHavokStepSeconds = 0x00BA790C;

// __fastcall with a dummy edx mirrors __thiscall: ECX is this, a stack
// argument is cleaned by the callee in both. The originals return nothing
// defined, but a caller might read EAX, so the replacements pass it on.
using UpdateFn = UInt32(__fastcall*)(TimeInfo* self, void* edx, UInt32 tick);
using InitFn = UInt32(__fastcall*)(TimeInfo* self, void* edx, UInt32 startTick);
using BracketFn = UInt32(__fastcall*)(TimeInfo* self, void* edx);

UpdateFn g_update = nullptr;
InitFn g_init = nullptr;
BracketFn g_freeze = nullptr;
BracketFn g_unfreeze = nullptr;

LogFn g_log = nullptr;
ShadowSettings g_settings;
SRWLOCK g_lock = SRWLOCK_INIT;
shadow::Counter g_counters[4];  // Update, Init, Freeze, Unfreeze
const char* const kNames[4] = {"TimeInfo::Update", "TimeInfo::Init", "TimeInfo::Freeze", "TimeInfo::Unfreeze"};
shadow::SummaryClock g_summary;
shadow::PlannerSample g_planner;
bool g_controlWordLogged = false;

State Capture(const TimeInfo* self) {
	State s{};
	std::memcpy(&s.info, self, sizeof(TimeInfo));
	s.clockMs = *reinterpret_cast<volatile UInt32*>(kClockMs);
	s.previousClockMs = *reinterpret_cast<volatile UInt32*>(kPreviousClockMs);
	return s;
}

void LogMismatch(const char* name, const State& before, const State& original, const State& rebuild, UInt32 mask,
                 const char* inputs) {
	char fields[512];
	int at = 0;
	for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(shadow::Field::Count) && at < 480; ++i) {
		if ((mask & (1u << i)) == 0) {
			continue;
		}
		const auto f = static_cast<shadow::Field>(i);
		at += std::snprintf(fields + at, sizeof(fields) - at, " %s before %08X original %08X rebuild %08X;",
		                    shadow::FieldName(f), shadow::Bits(before, f), shadow::Bits(original, f),
		                    shadow::Bits(rebuild, f));
	}
	g_log("Shadow MISMATCH %s (%s):%s", name, inputs, fields);
}

void Record(int which, bool mismatch, const char* name, const State& before, const State& original,
            const State& rebuild, UInt32 mask, const char* inputs) {
	AcquireSRWLockExclusive(&g_lock);
	const bool write = g_counters[which].Record(mismatch, g_settings.mismatchLines);
	ReleaseSRWLockExclusive(&g_lock);
	if (write) {
		LogMismatch(name, before, original, rebuild, mask, inputs);
	}
}

void LogControlWordOnce() {
	if (g_controlWordLogged || !g_settings.fpuControlWord) {
		return;
	}
	g_controlWordLogged = true;
	const unsigned int cw = _control87(0, 0);
	const unsigned int pc = cw & _MCW_PC;
	const char* precision = pc == _PC_24 ? "24-bit" : pc == _PC_53 ? "53-bit" : pc == _PC_64 ? "64-bit" : "?";
	const unsigned int rc = cw & _MCW_RC;
	const char* rounding = rc == _RC_NEAR ? "nearest" : rc == _RC_DOWN ? "down" : rc == _RC_UP ? "up" : "chop";
	g_log("Probe: x87 control word at the first TimeInfo::Update - precision %s, rounding %s (_control87 %08X)",
	      precision, rounding, cw);
}

void SamplePlanner() {
	if (!g_settings.havokPlanner) {
		return;
	}
	const UInt32 type = *reinterpret_cast<volatile UInt32*>(kHavokUpdateType);
	const UInt32 steps = *reinterpret_cast<volatile UInt32*>(kHavokStepCount);
	g_planner.Add(type, steps);
}

void SummaryIfDue(UInt32 tick) {
	AcquireSRWLockExclusive(&g_lock);
	const bool due = g_summary.Due(tick, g_settings.summaryEverySeconds * 1000);
	shadow::Counter counters[4];
	shadow::PlannerSample planner;
	if (due) {
		for (int i = 0; i < 4; ++i) counters[i] = g_counters[i];
		planner = g_planner;
		g_planner.Reset();
	}
	ReleaseSRWLockExclusive(&g_lock);
	if (!due) {
		return;
	}
	g_log("Shadow: %s %llu calls %llu mismatches; %s %llu/%llu; %s %llu/%llu; %s %llu/%llu", kNames[0],
	      static_cast<unsigned long long>(counters[0].calls), static_cast<unsigned long long>(counters[0].mismatches),
	      kNames[1], static_cast<unsigned long long>(counters[1].calls), static_cast<unsigned long long>(counters[1].mismatches),
	      kNames[2], static_cast<unsigned long long>(counters[2].calls), static_cast<unsigned long long>(counters[2].mismatches),
	      kNames[3], static_cast<unsigned long long>(counters[3].calls), static_cast<unsigned long long>(counters[3].mismatches));
	if (g_settings.havokPlanner && planner.frames != 0) {
		const float stepSeconds = *reinterpret_cast<volatile float*>(kHavokStepSeconds);
		g_log("Probe: Havok planner over %u frames - update type %u, steps per frame 0:%u 1:%u 2:%u 3+:%u, last step %.6f s",
		      planner.frames, planner.lastType, planner.stepCounts[0], planner.stepCounts[1], planner.stepCounts[2],
		      planner.stepCounts[3], stepSeconds);
	}
}

UInt32 __fastcall ShadowUpdate(TimeInfo* self, void* /*edx*/, UInt32 tick) {
	LogControlWordOnce();
	const float multiplier = *reinterpret_cast<volatile float*>(kTimeMultiplier);
	const State before = Capture(self);
	const UInt32 eax = g_update(self, nullptr, tick);
	const State original = Capture(self);

	State rebuild = before;
	engine::time::update::TickGlobals globals{rebuild.clockMs, rebuild.previousClockMs};
	engine::time::update::RunUpdate(rebuild.info, tick, globals, multiplier);
	rebuild.clockMs = globals.currentTickMs;
	rebuild.previousClockMs = globals.previousTickMs;

	const UInt32 mask = shadow::Differences(original, rebuild);
	char inputs[96];
	std::snprintf(inputs, sizeof(inputs), "this %08X tick %08X multiplier %08X", reinterpret_cast<UInt32>(self), tick,
	              *reinterpret_cast<const UInt32*>(&multiplier));
	Record(0, mask != 0, kNames[0], before, original, rebuild, mask, inputs);
	SamplePlanner();
	SummaryIfDue(tick);
	return eax;
}

UInt32 __fastcall ShadowInit(TimeInfo* self, void* /*edx*/, UInt32 startTick) {
	const State before = Capture(self);
	const UInt32 eax = g_init(self, nullptr, startTick);
	const State original = Capture(self);

	State rebuild = before;
	engine::time::timeinfo_ctor::Apply(rebuild.info, engine::time::timeinfo_ctor::InitialValuesFor(startTick));

	const UInt32 mask = shadow::Differences(original, rebuild);
	char inputs[64];
	std::snprintf(inputs, sizeof(inputs), "this %08X startTick %08X", reinterpret_cast<UInt32>(self), startTick);
	Record(1, mask != 0, kNames[1], before, original, rebuild, mask, inputs);
	return eax;
}

struct RecoveredClock {
	UInt32 now;
	UInt32 TickCount() const { return now; }
	UInt32 operator()() const { return now; }
};

UInt32 __fastcall ShadowFreeze(TimeInfo* self, void* /*edx*/) {
	const State before = Capture(self);
	const UInt32 eax = g_freeze(self, nullptr);
	const State original = Capture(self);

	const shadow::RecoveredTick tick = shadow::RecoverFreezeTick(before, original, before.info.freezeDepth == 0);
	State rebuild = before;
	RecoveredClock clock{tick.now};
	engine::time::freeze::RunFreeze(rebuild.info, clock);

	const UInt32 mask = shadow::Differences(original, rebuild);
	char inputs[96];
	std::snprintf(inputs, sizeof(inputs), "this %08X depth %u%s", reinterpret_cast<UInt32>(self), before.info.freezeDepth,
	              tick.transition ? (tick.consistent ? ", outermost" : ", outermost, the two fields disagree on now") : "");
	Record(2, mask != 0 || !tick.consistent, kNames[2], before, original, rebuild, mask, inputs);
	return eax;
}

UInt32 __fastcall ShadowUnfreeze(TimeInfo* self, void* /*edx*/) {
	const State before = Capture(self);
	const UInt32 eax = g_unfreeze(self, nullptr);
	const State original = Capture(self);

	const shadow::RecoveredTick tick = shadow::RecoverFreezeTick(before, original, before.info.freezeDepth == 1);
	State rebuild = before;
	const RecoveredClock clock{tick.now};
	const engine::time::unfreeze::ClockFields fields =
		engine::time::unfreeze::Unfreeze(engine::time::unfreeze::ClockFields{rebuild.info.freezeDepth, rebuild.info.clockMs,
		                                                                     rebuild.info.startTick}, clock);
	rebuild.info.freezeDepth = fields.depth;
	rebuild.info.clockMs = fields.clockMs;
	rebuild.info.startTick = fields.startTick;

	const UInt32 mask = shadow::Differences(original, rebuild);
	char inputs[96];
	std::snprintf(inputs, sizeof(inputs), "this %08X depth %u%s", reinterpret_cast<UInt32>(self), before.info.freezeDepth,
	              tick.transition ? (tick.consistent ? ", outermost" : ", outermost, the two fields disagree on now") : "");
	Record(3, mask != 0 || !tick.consistent, kNames[3], before, original, rebuild, mask, inputs);
	return eax;
}

// One entry detour: the bytes checked, the trampoline built, the patch
// written; the original reachable through *original afterwards.
bool Detour(const char* name, UInt32 address, const UInt8* bytes, UInt32 length, const void* replacement, void** original) {
	if (!Verify(address, bytes, length)) {
		char have[32];
		DescribeBytes(address, have, sizeof(have));
		g_log("Shadow %s: the entry at %08X reads %s, not the game's own bytes - another plugin's patch (OBVR's "
		      "PreciseFrameClock?) or another game version; not shadowed",
		      name, address, have);
		return false;
	}
	constexpr UInt32 capacity = 32;
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(capacity));
	if (trampoline == nullptr) {
		g_log("Shadow %s: no executable memory; not shadowed", name);
		return false;
	}
	if (BuildEntryTrampoline(trampoline, capacity, reinterpret_cast<UInt32>(trampoline), address, bytes, length) == 0) {
		g_log("Shadow %s: trampoline did not fit; not shadowed", name);
		return false;
	}
	UInt8 patch[16];
	if (BuildEntryPatch(patch, sizeof(patch), address, reinterpret_cast<UInt32>(replacement), length) == 0) {
		g_log("Shadow %s: patch did not fit; not shadowed", name);
		return false;
	}
	*original = trampoline;
	if (!SafeWrite(address, patch, length)) {
		*original = nullptr;
		g_log("Shadow %s: could not write the patch; not shadowed", name);
		return false;
	}
	g_log("Shadow %s: on at %08X - the original runs, the rebuild runs on a copy, both compared bit for bit", name,
	      address);
	return true;
}

}  // namespace

UInt32 InstallShadowTimeInfo(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_settings = settings;
	UInt32 made = 0;
	if (settings.update) {
		made += Detour(kNames[0], kUpdate, kUpdateBytes, sizeof(kUpdateBytes), reinterpret_cast<const void*>(&ShadowUpdate),
		               reinterpret_cast<void**>(&g_update)) ? 1 : 0;
	}
	if (settings.init) {
		made += Detour(kNames[1], kInit, kInitBytes, sizeof(kInitBytes), reinterpret_cast<const void*>(&ShadowInit),
		               reinterpret_cast<void**>(&g_init)) ? 1 : 0;
	}
	if (settings.freeze) {
		made += Detour(kNames[2], kFreeze, kFreezeBytes, sizeof(kFreezeBytes), reinterpret_cast<const void*>(&ShadowFreeze),
		               reinterpret_cast<void**>(&g_freeze)) ? 1 : 0;
	}
	if (settings.unfreeze) {
		made += Detour(kNames[3], kUnfreeze, kUnfreezeBytes, sizeof(kUnfreezeBytes),
		               reinterpret_cast<const void*>(&ShadowUnfreeze), reinterpret_cast<void**>(&g_unfreeze)) ? 1 : 0;
	}
	return made;
}

}  // namespace obng::plugin
