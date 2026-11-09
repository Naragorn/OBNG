#pragma once
// The engine-facing shape of the scene update (SceneUpdateLogic.h): the
// tree and action ports over NiAVObject memory in a 32-bit process, and the
// addresses and offsets they rest on. Used by the plugin's detour on
// NiAVObject::Update 0x00707370 and by the oracle test, which runs the
// original's bytes on the same memory.
//
// Evidence (read 2026-11-09 in the exe's instructions, re/functions/
// 0x0070A0D0.md): the offsets from 0x0070A0D0, 0x00707580, 0x0070C120,
// 0x0047C930 and 0x0053D7A0; the vtable slots from NiNode's vtable
// 0x00A7E38C (OBVR GameAddresses.h:1274 agrees on the vtable) and the
// .rdata scan of every vtable holding the two UpdateDownwardPass bodies.
// Second sources for the layout: OBVR GameTypes.h (parent 0x1C, local 0x30,
// world 0x64, flags 0x18), xOBSE's NiNode (children at 0xB0, the NiTArray's
// first free at 0xB6). Names are Gamebryo's for NiMain, proposals otherwise.
//
// This header is the 32-bit shape: handles are the objects' addresses. The
// logic it drives (SceneUpdateLogic.h) has no such dependency.
#include "engine/scene/SceneUpdateLogic.h"

#include <xmmintrin.h>

#include <cstdint>
#include <cstring>

namespace obng::engine::scene::scene_update::engine {

static_assert(sizeof(void*) == 4, "the engine ports hold for a 32-bit process; another host needs a handle table");

// ---- Addresses -------------------------------------------------------------------------------
inline constexpr std::uint32_t kNiAVObjectUpdateAddress = 0x00707370;      // Update(float time, bool controllers): +0x60 then the parent's +0x94 (read)
inline constexpr std::uint32_t kUpdateObjectControllersAddress = 0x0047C930;  // __thiscall(float time, bool properties), RET 8 (read)
inline constexpr std::uint32_t kBoundMergeAddress = 0x0072A6B0;             // NiBound::Merge __thiscall(const NiBound* other), RET 4 (read)
inline constexpr std::uint32_t kNiNodeVtable = 0x00A7E38C;                  // (read: .rdata; OBVR GameAddresses.h:1274)

// ---- Vtable slots (read: 0x00707370, 0x0070A0D0, 0x00707580, 0x0070C120, 0x00709EC0) -------------
inline constexpr std::uint32_t kSlotCollisionUpdateWorldData = 0x50;  // on the collision object at +0xA8
inline constexpr std::uint32_t kSlotUpdateDownwardPass = 0x60;
inline constexpr std::uint32_t kSlotUpdateWorldData = 0x74;
inline constexpr std::uint32_t kSlotUpdateWorldBound = 0x78;
inline constexpr std::uint32_t kSlotUpdateUpwardPass = 0x94;

// ---- Offsets -----------------------------------------------------------------------------------
inline constexpr std::uint32_t kControllerOffset = 0x0C;        // NiObjectNET's controller chain (read 0x0047C96A)
inline constexpr std::uint32_t kParentOffset = 0x1C;            // (read 0x0070C126, 0x00707389)
inline constexpr std::uint32_t kWorldBoundOffset = 0x20;        // NiBound (read 0x0070A145..5D)
inline constexpr std::uint32_t kBoundRadiusOffset = 0x2C;       // (read 0x0070A0F9)
inline constexpr std::uint32_t kLocalTransformOffset = 0x30;    // (read 0x0070C12F)
inline constexpr std::uint32_t kWorldTransformOffset = 0x64;    // (read 0x0070C147)
inline constexpr std::uint32_t kPropertyListOffset = 0x9C;      // NiTListBase head (read 0x0047C93B)
inline constexpr std::uint32_t kCollisionObjectOffset = 0xA8;   // (read 0x0070C151)
inline constexpr std::uint32_t kChildrenOffset = 0xB0;          // NiNode's NiTArray data (read 0x0070A110)
inline constexpr std::uint32_t kChildSlotsOffset = 0xB6;        // its first free entry, u16 (read 0x0070A0FC)
inline constexpr std::uint32_t kFlagsOffset = 0x18;             // u16 (read 0x00722AFA: bit 7 the bound-unchanged flag)
inline constexpr std::uint32_t kModelDataOffset = 0xB4;         // NiGeometry's data (read 0x00722AE5); its bound at +0x0C
inline constexpr std::uint32_t kModelBoundOffset = 0x0C;
inline constexpr std::uint32_t kSkinInstanceOffset = 0xB8;      // NiGeometry's skin instance (read 0x00722AB7)
inline constexpr std::uint32_t kSkinDataOffset = 0x08;          // in the instance (read 0x0072BB3F)
inline constexpr std::uint32_t kSkinRootParentOffset = 0x10;    // (read 0x0072BB9C)
inline constexpr std::uint32_t kSkinBonesOffset = 0x14;         // the bone node pointers (read 0x0072BB3C)
inline constexpr std::uint32_t kSkinToRootOffset = 0x0C;        // in the skin data (read 0x0072BBBC)
inline constexpr std::uint32_t kSkinBoneCountOffset = 0x40;     // (read 0x0072BB5B)
inline constexpr std::uint32_t kSkinBoneDataOffset = 0x44;      // a pointer to the per-bone records (read 0x0072BB44)
inline constexpr std::uint32_t kSkinBoneRecordSize = 0x4C;      // (read 0x0072BB95)
inline constexpr std::uint32_t kSkinBoneBoundOffset = 0x34;     // in the record (read 0x0072BB4B)

// ---- The calls ----------------------------------------------------------------------------------
using UpdateDownwardFn = void(__thiscall*)(void* self, float time, std::uint8_t controllers);
using UpdateObjectControllersFn = void(__thiscall*)(void* self, float time, std::uint8_t properties);
using NoArgFn = void(__thiscall*)(void* self);
using BoundMergeFn = void(__thiscall*)(void* self, const void* other);

inline void* Pointer(Handle h) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(h)); }
inline std::uint32_t Dword(Handle h, std::uint32_t offset) {
	std::uint32_t v;
	std::memcpy(&v, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + offset)), 4);
	return v;
}
inline std::uint16_t Word(Handle h, std::uint32_t offset) {
	std::uint16_t v;
	std::memcpy(&v, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + offset)), 2);
	return v;
}
inline std::uint32_t Slot(Handle object, std::uint32_t slot) { return Dword(Dword(object, 0), slot); }

