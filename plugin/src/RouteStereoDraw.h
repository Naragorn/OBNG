#pragma once

// The stereo draw's route (RouteStereoDraw.cpp): the game runs OBNG's
// rebuilt DrawPrimitive 0x007672F0 and DrawSkinnedPrimitive 0x00767520 (the
// stereo-draw slice) instead of the originals, with the eye plan [Route]
// StereoEyes. The decisions are in RouteStereoDrawLogic.h; the eye switch
// between a partition's two draws in StereoEyeSwitchLogic.h; the eye data
// comes from OBVR through StereoEyesApi.h (the three exported functions in
// Plugin.cpp forward to the three below) or from [Route] StereoTestEyeShift.

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "StereoEyesApi.h"
#include "Types.h"

namespace obng::plugin {

// Puts the two detours when settings.stereoDraw is on; returns how many were
// made (0..2). With one made the route is off (both or none).
UInt32 InstallRouteStereoDraw(const RouteSettings& settings, LogFn log);

// The route's line since the last summary; nothing when the route is off.
void LogStereoDrawSummary();

// Behind ObngStereoPlan / ObngStereoSetEyes / ObngStereoClearEyes
// (StereoEyesApi.h states the contract).
UInt32 StereoPlan();
UInt32 SetStereoEyes(const ObngStereoEyes* eyes);
void ClearStereoEyes();

// The viewport of eye 0 as the interface set it, for the unrouted-draw
// probe; false when no eye data is set. Render thread.
bool StereoEyeZeroViewport(ObngStereoViewport* out);

}  // namespace obng::plugin
