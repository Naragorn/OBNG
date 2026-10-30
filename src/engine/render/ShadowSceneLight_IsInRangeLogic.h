#pragma once

// The decisions of 0x007D2EA0 (proposed ShadowSceneLight::IsInRange; the name
// is the label knowledge/shadow-light-list-remove.md uses, no Bethesda or
// Gamebryo symbol is known), as pure functions over plain values, and its
// plane loop over three injected ports. Evidence: re/functions/0x007D2EA0.md
// and the export re/export/draw-setup/0x007D2EA0/ (asm.s, data.json); every
// address below is an instruction address in that asm.s (read) unless marked
// otherwise. Names are proposals until the draw-setup slice's refutation
// round; the address stays next to each.
//
// What the body does (read): a null source light is out of range (B1); a
// directional one (GetRTTI == NiRTTI_NiDirectionalLight) reaches everything
// (B2); otherwise this+0x14C picks the test (B3): the plane path asks
// 0x0047DA70 (proposed NiBound::WhichSide) about every active plane of
// this+0x150 (mask this+0x1B0) and retires the planes the bound is wholly on
// the positive side of (B4..B7); the distance path compares the light's
// range with |light - centre| - radius (B8).
//
// pseudo.c and the instructions: pseudo.c hides that the mask is re-read from
// memory for every plane and that the bound is a stack copy (C3, C5), and it
// shows the distance path as one comparison without the stores that round;
// the instructions are followed here (AGENTS.md).
//
// Floating point (distance path only; the plane path does no x87 work of its
// own). The game's x87 runs at precision control 24 bits (measured,
// knowledge/frame-clock.md) under a rounding control that depends on the
// call site: the shadow walk 0x007D59E0 calls this function with FCW 0x0C7F,
// truncate (measured, artifacts/shadow/obng-range-walk-routed/
// 20261030-131826). The mode is therefore an input (which_side::X87Rounding,
// no default). FSUB, FMUL and FADDP round the significand to 24 bits in the
// register under the mode and keep the register's exponent range; FSTP float
// rounds to the float format under the mode. The rebuild models each of
// those roundings where the instructions make it, with the helpers of
// NiBound_WhichSideLogic.h (exact products and TwoSum sums rounded to 24
// bits, StoreSingle). Where it matters (measured, emulator):
//   - the sum of squares keeps its products at 24 bits, so a sum that would
//     be exact at 64 bits rounds (case D7);
//   - a sum in the float subnormal range is rounded twice, once to 24 bits
//     and once by the store, which can differ from one float rounding by one
//     unit (case D10: 0xF0 where one rounding gives 0xEF);
//   - q - r is rounded before the compare (case D11: equality by rounding);
//   - a sum of squares beyond FLT_MAX stores as +inf at nearest and as
//     FLT_MAX under truncate, which flips the range test of a garbage-sized
//     bound (tools/difftest/cases/0x007D2EA0-rounding.json).
// The order of the sum is (dy*dy + dx*dx) + dz*dz (read, 0x007D2F93..
// 0x007D2FA1, C10); a different grouping differs (cases D8, D9).
// The square root 0x00982C30 does not run under the caller's rounding
// control: it saves the control word, loads (caller & 0x300) | 0x7F -
// precision kept, rounding to nearest, every exception masked - runs FSQRT
// and restores the saved word (read 2026-10-30 with objdump in the
// reference Oblivion.exe, md5 cdd2f0c5...: FSTCW 0x00982C4E, CALL 0x00991B15
// unless the word is 0x027F, AND EDX,0x300 / OR EDX,0x7F / FLDCW
// 0x00991B19..0x00991B27, FSQRT 0x00982C6C, FLDCW [ESP] 0x00991BA6 or
// 0x00991BD0). So the root is FSQRT at 24 bits, round to nearest, whatever
// the caller's mode; it stays a port here (the game's own function in the
// plugin), and only the store after it rounds under the caller's mode.
// NaN payloads are not modelled (SSE's default NaN differs from the x87's
// indefinite); they reach only the comparison, which is false for any NaN
// (derived from C11).

#include "engine/render/NiBound_WhichSideLogic.h"

