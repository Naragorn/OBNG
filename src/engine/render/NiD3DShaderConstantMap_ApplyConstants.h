#pragma once

// The engine-facing shape of 0x009A8E30 (proposed
// NiD3DShaderConstantMap::ApplyConstants): the views of the bytes it touches,
// its calling convention, the callees' conventions, and the ports of
// NiD3DShaderConstantMap_ApplyConstantsLogic.h over real engine memory.
// 32-bit only; the decisions live in the logic header.
//
// Evidence: re/functions/0x009A8E30.md, re/export/view-reapply/0x009A8E30/asm.s.
// Names are proposals until the view-reapply slice's refutation round.
//
// ConstantReapply.h states the contract and the ports' concept names and
// defines no layout. The views below are local to this rebuild and are
// layout requests for a shared header, not definitions of the engine's
// classes: they cover the bytes this function touches, at the offsets the
// instructions use; what the engine keeps in between is unread here.

#include "engine/render/ConstantReapply.h"
#include "engine/render/NiD3DShaderConstantMap_ApplyConstantsLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::apply_constants::engine {

static_assert(sizeof(void*) == 4, "the views mirror a 32-bit binary");

inline constexpr std::uint32_t kApplyConstantsAddress = 0x009A8E30;
inline constexpr std::uint32_t kGetExtraDataAddress = 0x006FF9C0;  // CALL at 0x009A8E4D; xOBSE NiNodes.h:506
                                                                  // (re/symbols/xobse.csv) names it
                                                                  // NiObjectNET::GetExtraData
inline constexpr std::uint32_t kExtraDataNameSlotAddress = 0x00B29F84;  // MOV EAX,[0x00b29f84] 0x009A8E42; file
                                                                       // value 0x00A8A9BC -> "__NDL_SCMData"
                                                                       // (data.json)

// ---- The callees' conventions -------------------------------------------------

// 0x006FF9C0: thiscall, ECX = the object (MOV ECX,[ESP+0x18] 0x009A8E3B),
// one dword argument (the name pointer, PUSH EAX 0x009A8E47), callee-cleaned
// (RET 4, map C3; the body does not adjust ESP after the call). Result EAX =
// the extra data or 0 (map C3). The parameter types are carried as the
// pointers the call passes; xOBSE's declared types were not compared here.
using GetExtraDataFn = void*(__thiscall*)(void* object, const char* name);

