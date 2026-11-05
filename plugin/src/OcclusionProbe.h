#pragma once

// The occlusion probe ([Probe] Occlusion, OcclusionProbe.cpp): samples one
// eye pass in N and asks, per geometry drawn in it, whether the geometry's
// world bound lies wholly behind the pass's finished depth buffer - the
// share an occlusion culler could leave out. A measurement, off in play.

#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// Puts the probe's four detours (the finish and the three draw entries that
// take a geometry); returns how many were made. `every` is N.
UInt32 InstallOcclusionProbe(UInt32 every, LogFn log);

// Asks for the probe's summary lines; the render thread writes them at its
// next finish (the counts since the last lines, then reset). Does nothing
// when the probe is not on.
void LogOcclusionSummary();

// A scene-graph object's name (NiObjectNET +0x08) for a log line, cut to the
// buffer and made printable; empty when the pointer cannot be read.
void ReadObjectName(UInt32 object, char* out, UInt32 capacity);

}  // namespace obng::plugin
