#pragma once

// The stereo contract of the two draw functions - NiDX9Renderer::DrawPrimitive
// 0x007672F0 and DrawSkinnedPrimitive 0x00767520 - rebuilt as units for
// single-pass stereo (docs/subsystems/single-pass-stereo.md). This header
// states what both rebuilds share: the port names, the eye plan and the one
// rule that differs from the original. It defines no engine layout; the
// rebuilds' own <Name>.h headers carry those.
//
// The rule (read in both maps, re/functions/0x007672F0.md and
// 0x00767520.md; the contract in docs/subsystems/single-pass-stereo.md):
// per pass and per partition the original does, in this order, shader slot
// 12, slot 13, slot 15 (the buffer record), slot 14, the render state's
// sub-object slot 1 ([[renderer+0x8AC]+0xFF0] +0x04), the device draw(s),
// slot 16. With a plan of ONE eye the rebuild does exactly that and nothing
// else (the 1:1 gate). With a plan of TWO eyes it does the same up to and
// including the first eye's draw(s), then: eyeSwitch.Switch(1) (the port
// that puts the second eye's view translation into the renderer and the
// device, sets the second viewport and re-applies the constants), the
// draw(s) again with the same arguments, eyeSwitch.Switch(0), then slot 16
// - so slots 12..14 and the record run once per partition and only the
// device draws double. Nothing else in the original's sequence moves.
//
// Marks: read = the maps' instruction-level readings; derived = the
// contract's reasoning; the eye switch's own body is OBVR's and the
// plugin's, not this header's.

#include <cstdint>

namespace obng::engine::render::stereo_draw {

// How many eyes one draw serves, and nothing else: the plan is an input of
// the draw logic, decided by the route per frame (1 in the dual pass and in
// the 1:1 gate, 2 in the single pass).
struct EyePlan {
	std::uint8_t eyes = 1;  // 1 or 2
};

constexpr bool DrawsTwice(EyePlan plan) {
	return plan.eyes == 2;
}

// The ports both draw rebuilds take (concept names; each rebuild's <Name>.h
// states the engine shapes behind them):
//   shader.Slot(n, args...)         the shader interface's vtable slot n (10..19), the
//                                   original's arguments, its return as the original reads it
//   renderState.SubObjectSlot1()    [[renderer+0x8AC]+0xFF0] slot +0x04 (per draw, before the draw)
//   renderState.SetDrawFlag(0)      [renderer+0x8AC] slot +0xFC with 0 (after the pipeline close)
//   device.DrawIndexedPrimitive(..) IDirect3DDevice9 slot 82, the original's argument order
//   device.DrawPrimitive(..)        slot 81 (DrawPrimitive 0x007672F0's arm without an index buffer)
//   eyeSwitch.Switch(eye)           the eye state for eye 0 or 1 (a no-op port in the 1:1 gate)
// A rebuild records every port call in order in its tests; the emulator
// gate compares that order with the original's traced calls.

} // namespace obng::engine::render::stereo_draw
