#pragma once

// The engine-facing shape of the shadow light's range walk: the ports the
// real integration will implement around 0x007D59E0 (proposed
// ShadowSceneLight::CollectGeometryInRange), as a template interface. The
// walk itself lives in ShadowLightRangeWalkLogic.h. No engine code is called
// from here yet; the constants below are what the ports will wrap.
//
// Evidence: re/functions/0x007D59E0.md (the walk, map only, held in the
// range-callers slice), re/functions/0x007D2EA0.md and
// ShadowSceneLight_IsInRange.h (the range test, verified (emulator)),
// knowledge/shadow-light-list-remove.md (AddGeometry). Marks as there:
// read / derived / unverified. Names are proposals; the address stays next
// to each.
//
// 64-bit clean: objects cross the ports as std::uint32_t handles
// (ShadowLightRangeWalkLogic.h ObjectHandle); nothing here depends on the
// pointer size. A 32-bit engine port may use the object's address as its
// handle; any other host needs a handle table.

#include "engine/render/ShadowLightRangeWalkLogic.h"

#include <concepts>
#include <cstdint>

namespace obng::engine::render::shadow_light_range_walk::engine {

// ---- The functions the real ports will wrap ------------------------------------------

// The walk being replaced: __thiscall(ShadowSceneLight* this; NiAVObject*
// object), RET 4, AL = 1 unless the gate is 0 (read, 0x007D59E0.md C1).
// Callers: itself and 0x007D6900 (read, meta.json). Two sources for the
// address: the function's own export and the callers' CALL targets
// (0x004073D0 claim 12 through 0x007D6900, read in that slice).
inline constexpr std::uint32_t kCollectGeometryInRangeAddress = 0x007D59E0;

// AddGeometry: __thiscall(ShadowSceneLight* this; geometry), called at
// 0x007D5AFC with ECX = this and one PUSH, no ADD ESP after (read, C10).
// Second code path for the address: 0x007C6100 calls it per in-range light
// (knowledge/culling-and-draw-setup.md, read in the range-callers slice).
// What it does (AddHead into +0xE4 under a critical section, ret 4) is
// unverified: knowledge/shadow-light-list-remove.md, not this slice's export.
// Whether it reads +0x1B0 is unverified; the port receives the live mask so
// it can write +0x1B0 first should it turn out to (see addGeometry below).
inline constexpr std::uint32_t kAddGeometryAddress = 0x007D5790;

// The switch node's active child: __thiscall(NiSwitchNode* this) -> NiAVObject*
// (CALL target read at 0x007D5A49 with ECX = the node). Body read
// (re/export/range-callers/0x007C59E0/asm.s): index = [+0xE0], null when
// index < 0 (JL 0x007C59E8) or index >= the u16 [+0xB6] (signed CMP/JGE
// 0x007C59F1/3), else [[+0xB0] + 4*index]; plain RET. One code path only (no
// other caller found in re/export/rea-all): the address does not meet the
// two-source rule yet; OBNG.dll's range-walk shadow calls it only as the port
// the original also calls.
inline constexpr std::uint32_t kSwitchGetActiveChildAddress = 0x007C59E0;

// "Has a non-zero bound radius": __thiscall(NiAVObject* this) -> AL (CALL read
// at 0x007D5A5A, ECX = the active child, AL tested at 0x007D5A5F). Body read
// (re/export/range-callers/0x0049FFC0/asm.s): FLDZ / FCOMP [ECX+0x2C] /
// FNSTSW / TEST AH,0x44 / JNP - EAX 0 when +0x2C compares equal to 0.0, else
// EAX 1; NaN sets C2 and returns 1, the same as the plain-node filter B11
// (derived from the condition codes). One code path only, as above.
inline constexpr std::uint32_t kHasNonZeroBoundRadiusAddress = 0x0049FFC0;

// The gate: dword compared with 0 at 0x007D59E1 (read). Meaning (shadow
// quality level) unverified; 0x004073D0 compares it with 3 (read there; OBVR
// GameAddresses.h:1061 agrees).
inline constexpr std::uint32_t kWalkGateAddress = 0x00B42F48;

// ---- The offsets the ports read (layout requests; read in 0x007D59E0.md) --------------

inline constexpr std::uint32_t kSourceLightOffset = 0x100;   // ShadowSceneLight: NiLight* (0x007D5A03)
inline constexpr std::uint32_t kPlaneSourceOffset = 0x14C;   // ShadowSceneLight: dword, compared with 0 (0x007D2ED3)
inline constexpr std::uint32_t kPlanesOffset = 0x150;        // ShadowSceneLight: NiPlane[6] (0x007D2F05)
inline constexpr std::uint32_t kActivePlanesOffset = 0x1B0;  // ShadowSceneLight: u32 mask (0x007D59F4)
inline constexpr std::uint32_t kFlagsOffset = 0x18;          // NiAVObject: byte, bit 0 app-culled (0x007D5AB4)
inline constexpr std::uint32_t kWorldBoundOffset = 0x20;     // NiAVObject: NiBound (0x007D2EDE)
inline constexpr std::uint32_t kBoundRadiusOffset = 0x2C;    // NiAVObject: float (0x007D5AAA)
inline constexpr std::uint32_t kChildrenDataOffset = 0xB0;   // NiNode: NiAVObject** (0x007D5A9B)
inline constexpr std::uint32_t kChildCountOffset = 0xB6;     // NiNode: u16 (0x007D5A8C, 0x007D5AC2)
inline constexpr std::uint32_t kGetRttiSlot = 0x04;          // vtable slot (0x007D5A2E)
inline constexpr std::uint32_t kGetAsNodeSlot = 0x08;        // vtable slot (0x007D5A1A); name unverified
inline constexpr std::uint32_t kGetAsGeometrySlot = 0x10;    // vtable slot (0x007D5AE5, 0x007D5AF2); name unverified

// ---- The ports -----------------------------------------------------------------------------

// The scene, as both walks see it. Each member stands for one engine
// operation of 0x007D59E0, called where and as often as the original calls it
// (ShadowLightRangeWalkLogic.h K4):
//   gate()                      [0x00B42F48], read once per (recursive) call
//   asNode(o)                   o->vt[+0x08](): the node or null
//   nodeRtti(n)                 n->vt[+0x04](), the address compared with 0x00B3FD70
//   activeChild(n)              0x007C59E0 on n (exact NiSwitchNode only)
//   hasNonZeroBoundRadius(o)    0x0049FFC0 on o (the switch's active child only)
//   childCount(n)               the u16 n+0xB6, re-read after every child
//   child(n, i)                 n+0xB0 [i], read per child
//   bound(o)                    o+0x20..+0x2C (centre, radius); the plain-node
//                               filter reads only .radius (+0x2C)
//   flags(o)                    the byte o+0x18
//   asGeometry(o)               o->vt[+0x10](), called twice for a geometry
//   addGeometry(g, liveMask)    this->AddGeometry(g) 0x007D5790; liveMask is
//                               what +0x1B0 holds at that call in the original
template <class S>
concept RangeWalkScene = requires(S& scene, ObjectHandle object, std::uint32_t index, std::uint32_t mask) {
	{ scene.gate() } -> std::convertible_to<std::uint32_t>;
	{ scene.asNode(object) } -> std::convertible_to<ObjectHandle>;
	{ scene.nodeRtti(object) } -> std::convertible_to<std::uint32_t>;
	{ scene.activeChild(object) } -> std::convertible_to<ObjectHandle>;
	{ scene.hasNonZeroBoundRadius(object) } -> std::convertible_to<bool>;
	{ scene.childCount(object) } -> std::convertible_to<std::uint16_t>;
	{ scene.child(object, index) } -> std::convertible_to<ObjectHandle>;
	{ scene.bound(object) } -> std::convertible_to<which_side::SphereValues>;
	{ scene.flags(object) } -> std::convertible_to<std::uint8_t>;
	{ scene.asGeometry(object) } -> std::convertible_to<ObjectHandle>;
	scene.addGeometry(object, mask);
};

// The light as ReferenceWalk sees it (the oracle):
//   readMask() / writeMask(m)   this+0x1B0
//   isInRange(o)                this->IsInRange(o, this+0x100), 0x007D2EA0 -
//                               in the tests its verified rebuild over views
template <class L>
concept RangeWalkReferenceLight = requires(L& light, ObjectHandle object, std::uint32_t mask) {
	{ light.readMask() } -> std::convertible_to<std::uint32_t>;
	light.writeMask(mask);
	{ light.isInRange(object) } -> std::convertible_to<bool>;
};

// The light as BatchedWalk sees it is LightValues (ShadowLightRangeWalkLogic.h),
// filled once per walk by the port: hasSource = this+0x100 != null; sourceRtti
// = source->vt[+0x04]() (only when hasSource); planeSource = the dword +0x14C;
// planes = +0x150; activePlanes = +0x1B0; sourcePosition = source+0x88;
// sourceRange = source+0xF8 (offsets read in 0x007D2EA0.md); rounding = bits
// 10-11 of the x87 control word at the walk's entry (FNSTCW; measured 0x0C7F,
// truncate). The square-root port is 0x00982C30's
// (ShadowSceneLight_IsInRangeLogic.h Distance).

} // namespace obng::engine::render::shadow_light_range_walk::engine