#include <cstdint>

namespace obng::engine::render::shadow_scene_light_is_in_range {

// The address compared at 0x007D2EC2 (CMP EAX,0xB3FCFC; read) and named
// NiRTTI_NiDirectionalLight by xOBSE (obse/obse/NiRTTI_1_2_416.inl:145, via
// re/symbols/xobse.csv): two sources, so a constant. Only the address is
// compared; the bytes there are not read (C2, data.json).
inline constexpr std::uint32_t kNiRttiNiDirectionalLight = 0x00B3FCFC;

// B7: EDI from 0, ADD EDI,1 / CMP EDI,6 / JC 0x007D2F10 at 0x007D2F44..
// 0x007D2F4D (read): six planes, ascending.
inline constexpr unsigned kPlaneCount = 6;

// B2: CMP EAX,0xB3FCFC / JNZ at 0x007D2EC2/0x007D2EC7. Equal: in range.
constexpr bool IsDirectional(std::uint32_t rttiAddress) {
	return rttiAddress == kNiRttiNiDirectionalLight;
}

// B3: CMP dword [EBX+0x14C],0 at 0x007D2ED3, JZ 0x007D2F5D at 0x007D2EFF.
// Any non-zero dword selects the plane path; the value is never dereferenced
// here (C13; measured, emulator case C12 with 0x00000001).
constexpr bool UsesPlanePath(std::uint32_t planeSource) {
	return planeSource != 0;
}

// MOV ESI,1 / MOV ECX,EDI / SHL ESI,CL at 0x007D2F10..0x007D2F17 (C4).
constexpr std::uint32_t PlaneBit(unsigned plane) {
	return std::uint32_t{1} << plane;
}

// B4: TEST dword [EBX+0x1B0],ESI / JZ 0x007D2F44 at 0x007D2F19/0x007D2F1F.
constexpr bool IsPlaneActive(std::uint32_t mask, unsigned plane) {
	return (mask & PlaneBit(plane)) != 0;
}

// What one WhichSide answer does to the loop.
enum class PlaneOutcome : std::uint8_t {
	OutOfRange,  // B5: EAX == 2 -> result byte 0 (0x007D2F30), the loop goes on (C6)
	Retire,      // B6: EAX == 1 -> NOT ESI / AND [EBX+0x1B0],ESI (0x007D2F3C/0x007D2F3E)
	Keep,        // any other EAX: nothing (0x007D2F3A JNZ to the increment)
};

// B5/B6: CMP EAX,2 / JNZ, then CMP EAX,1 / JNZ at 0x007D2F2B..0x007D2F3A,
// full-dword compares (read; measured, emulator case C11: 3, -1, 0x102,
// 0x101 and 0x80000001 change nothing).
constexpr PlaneOutcome ClassifySide(std::int32_t side) {
	if (side == 2) {
		return PlaneOutcome::OutOfRange;
	}
	if (side == 1) {
		return PlaneOutcome::Retire;
	}
	return PlaneOutcome::Keep;
}

// The plane loop 0x007D2F03..0x007D2F4F over ports:
//   readMask()        -> the dword at this+0x1B0, read for every plane
//                        (TEST at 0x007D2F19) and again by the AND (C5);
//   writeMask(value)  -> the store of AND [EBX+0x1B0],ESI (0x007D2F3E);
//   testPlane(i)      -> WhichSide(bound copy, this+0x150+16*i) (CALL
//                        0x0047DA70 at 0x007D2F26), only for active planes.
// The result starts true (MOV byte [ESP+0x2C],1 at 0x007D2EF2) and a 2 clears
// it without leaving the loop, so later planes are still tested and still
// retired (C6) - an early exit would leave their bits set.
template <class ReadMask, class WriteMask, class TestPlane>
bool TestPlanes(ReadMask&& readMask, WriteMask&& writeMask, TestPlane&& testPlane) {
	bool inRange = true;
	for (unsigned plane = 0; plane < kPlaneCount; ++plane) {
		if (!IsPlaneActive(readMask(), plane)) {
			continue;
		}
		switch (ClassifySide(testPlane(plane))) {
		case PlaneOutcome::OutOfRange:
			inRange = false;
			break;
		case PlaneOutcome::Retire:
			writeMask(readMask() & ~PlaneBit(plane));
			break;
		case PlaneOutcome::Keep:
			break;
		}
	}
	return inRange;
}

// ---- The distance path 0x007D2F5D..0x007D2FDD -------------------------------------

struct PointValues {
	float x;
	float y;
	float z;
};

// The stack copy of the object's bound (0x007D2EDE..0x007D2EFB, C3).
struct BoundValues {
	PointValues centre;
	float radius;
};

// Every value the path stores, in the order it stores them.
struct DistanceTerms {
	float dx;            // FSTP [ESP+0x8]  0x007D2F67
	float dy;            // FSTP [ESP+0xC]  0x007D2F75
	float dz;            // FSTP [ESP+0x10] 0x007D2F83
	float sumOfSquares;  // FSTP [ESP+0x28] 0x007D2FA3, s
	float distance;      // FSTP [ESP+0x28] 0x007D2FB0, q = sqrt(s)
	float gap;           // FSTP [ESP+0x28] 0x007D2FC1, d = q - radius
};

// FLD light / FSUB centre / FSTP float: one 24-bit rounding, then the store,
// both under the caller's mode.
inline float DifferencePc24(float light, float centre, which_side::X87Rounding mode) {
	return which_side::StoreSingle(which_side::SubPc24(light, centre, mode), mode);
}

// 0x007D2F87..0x007D2FA3 (read, C10): FLD dy, dx, dz; FLD ST1 / FMULP ST2
// (dx*dx); FLD ST2 / FMULP ST3 (dy*dy); FXCH / FADDP ST2,ST0 (dy*dy + dx*dx);
// FMUL ST0 (dz*dz); FADDP (+ dz*dz); FSTP float. Every product and sum is
// rounded to 24 bits in the register, only the final sum by the store.
inline float SumOfSquaresPc24(float dx, float dy, float dz, which_side::X87Rounding mode) {
	const double xx = which_side::MulPc24(dx, dx, mode);
	const double yy = which_side::MulPc24(dy, dy, mode);
	const double yyPlusXx = which_side::AddPc24(yy, xx, mode);
	const double zz = which_side::MulPc24(dz, dz, mode);
	return which_side::StoreSingle(which_side::AddPc24(yyPlusXx, zz, mode), mode);
}

// The path up to the compare, under the caller's rounding mode. squareRoot
// is 0x00982C30: it takes the float s in ST0 (FLD [ESP+0x28] at 0x007D2FA7,
// exact) and leaves its result in ST0, computed at round to nearest whatever
// the mode (see above); so it is a port that returns the register value, and
// the store at 0x007D2FB0 rounds it to float under the mode.
template <class SquareRoot>
DistanceTerms Distance(PointValues light, BoundValues bound, which_side::X87Rounding mode, SquareRoot&& squareRoot) {
	DistanceTerms terms{};
	terms.dx = DifferencePc24(light.x, bound.centre.x, mode);
	terms.dy = DifferencePc24(light.y, bound.centre.y, mode);
	terms.dz = DifferencePc24(light.z, bound.centre.z, mode);
	terms.sumOfSquares = SumOfSquaresPc24(terms.dx, terms.dy, terms.dz, mode);
	terms.distance = which_side::StoreSingle(squareRoot(terms.sumOfSquares), mode);
	// FLD q / FSUB radius / FSTP float at 0x007D2FB4..0x007D2FC1.
	terms.gap = which_side::StoreSingle(which_side::SubPc24(terms.distance, bound.radius, mode), mode);
	return terms;
}

// B8: FLD d; FLD range; FCOMPP; FNSTSW AX; TEST AH,0x41; JZ keeps the true
// (0x007D2FC5..0x007D2FD6). C0 (range < d) and C3 (equal) are both clear
// exactly when range > d with both ordered; unordered sets both (derived,
// C11). The C++ > is false on equality and on any NaN.
constexpr bool RangeExceeds(float range, float gap) {
	return range > gap;
}

} // namespace obng::engine::render::shadow_scene_light_is_in_range
