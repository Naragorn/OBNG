#pragma once

// [Probe] MusicGraph=1: the music player's track start split - its seven CoCreateInstance(CLSID_FilterGraph) calls
// (0x006AB366, 0x006ACF46, 0x006AD216, 0x006AD4F8, 0x006AD806, 0x006ADB76, 0x006ADD56) timed, and the graph's
// RenderFile (vtable +0x34, swapped in quartz's vtable from the first graph on) timed with its path. A line each start.

#include "ShadowTimeInfo.h"

namespace obng::plugin {

void InstallMusicGraphProbe(const char* iniPath, LogFn log);

}  // namespace obng::plugin
