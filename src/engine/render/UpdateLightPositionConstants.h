#pragma once

// The engine-facing shape of 0x007C9140 (proposed
// UpdateLightPositionConstants): the layouts it reads and writes, its calling
// convention and its one callee's as called here. The decisions live in
// UpdateLightPositionConstantsLogic.h.
//
// Evidence: re/functions/0x007C9140.md, re/export/perf-levers/0x007C9140/asm.s.
// Names are proposals until the perf-levers slice's refutation round.
//
// Only addresses with two sources are constants here (AGENTS.md):
//   0x007C9140 the function itself - the export (meta.json, asm.s) and OBVR
//              docs/performance-spec.md:1282 ("the light constants 0x007C9140
//              15 % - three light positions transformed by
//              D3DXVec3TransformCoord for every draw", OBVR's profile).
// The globals it touches have one source each (this body; the
// knowledge/bone-palettes.md entry is OBNG's own reading of it) and stay in
// comments until a second one agrees:
//   0x00B45040  ShaderConstantDescView* [3]   slot records, byte +0x08 read
//               (0x007C9160/0x007C9162; bound 0x00B4504C at 0x007C9216)
//   0x00B46528  LightPositionEntryView[3]     +0x00..+0x08 read (0x007C916C,
//               0x007C917B, 0x007C918A; stride ADD ESI,0x10 at 0x007C9213)
//   0x00B465A8  LightWEntryView[3]            +0x00 read (0x007C919C)
//   0x00B44FD8  LightConstantView[3]          written (0x007C91EC..0x007C920A)
// The structs are views: they cover the bytes this function touches, with the
// stride the indexing implies; what else the engine keeps there is unverified.

#include "engine/render/UpdateLightPositionConstantsLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::update_light_position_constants::engine {

inline constexpr std::uint32_t kUpdateLightPositionConstantsAddress = 0x007C9140;

static_assert(sizeof(void*) == 4, "the views mirror a 32-bit binary");

// The record a slot pointer at 0x00B45040 + 4i points at: only byte +0x08 is
// read (CMP byte ptr [EAX+0x8],0x0 at 0x007C9162), once per slot after the
// earlier slots' stores - the rebuild's slotFlag port. A shader-constant
// descriptor with a "used by the current shader" byte is a guess
// (unverified, re/functions/0x007C9140.md open questions).
struct ShaderConstantDescView {
	std::uint8_t unread00[0x08];
	std::uint8_t enabled;  // +0x08
};
static_assert(offsetof(ShaderConstantDescView, enabled) == 0x08, "flag byte at +0x08");
static_assert(sizeof(ShaderConstantDescView) == 0x09, "the view ends after the flag byte");

// One entry of the light position table 0x00B46528: three floats read, the
// fourth implied by the 0x10 stride and not read here.
struct LightPositionEntryView {
	float x;  // +0x00 (FLD [ESI+0xB46528] at 0x007C916C)
	float y;  // +0x04 (FLD [ESI+0xB4652C] at 0x007C917B)
	float z;  // +0x08 (FLD [ESI+0xB46530] at 0x007C918A)
	std::uint8_t unread0C[0x04];
};
static_assert(offsetof(LightPositionEntryView, x) == 0x00, "x at +0x00");
static_assert(offsetof(LightPositionEntryView, y) == 0x04, "y at +0x04");
static_assert(offsetof(LightPositionEntryView, z) == 0x08, "z at +0x08");
static_assert(sizeof(LightPositionEntryView) == 0x10, "stride ADD ESI,0x10 at 0x007C9213");

// One entry of 0x00B465A8: the float the output's w comes from. Whether this
// and 0x00B46528 are one table (0x00B465A8 = 0x00B46528 + 0x80) is unverified.
struct LightWEntryView {
	float w;  // +0x00 (FLD [ESI+0xB465A8] at 0x007C919C)
	std::uint8_t unread04[0x0C];
};
static_assert(offsetof(LightWEntryView, w) == 0x00, "w at +0x00");
static_assert(sizeof(LightWEntryView) == 0x10, "stride ADD ESI,0x10 at 0x007C9213");

// One vec4 of the constant block 0x00B44FD8, written whole by an enabled slot.
struct LightConstantView {
	float x;  // +0x00 (FSTP [ESI+0xB44FD8] at 0x007C91EC)
	float y;  // +0x04 (FSTP [ESI+0xB44FDC] at 0x007C91F6)
	float z;  // +0x08 (FSTP [ESI+0xB44FE0] at 0x007C9200)
	float w;  // +0x0C (FSTP [ESI+0xB44FE4] at 0x007C920A)
};
static_assert(offsetof(LightConstantView, w) == 0x0C, "w at +0x0C");
static_assert(sizeof(LightConstantView) == 0x10, "stride ADD ESI,0x10 at 0x007C9213");

// The three vec4s of the block end at 0x00B45008 (derived: 0x00B44FD8 +
// 3 * 0x10), before the slot pointers at 0x00B45040.
inline constexpr std::size_t kConstantBlockBytes = kSlotCount * sizeof(LightConstantView);
static_assert(kConstantBlockBytes == 0x30, "three vec4s");

// ---- Calling conventions --------------------------------------------------------

// 0x007C9140 itself: stdcall, four dword arguments, RET 0x10 at 0x007C9229;
// no register input (ECX, EDX, EAX are written before they are read:
// 0x007C9177, 0x007C9186, 0x007C9160). arg1 is held in EBP and passed as pM
// (0x007C9149, 0x007C9172), arg2 read as float (0x007C91BA, 0x007C91E0), arg3
// and arg4 as integers (0x007C91A8, 0x007C9144). Returns nothing on purpose
// (re/functions/0x007C9140.md).
using UpdateLightPositionConstantsFn = void(__stdcall*)(const void* matrix, float scale, std::uint32_t passId,
	std::uint32_t skipAdjust);

// 0x009A1B0A D3DXVec3TransformCoord, an import of D3DX9_27.DLL (meta.json):
// pushed right to left pOut = [ESP+0x10] local, pV = [ESP+0x1C] local,
// pM = arg1 (0x007C9172..0x007C9190); stdcall, it pops its 12 bytes (derived:
// nothing adjusts ESP after the CALL at 0x007C9195 and the later [ESP+0x30]/
// [ESP+0x34] operands address arg2/arg3 only with those bytes gone). The
// result is ignored here. What it computes is D3DX's (unverified here: the
// export has no D3DX body); the rebuild calls it through the transform port.
using D3DXVec3TransformCoordFn = void*(__stdcall*)(float* out, const float* in, const void* matrix);

} // namespace obng::engine::render::update_light_position_constants::engine
