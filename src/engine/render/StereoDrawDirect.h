#pragma once

// The stereo contract of NiDX9Renderer's two direct draw entries - vtable
// 0x00A88EA4 slots 90 (0x007694F0) and 91 (0x00769700), read in .rdata -
// rebuilt as units for single-pass stereo beside DrawPrimitive and
// DrawSkinnedPrimitive (StereoDraw.h, docs/subsystems/single-pass-stereo.md).
// Measured 2026-11-04 on OBVR's water scene ([Probe] DrawEntries, run
// single-pass-water-on/20261104-161055): slot 91 runs 400-550 times a
// frame and slot 90 about 30, and they are what draws the near terrain and
// the SpeedTree trees that the routed pair never sees; the renderer's other
// two entries (EndBatch 0x00766F30, slot 93 0x00766880) ran 0 times.
//
// What the two do (pseudocode, to be read at instruction level by the
// slice): with the renderer's byte +0x6F0 clear and the data's vertex count
// (data slot +0x50) non-zero, decide skinned or whole (0x00768890(geometry,
// 0)), cast geometry+0xBC to NiD3DShaderInterface (0x00560920, no default
// fallback), call the shader's slot 18 (+0x48), call the render state's
// sub-object slot 1 ([[renderer+0x8AC]+0xFF0] +0x04), then the device draws
// with the pipeline AS THE CALLER LEFT IT - no shader slots 12..16 here:
// whole: DrawPrimitive without an index buffer, else one DrawIndexedPrimitive
// per array entry (the DrawPrimitive rebuild's draw loop); skinned: per
// partition, the bone gate with the default shader (renderer+0x6D8), one
// DrawIndexedPrimitive per array entry (the DrawSkinnedPrimitive rebuild's
// loop); then data+0x2E &= 0xF000. Slot 91 additionally copies the
// geometry's world bound +0x20..+0x2C to renderer +0x658..+0x664 before the
// shader cast (as DrawSkinnedPrimitive does per pass).
//
// The rule, as in StereoDraw.h: with a plan of ONE eye the rebuild does
// exactly the original, call for call (the 1:1 gate against the emulator's
// traces). With TWO eyes it does the same up to and including the first
// eye's draw(s), then eyeSwitch.Switch(1), the draw(s) again with the same
// arguments, eyeSwitch.Switch(0), then the flags clear. For the skinned
// arm the double draw is per partition, as in DrawSkinnedPrimitive.
//
// The eye switch's constants re-apply has no slot-14 call to capture here;
// the route builds its arguments from the geometry as Do_RenderShape
// 0x007693E0 hands them to the shader slots (re/functions/0x007693E0.md):
// geometry, skin = geometry+0xB8, partition (0, or the partition), record
// (data+0x38 for the whole draw, partition+0x28 per partition),
// propertyState = renderer+0x0C, effectState = renderer+0x10, world =
// geometry+0x64, worldBound = geometry+0x20. That is the plugin's, not
// this header's.

#include "engine/render/StereoDraw.h"

namespace obng::engine::render::stereo_draw_direct {

using stereo_draw::DrawsTwice;
using stereo_draw::EyePlan;

// Ports (concept names; each rebuild's <Name>.h states the engine shapes):
//   memory.*                  the reads: renderer +0x6F0 / +0x0C / +0x10 / +0x280 / +0x658 / +0x6D8 / +0x8AC / +0xA94,
//                             geometry +0x20..+0x2C / +0xB4 / +0xB8 / +0xBC, data slot +0x50 and +0x2E / +0x38,
//                             skin +0x0C, partition array and fields, the buffer record fields
//   memory.IsSkinned(geometry)          0x00768890(geometry, 0), cdecl or thiscall as read
//   memory.CastToShaderInterface(o)     0x00560920(0x00B42858, o)
//   shader.Slot18(shader)               [[shader]+0x48], ECX = shader, no stack argument (as read)
//   renderState.SubObjectSlot1()        [[[renderer+0x8AC]+0xFF0]]+0x04
//   device.DrawPrimitive / DrawIndexedPrimitive   slots 81 / 82
//   debug.CannotRenderPartition(bones)  0x0060D0A0(format, bones) (the skinned bone gate's message)
//   eyeSwitch.Switch(eye)               StereoDraw.h; never called with one eye

}  // namespace obng::engine::render::stereo_draw_direct