// The tree port: kinds from the vtable, children from the NiTArray.
struct Tree {
	Kind KindOf(Handle h) const {
		return Classify({Slot(h, kSlotUpdateDownwardPass), Slot(h, kSlotUpdateWorldData), Slot(h, kSlotUpdateWorldBound)});
	}
	std::uint32_t SlotCount(Handle h) const { return Word(h, kChildSlotsOffset); }
	Handle ChildAt(Handle h, std::uint32_t i) const { return Dword(Dword(h, kChildrenOffset), 4 * i); }
};

inline Transform TransformAt(Handle h, std::uint32_t offset) {
	Transform t;
	std::memcpy(&t, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + offset)), sizeof(t));
	return t;
}
inline Bound BoundAt(Handle h, std::uint32_t offset) {
	Bound b;
	std::memcpy(&b, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + offset)), sizeof(b));
	return b;
}

// A skin instance as SkinBound reads it (SceneUpdateLogic.h SkinPort).
struct SkinView {
	Handle instance;
	Handle Data() const { return Dword(instance, kSkinDataOffset); }
	std::uint32_t BoneCount() const { return Dword(Data(), kSkinBoneCountOffset); }
	Bound BoneBound(std::uint32_t i) const { return BoundAt(Dword(Data(), kSkinBoneDataOffset) + i * kSkinBoneRecordSize, kSkinBoneBoundOffset); }
	Transform BoneWorld(std::uint32_t i) const { return TransformAt(Dword(Dword(instance, kSkinBonesOffset), 4 * i), kWorldTransformOffset); }
	Transform RootParentWorld() const { return TransformAt(Dword(instance, kSkinRootParentOffset), kWorldTransformOffset); }
	Transform SkinToRoot() const { return TransformAt(Data(), kSkinToRootOffset); }
};

// A geometry as GeometryBound reads it (SceneUpdateLogic.h GeometryPort).
struct GeometryView {
	Handle object;
	bool HasSkin() const { return Dword(object, kSkinInstanceOffset) != 0; }
	Bound ModelBound() const { return BoundAt(Dword(object, kModelDataOffset), kModelBoundOffset); }
	Transform World() const { return TransformAt(object, kWorldTransformOffset); }
	Bound WorldBound() const { return BoundAt(object, kWorldBoundOffset); }
	std::uint16_t Flags() const { return Word(object, kFlagsOffset); }
};

