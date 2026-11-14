#pragma once

// The occlusion culler's guards, the part that reads the game
// (OcclusionCullGuardLogic.h has the decisions): which object an address
// holds, what hangs under the far LOD's root, the passes after the player's
// cell changed, and the node log. Render thread, called by OcclusionCull.cpp.

#include "OcclusionCullLogic.h"
#include "OcclusionProbeLogic.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

// The object's name; a pointer that cannot be read gives the empty name.
void OcclusionCullReadName(UInt32 object, char out[occlusion::kNameWidth]);

// The identity of the object at this address (never 0): its name, its
// parent, for a geometry its model data, and the place of its bound's centre.
std::uint32_t OcclusionCullIdentity(UInt32 object, bool node, const float centre[3]);

// Whether the object hangs under "LODRoot" 0x00B333A8 (the land LOD, the
// distant objects, the LOD trees). Asked once per identity.
bool OcclusionCullIsLod(std::uint32_t object);

// At an eye pass's begin, after the results were read: the player's cell is
// looked at, and a window that closed is written (`live` are the counts
// since the last summary).
void OcclusionCullGuardPass(UInt32 frame, const occlusion_cull::Stats& live);

// At the summary, before the counts are reset: the guards' line, and the
// counts added to the totals.
void OcclusionCullGuardSummary(const occlusion_cull::Stats& window, occlusion_cull::LodRule rule);

// [Route] OcclusionCullNodeLog: one node left out, by name - the node, its
// vtable, its bound, three parents and its first children.
void OcclusionCullLogNode(UInt32 node, UInt32 frame, LogFn log);

void OcclusionCullGuardInit(LogFn log);

}  // namespace obng::plugin
