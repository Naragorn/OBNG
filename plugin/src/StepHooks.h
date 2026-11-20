#pragma once

// The main loop's call of the update step 0x0040D800 at 0x0040F270 (read 2026-11-19), shared: OBNG's per-step work
// registers here and runs on the main thread right before each step (the next frame's start), in the order added.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

using StepHook = void (*)();

// Adds a hook; the call site is patched with the first one. False when the site is not the game's own or full.
bool AddStepHook(StepHook hook, const char* name, LogFn log);

}  // namespace obng::plugin
