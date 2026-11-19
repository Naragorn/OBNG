#pragma once

// [Route] AnimationPauseBatch (src/engine/io/AnimationPauseBatchLogic.h): an actor's animation setup 0x00475D80 keeps
// the background loader paused from its first KF miss to its end, so it waits for the loader once instead of once a
// file. The setup's entry detoured; the resume's call inside the KF load 0x00439FF0 at 0x0043A088 patched. 0 off, 1 on.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallAnimationPauseBatch(const char* iniPath, LogFn log);
void LogAnimationPauseBatchSummary();

}  // namespace obng::plugin
