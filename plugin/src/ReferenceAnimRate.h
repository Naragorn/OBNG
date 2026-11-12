#pragma once
// [Route] ReferenceAnimEvery: a reference found with nothing to animate is
// asked again only in one frame of n by the cells' animation update
// (TESObjectCELL::UpdateReferenceAnimations 0x004CCC50 -> the reference's
// slot +0x160, 0x004E3F30 for a plain reference); one with something to do is
// called every frame as before (engine/scene/ReferenceAnimRateLogic.h).
// Class C: a different computation on purpose. 1 = off. Takes 0x004E3F30's
// entry, so not together with a [Timed] line on it. ReferenceAnimAlternate=1
// switches it by summary window for a measurement.
#include "ShadowTimeInfo.h"

namespace obng::plugin {

// Reads its key from the INI at iniPath; installs nothing when off.
void InstallReferenceAnimRate(const char* iniPath, LogFn log);
void LogReferenceAnimRateSummary();

}  // namespace obng::plugin
