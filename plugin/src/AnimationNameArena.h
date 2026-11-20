#pragma once

// [Route] AnimationNameArena (src/engine/io/AnimationNameArenaLogic.h): the list of KF file names an actor's
// animation set up 0x004E3490 asks 0x00435830 for is made in a block of OBNG's own - the same names in the same
// order - and the three frees of that list in 0x00475D80 leave it alone; the block is given back when the set up
// 0x00475D80 returns ([Route] AnimationPauseBatch's detour says when) or frees the list's head. 0 off, 1 the route,
// 2 the check: the game's list is made too and compared name for name, and the game goes on with its own.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallAnimationNameArena(const char* iniPath, LogFn log);
void LogAnimationNameArenaSummary();

// The animation set up 0x00475D80 called with this list has returned on this thread (AnimationPauseBatch's detour).
void AnimationNameArenaSetupEnds(void* list);

}  // namespace obng::plugin
