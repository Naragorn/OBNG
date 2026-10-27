#pragma once

#include "Types.h"

namespace obng::plugin {

using LogFn = void (*)(const char* format, ...);

struct ShadowSettings {
	bool update = true;
	bool init = true;
	bool freeze = true;
	bool unfreeze = true;
	bool fpuControlWord = true;  // log the x87 control word at the first Update
	bool havokPlanner = true;    // sample the planner's outputs every frame, histogram in the summary
	UInt32 mismatchLines = 20;   // mismatches written in full per function
	UInt32 summaryEverySeconds = 10;
};

// Puts the shadow detours on the TimeInfo functions the settings name:
// each call runs the original on the game's state and the rebuild on a
// copy, compares bit for bit, and the game continues with the original's
// result. Returns how many detours were made; what was skipped is logged.
UInt32 InstallShadowTimeInfo(const ShadowSettings& settings, LogFn log);

}  // namespace obng::plugin
