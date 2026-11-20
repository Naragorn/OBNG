#pragma once

// [Route] AnimationLazyAppend=1 (src/engine/io/AnimationLazyAppendLogic.h): the append of an animation file to an
// actor's list of files still to be cloned (the call of 0x0067B1E0 at 0x00474348) keeps the list's last node between
// the appends of one animation set up, instead of walking the list from its head each time. The set up's begin and
// end come from [Route] AnimationPauseBatch's detour of 0x00475D80; without that route every append walks as the
// game's does.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallAnimationLazyAppend(const char* iniPath, LogFn log);
void LogAnimationLazyAppendSummary();

// An animation set up 0x00475D80 begins and ends on this thread (called by AnimationPauseBatch's detour).
void AnimationSetupBegins();
void AnimationSetupEnds();

}  // namespace obng::plugin