// The map's slots, thiscall with ECX = the map (MOV ECX,ESI before each
// call), every argument one dword, callee-cleaned: the targets' `ret N`
// match the pushes (map, Purpose: +0x54 ret 0x2C, +0x58 ret 0xC, +0x5C
// ret 0x34, +0x60 ret 0xC, +0x64 ret 0x24, +0x6C ret 0x2C, read with
// objdump). The types are proposals, so the views carry them as
// std::uint32_t - the same ABI on this 32-bit target.
using MapSlot3Fn = std::uint32_t(__thiscall*)(void* map, std::uint32_t, std::uint32_t, std::uint32_t);
using MapSlot9Fn = std::uint32_t(__thiscall*)(void* map, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using MapSlot11Fn = std::uint32_t(__thiscall*)(void* map, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using MapSlot13Fn = std::uint32_t(__thiscall*)(void* map, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t, std::uint32_t);

struct ShaderConstantMapView;

// 0x009A8E30 itself: thiscall, ECX = the map (moved to ESI at 0x009A8E39),
// eleven dword stack arguments, callee-cleaned (RET 0x2C at 0x009A903A);
// EAX = 0 or 1 (0x009A902C..0x009A9033).
using ApplyConstantsFn = std::uint32_t(__thiscall*)(ShaderConstantMapView* self, std::uint32_t a1, std::uint32_t a2,
	std::uint32_t a3, std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8,
	std::uint32_t a9, std::uint32_t a10, std::uint32_t a11);

// Vtable of NiD3DShaderConstantMap 0x00AB2A4C and its subclasses
// NiD3DSCM_Vertex 0x00AB2BAC / NiD3DSCM_Pixel 0x00AB297C (map, Identity).
struct ShaderConstantMapVtableView {
	void* unread00[16];
	ApplyConstantsFn applyConstants;  // +0x40, 0x009A8E30 in all three (map C1)
	void* unread44[4];
	MapSlot11Fn defined;    // +0x54 (MOV EDX,[EDX+0x54] 0x009A8EB1), call 0x009A9008
	MapSlot3Fn constant;    // +0x58 (MOV EAX,[EDX+0x58] 0x009A8F38), call 0x009A8F3E
	MapSlot13Fn attribute;  // +0x5C (MOV EDX,[EDX+0x5C] 0x009A8F69), call 0x009A8F85
	MapSlot3Fn global;      // +0x60 (MOV EAX,[EDX+0x60] 0x009A8F98), call 0x009A8F9E
	MapSlot9Fn operator_;   // +0x64 (MOV EDX,[EDX+0x64] 0x009A8FB0), call 0x009A8FCE
	void* unread68;         // +0x68, never called from this body (map)
	MapSlot11Fn object;     // +0x6C (MOV EDX,[EDX+0x6C] 0x009A8FDC), call 0x009A9008
};
static_assert(offsetof(ShaderConstantMapVtableView, applyConstants) == 0x40, "ApplyConstants at +0x40");
static_assert(offsetof(ShaderConstantMapVtableView, defined) == kSlotDefined, "defined at +0x54");
static_assert(offsetof(ShaderConstantMapVtableView, constant) == kSlotConstant, "constant at +0x58");
static_assert(offsetof(ShaderConstantMapVtableView, attribute) == kSlotAttribute, "attribute at +0x5C");
static_assert(offsetof(ShaderConstantMapVtableView, global) == kSlotGlobal, "global at +0x60");
static_assert(offsetof(ShaderConstantMapVtableView, operator_) == kSlotOperator, "operator at +0x64");
static_assert(offsetof(ShaderConstantMapVtableView, object) == kSlotObject, "object at +0x6C");
static_assert(sizeof(ShaderConstantMapVtableView) == 0x70, "the view ends after slot +0x6C");

// IDirect3DDevice9 methods 94 and 109 (Windows SDK d3d9.h, counting from
// QueryInterface = 0; map, Struct accesses): SetVertexShaderConstantF(UINT
// StartRegister, CONST float* pConstantData, UINT Vector4fCount) and
// SetPixelShaderConstantF with the same parameters; COM stdcall, the device
// pushed last (PUSH ECX 0x009A8EED / 0x009A8F23). The HRESULT is tested for
// < 0 only (SETL, 0x009A8EF4 / 0x009A8F2A).
using SetShaderConstantFFn = std::int32_t(__stdcall*)(void* device, std::uint32_t startRegister, const float* data,
	std::uint32_t vector4fCount);

struct DeviceVtableView {
	void* unread000[94];
	SetShaderConstantFFn setVertexShaderConstantF;  // +0x178 (MOV EAX,[EAX+0x178] 0x009A8EE1)
	void* unread17C[14];
	SetShaderConstantFFn setPixelShaderConstantF;   // +0x1B4 (MOV EAX,[EAX+0x1B4] 0x009A8F17)
};
static_assert(offsetof(DeviceVtableView, setVertexShaderConstantF) == 0x178, "SetVertexShaderConstantF at +0x178");
static_assert(offsetof(DeviceVtableView, setPixelShaderConstantF) == 0x1B4, "SetPixelShaderConstantF at +0x1B4");
static_assert(sizeof(DeviceVtableView) == 0x1B8, "the view ends after SetPixelShaderConstantF");

struct DeviceView {
	const DeviceVtableView* vtable;  // +0x00 (MOV EAX,[ECX] 0x009A8EDF / 0x009A8F15)
};

// One entry of the map (class name unverified; map, Layout requests).
struct ShaderConstantEntryView {
	std::uint8_t unread00[0x08];
	std::uint8_t live;            // +0x08 (CMP byte [EAX+0x8],0 0x009A8E7E)
	std::uint8_t unread09[0x07];
	std::uint32_t id;             // +0x10, the predefined id; read by the view filter only (ConstantReapply.h)
	std::uint32_t typeFlags;      // +0x14 (0x009A8E9E)
	std::uint32_t target;         // +0x18 (0x009A8EC5), 1 vertex / 2 pixel for type 0x10000000
	std::uint32_t startRegister;  // +0x1C (0x009A8E88), 0xFFFFFFFF = unassigned (unverified meaning)
	std::uint32_t registerCount;  // +0x20 (0x009A8ECD / 0x009A8F03)
	std::uint32_t gate;           // +0x24 (0x009A8E94), meaning unverified
	std::uint8_t unread28[0x08];
	const float* data;            // +0x30 (0x009A8ED0 / 0x009A8F06)
};
static_assert(offsetof(ShaderConstantEntryView, live) == 0x08, "live at +0x08");
static_assert(offsetof(ShaderConstantEntryView, id) == 0x10, "id at +0x10");
static_assert(offsetof(ShaderConstantEntryView, typeFlags) == 0x14, "type flags at +0x14");
static_assert(offsetof(ShaderConstantEntryView, target) == 0x18, "target at +0x18");
static_assert(offsetof(ShaderConstantEntryView, startRegister) == 0x1C, "start register at +0x1C");
static_assert(offsetof(ShaderConstantEntryView, registerCount) == 0x20, "register count at +0x20");
static_assert(offsetof(ShaderConstantEntryView, gate) == 0x24, "gate at +0x24");
static_assert(offsetof(ShaderConstantEntryView, data) == 0x30, "data at +0x30");
static_assert(sizeof(ShaderConstantEntryView) == 0x34, "the view ends after the data pointer");

// this.
struct ShaderConstantMapView {
	const ShaderConstantMapVtableView* vtable;  // +0x00 (MOV EDX,[ESI] e.g. 0x009A8EAF)
	std::uint8_t unread04[0x0C];
	ShaderConstantEntryView* const* entries;    // +0x10 (MOV ECX,[ESI+0x10] 0x009A8E70), stride 4
	std::uint16_t unread14;
	std::uint16_t entryCount;                   // +0x16 (CMP word [ESI+0x16],AX 0x009A8E58; MOVZX 0x009A9017)
	std::uint8_t unread18[0x08];
	std::uint32_t lastProgram;                  // +0x20 (MOV [ESI+0x20],EBP 0x009A8E3F)
	std::uint8_t unread24[0x04];
	DeviceView* device;                         // +0x28 (MOV ECX,[ESI+0x28] 0x009A8ED3 / 0x009A8F09)
};
static_assert(offsetof(ShaderConstantMapView, entries) == 0x10, "entries at +0x10");
static_assert(offsetof(ShaderConstantMapView, entryCount) == 0x16, "entry count at +0x16");
static_assert(offsetof(ShaderConstantMapView, lastProgram) == 0x20, "last program at +0x20");
static_assert(offsetof(ShaderConstantMapView, device) == 0x28, "device at +0x28");
static_assert(sizeof(ShaderConstantMapView) == 0x2C, "the view ends after the device");

// ---- The ports over engine memory ------------------------------------------------

inline Handle ToHandle(const void* pointer) {
	return static_cast<Handle>(reinterpret_cast<std::uintptr_t>(pointer));
}

template <class T>
T* FromHandle(Handle handle) {
	return reinterpret_cast<T*>(static_cast<std::uintptr_t>(handle));
}

// memory, extraData, slots and device of the logic's Apply. Every read happens
// when the logic asks for it, as the original's loads do. getExtraData is
// 0x006FF9C0 in the game; nameSlot is the global 0x00B29F84, read at each
// lookup as the original reads it at each call (0x009A8E42).
struct EnginePorts {
	GetExtraDataFn getExtraData;
	const char* const* nameSlot;

	static ShaderConstantMapView* MapAt(Handle map) {
		return FromHandle<ShaderConstantMapView>(map);
	}
	static const ShaderConstantEntryView* EntryAt(Handle entry) {
		return FromHandle<const ShaderConstantEntryView>(entry);
	}

	void StoreLastProgram(Handle map, Handle program) const {
		MapAt(map)->lastProgram = program;
	}
	std::uint16_t EntryCount(Handle map) const {
		return MapAt(map)->entryCount;
	}
	Handle Entry(Handle map, std::uint32_t index) const {
		return ToHandle(MapAt(map)->entries[index]);
	}
	std::uint8_t EntryLive(Handle entry) const {
		return EntryAt(entry)->live;
	}
	std::uint32_t EntryRegister(Handle entry) const {
		return EntryAt(entry)->startRegister;
	}
	std::uint32_t EntryGate(Handle entry) const {
		return EntryAt(entry)->gate;
	}
	std::uint32_t EntryTypeFlags(Handle entry) const {
		return EntryAt(entry)->typeFlags;
	}
	std::uint32_t EntryTarget(Handle entry) const {
		return EntryAt(entry)->target;
	}
	std::uint32_t EntryRegisterCount(Handle entry) const {
		return EntryAt(entry)->registerCount;
	}
	Handle EntryData(Handle entry) const {
		return ToHandle(EntryAt(entry)->data);
	}
	std::uint32_t EntryId(Handle entry) const {
		return EntryAt(entry)->id;
	}
	Handle Device(Handle map) const {
		return ToHandle(MapAt(map)->device);
	}

	Handle Lookup(Handle geometry) const {
		return ToHandle(getExtraData(FromHandle<void>(geometry), *nameSlot));
	}

	std::uint32_t Call(Handle mapHandle, const SlotCall& call) const {
		ShaderConstantMapView* map = MapAt(mapHandle);
		const ShaderConstantMapVtableView& vt = *map->vtable;
		const Handle* a = call.arguments;
		switch (call.slotOffset) {
		case kSlotDefined: return vt.defined(map, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
		case kSlotConstant: return vt.constant(map, a[0], a[1], a[2]);
		case kSlotAttribute:
			return vt.attribute(map, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
		case kSlotGlobal: return vt.global(map, a[0], a[1], a[2]);
		case kSlotOperator: return vt.operator_(map, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
		case kSlotObject: return vt.object(map, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
		default: return 1;  // unreachable: the logic issues the six slots above only
		}
	}

	std::int32_t SetVertexShaderConstantF(Handle deviceHandle, std::uint32_t start, Handle data, std::uint32_t count) const {
		auto* device = FromHandle<DeviceView>(deviceHandle);
		return device->vtable->setVertexShaderConstantF(device, start, FromHandle<const float>(data), count);
	}
	std::int32_t SetPixelShaderConstantF(Handle deviceHandle, std::uint32_t start, Handle data, std::uint32_t count) const {
		auto* device = FromHandle<DeviceView>(deviceHandle);
		return device->vtable->setPixelShaderConstantF(device, start, FromHandle<const float>(data), count);
	}
};

// The rebuild with the original's signature, minus the calling convention:
// the plugin's thiscall detour forwards here. In the game getExtraData is
// FromHandle<...>(kGetExtraDataAddress) and nameSlot
// FromHandle<const char* const>(kExtraDataNameSlotAddress).
inline std::uint32_t ApplyConstants(ShaderConstantMapView* self, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
	std::uint32_t a4, std::uint32_t a5, std::uint32_t a6, std::uint32_t a7, std::uint32_t a8, std::uint32_t a9,
	std::uint32_t a10, std::uint32_t a11, EntryFilter filter, GetExtraDataFn getExtraData,
	const char* const* nameSlot) {
	EnginePorts ports{getExtraData, nameSlot};
	const ApplyInput in{a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
	return Apply(ToHandle(self), in, filter, ports, ports, ports, ports);
}

} // namespace obng::engine::render::apply_constants::engine
