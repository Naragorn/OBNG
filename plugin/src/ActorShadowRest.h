#pragma once
// [Route] ActorShadowEvery=n (1 = off): the actor shadow step 0x004073D0
// draws the map of a shadow light whose caster stands still (within
// ActorShadowStill units of the light's last draw, 4 by default) in one step
// of n only; between them the light keeps its map, projection and geometry
// (engine/render/ActorShadowRestLogic.h). Class C: a resting shadow is up to
// n - 1 steps old. ActorShadowAlternate=1 switches it on and off in blocks of
// n steps for a measurement (the step's own time by side in the summary).
//
// Five patches, all inside the step and checked against the game's bytes
// first (none or all are made): the scene render's call of the step
// (0x0040C91B, to time it and number the steps), the map's release in the
// lights' reset (0x007C5C61), the walk's draw block (0x004078DA), the call of
// the maps' render (0x00407A4B) and its call of one light's map (0x007C6943).
// Not the step's entry: OBVR's shadow share and [Probe] GpuPasses take that.
#include "ShadowTimeInfo.h"

namespace obng::plugin {

// Reads its keys from the INI at iniPath; installs nothing when off.
void InstallActorShadowRest(const char* iniPath, LogFn log);
void LogActorShadowRestSummary();

}  // namespace obng::plugin
