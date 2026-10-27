#pragma once

#include "Types.h"

namespace obng::plugin {

using LogFn = void (*)(const char* format, ...);

struct ShadowSettings {
	bool update = true;
	bool init = true;
	bool freeze = true;
	bool unfreeze = true;
	bool loadingMenuTick = true;
	bool loadingMenuCtor = true;
	bool fpuControlWord = true;  // log the x87 control word at the first Update
	bool havokPlanner = true;    // sample the planner's outputs every frame, histogram in the summary
	UInt32 mismatchLines = 20;   // mismatches written in full per function
	UInt32 summaryEverySeconds = 10;

	UInt32 Asked() const {
		return (update ? 1u : 0u) + (init ? 1u : 0u) + (freeze ? 1u : 0u) + (unfreeze ? 1u : 0u) +
		       (loadingMenuTick ? 1u : 0u) + (loadingMenuCtor ? 1u : 0u);
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

// One entry detour: the bytes checked, the trampoline built, the patch
// written; the original reachable through *original afterwards. Shared by
// the two installers.
bool InstallEntryDetour(const char* name, UInt32 address, const UInt8* bytes, UInt32 length, const void* replacement,
                        void** original, LogFn log);

}  // namespace obng::plugin
