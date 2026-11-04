#pragma once

// The contract of NiD3DShaderConstantMap::ApplyConstants 0x009A8E30 rebuilt
// as a unit with one documented deviation, for single-pass stereo's second
// eye (docs/subsystems/single-pass-stereo.md, step 2): an entry predicate.
//
// The original (re/functions/0x009A8E30.md, held) stores the program in the
// map, looks the geometry's "__NDL_SCMData" extra data up once, then walks
// the map's entry pointer array in index order and dispatches each live
// entry by its type nibble ([entry+0x14] & 0xF0000000) to a map slot
// (+0x54 defined, +0x58 constant through the program, +0x5C attribute,
// +0x60 global, +0x64 operator, +0x6C object) or, for type 0x10000000 with
// target 1 / 2, straight to the device (SetVertexShaderConstantF /
// SetPixelShaderConstantF); the result flag clears on any non-zero return
// or unknown type; the walk ends at the first entry whose +0x08 is clear.
//
// The deviation: the walk takes an EntryFilter. With the whole filter
// (every entry passes) the rebuild does exactly what the original does, call
// for call (the 1:1 gate, checked against the emulator's traces). With the
// view filter only live entries of type 0x20000000 (defined) whose
// predefined id [entry+0x10] is in kViewIds are dispatched - the ids whose
// handler reads the view matrix renderer+0x980 (re/functions/0x009A56D0.md,
// the case table: 3-4 / 15-16 V, 7-8 / 19-20 W x V, 9-10 / 21-22 V x P,
// 11-12 / 23-24 W x V x P, 25-28 S x V, 29-32 S x V x P; on the fixed scene
// the ones that changed with the eye were 19, 23, 27, 31: the view probe,
// run obng-walkers-stereo/20261104-124811). Every other entry is skipped
// without a call and without touching the result flag, and the extra-data
// lookup is made only when the filter can pass a type-0x30000000 entry (the
// lookup's result feeds slot +0x5C alone, C4 of the map): never for the view
// filter, so the second eye's re-apply takes no critical section.
//
// Marks: read = the map's instruction-level readings; derived = this
// header's reasoning from them; measured = the view probe's run.

#include <cstdint>

namespace obng::engine::render::constant_reapply {

enum class EntryFilter : std::uint8_t {
	kWhole,  // the original's walk, every entry
	kView,   // defined entries with a view-dependent id only
};

// The predefined ids whose SetDefinedConstant case reads renderer+0x980
// (read in 0x009A56D0.md's case table).
constexpr bool IsViewId(std::uint32_t id) {
	switch (id) {
	case 3: case 4: case 7: case 8: case 9: case 10: case 11: case 12:
	case 15: case 16: case 19: case 20: case 21: case 22: case 23: case 24:
	case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
		return true;
	default:
		return false;
	}
}

constexpr std::uint32_t kTypeMask = 0xF0000000u;
constexpr std::uint32_t kTypeDefined = 0x20000000u;
constexpr std::uint32_t kTypeAttribute = 0x30000000u;

// Whether an entry (its type flags and predefined id) is dispatched under
// the filter. Liveness, the null pointer and the register gate stay the
// original's tests, made before this one.
constexpr bool Passes(EntryFilter filter, std::uint32_t typeFlags, std::uint32_t id) {
	if (filter == EntryFilter::kWhole) {
		return true;
	}
	return (typeFlags & kTypeMask) == kTypeDefined && IsViewId(id);
}

// Whether the extra-data lookup (0x006FF9C0 under the critical section
// 0x00B3F600) is made at all under the filter: only when a type-0x30000000
// entry could be dispatched (derived from the map's C4: the lookup's result
// is read by slot +0x5C alone).
constexpr bool LooksUpExtraData(EntryFilter filter) {
	return filter == EntryFilter::kWhole;
}

// The ports the rebuild takes (concept names; the rebuild's <Name>.h
// states the engine shapes behind them):
//   memory.EntryCount(map) / memory.Entry(map, i)          [map+0x16] u16 / [[map+0x10] + 4*i]
//   memory.EntryLive/TypeFlags/Target/Register/Count/Gate/Id/Data(entry)   +0x08 / +0x14 / +0x18 / +0x1C / +0x20 / +0x24 / +0x10 / +0x30
//   memory.StoreLastProgram(map, program)                  [map+0x20] = a1
//   memory.Device(map)                                     [map+0x28]
//   extraData.Lookup(geometry)                             0x006FF9C0(ECX = geometry, [0x00B29F84])
//   slots.Call(map, slotOffset, args...)                   the map's vtable slot, thiscall, the original's argument list per slot
//   device.SetVertexShaderConstantF / SetPixelShaderConstantF(device, start, data, count)   vtable +0x178 / +0x1B4
// A rebuild records every port call in order in its tests; the emulator
// gate compares that order with the original's traced calls.

}  // namespace obng::engine::render::constant_reapply
