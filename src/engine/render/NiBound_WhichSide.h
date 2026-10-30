#pragma once

// The engine-facing shape of 0x0047DA70 (proposed NiBound::WhichSide; name
// unverified, C16 of the mapping): the two 16-byte layouts it reads and its
// calling convention. The decisions live in NiBound_WhichSideLogic.h.
//
// Evidence: re/functions/0x0047DA70.md, re/export/perf-levers/0x0047DA70/
// (asm.s). No shared header defines a point, bound or plane yet; the layouts
// below are local to this rebuild and are a layout request for a shared
// header (NiPoint3 / NiBound / NiPlane), not a definition of the shared types.
// The type names are proposals (Gamebryo's, unverified).

#include "engine/render/NiBound_WhichSideLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::which_side::engine {

struct NiPoint3 {
	float x;
	float y;
	float z;
};

// `this` (ECX): the sphere. Callers pass an object's +0x20..+0x2C block
// (LEA ECX,[EBX+0x20] 0x0049CCB9, ADD ECX,0x20 0x0070DFF6; read, C12/C13).
struct NiBound {
	NiPoint3 center;  // +0x00: FMUL [ECX] 0x0047DA7C, [ECX+0x4] 0x0047DA77, [ECX+0x8] 0x0047DA83 (read)
	float radius;     // +0x0C: FLD [ECX+0xC] 0x0047DA9B, 0x0047DAB3 (read)
};

// The stack argument: the plane, distance = dot(normal, p) - constant.
struct NiPlane {
	NiPoint3 normal;  // +0x00: FLD [EAX] 0x0047DA7A, [EAX+0x4] 0x0047DA74, [EAX+0x8] 0x0047DA80 (read)
	float constant;   // +0x0C: FSUB [EAX+0xC] 0x0047DA90 (read)
};

static_assert(sizeof(NiPoint3) == 0x0C, "three floats");
static_assert(offsetof(NiBound, center) == 0x00, "centre at +0x00");
static_assert(offsetof(NiBound, radius) == 0x0C, "radius at +0x0C");
static_assert(sizeof(NiBound) == 0x10, "the bound is 16 bytes (0x007D2EDE..0x007D2EFB copy +0x20..+0x2C)");
static_assert(offsetof(NiPlane, normal) == 0x00, "normal at +0x00");
static_assert(offsetof(NiPlane, constant) == 0x0C, "constant at +0x0C");
static_assert(sizeof(NiPlane) == 0x10, "planes are 16 bytes apart in the callers' arrays (0x007D2F05, 0x0070DFE9)");
static_assert(sizeof(void*) == 4, "a 32-bit build");

inline constexpr std::uint32_t kWhichSideAddress = 0x0047DA70;

// 0x0047DA70 itself: `this` in ECX (dereferenced, never loaded), one stack
// argument, the plane pointer (MOV EAX,[ESP+0x4] 0x0047DA70), callee cleans
// (RET 0x4 at 0x0047DAB0, 0x0047DAC4, 0x0047DAC9) - thiscall (read). Returns
// 0, 1 or 2 in EAX; changes EAX, EFLAGS, the x87 condition codes and its own
// argument slot only; the x87 stack is balanced on every exit (read; derived
// from the instruction stack effects).
using WhichSideFn = std::int32_t(__thiscall*)(const NiBound* self, const NiPlane* plane);

// The rebuild over the engine's layouts: the value the original leaves in EAX
// when called with the x87 rounding control `mode` (the caller's control
// word; the function does not change it, read: no FLDCW in its 32
// instructions).
inline std::int32_t WhichSide(const NiBound& self, const NiPlane& plane, X87Rounding mode) {
	const SphereValues sphere{self.center.x, self.center.y, self.center.z, self.radius};
	const PlaneValues values{plane.normal.x, plane.normal.y, plane.normal.z, plane.constant};
	return static_cast<std::int32_t>(which_side::WhichSide(sphere, values, mode));
}

} // namespace obng::engine::render::which_side::engine
