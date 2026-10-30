#pragma once

#include "Types.h"

namespace obng::plugin {

using LogFn = void (*)(const char* format, ...);

// The range-walk shadow needs two detours: the walk's entry 0x007D59E0 and
// the recording one on AddGeometry 0x007D5790.
constexpr UInt32 kRangeWalkDetours = 2;

struct ShadowSettings {
	bool update = true;
	bool init = true;
	bool freeze = true;
	bool unfreeze = true;
	bool loadingMenuTick = true;
	bool loadingMenuCtor = true;
	bool rangeWalk = false;      // the shadow-light range walk 0x007D59E0 against BatchedWalk (ShadowRangeWalk.cpp); off in play
	bool fpuControlWord = true;  // log the x87 control word at the first Update
	bool havokPlanner = true;    // sample the planner's outputs every frame, histogram in the summary
	bool callCounters = false;   // count the scene pass's hot functions per return site (Probes.cpp); off in play
	bool memoryProbe = true;     // commit, working set and address space in the summary
	UInt32 mismatchLines = 20;   // mismatches written in full per function
	UInt32 summaryEverySeconds = 10;
	bool summaryTimer = false;   // a thread drives the summary (runs with no per-frame detour); off in play

	UInt32 Asked() const {
		return (update ? 1u : 0u) + (init ? 1u : 0u) + (freeze ? 1u : 0u) + (unfreeze ? 1u : 0u) +
		       (loadingMenuTick ? 1u : 0u) + (loadingMenuCtor ? 1u : 0u) + (rangeWalk ? kRangeWalkDetours : 0u);
	}
};

// Puts the shadow detours on the TimeInfo functions the settings name:
// each call runs the original on the game's state and the rebuild on a
// copy, compares bit for bit, and the game continues with the original's
// result. Returns how many detours were made; what was skipped is logged.
UInt32 InstallShadowTimeInfo(const ShadowSettings& settings, LogFn log);

// The same for LoadingMenu::Tick and LoadingMenu::LoadingMenu
// (ShadowLoadingMenu.cpp). Call after InstallShadowTimeInfo.
UInt32 InstallShadowLoadingMenu(const ShadowSettings& settings, LogFn log);

// Whether TimeInfo::Update's entry still holds the game's own bytes. When
// another plugin's clock (OBVR's or OblivionFrameFixes' PreciseFrameClock)
// sits there, every shadow whose rebuild contains an Update - the
// LoadingMenu's Tick and constructor - would compare against that plugin,
// not against the game, and must stay off.
bool TimeInfoUpdateEntryIsTheGames();

// The x87 control word logged once, from whichever shadow runs first with
// the probe on (Update is off in play because of OBVR's clock, so Tick,
// which runs every frame, carries the probe there).
void LogFpuControlWordOnce(const char* where);

// What an entry detour is for, as its log lines say it: a shadow compares
// (the original runs), a route replaces (OBNG's rebuild runs instead).
enum class DetourRole : UInt8 { Shadow, Route };

// One entry detour: the bytes checked, the trampoline built, the patch
// written; the original reachable through *original afterwards. Shared by
// the shadow installers and the route.
bool InstallEntryDetour(const char* name, UInt32 address, const UInt8* bytes, UInt32 length, const void* replacement,
                        void** original, LogFn log, DetourRole role = DetourRole::Shadow);

}  // namespace obng::plugin
