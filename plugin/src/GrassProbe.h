#pragma once

// The grass probe ([Probe] Grass, GrassProbe.cpp): per world pass, the grass
// patches under the engine's Grass node, those with the hidden bit and those
// the pass drew, by distance from the camera, next to the camera's heading -
// for distant grass that goes and comes as the head turns. No Direct3D
// object, a few reads per patch and per draw: it may stay on while playing.
// It counts through the stereo route's detours, so it needs [Route]
// StereoDraw=1.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Whether the probe is on (set once by InstallGrassProbe).
extern bool g_grassProbeOn;

void InstallGrassProbe(LogFn log);

// The route's detours call this for every geometry they draw while the
// probe is on; worldViewport says the device's viewport is eye 0's (a draw
// of the world pass), or that no eye data is set and the draw cannot be
// told apart.
void GrassProbeNoteDraw(UInt32 geometry, bool worldViewport);

// The route calls this when the eye data of a new single-pass frame is set:
// the draws counted since the last call are the pass before it.
void GrassProbePassBoundary();

}  // namespace obng::plugin
