#pragma once

// The decisions of 0x009A8E30 (proposed NiD3DShaderConstantMap::ApplyConstants)
// over plain values and ports, with the entry filter of ConstantReapply.h.
// 64-bit clean: every engine object is a std::uint32_t handle, every engine
// read and call goes through a port. The engine shapes behind the ports live
// in NiD3DShaderConstantMap_ApplyConstants.h.
//
// Evidence: re/functions/0x009A8E30.md (the map, held in slice
// constant-maps), re/export/view-reapply/0x009A8E30/asm.s (the instructions).
// Names are proposals; the address stays next to each. Marks: read = seen in
// the instructions (address given), derived = follows from read facts (which
// said), unverified = not checked.
//
// The original, in instruction order (read, asm.s):
//      [this+0x20] = a1 (0x009A8E3F)
//      extraData = 0x006FF9C0(ECX = a2, [0x00B29F84]) (0x009A8E4D), unconditional
//   B1  0x009A8E60  count [this+0x16] == 0 (JBE)     -> return 0, no loop
//      per index i, entry = [[this+0x10] + 4*i]:
//   B2  0x009A8E78  entry == 0                       -> next index
//   B3  0x009A8E82  byte [entry+0x08] == 0           -> the walk ends
//   B4  0x009A8E92  [entry+0x1C] != -1               -> dispatch
//   B5  0x009A8E98  [entry+0x1C] == -1, [entry+0x24] == 0 -> next index
//      type = [entry+0x14] & 0xF0000000:
//   B6  0x009A8EAD  0x20000000 -> slot +0x54 (a1, entry, a2..a10)
//   B7  0x009A8EBF  0x10000000 -> the constant path:
//   B8  0x009A8ECB    [entry+0x18] == 1 -> device +0x178 (SetVertexShaderConstantF)
//   B9  0x009A8F01    [entry+0x18] == 2 -> device +0x1B4 (SetPixelShaderConstantF),
//                     else slot +0x58 (a1, entry, a10)
//   B10 0x009A8F4B  0x30000000 -> slot +0x5C (a1, entry, a2..a11, extraData)
//   B11 0x009A8F92  0x40000000 -> slot +0x60 (a1, entry, a10)
//   B12 0x009A8FA8  0x50000000 -> slot +0x64 (a1, entry, a2, a3, a6..a10)
//   B13 0x009A8FD8  0x60000000 -> slot +0x6C (a1, entry, a2..a10); any other
//                   type clears the flag without a call
//   B14 0x009A900C  status != 0 -> the flag clears; the walk goes on
//   B15 0x009A9024  i + 1 < zero-extended count (JC)  -> next index
//      return flag cleared ? 1 : 0 (XOR EAX,EAX; CMP; SETZ AL 0x009A902C..0x009A9033)
//
// The one deviation, by contract (ConstantReapply.h): an EntryFilter. With
// kWhole every step above runs as the original's. With kView the extra-data
// lookup is not made, and after the original's null / liveness / register
// tests an entry is dispatched only when constant_reapply::Passes holds
// (type 0x20000000 with a view id [entry+0x10]); any other entry is skipped
// without a call and without touching the result flag. The program store,
// the walk's end at a dead entry and the count bound stay the original's.
//
// The stack-slot reuse of the original (the flag in the a1 slot, the entry's
// register and data in the a10 / a9 slots, map "Calling convention") is not
// part of the contract: every slot call takes a9 from EBX and a10 from EDI,
// loaded before the loop (0x009A8E67, 0x009A8E6C, read), so the scratch never
// reaches a callee; the emulator gate confirms it (the traced arguments).
//
// No x87 instruction is in the body (read: asm.s holds none), so no rounding
// is modelled here; every value is a 32-bit integer or handle.

#include "engine/render/ConstantReapply.h"

#include <cstdint>

