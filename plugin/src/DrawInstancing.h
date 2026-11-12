#pragma once
// [Route] DrawInstancing (DrawInstancingLogic.h has the what and why): the
// copies of a mesh that one of the shader accumulator's pass lists draws with
// ShadowLightShader's SLS2003 programs wait at the list's draw step and are
// drawn at the list's end - 1: each by itself, in the new order, the
// constants an instanced draw would use compared with the engine's own
// uploads; 2: each group of two or more as one instanced draw. 0 (the
// default) installs nothing. [Route] DrawInstancingAlternate=1 switches it
// on and off by summary window, for a measurement inside one run.
//
// Two patches: the entry of BSShaderAccumulator::RenderPassList 0x007AC9A0
// (the list's begin and end) and, inside DrawRenderPass 0x007A9820, its call
// of the geometry's slot 33 (0x007A98C3..0x007A98CF: the record's draw, after
// its pass id, record and light constants are published). A record that waits
// is drawn later through DrawRenderPass's own body again (a trampoline of its
// original entry bytes, past any detour on the entry), so that its globals and
// light constants are written anew. Needs [Route] StereoDraw=1: the instanced
// draw takes the place of the whole draw's device call (RoutedDrawDevice.h).
#include <cstdint>

#include "DrawInstancingDevice.h"
#include "RouteSettings.h"
#include "ShadowTimeInfo.h"

namespace obng::plugin {

// Reads [Route] DrawInstancing and DrawInstancingAlternate from the INI at
// iniPath; installs nothing when off or without the stereo route.
void InstallDrawInstancing(const char* iniPath, const RouteSettings& route, LogFn log);

// Asks for the summary lines; the render thread writes them at its next pass
// list (the counts since the last lines, then reset).
void LogDrawInstancingSummary();

namespace draw_instancing {

// On when installed; the whole draw's device port asks the two below only then.
inline bool g_on = false;

// Every indexed device draw of the whole draw (render thread): true when the
// instancing drew the call itself (the instanced draw of a group's leader).
// Otherwise it may only have looked (a prediction learned, mode 1's state read).
bool RouteIndexedDraw(std::uint32_t device, std::uint32_t shader, std::uint32_t geometry, const IndexedCall& call);

// A plain (not indexed) device draw of the whole draw: a group's leader drawn
// so cannot be instanced.
void RoutePlainDraw(std::uint32_t geometry);

}  // namespace draw_instancing

}  // namespace obng::plugin
