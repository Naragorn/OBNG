#include "QuestTargetDefer.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "StepHooks.h"
#include "engine/world/QuestTargetDeferLogic.h"

namespace obng::plugin {

namespace {

namespace qtd = obng::engine::world::quest_target_defer;

constexpr UInt32 kSite = 0x004454E6;                         // call 0x00665260 in the grid's move 0x00444FB0 (read)
constexpr UInt8 kSiteBytes[] = {0xE8, 0x75, 0xFD, 0x21, 0x00};
constexpr UInt32 kTargets = 0x00665260;                      // __thiscall(player, ref), RET 4 (read)
constexpr UInt32 kPlayer = 0x00B333C4;                       // PlayerCharacter* (read: the site's ECX)

using TargetsFn = void(__thiscall*)(void* player, void* ref);

LogFn g_log = nullptr;
bool g_on = false;
qtd::Pending g_pending;  // the main thread's only (the crossing and the step both run on it)
double g_ticksPerMs = 0.0;
unsigned long long g_runs = 0, g_dropped = 0;
double g_runMs = 0.0, g_longestMs = 0.0;

UInt32 PlayerNow() {
	return *reinterpret_cast<const UInt32*>(kPlayer);
}

// In place of the grid's call: kept for the next step.
void __fastcall Keep(void* player, void*, void* ref) {
	qtd::Keep(g_pending, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(player)), static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(ref)));
}

// At the next update step's start: the kept call made.
void RunKept() {
	const UInt32 player = g_pending.player, ref = g_pending.argument;
	const qtd::Outcome outcome = qtd::Take(g_pending, PlayerNow());
	if (outcome == qtd::Outcome::None) {
		return;
	}
	if (outcome == qtd::Outcome::Drop) {
		++g_dropped;
		return;
	}
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	reinterpret_cast<TargetsFn>(kTargets)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(player)), reinterpret_cast<void*>(static_cast<std::uintptr_t>(ref)));
	QueryPerformanceCounter(&t1);
	const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) / g_ticksPerMs;
	++g_runs;
	g_runMs += ms;
	g_longestMs = ms > g_longestMs ? ms : g_longestMs;
}

}  // namespace

void InstallQuestTargetDefer(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (qtd::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "QuestTargetDefer", 0, iniPath))) == qtd::Mode::Off) {
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	if (!Verify(kSite, kSiteBytes, sizeof(kSiteBytes))) {
		log("Route: quest target defer - the call at %08X is not the game's own; not routed", kSite);
		return;
	}
	if (!AddStepHook(&RunKept, "the quest targets' rebuild kept from a crossing", log)) {
		log("Route: quest target defer - no step hook; not routed");
		return;
	}
	UInt8 patch[sizeof(kSiteBytes)] = {0xE8};
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Keep)) - (kSite + 5);
	std::memcpy(patch + 1, &rel, 4);
	if (!SafeWrite(kSite, patch, sizeof(patch))) {
		log("Route: quest target defer - the call at %08X could not be written; not routed", kSite);
		return;
	}
	g_on = true;
	log("Route: quest target defer - the quest targets' rebuild after a cell crossing runs at the next update step's start");
}

void LogQuestTargetDeferSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: quest target defer - %llu rebuilds a step later (%.3f ms in all, the longest %.3f ms), %llu dropped (another player), %llu merged",
	      g_runs, g_runMs, g_longestMs, g_dropped, static_cast<unsigned long long>(g_pending.merged));
	g_runs = g_dropped = 0;
	g_runMs = g_longestMs = 0.0;
}

}  // namespace obng::plugin