namespace obng::engine::render::apply_constants {

using Handle = std::uint32_t;  // an engine address, never dereferenced here
using constant_reapply::EntryFilter;

// ---- Inputs -----------------------------------------------------------------

// The eleven stack arguments (ESP0+0x04..+0x2C, read; RET 0x2C at
// 0x009A903A). The roles are proposals (map, parameter table).
struct ApplyInput {
	Handle program = 0;        // a1, stored at [this+0x20], first argument of every slot (EBP)
	Handle geometry = 0;       // a2, ECX of 0x006FF9C0 (0x009A8E3B)
	Handle skin = 0;           // a3, forwarded only
	Handle partition = 0;      // a4, forwarded only
	std::uint32_t a5 = 0;      // role unverified, forwarded only
	Handle propertyState = 0;  // a6, forwarded only
	Handle effectState = 0;    // a7, forwarded only
	std::uint32_t a8 = 0;      // role unverified, forwarded only
	std::uint32_t a9 = 0;      // role unverified, forwarded from EBX (0x009A8E67)
	std::uint32_t passIndex = 0;  // a10, forwarded from EDI (0x009A8E6C)
	std::uint32_t a11 = 0;     // read only for slot +0x5C (0x009A8F52)
};

// ---- Constants --------------------------------------------------------------

// The map's vtable slots this body calls (read: MOV EDX/EAX,[EDX+n] at
// 0x009A8EB1, 0x009A8F38, 0x009A8F69, 0x009A8F98, 0x009A8FB0, 0x009A8FDC).
// The names after the offsets are proposals (map, "Layout requests").
constexpr std::uint8_t kSlotDefined = 0x54;
constexpr std::uint8_t kSlotConstant = 0x58;
constexpr std::uint8_t kSlotAttribute = 0x5C;
constexpr std::uint8_t kSlotGlobal = 0x60;
constexpr std::uint8_t kSlotOperator = 0x64;
constexpr std::uint8_t kSlotObject = 0x6C;

constexpr std::uint32_t kTypeConstant = 0x10000000u;  // CMP ECX,0x10000000 0x009A8EB9
constexpr std::uint32_t kTypeGlobal = 0x40000000u;    // 0x009A8F8C
constexpr std::uint32_t kTypeOperator = 0x50000000u;  // 0x009A8FA2
constexpr std::uint32_t kTypeObject = 0x60000000u;    // 0x009A8FD2
// kTypeDefined 0x20000000 (0x009A8EA7) and kTypeAttribute 0x30000000
// (0x009A8F45) come from ConstantReapply.h, as does kTypeMask (0x009A8EA1).

constexpr std::uint32_t kUnassignedRegister = 0xFFFFFFFFu;  // CMP ECX,-1 0x009A8E8B
constexpr std::uint32_t kVertexTarget = 1;                  // CMP ECX,0x1 0x009A8EC8
constexpr std::uint32_t kPixelTarget = 2;                   // CMP ECX,0x2 0x009A8EFE

constexpr std::uint8_t kMaxSlotArguments = 13;  // slot +0x5C (ret 0x34, map)

// ---- Port values ------------------------------------------------------------

// One map-slot call: the vtable offset, ECX = the map, the stack arguments
// first argument first (the last pushed first).
struct SlotCall {
	std::uint8_t slotOffset = 0;
	std::uint8_t argumentCount = 0;
	Handle arguments[kMaxSlotArguments] = {};
};

constexpr bool operator==(const SlotCall& a, const SlotCall& b) {
	if (a.slotOffset != b.slotOffset || a.argumentCount != b.argumentCount) {
		return false;
	}
	for (std::uint8_t i = 0; i < a.argumentCount; ++i) {
		if (a.arguments[i] != b.arguments[i]) {
			return false;
		}
	}
	return true;
}

// Which handler an entry's type nibble selects (B6, B7, B10..B13).
enum class Handler : std::uint8_t {
	kDefined,    // 0x20000000, slot +0x54
	kConstant,   // 0x10000000, the device or slot +0x58 by target
	kAttribute,  // 0x30000000, slot +0x5C
	kGlobal,     // 0x40000000, slot +0x60
	kOperator,   // 0x50000000, slot +0x64
	kObject,     // 0x60000000, slot +0x6C
	kUnknown,    // any other nibble: no call, the flag clears (0x009A8FD8 -> 0x009A900E)
};

// Where a type-0x10000000 entry goes (B8, B9).
enum class ConstantRoute : std::uint8_t {
	kVertexDevice,  // target 1, device +0x178
	kPixelDevice,   // target 2, device +0x1B4
	kProgram,       // any other target, slot +0x58
};

// ---- The decisions, pure ----------------------------------------------------

// B1 and B15 in one: index i is applied while i < the zero-extended u16
// count, unsigned. B1 is CMP word [ESI+0x16],AX with AX = 0 then JBE
// (0x009A8E58, 0x009A8E60): count <= 0, i.e. i = 0 is not below the count;
// B15 is MOVZX ECX,word [ESI+0x16]; ADD EAX,1; CMP EAX,ECX; JC
// (0x009A9017..0x009A9024).
constexpr bool HasEntry(std::uint32_t index, std::uint16_t count) {
	return index < static_cast<std::uint32_t>(count);
}

// B2 (TEST EAX,EAX / JZ 0x009A8E76..0x009A8E78).
constexpr bool IsNullEntry(Handle entry) {
	return entry == 0;
}

// B3 (CMP byte [EAX+0x8],0 / JZ 0x009A902A at 0x009A8E7E..0x009A8E82): a
// clear byte ends the whole walk, not just this entry.
constexpr bool EndsWalk(std::uint8_t live) {
	return live == 0;
}

// B4 / B5 (CMP ECX,-1 / JNZ 0x009A8E8B..0x009A8E92, then CMP dword
// [EAX+0x24],0 / JZ 0x009A8E94..0x009A8E98): the gate +0x24 is read only for
// register -1, so it is read through readGate only then.
template <class ReadGate>
constexpr bool RegisterGatePasses(std::uint32_t startRegister, ReadGate&& readGate) {
	return startRegister != kUnassignedRegister || readGate() != 0;
}

// B6, B7, B10..B13: the compares in the original's order on the masked type
// (AND ECX,0xF0000000 0x009A8EA1, then 0x009A8EA7 .. 0x009A8FD2).
constexpr Handler HandlerOf(std::uint32_t typeFlags) {
	switch (typeFlags & constant_reapply::kTypeMask) {
	case constant_reapply::kTypeDefined: return Handler::kDefined;
	case kTypeConstant: return Handler::kConstant;
	case constant_reapply::kTypeAttribute: return Handler::kAttribute;
	case kTypeGlobal: return Handler::kGlobal;
	case kTypeOperator: return Handler::kOperator;
	case kTypeObject: return Handler::kObject;
	default: return Handler::kUnknown;
	}
}

// B8 / B9 ([entry+0x18] against 1 and 2, the whole dword: 0x009A8EC5..0x009A8F01).
constexpr ConstantRoute RouteOf(std::uint32_t target) {
	if (target == kVertexTarget) {
		return ConstantRoute::kVertexDevice;
	}
	if (target == kPixelTarget) {
		return ConstantRoute::kPixelDevice;
	}
	return ConstantRoute::kProgram;
}

// The device calls' status: XOR ECX,ECX; TEST EAX,EAX; SETL CL
// (0x009A8EF0..0x009A8EF4 and 0x009A8F26..0x009A8F2A): 1 exactly for a
// negative HRESULT (FAILED), 0 for S_OK and any positive success code.
constexpr std::uint32_t DeviceStatus(std::int32_t hresult) {
	return hresult < 0 ? 1u : 0u;
}

// B14 (TEST EAX,EAX / JZ 0x009A900A..0x009A900C): the whole 32-bit status.
constexpr bool ClearsFlag(std::uint32_t status) {
	return status != 0;
}

// The return (0x009A902C..0x009A9033): EAX = (flag == 0), in the full
// register (XOR EAX,EAX before SETZ AL).
constexpr std::uint32_t Result(bool flagSet) {
	return flagSet ? 0u : 1u;
}

// Whether an entry that passed the original's tests is dispatched under the
// filter. The id [entry+0x10] is not read by the original's body; it is read
// through readId only under kView and only for a defined entry, so kWhole
// reads nothing the original does not.
template <class ReadId>
constexpr bool FilterPasses(EntryFilter filter, std::uint32_t typeFlags, ReadId&& readId) {
	if (filter == EntryFilter::kWhole) {
		return true;
	}
	if ((typeFlags & constant_reapply::kTypeMask) != constant_reapply::kTypeDefined) {
		return false;
	}
	return constant_reapply::Passes(filter, typeFlags, readId());
}

// ---- The argument lists, first argument first, as pushed (read) ------------------

// Slots +0x54 and +0x6C share the pushes 0x009A8FDF..0x009A9005:
// (a1, entry, a2, a3, a4, a5, a6, a7, a8, a9, a10).
constexpr SlotCall ElevenArgumentCall(std::uint8_t slotOffset, const ApplyInput& in, Handle entry) {
	return SlotCall{slotOffset, 11,
		{in.program, entry, in.geometry, in.skin, in.partition, in.a5, in.propertyState, in.effectState, in.a8, in.a9,
			in.passIndex, 0, 0}};
}

// Slots +0x58 and +0x60: (a1, entry, a10) (0x009A8F36..0x009A8F3B,
// 0x009A8F96..0x009A8F9B).
constexpr SlotCall ThreeArgumentCall(std::uint8_t slotOffset, const ApplyInput& in, Handle entry) {
	return SlotCall{slotOffset, 3, {in.program, entry, in.passIndex, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
}

// Slot +0x5C: (a1, entry, a2..a11, extraData) (0x009A8F4D..0x009A8F82).
constexpr SlotCall AttributeCall(const ApplyInput& in, Handle entry, Handle extraData) {
	return SlotCall{kSlotAttribute, 13,
		{in.program, entry, in.geometry, in.skin, in.partition, in.a5, in.propertyState, in.effectState, in.a8, in.a9,
			in.passIndex, in.a11, extraData}};
}

// Slot +0x64: (a1, entry, a2, a3, a6, a7, a8, a9, a10) (0x009A8FAA..0x009A8FCB).
constexpr SlotCall OperatorCall(const ApplyInput& in, Handle entry) {
	return SlotCall{kSlotOperator, 9,
		{in.program, entry, in.geometry, in.skin, in.propertyState, in.effectState, in.a8, in.a9, in.passIndex, 0, 0, 0,
			0}};
}

// ---- The apply over ports ---------------------------------------------------------
//
// Ports (each a reference; one object may serve several), the concept names
// of ConstantReapply.h:
//   memory.StoreLastProgram(map, program)       [map+0x20] = a1            0x009A8E3F
//   memory.EntryCount(map)        -> uint16     [map+0x16], re-read per index  0x009A8E58 / 0x009A9017
//   memory.Entry(map, i)          -> Handle     [[map+0x10] + 4*i], re-read     0x009A8E70 / 0x009A8E73
//   memory.EntryLive(entry)       -> uint8      byte [entry+0x08]               0x009A8E7E
//   memory.EntryRegister(entry)   -> uint32     [entry+0x1C]                    0x009A8E88
//   memory.EntryGate(entry)       -> uint32     [entry+0x24]                    0x009A8E94
//   memory.EntryTypeFlags(entry)  -> uint32     [entry+0x14]                    0x009A8E9E
//   memory.EntryTarget(entry)     -> uint32     [entry+0x18]                    0x009A8EC5
//   memory.EntryRegisterCount(entry) -> uint32  [entry+0x20]                    0x009A8ECD / 0x009A8F03
//   memory.EntryData(entry)       -> Handle     [entry+0x30]                    0x009A8ED0 / 0x009A8F06
//   memory.EntryId(entry)         -> uint32     [entry+0x10], kView only (not read by the original)
//   memory.Device(map)            -> Handle     [map+0x28], re-read per upload  0x009A8ED3 / 0x009A8F09
//   extraData.Lookup(geometry)    -> Handle     0x006FF9C0(ECX = geometry, [0x00B29F84])
//   slots.Call(map, SlotCall)     -> uint32     [[map] + slotOffset], thiscall, ECX = map
//   device.SetVertexShaderConstantF(device, start, data, count) -> int32   [[device]+0x178]
//   device.SetPixelShaderConstantF(device, start, data, count)  -> int32   [[device]+0x1B4]
//
// The entry's register is read once (0x009A8E88) and that value is what the
// device receives as StartRegister (the a10 slot holds it, 0x009A8E8E, and is
// pushed at 0x009A8EEC / 0x009A8F22; read). The other fields are read where
// the original first reads them; none is read twice by the original within
// one entry, and no store of this body lies between those reads.
//
// Returns the original's EAX: 0 when every dispatched entry's status was 0,
// 1 otherwise. A null geometry under kWhole reaches the lookup as the
// original's does (map C18: a precondition, not refused here; the engine
// port calls the game's own 0x006FF9C0).
template <class Memory, class ExtraData, class Slots, class Device>
std::uint32_t Apply(Handle map, const ApplyInput& in, EntryFilter filter, Memory& memory, ExtraData& extraData,
	Slots& slots, Device& device) {
	memory.StoreLastProgram(map, in.program);
	Handle extra = 0;
	if (constant_reapply::LooksUpExtraData(filter)) {
		extra = extraData.Lookup(in.geometry);
	}
	bool flag = true;  // MOV byte [ESP+0x18],0x1 0x009A8E48
	for (std::uint32_t index = 0; HasEntry(index, memory.EntryCount(map)); ++index) {  // B1, B15
		const Handle entry = memory.Entry(map, index);
		if (IsNullEntry(entry)) {  // B2
			continue;
		}
		if (EndsWalk(memory.EntryLive(entry))) {  // B3
			break;
		}
		const std::uint32_t startRegister = memory.EntryRegister(entry);
		if (!RegisterGatePasses(startRegister, [&] { return memory.EntryGate(entry); })) {  // B4, B5
			continue;
		}
		const std::uint32_t typeFlags = memory.EntryTypeFlags(entry);
		if (!FilterPasses(filter, typeFlags, [&] { return memory.EntryId(entry); })) {  // the deviation
			continue;
		}
		std::uint32_t status = 0;
		switch (HandlerOf(typeFlags)) {
		case Handler::kDefined:  // B6
			status = slots.Call(map, ElevenArgumentCall(kSlotDefined, in, entry));
			break;
		case Handler::kConstant:  // B7
			switch (RouteOf(memory.EntryTarget(entry))) {
			case ConstantRoute::kVertexDevice: {  // B8
				const std::uint32_t count = memory.EntryRegisterCount(entry);
				const Handle data = memory.EntryData(entry);
				status = DeviceStatus(device.SetVertexShaderConstantF(memory.Device(map), startRegister, data, count));
				break;
			}
			case ConstantRoute::kPixelDevice: {  // B9
				const std::uint32_t count = memory.EntryRegisterCount(entry);
				const Handle data = memory.EntryData(entry);
				status = DeviceStatus(device.SetPixelShaderConstantF(memory.Device(map), startRegister, data, count));
				break;
			}
			case ConstantRoute::kProgram:  // B9 taken
				status = slots.Call(map, ThreeArgumentCall(kSlotConstant, in, entry));
				break;
			}
			break;
		case Handler::kAttribute:  // B10
			status = slots.Call(map, AttributeCall(in, entry, extra));
			break;
		case Handler::kGlobal:  // B11
			status = slots.Call(map, ThreeArgumentCall(kSlotGlobal, in, entry));
			break;
		case Handler::kOperator:  // B12
			status = slots.Call(map, OperatorCall(in, entry));
			break;
		case Handler::kObject:  // B13
			status = slots.Call(map, ElevenArgumentCall(kSlotObject, in, entry));
			break;
		case Handler::kUnknown:  // B13 taken: straight to the flag clear 0x009A900E
			flag = false;
			continue;
		}
		if (ClearsFlag(status)) {  // B14
			flag = false;
		}
	}
	return Result(flag);
}

} // namespace obng::engine::render::apply_constants
