#pragma once

// The decisions of 0x007672F0 (proposed NiDX9Renderer::DrawPrimitive: the
// whole-geometry draw, no skin partitions) over plain values and ports, with
// the stereo contract of StereoDraw.h. 64-bit clean: every engine object is a
// std::uint32_t handle, every engine read goes through a port. The engine
// shapes behind the ports live in NiDX9Renderer_DrawPrimitive.h.
//
// Evidence: re/functions/0x007672F0.md (the map), re/export/stereo-draw/
// 0x007672F0/asm.s (the instructions). Names are proposals; the address stays
// next to each. Marks: read = seen in the instructions (address given),
// derived = follows from read facts (which said), unverified = not checked.
//
// The original, in instruction order (read, asm.s):
//   B1 0x007672FD  geometry == 0                -> default shader [this+0xA94], no cast
//   B2 0x00767317  cast of [geometry+0xBC] != 0 -> that shader, else [this+0xA94]
//   B3 0x00767347  slot 10 result != 0          -> return at once (no slot 11, no pass, no close)
//      slot 11; slot 18
//   B4 0x0076737A  slot 18 result == 0          -> no pass; the close
//      per pass: slot 12, slot 13, slot 15 (the buffer record), slot 14,
//                [[this+0x8AC]+0xFF0] slot +0x04, the device draw(s), slot 16, slot 19
//   B5 0x00767415  record+0x30 == 0             -> one DrawPrimitive (slot 81)
//   B6 0x00767422  record+0x44 == 0 (JBE)       -> no DrawIndexedPrimitive
//   B7 0x0076742D  record+0x48 == 0             -> count = record+0x3C, else lengths[i] - 2
//   B8 0x0076747E  i + 1 < record+0x44 (JC)     -> next DrawIndexedPrimitive, start += count + 2
//   B9 0x007674D2  slot 19 result != 0          -> next pass
//      the close: slot 17, [[this+0x8AC]]+0xFC(0)
//
// The one deviation, by contract (StereoDraw.h): with an EyePlan of two eyes
// each pass's draw(s) run a second time between eyeSwitch.Switch(1) and
// eyeSwitch.Switch(0), right before slot 16. With one eye nothing differs.
//
// No x87 instruction is in the body (read: asm.s holds none), so no rounding
// is modelled here; every value is a 32-bit integer or handle, and the
// arithmetic is the original's unsigned 32-bit arithmetic with its wrap.

#include "engine/render/StereoDraw.h"

#include <cstdint>

