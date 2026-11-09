#pragma once

// The volatile pack's skip in the game (src/engine/render/VolatilePackLogic.h):
// entry detours on NiDX9Renderer::PackSkinnedGeometryBuffer 0x00763130 and the
// dynamic buffer unlock 0x00776D80.
//
//   [Route] VolatilePack=1: a volatile geometry (a named NPC's head) whose
//     positions and normals equal the ones of its record's last pack, and
//     whose vertex buffers nobody else has written since, is handed to the
//     packer as static for that call - the packer's own early-out returns
//     without touching the record or the buffers. Everything else packs as
//     before (InstallRouteVolatilePack).
//   [Shadow] VolatilePack=1: the same decision, but a pack that would be
//     skipped runs, and what it writes is compared with the record's last
//     pack byte for byte, leaving out the entry the packer does not write
//     (the colour entry of a data without colours); the game gets the
//     original's pack either way (InstallShadowVolatilePack).
//
// Both take the same entries as [Probe] PackCache; Load installs neither
// beside it, and the shadow wins over the route.

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"
#include "Types.h"

namespace obng::plugin {

UInt32 InstallRouteVolatilePack(const RouteSettings& settings, LogFn log);
UInt32 InstallShadowVolatilePack(const ShadowSettings& settings, LogFn log);
void LogVolatilePackSummary();

}  // namespace obng::plugin
