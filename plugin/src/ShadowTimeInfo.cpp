// The shadow detours on the four TimeInfo functions. Each replacement runs
// the original first, on the game's own state, so the game never sees the
// rebuild's result; then it runs the rebuilt logic on a copy of the state
// as it was before, and compares what the two left, bit for bit. A
// difference is counted and, within the log budget, written with every
// field of both sides (ShadowRecorder).

#include "ShadowTimeInfo.h"

#include <windows.h>

#include <cfloat>
#include <cstdio>
#include <cstring>

#include "EntryDetour.h"
#include "Memory.h"
#include "ShadowLogic.h"
#include "ShadowRecorder.h"
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

// The Havok planner's outputs (re/functions/0x00889810.md).
constexpr UInt32 kHavokUpdateType = 0x00BA7918;
constexpr UInt32 kHavokStepCount = 0x00BA7914;

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
bool g_controlWordLogged = false;

State Capture(const TimeInfo* self) {
	State s{};
	std::memcpy(&s.info, self, sizeof(TimeInfo));
	s.clockMs = *reinterpret_cast<volatile UInt32*>(kClockMs);
	s.previousClockMs = *reinterpret_cast<volatile UInt32*>(kPreviousClockMs);
	return s;
}

void Report(Shadowed which, bool mismatch, const State& before, const State& original, const State& rebuild, UInt32 mask,
            const char* inputs) {
	char detail[512] = {};
	if (mismatch) {
		DescribeStateDifferences(before, original, rebuild, mask, detail, sizeof(detail));
	}
	RecordCall(which, mismatch, detail, inputs);
}

void LogControlWordOnce() {
	LogFpuControlWordOnce("TimeInfo::Update");
}

void SamplePlanner() {
	if (!g_settings.havokPlanner) {
		return;
	}
	const UInt32 type = *reinterpret_cast<volatile UInt32*>(kHavokUpdateType);
	const UInt32 steps = *reinterpret_cast<volatile UInt32*>(kHavokStepCount);
	Planner().Add(type, steps);
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
	Report(Shadowed::TimeInfoUpdate, mask != 0, before, original, rebuild, mask, inputs);
	SamplePlanner();
	SummaryIfDue(tick, &Planner(), true);
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
	Report(Shadowed::TimeInfoInit, mask != 0, before, original, rebuild, mask, inputs);
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
	Report(Shadowed::TimeInfoFreeze, mask != 0 || !tick.consistent, before, original, rebuild, mask, inputs);
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
	Report(Shadowed::TimeInfoUnfreeze, mask != 0 || !tick.consistent, before, original, rebuild, mask, inputs);
	return eax;
}

}  // namespace

void LogFpuControlWordOnce(const char* where) {
	if (g_controlWordLogged || !g_settings.fpuControlWord) {
		return;
	}
	g_controlWordLogged = true;
	const unsigned int cw = _control87(0, 0);
	const unsigned int pc = cw & _MCW_PC;
	const char* precision = pc == _PC_24 ? "24-bit" : pc == _PC_53 ? "53-bit" : pc == _PC_64 ? "64-bit" : "?";
	const unsigned int rc = cw & _MCW_RC;
	const char* rounding = rc == _RC_NEAR ? "nearest" : rc == _RC_DOWN ? "down" : rc == _RC_UP ? "up" : "chop";
	g_log("Probe: x87 control word at the first %s - precision %s, rounding %s (_control87 %08X)", where, precision,
	      rounding, cw);
}

bool TimeInfoUpdateEntryIsTheGames() {
	return Verify(kUpdate, kUpdateBytes, sizeof(kUpdateBytes));
}

bool InstallEntryDetour(const char* name, UInt32 address, const UInt8* bytes, UInt32 length, const void* replacement,
                        void** original, LogFn log, DetourRole role) {
	const bool route = role == DetourRole::Route;
	const char* who = route ? "Route" : "Shadow";
	const char* refused = route ? "not routed" : "not shadowed";
	if (!Verify(address, bytes, length)) {
		char have[32];
		DescribeBytes(address, have, sizeof(have));
		log("%s %s: the entry at %08X reads %s, not the game's own bytes - another plugin's patch (OBVR's "
		    "PreciseFrameClock?) or another game version; %s",
		    who, name, address, have, refused);
		return false;
	}
	constexpr UInt32 capacity = 32;
	auto* trampoline = static_cast<UInt8*>(AllocExecutable(capacity));
	if (trampoline == nullptr) {
		log("%s %s: no executable memory; %s", who, name, refused);
		return false;
	}
	if (BuildEntryTrampoline(trampoline, capacity, reinterpret_cast<UInt32>(trampoline), address, bytes, length) == 0) {
		log("%s %s: trampoline did not fit; %s", who, name, refused);
		return false;
	}
	UInt8 patch[16];
	if (BuildEntryPatch(patch, sizeof(patch), address, reinterpret_cast<UInt32>(replacement), length) == 0) {
		log("%s %s: patch did not fit; %s", who, name, refused);
		return false;
	}
	*original = trampoline;
	if (!SafeWrite(address, patch, length)) {
		*original = nullptr;
		log("%s %s: could not write the patch; %s", who, name, refused);
		return false;
	}
	if (route) {
		log("Route %s: on at %08X - the game runs OBNG's rebuild instead of the original", name, address);
	} else {
		log("Shadow %s: on at %08X - the original runs, the rebuild runs on a copy, both compared bit for bit", name,
		    address);
	}
	return true;
}

UInt32 InstallShadowTimeInfo(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_settings = settings;  // the recorder is set up by Load (Host::initRecorder) before any installer
	UInt32 made = 0;
	if (settings.update) {
		made += InstallEntryDetour(ShadowName(Shadowed::TimeInfoUpdate), kUpdate, kUpdateBytes, sizeof(kUpdateBytes),
		                           reinterpret_cast<const void*>(&ShadowUpdate), reinterpret_cast<void**>(&g_update), log) ? 1 : 0;
	}
	if (settings.init) {
		made += InstallEntryDetour(ShadowName(Shadowed::TimeInfoInit), kInit, kInitBytes, sizeof(kInitBytes),
		                           reinterpret_cast<const void*>(&ShadowInit), reinterpret_cast<void**>(&g_init), log) ? 1 : 0;
	}
	if (settings.freeze) {
		made += InstallEntryDetour(ShadowName(Shadowed::TimeInfoFreeze), kFreeze, kFreezeBytes, sizeof(kFreezeBytes),
		                           reinterpret_cast<const void*>(&ShadowFreeze), reinterpret_cast<void**>(&g_freeze), log) ? 1 : 0;
	}
	if (settings.unfreeze) {
		made += InstallEntryDetour(ShadowName(Shadowed::TimeInfoUnfreeze), kUnfreeze, kUnfreezeBytes, sizeof(kUnfreezeBytes),
		                           reinterpret_cast<const void*>(&ShadowUnfreeze), reinterpret_cast<void**>(&g_unfreeze), log) ? 1 : 0;
	}
	return made;
}

}  // namespace obng::plugin
