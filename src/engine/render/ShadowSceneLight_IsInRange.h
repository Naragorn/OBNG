#pragma once

// The engine-facing shape of 0x007D2EA0 (proposed ShadowSceneLight::IsInRange):
// the layouts it reads and writes, its calling convention and the rebuild over
// them. The decisions live in ShadowSceneLight_IsInRangeLogic.h.
//
// Evidence: re/functions/0x007D2EA0.md, re/export/draw-setup/0x007D2EA0/asm.s.
// Names are proposals until the draw-setup slice's refutation round.
//
// No shared header defines these classes yet. The point, bound and plane
// layouts are the ones the WhichSide rebuild already declares
// (NiBound_WhichSide.h), reused, not redefined; the three views below are
// local to this rebuild and are layout requests for a shared header, not
// definitions of the engine's classes. They cover the bytes this function
// touches, with the offsets the instructions use; what else the engine keeps
// in between is unread here.

#include "engine/render/NiBound_WhichSide.h"
#include "engine/render/ShadowSceneLight_IsInRangeLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::shadow_scene_light_is_in_range::engine {

using which_side::engine::NiBound;
using which_side::engine::NiPlane;
using which_side::engine::NiPoint3;

static_assert(sizeof(void*) == 4, "the views mirror a 32-bit binary");

inline constexpr std::uint32_t kIsInRangeAddress = 0x007D2EA0;

struct NiLightView;

// Virtual slot +0x04 of the source light: called with ECX = the light and no
// stack argument (MOV EAX,[ESI] / MOV EDX,[EAX+0x4] / MOV ECX,ESI / CALL EDX
// at 0x007D2EB9..0x007D2EC0; read); its EAX is compared with an NiRTTI
// address. GetRTTI is the name derived from that compare and from 0x007D59E0
// comparing the same slot with NiRTTI_NiSwitchNode (C2; the slot name is
// unverified).
using GetRttiFn = const void*(__thiscall*)(const NiLightView* self);

struct NiObjectVtableView {
	const void* slot00;
	GetRttiFn getRtti;  // +0x04 (MOV EDX,[EAX+0x4] 0x007D2EBB)
};
static_assert(offsetof(NiObjectVtableView, getRtti) == 0x04, "GetRTTI at slot +0x04");

// arg2, the source light (0x007D59E0 passes this+0x100, read at 0x007D5A03).
struct NiLightView {
	const NiObjectVtableView* vtable;  // +0x00 (MOV EAX,[ESI] 0x007D2EB9)
	std::uint8_t unread04[0x84];
	NiPoint3 worldTranslate;           // +0x88..+0x90 (FLD [ESI+0x88/0x8C/0x90] 0x007D2F5D/6B/79);
	                                   // OBVR GameAddresses.h kNodeWorldTranslateOffset = 0x88 agrees
	std::uint8_t unread94[0x64];
	float range;                       // +0xF8 (FLD [ESI+0xF8] 0x007D2FC9); "range" unverified
};
static_assert(offsetof(NiLightView, vtable) == 0x00, "vtable at +0x00");
static_assert(offsetof(NiLightView, worldTranslate) == 0x88, "world translate at +0x88");
static_assert(offsetof(NiLightView, range) == 0xF8, "range at +0xF8");
static_assert(sizeof(NiLightView) == 0xFC, "the view ends after the range");

// arg1, the object whose world bound is tested (geometry or node).
struct NiAVObjectView {
	std::uint8_t unread00[0x20];
	NiBound worldBound;  // +0x20..+0x2C (MOV [EAX+0x20/0x24/0x28/0x2C] 0x007D2EDE..0x007D2EEF)
};
static_assert(offsetof(NiAVObjectView, worldBound) == 0x20, "world bound at +0x20");
static_assert(sizeof(NiAVObjectView) == 0x30, "the view ends after the radius");