namespace obng::engine::render::draw_primitive {

using Handle = std::uint32_t;  // an engine address, never dereferenced here
using stereo_draw::EyePlan;

// ---- Inputs -----------------------------------------------------------------

// The six stack arguments (ESP0+0x04..+0x18, read; RET 0x18 at 0x00767513).
struct DrawInput {
	Handle geometry = 0;    // a1, NiGeometry* (proposal), may be 0 (0x0076B160 passes 0)
	Handle data = 0;        // a2, never read by the body (read: no instruction addresses ESP0+0x08)
	Handle skin = 0;        // a3, passed on, never dereferenced here
	Handle world = 0;       // a4, const NiTransform*, passed on
	Handle worldBound = 0;  // a5, const NiBound* (proposal), passed on
	Handle record = 0;      // a6, the buffer record of the first pass (ESI at 0x00767327)
};

// this+0x0C and this+0x10, re-read before every shader call that takes them
// (MOV ECX,[EDI+0x10] / MOV EDX,[EDI+0xC], e.g. 0x0076732E/0x00767332).
struct RendererStates {
	Handle propertyState = 0;  // this+0x0C (proposal: NiPropertyState*)
	Handle effectState = 0;    // this+0x10 (proposal: NiDynamicEffectState*)
};

// The fields of the buffer record the draw reads (GeometryBufferData,
// re/functions/0x00763130.md for the names; the offsets read here).
struct BufferRecord {
	std::uint32_t vertexCount = 0;      // +0x14, DIP NumVertices (0x00767453)
	std::uint32_t indexBuffer = 0;      // +0x30, only tested for zero (0x00767412)
	std::uint32_t baseVertexIndex = 0;  // +0x34, DIP BaseVertexIndex / DP StartVertex (0x00767457, 0x00767498)
	std::uint32_t primitiveType = 0;    // +0x38, D3DPRIMITIVETYPE (0x0076745D, 0x0076749C)
	std::uint32_t triangleCount = 0;    // +0x3C, count without lengths, and the DP count (0x0076743C, 0x00767486)
	std::uint32_t arrayCount = 0;       // +0x44, the number of DIPs (0x00767417, 0x00767473)
	Handle arrayLengths = 0;            // +0x48, u16 index count per strip, or 0 (0x00767428)
};

// ---- Port values ------------------------------------------------------------

// One shader-interface call: vtable slot n (10..19) at +4n, ECX = the shader,
// the stack arguments in push order reversed (first argument first).
struct ShaderCall {
	std::uint8_t slot = 0;
	std::uint8_t argumentCount = 0;
	Handle arguments[8] = {};
};

constexpr bool operator==(const ShaderCall& a, const ShaderCall& b) {
	if (a.slot != b.slot || a.argumentCount != b.argumentCount) {
		return false;
	}
	for (std::uint8_t i = 0; i < a.argumentCount; ++i) {
		if (a.arguments[i] != b.arguments[i]) {
			return false;
		}
	}
	return true;
}

// IDirect3DDevice9::DrawIndexedPrimitive (slot 82, +0x148) as pushed at
// 0x00767451..0x00767461: (type, BaseVertexIndex, MinVertexIndex 0,
// NumVertices, startIndex, primCount). BaseVertexIndex is an INT in d3d9.h;
// it is carried as its 32-bit pattern.
struct IndexedDraw {
	std::uint32_t primitiveType = 0;
	std::uint32_t baseVertexIndex = 0;
	std::uint32_t minVertexIndex = 0;
	std::uint32_t vertexCount = 0;
	std::uint32_t startIndex = 0;
	std::uint32_t primitiveCount = 0;
	constexpr bool operator==(const IndexedDraw&) const = default;
};

// IDirect3DDevice9::DrawPrimitive (slot 81, +0x144) as pushed at
// 0x00767486..0x007674A0: (type, StartVertex, PrimitiveCount).
struct PlainDraw {
	std::uint32_t primitiveType = 0;
	std::uint32_t startVertex = 0;
	std::uint32_t primitiveCount = 0;
	constexpr bool operator==(const PlainDraw&) const = default;
};

enum class Outcome : std::uint8_t {
	Drawn,            // the pipeline closed: slot 17 and the draw flag ran
	PipelineRefused,  // B3: slot 10 returned non-zero; nothing after it ran
	NullRecord,       // slot 15 returned 0: refused where the original faults (see Draw)
};

// ---- The decisions, pure ----------------------------------------------------

// B1 (TEST EBP,EBP / JZ 0x007672F7..0x007672FD): the geometry's own shader is
// looked at only for a non-null geometry.
constexpr bool LooksAtGeometryShader(Handle geometry) {
	return geometry != 0;
}

// B2 (TEST EBX,EBX / JNZ 0x00767315..0x00767317, else MOV EBX,[EDI+0xA94]):
// the cast result when non-null, the default shader otherwise; the default is
// read only on that path and not null-tested (read).
template <class ReadDefault>
constexpr Handle ChooseShader(Handle castResult, ReadDefault&& readDefault) {
	return castResult != 0 ? castResult : readDefault();
}

// B3 (TEST EAX,EAX / JNZ 0x00767345..0x00767347): the whole 32-bit result is
// tested, not AL.
constexpr bool PipelineRefused(std::uint32_t slot10Result) {
	return slot10Result != 0;
}

// B4 (JZ 0x0076737A after slot 18) and B9 (JNZ 0x007674D2 after slot 19):
// a pass runs on a non-zero 32-bit result.
constexpr bool PassRuns(std::uint32_t slotResult) {
	return slotResult != 0;
}

// B5 (CMP [ESI+0x30],EAX with EAX = 0 / JZ 0x00767412..0x00767415).
constexpr bool IsIndexed(const BufferRecord& record) {
	return record.indexBuffer != 0;
}

// B7: with a length array, MOVZX EBP,word [EAX+ECX*2] then SUB EBP,2
// (0x00767433..0x00767437): the u16 zero-extended, then a 32-bit subtraction
// that wraps below 2 (unchecked, read); without one, record+0x3C (0x0076743C).
constexpr std::uint32_t PrimitiveCount(const BufferRecord& record, std::uint16_t stripLength) {
	return record.arrayLengths != 0 ? static_cast<std::uint32_t>(stripLength) - 2u : record.triangleCount;
}

// B8's start step: LEA ECX,[EAX+EBP+0x2] (0x00767468), 32-bit, wrapping.
constexpr std::uint32_t NextStartIndex(std::uint32_t start, std::uint32_t count) {
	return start + count + 2u;
}

// B6 and B8 together: DIP number `index` runs while index < arrayCount,
// unsigned (JBE at 0x00767422 for index 0, JC at 0x0076747E after it).
constexpr bool DrawsIndexedEntry(const BufferRecord& record, std::uint32_t index) {
	return index < record.arrayCount;
}

constexpr IndexedDraw MakeIndexedDraw(const BufferRecord& record, std::uint32_t start, std::uint32_t count) {
	return IndexedDraw{record.primitiveType, record.baseVertexIndex, 0u, record.vertexCount, start, count};
}

constexpr PlainDraw MakePlainDraw(const BufferRecord& record) {
	return PlainDraw{record.primitiveType, record.baseVertexIndex, record.triangleCount};
}

// The shader calls, arguments first-first as pushed (read in asm.s; the map's
// table "Every indirect call"). current is ESI: a6 until the first slot 15,
// then the last slot 15 result (C8).
constexpr ShaderCall SevenArgumentCall(std::uint8_t slot, const DrawInput& in, Handle current, RendererStates s) {
	// slots 10, 11, 12, 17: (a1, a3, ESI, this+0xC, this+0x10, a4, a5)
	return ShaderCall{slot, 7, {in.geometry, in.skin, current, s.propertyState, s.effectState, in.world, in.worldBound, 0}};
}

constexpr ShaderCall EightArgumentCall(std::uint8_t slot, const DrawInput& in, Handle current, RendererStates s) {
	// slots 13, 14, 16: (a1, a3, 0, ESI, this+0xC, this+0x10, a4, a5); the 0 is
	// where 0x00767520 passes the partition (0x007673BE, 0x007673F5, 0x007674BF)
	return ShaderCall{slot, 8, {in.geometry, in.skin, 0, current, s.propertyState, s.effectState, in.world, in.worldBound}};
}

constexpr ShaderCall RecordCall(const DrawInput& in, Handle current, RendererStates s) {
	// slot 15: (a1, 0, ESI, this+0xC) - pushes 0x007673CE..0x007673D2
	return ShaderCall{15, 4, {in.geometry, 0, current, s.propertyState, 0, 0, 0, 0}};
}

constexpr ShaderCall NoArgumentCall(std::uint8_t slot) {
	// slots 18 and 19: ECX only
	return ShaderCall{slot, 0, {}};
}

// ---- The draw over ports ----------------------------------------------------
//
// Ports (each a reference; one object may serve several):
//   memory.GeometryShaderCandidate(geometry) -> Handle    [geometry+0xBC]        0x007672FF
//   memory.CastToShaderInterface(object)     -> Handle    0x00560920(0x00B42858, object), cdecl
//   memory.DefaultShader()                   -> Handle    [this+0xA94]           0x00767319
//   memory.States()                          -> RendererStates  [this+0x0C], [this+0x10]
//   memory.ReadRecord(record)                -> BufferRecord
//   memory.ReadStripLength(lengths, index)   -> uint16    word [lengths + 2*index] 0x00767433
//   shader.Slot(shader, ShaderCall)          -> uint32    [[shader]+4n], ECX = shader
//   renderState.SubObjectSlot1()                          [[[this+0x8AC]+0xFF0]]+0x04
//   renderState.SetDrawFlag(0)                            [[this+0x8AC]]+0xFC
//   device.DrawIndexedPrimitive(IndexedDraw)              [[this+0x280]]+0x148
//   device.DrawPrimitive(PlainDraw)                       [[this+0x280]]+0x144
//   eyeSwitch.Switch(eye)                                 StereoDraw.h; never called with one eye
//
// The record is read once per pass, after the slot-1 call, where the original
// first reads it (0x00767412); the original re-reads +0x44/+0x48/+0x14/+0x34/
// +0x38 inside the DIP loop. The two agree unless a DrawIndexedPrimitive call
// writes the record: derived from E1 of the map (no engine call and no store
// in that window, only the device's DIPs, which hold no pointer to engine
// memory - the last part is unverified). The lengths are read per DIP, as in
// the original.

// The pass's device draw(s) for one eye: B5..B8.
template <class Memory, class Device>
void IssueDraws(const BufferRecord& record, Memory& memory, Device& device) {
	if (!IsIndexed(record)) {  // B5
		device.DrawPrimitive(MakePlainDraw(record));
		return;
	}
	std::uint32_t start = 0;  // 0x0076741A: the a6 slot reused as the start index (C11)
	for (std::uint32_t index = 0; DrawsIndexedEntry(record, index); ++index) {  // B6, B8
		const std::uint16_t length = record.arrayLengths != 0 ? memory.ReadStripLength(record.arrayLengths, index) : 0;
		const std::uint32_t count = PrimitiveCount(record, length);  // B7
		device.DrawIndexedPrimitive(MakeIndexedDraw(record, start, count));
		start = NextStartIndex(start, count);
	}
}

// The whole function. Returns what the original's path was; the original's
// EAX is not part of the contract (both callers ignore it: 0x007694DC,
// 0x0076B2E5, read in the map).
//
// The null record (C9): the original passes slot 15's result to slot 14 and
// calls the slot-1 sub-object without a null test, then reads [record+0x30]
// at 0x00767412 - a read of address 0x30, which faults (derived: the first
// 64 KiB of a Win32 address space are never mapped). The rebuild keeps every
// call the original makes up to that read (slot 14 with record 0, the slot-1
// call) and refuses there with Outcome::NullRecord instead of faulting: no
// draw, no slot 16, 19 or 17, no draw flag - none of which the original
// reaches either. Whether slot 14 itself faults on record 0 first is
// unverified (map C9); if it does, the rebuild faults in the same call.
template <class Memory, class Shader, class RenderState, class Device, class EyeSwitch>
Outcome Draw(const DrawInput& in, EyePlan plan, Memory& memory, Shader& shader, RenderState& renderState, Device& device,
	EyeSwitch& eyeSwitch) {
	Handle castResult = 0;
	if (LooksAtGeometryShader(in.geometry)) {  // B1
		castResult = memory.CastToShaderInterface(memory.GeometryShaderCandidate(in.geometry));
	}
	const Handle chosen = ChooseShader(castResult, [&memory] { return memory.DefaultShader(); });  // B2

	Handle current = in.record;  // ESI
	if (PipelineRefused(shader.Slot(chosen, SevenArgumentCall(10, in, current, memory.States())))) {  // B3
		return Outcome::PipelineRefused;
	}
	shader.Slot(chosen, SevenArgumentCall(11, in, current, memory.States()));

	bool pass = PassRuns(shader.Slot(chosen, NoArgumentCall(18)));  // B4
	while (pass) {
		shader.Slot(chosen, SevenArgumentCall(12, in, current, memory.States()));
		shader.Slot(chosen, EightArgumentCall(13, in, current, memory.States()));
		current = shader.Slot(chosen, RecordCall(in, current, memory.States()));  // MOV ESI,EAX 0x007673ED
		shader.Slot(chosen, EightArgumentCall(14, in, current, memory.States()));
		renderState.SubObjectSlot1();
		if (current == 0) {
			return Outcome::NullRecord;  // the original faults at 0x00767412
		}
		const BufferRecord record = memory.ReadRecord(current);
		IssueDraws(record, memory, device);
		if (stereo_draw::DrawsTwice(plan)) {
			eyeSwitch.Switch(1);
			IssueDraws(record, memory, device);
			eyeSwitch.Switch(0);
		}
		shader.Slot(chosen, EightArgumentCall(16, in, current, memory.States()));
		pass = PassRuns(shader.Slot(chosen, NoArgumentCall(19)));  // B9
	}

	shader.Slot(chosen, SevenArgumentCall(17, in, current, memory.States()));
	renderState.SetDrawFlag(0);
	return Outcome::Drawn;
}

} // namespace obng::engine::render::draw_primitive