// The action port: every action on the object's memory or through the
// engine's own function, exactly where the original does it.
struct Port {
	bool HasControllers(Handle h) const { return Dword(h, kPropertyListOffset) != 0 || Dword(h, kControllerOffset) != 0; }
	void UpdateObjectControllers(Handle h, float time) {
		reinterpret_cast<UpdateObjectControllersFn>(kUpdateObjectControllersAddress)(Pointer(h), time, 1);
	}
	const Transform* ParentWorld(Handle h) const {
		const Handle parent = Dword(h, kParentOffset);
		return parent == 0 ? nullptr : reinterpret_cast<const Transform*>(static_cast<std::uintptr_t>(parent + kWorldTransformOffset));
	}
	Transform Local(Handle h) const {
		Transform t;
		std::memcpy(&t, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + kLocalTransformOffset)), sizeof(t));
		return t;
	}
	void WriteWorld(Handle h, const Transform& t) { std::memcpy(Pointer(h + kWorldTransformOffset), &t, sizeof(t)); }
	void AfterWorldData(Handle h) {
		const Handle collision = Dword(h, kCollisionObjectOffset);
		if (collision != 0) {
			reinterpret_cast<NoArgFn>(Slot(collision, kSlotCollisionUpdateWorldData))(Pointer(collision));
		}
	}
	void UpdateWorldBound(Handle h) { reinterpret_cast<NoArgFn>(Slot(h, kSlotUpdateWorldBound))(Pointer(h)); }
	void UpdateDownwardOriginal(Handle h, float time, bool controllers) {
		reinterpret_cast<UpdateDownwardFn>(Slot(h, kSlotUpdateDownwardPass))(Pointer(h), time, controllers ? 1 : 0);
	}
	float BoundRadius(Handle h) const {
		float r;
		std::memcpy(&r, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + kBoundRadiusOffset)), 4);
		return r;
	}
	void SetBoundRadius(Handle h, float r) { std::memcpy(Pointer(h + kBoundRadiusOffset), &r, 4); }
	void CopyBound(Handle to, Handle from) { std::memcpy(Pointer(to + kWorldBoundOffset), Pointer(from + kWorldBoundOffset), sizeof(Bound)); }
	void MergeBound(Handle to, Handle from) {
		reinterpret_cast<BoundMergeFn>(kBoundMergeAddress)(Pointer(to + kWorldBoundOffset), Pointer(from + kWorldBoundOffset));
	}
	Bound ReadBound(Handle h) const { return BoundAt(h, kWorldBoundOffset); }
	void WriteBound(Handle h, const Bound& b) { std::memcpy(Pointer(h + kWorldBoundOffset), &b, sizeof(b)); }
	void GeometryBoundSelf(Handle h) {
		const GeometryView geometry{h};
		const SkinView skin{Dword(h, kSkinInstanceOffset)};
		Bound bound;
		std::uint16_t flags;
		GeometryBound(geometry, skin, bound, flags);
		WriteBound(h, bound);
		std::memcpy(Pointer(h + kFlagsOffset), &flags, 2);
	}
};

// The rounding the original computes under is the x87 control word's (the
// game runs 0x007F, round to nearest, and 0x0C7F, round toward zero, in
// different phases: knowledge/frame-clock.md, measured), and Run computes in
// SSE under the MXCSR's. For the same bits the two must agree: this scope
// copies the x87 rounding control (bits 10-11) into the MXCSR's (bits 13-14,
// the same encoding) for the run and puts the MXCSR back afterwards. The
// precision control needs no copy: at 24 bits the x87 rounds every product
// and sum to a float, which is what SSE single arithmetic does.
struct RoundingFromX87 {
	std::uint32_t saved;
	RoundingFromX87() {
		std::uint16_t word;
#if defined(_MSC_VER) && !defined(__clang__)
		__asm fnstcw word
#else
		asm volatile("fnstcw %0" : "=m"(word));
#endif
		saved = _mm_getcsr();
		_mm_setcsr((saved & ~0x6000u) | (static_cast<std::uint32_t>((word >> 10) & 3u) << 13));
	}
	~RoundingFromX87() { _mm_setcsr(saved); }
	RoundingFromX87(const RoundingFromX87&) = delete;
	RoundingFromX87& operator=(const RoundingFromX87&) = delete;
};

// The parent's upward pass after the subtree (read 0x00707389..9A): its
// virtual +0x94 (NiNode::UpdateUpwardPass 0x00709EC0: its own bound, then
// its parent's +0x94).
inline void UpdateUpwardFrom(Handle object) {
	const Handle parent = Dword(object, kParentOffset);
	if (parent != 0) {
		reinterpret_cast<NoArgFn>(Slot(parent, kSlotUpdateUpwardPass))(Pointer(parent));
	}
}

// The whole replacement of NiAVObject::Update's body for a subtree whose
// order is current: the flat run under the engine's rounding, then the
// parent's upward pass.
inline void UpdateSubtree(const Order& order, float time, bool controllers) {
	Port port;
	{
		RoundingFromX87 rounding;
		Run(order, port, time, controllers);
	}
	UpdateUpwardFrom(order.root);
}

}  // namespace obng::engine::scene::scene_update::engine