// this, the ShadowSceneLight (vtable 0x00A912B4 per
// knowledge/shadow-light-list-remove.md, not read here).
struct ShadowSceneLightView {
	std::uint8_t unread000[0x14C];
	const void* planeSource;        // +0x14C (CMP dword [EBX+0x14C],0 0x007D2ED3); type unknown
	NiPlane planes[6];              // +0x150 + 16*i (LEA EBP,[EBX+0x150] 0x007D2F05, ADD EBP,0x10 0x007D2F47)
	std::uint32_t activePlanes;     // +0x1B0 (TEST 0x007D2F19, AND 0x007D2F3E); constructor value 0x3F
};
static_assert(offsetof(ShadowSceneLightView, planeSource) == 0x14C, "plane source at +0x14C");
static_assert(offsetof(ShadowSceneLightView, planes) == 0x150, "planes at +0x150");
static_assert(offsetof(ShadowSceneLightView, activePlanes) == 0x1B0, "mask at +0x1B0");
static_assert(offsetof(ShadowSceneLightView, planes) + kPlaneCount * sizeof(NiPlane) ==
		offsetof(ShadowSceneLightView, activePlanes),
	"the six planes end where the mask begins");
static_assert(sizeof(ShadowSceneLightView) == 0x1B4, "the view ends after the mask");

// ---- Calling conventions --------------------------------------------------------

// 0x007D2EA0 itself: thiscall, ECX = this (moved to EBX at 0x007D2EAB), two
// stack arguments, callee cleans (RET 0x8 at 0x007D2EB6, 0x007D2ED0,
// 0x007D2F5A, 0x007D2FE6). The result is AL; the upper 24 bits of EAX are
// left over (the RTTI pointer, WhichSide's return, arg1, the FNSTSW word) and
// both callers test AL only (0x007C61C7 MOV BL,AL; 0x007D5A10 TEST AL,AL) -
// read, so a bool is the whole contract.
using IsInRangeFn = bool(__thiscall*)(ShadowSceneLightView* self, const NiAVObjectView* object,
	const NiLightView* source);

// 0x00982C30, the square root (C14): argument and result in ST0, no stack
// argument, so it has no C++ function type; the rebuild calls a port
// double(float) in its place (see Distance in the logic header).

// The plane port as the original calls it: WhichSide(bound copy, plane).
struct WhichSidePort {
	std::int32_t operator()(const NiBound& bound, const NiPlane& plane) const {
		return which_side::engine::WhichSide(bound, plane);
	}
};

// ---- The rebuild --------------------------------------------------------------

// testPlane(bound, plane) -> int32 stands for CALL 0x0047DA70 (WhichSidePort
// for the WhichSide rebuild); squareRoot(float) -> double for CALL 0x00982C30.
// object may be anything on the B1 and B2 paths: it is read only after them.
template <class TestPlane, class SquareRoot>
bool IsInRange(ShadowSceneLightView& self, const NiAVObjectView* object, const NiLightView* source,
	TestPlane&& testPlane, SquareRoot&& squareRoot) {
	if (source == nullptr) {  // B1: TEST ESI,ESI / JNZ 0x007D2EA9..0x007D2EAD
		return false;
	}
	const auto rtti = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source->vtable->getRtti(source)));
	if (IsDirectional(rtti)) {  // B2
		return true;
	}
	// The bound is copied before the branch (0x007D2EDE..0x007D2EFB, C3), and
	// the copy is what WhichSide receives (LEA ECX,[ESP+0x20] 0x007D2F22).
	const NiBound bound = object->worldBound;
	if (UsesPlanePath(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(self.planeSource)))) {  // B3
		return TestPlanes([&self] { return self.activePlanes; },
			[&self](std::uint32_t mask) { self.activePlanes = mask; },
			[&](unsigned plane) { return static_cast<std::int32_t>(testPlane(bound, self.planes[plane])); });
	}
	const BoundValues values{{bound.center.x, bound.center.y, bound.center.z}, bound.radius};
	const PointValues light{source->worldTranslate.x, source->worldTranslate.y, source->worldTranslate.z};
	return RangeExceeds(source->range, Distance(light, values, squareRoot).gap);  // B8
}

} // namespace obng::engine::render::shadow_scene_light_is_in_range::engine
