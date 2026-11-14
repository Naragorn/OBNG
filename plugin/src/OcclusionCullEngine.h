#pragma once

// The renderer's matrices and the world's origin, as the occlusion culler's
// files read them (re/functions/0x00762640.md: NiDX9Renderer's view +0x980,
// projection +0x9C0, inverse view +0xA00; the origin global beside the
// renderer's). The 32-bit engine-facing shape, for the plugin only.

#include "Types.h"

namespace obng::plugin::occlusion_cull::engine {

constexpr UInt32 kRendererViewOffset = 0x980;
constexpr UInt32 kRendererProjOffset = 0x9C0;
constexpr UInt32 kRendererInverseViewOffset = 0xA00;
constexpr UInt32 kOriginGlobal = 0x00B3F92C;

}  // namespace obng::plugin::occlusion_cull::engine
