#pragma once

// NiDX9Renderer::DrawSkinnedPrimitive 0x00767520 (proposed name; method from
// the string at 0x00A88D90, class derived - re/functions/0x00767520.md
// "Identity") rebuilt over plain values and ports, with the stereo contract of
// StereoDraw.h.
//
// The original draws one skinned geometry: it chooses the shader, opens the
// pipeline (shader slots 10, 11), runs the passes the shader opens (slots
// 18, 19) and in each pass every partition of the skin partition (slots 13,
// 15, 14, the render state's sub-object slot 1, one DrawIndexedPrimitive per
// array of the buffer record, slot 16), then closes (slot 17 and the render
// state's draw flag). DrawSkinnedPrimitive below makes the same port calls, in
// the same order, with the same arguments; every port read happens where the
// original reads that memory, so a callee that changes it is seen at the same
// point (a re-read in the original is a re-read here).
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts (which said), unverified = not checked.
// Names are proposals; the address stays next to each.
//
// No x87, SSE or other floating-point instruction is in the body (read: asm.s
// 0x00767520-0x0076780B holds only integer instructions); the world bound is
// copied as four dwords by MOV (0x00767617-0x00767635), so its bits pass
// unchanged, NaN payloads included. The x87 precision control does not touch
// any result of this function (derived from the two read facts).
//
// 64-bit clean: engine objects are std::uint32_t handles the ports resolve;
// no pointer is assumed to be four bytes here. The engine shapes (offsets,
// vtable slots, calling conventions) are in NiDX9Renderer_DrawSkinnedPrimitive.h.

#include "engine/render/StereoDraw.h"

#include <concepts>
#include <cstdint>

namespace obng::engine::render::draw_skinned_primitive {

// An engine object (renderer, geometry, shader, partition, record, ...) as a
// 32-bit handle; 0 is null.
using Handle = std::uint32_t;
inline constexpr Handle kNull = 0;

// D3DPRIMITIVETYPE values (d3d9types.h, read by the map: D3DPT_TRIANGLELIST 4,
// D3DPT_TRIANGLESTRIP 5); the body builds them as SETNZ + ADD 4 (0x00767548,
// 0x0076754B, read).
inline constexpr std::uint32_t kTriangleList = 4;
inline constexpr std::uint32_t kTriangleStrip = 5;

// AND word [data+0x2E],0xF000 (0x007677B7, read): the bits kept per pass.
inline constexpr std::uint16_t kPassFlagsKept = 0xF000;

// DrawIndexedPrimitive's MinVertexIndex: PUSH 0x0 (0x00767745, read).
inline constexpr std::uint32_t kMinVertexIndex = 0;

// The stack arguments of 0x00767520 (ESP0+4..+0x14, RET 0x14 at 0x0076780B;
// read) and ECX.
struct DrawArguments {
	Handle renderer = kNull;    // ECX -> ESI (0x0076752A)
	Handle geometry = kNull;    // a1, NiGeometry* (proposal), may be null (0x00767556)
	Handle data = kNull;        // a2, NiGeometryData* (proposal); only its +0x2E is written
	Handle skin = kNull;        // a3, NiSkinInstance* (proposal); null returns at once
	Handle world = kNull;       // a4, const NiTransform*; handed on, never read here
	Handle worldBound = kNull;  // a5, const NiBound*; copied per pass
};

// The arguments of shader slots 10, 11, 12 and 17 in call order (pushes read
// at 0x0076758E-0x0076759F, 0x007675B5-0x007675C9, 0x007675FC-0x0076760D,
// 0x007677DA-0x007677EB). `record` is EDI: partition 0's +0x28 until the first
// drawn partition, then the last record slot 15 returned (C8, read).
struct PipelineCall {
	Handle geometry;
	Handle skin;
	Handle record;
	Handle propertyState;  // renderer +0x0C, re-read per call
	Handle effectState;    // renderer +0x10, re-read per call
	Handle world;
	Handle worldBound;
};

// The arguments of shader slots 13, 14 and 16 (pushes read at
// 0x00767687-0x0076769C, 0x007676CC-0x007676E1, 0x0076777D-0x00767792).
// `record` is the partition's +0x28 for slot 13 and slot 15's result for
// slots 14 and 16.
struct PartitionCall {
	Handle geometry;
	Handle skin;
	Handle partition;
	Handle record;
	Handle propertyState;
	Handle effectState;
	Handle world;
	Handle worldBound;
};

// The arguments of shader slot 15 (pushes read at 0x007676AD-0x007676B0):
// note the order differs from slot 13 (no skin, no effect state).
struct RecordCall {
	Handle geometry;
	Handle partition;
	Handle bufferData;  // the partition's +0x28
	Handle propertyState;
};

// IDirect3DDevice9::DrawIndexedPrimitive (slot 82, +0x148) in the device's
// argument order (pushes read at 0x00767737-0x0076774D).
struct DrawIndexedCall {
	Handle device;                 // renderer +0x280, re-read per draw (0x00767729)
	std::uint32_t primitiveType;   // kTriangleList or kTriangleStrip, fixed per call (C3)
	std::uint32_t baseVertexIndex; // record +0x34 (an INT in d3d9.h; passed as its bits)
	std::uint32_t minVertexIndex;  // kMinVertexIndex
	std::uint32_t numVertices;     // the partition's u16 +0x1C, zero-extended (MOVZX 0x0076773D)
	std::uint32_t startIndex;
	std::uint32_t primitiveCount;
};

// ---- The decisions (pure; each names its branch in 0x00767520.md) ---------------------

// B1 (0x0076752C JZ): a null skin returns before any read or call.
constexpr bool HasSkin(Handle skin) {
	return skin != kNull;
}

// C3 (0x0076753E-0x0076754E): partition 0's u16 +0x22 against 0; one type for
// every partition of the call.
constexpr std::uint32_t PrimitiveTypeFor(std::uint16_t stripCountOfPartition0) {
	return stripCountOfPartition0 != 0 ? kTriangleStrip : kTriangleList;
}

// B2/B3 (0x00767558 JZ, 0x00767572 JNZ): the cast result when there is a
// geometry and the cast is non-null, otherwise the renderer's +0xA94 (used
// unchecked, read at 0x00767574).
constexpr bool FallsBackToDefaultShader(Handle geometry, Handle castResult) {
	return geometry == kNull || castResult == kNull;
}

// B4 (0x007675A4 TEST EAX,EAX / 0x007675A6 JNZ): any non-zero dword refuses.
constexpr bool PipelineRefused(std::uint32_t slot10Result) {
	return slot10Result != 0;
}

// B5 (0x007675E4 JZ) and B14 (0x007677C8 JNZ): slots 18/19 open a pass on any
// non-zero dword.
constexpr bool PassOpened(std::uint32_t slot18Or19Result) {
	return slot18Or19Result != 0;
}

// B6 (CMP dword [count],0 at 0x00767612, JBE at 0x00767643): unsigned
// "below or equal 0" is "equal 0".
constexpr bool PartitionLoopRuns(std::uint32_t partitionCount) {
	return partitionCount != 0;
}

// B7/B8 (0x00767650 CMP EBX,[ESI+0xA94] / JNZ; 0x0076765C CMP EAX,[ESI+0x6D8]
// / JBE): only the default shader (by pointer identity) is bone-gated; the u16
// bone count, zero-extended, against the dword limit, unsigned; equal passes.
constexpr bool SkippedByBoneGate(Handle shader, Handle defaultShader, std::uint16_t boneCount,
		std::uint32_t maxBonesWithoutShader) {
	return shader == defaultShader && static_cast<std::uint32_t>(boneCount) > maxBonesWithoutShader;
}

// B9 (0x007676B7 TEST EDI,EDI / JZ): a null record aborts the whole draw.
constexpr bool RecordMissing(Handle record) {
	return record == kNull;
}

// B10 (CMP [EDI+0x44],0 / JBE at 0x00767706): no array, no draw.
constexpr bool HasArrays(std::uint32_t arrayCount) {
	return arrayCount != 0;
}

// B11 (0x0076770D JZ): with a lengths array the count is the u16 length
// zero-extended minus 2 in 32 bits (MOVZX / SUB ECX,2 at 0x00767713-0x00767717),
// so a length below 2 wraps to 0xFFFFFFFE or 0xFFFFFFFF and is passed on
// unchecked (C5); without one it is the record's +0x3C.
constexpr std::uint32_t PrimitiveCount(bool hasLengths, std::uint16_t length, std::uint32_t triangleCount) {
	return hasLengths ? static_cast<std::uint32_t>(length) - 2u : triangleCount;
}

// C4 (LEA EDX,[EAX+ECX+2] at 0x00767758): the next array starts count + 2
// indices later, modulo 2^32.
constexpr std::uint32_t NextStartIndex(std::uint32_t startIndex, std::uint32_t primitiveCount) {
	return startIndex + primitiveCount + 2u;
}

// B12 (0x00767763 CMP EAX,[EDI+0x44] / JC) and B13 (0x007677A1 CMP / JC):
// the incremented index against the count, unsigned below.
constexpr bool LoopContinues(std::uint32_t nextIndex, std::uint32_t count) {
	return nextIndex < count;
}

// C9 (0x007677B7): the low 12 bits of the data's flags word cleared per pass.
constexpr std::uint16_t ClearedPassFlags(std::uint16_t flags) {
	return static_cast<std::uint16_t>(flags & kPassFlagsKept);
}

// ---- The ports -------------------------------------------------------------------------

// Each member is one engine read or call of 0x00767520 (offsets and slots in
// NiDX9Renderer_DrawSkinnedPrimitive.h). Reads have no effect; every call
// member stands for exactly one call site of the original.
//   shaderCandidate(g)            [g+0xBC]                                (0x0076755A)
//   castToShaderInterface(o)      0x00560920(NiRTTI 0x00B42858, o)        (0x00767566)
//   defaultShader()               [renderer+0xA94]                        (0x00767574, 0x00767650)
//   propertyState()/effectState() [renderer+0x0C] / [renderer+0x10]       (per call)
//   skinPartition(s)              [s+0x0C]                                (0x00767532)
//   partitions(sp)                [sp+0x0C]                               (0x0076753B)
//   partitionCount(sp)            [sp+0x08], after slot 11                (0x007675D2)
//   partitionAt(array, i)         array + 0x2C*i                          (ADD EBP,0x2C 0x0076779E)
//   stripCount/boneCount/vertexCount(p)  u16 +0x22 / +0x20 / +0x1C
//   bufferData(p)                 [p+0x28]                                (0x00767581, 0x0076767F)
//   maxBonesWithoutShader()       [renderer+0x6D8], per partition         (0x0076765C)
//   worldBoundWord(b, k)          dword k of the bound                    (0x00767617..)
//   setCurrentWorldBoundWord(k,v) [renderer+0x658+4k] = v                 (0x0076761A..)
//   dataFlags(d)/setDataFlags(d,v) the u16 [d+0x2E]                       (0x007677B7)
//   arrayCount/arrayLengths/triangleCount/baseVertexIndex(r)  [r+0x44/+0x48/+0x3C/+0x34]
//   arrayLength(lengths, i)       u16 lengths[i]                          (0x00767713)
//   device()                      [renderer+0x280], per draw              (0x00767729)
//   shaderSlot10..19(shader, ..)  [[shader]+0x28..+0x4C], ECX = shader
//   cannotRenderPartition(n)      0x0060D0A0(0x00A88D90, n): a bare RET in this build (C11)
//   renderStateSubObjectSlot1()   [[[renderer+0x8AC]+0xFF0]]+0x04         (0x007676F7)
//   renderStateSetDrawFlag(v)     [[renderer+0x8AC]]+0xFC (v)             (0x00767802)
//   drawIndexedPrimitive(call)    [[renderer+0x280]]+0x148                (0x0076774E)
//   eyeSwitch(eye)                StereoDraw.h's port; never called with one eye
template <class P>
concept DrawSkinnedPrimitivePorts = requires(P& ports, Handle handle, std::uint32_t index, std::uint32_t word,
		std::uint16_t flags, std::uint8_t eye, const PipelineCall& pipeline, const PartitionCall& partition,
		const RecordCall& record, const DrawIndexedCall& draw) {
	{ ports.shaderCandidate(handle) } -> std::convertible_to<Handle>;
	{ ports.castToShaderInterface(handle) } -> std::convertible_to<Handle>;
	{ ports.defaultShader() } -> std::convertible_to<Handle>;
	{ ports.propertyState() } -> std::convertible_to<Handle>;
	{ ports.effectState() } -> std::convertible_to<Handle>;
	{ ports.skinPartition(handle) } -> std::convertible_to<Handle>;
	{ ports.partitions(handle) } -> std::convertible_to<Handle>;
	{ ports.partitionCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.partitionAt(handle, index) } -> std::convertible_to<Handle>;
	{ ports.stripCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.boneCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.vertexCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.bufferData(handle) } -> std::convertible_to<Handle>;
	{ ports.maxBonesWithoutShader() } -> std::convertible_to<std::uint32_t>;
	{ ports.worldBoundWord(handle, index) } -> std::convertible_to<std::uint32_t>;
	ports.setCurrentWorldBoundWord(index, word);
	{ ports.dataFlags(handle) } -> std::convertible_to<std::uint16_t>;
	ports.setDataFlags(handle, flags);
	{ ports.arrayCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.arrayLengths(handle) } -> std::convertible_to<Handle>;
	{ ports.arrayLength(handle, index) } -> std::convertible_to<std::uint16_t>;
	{ ports.triangleCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.baseVertexIndex(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.device() } -> std::convertible_to<Handle>;
	{ ports.shaderSlot10(handle, pipeline) } -> std::convertible_to<std::uint32_t>;
	ports.shaderSlot11(handle, pipeline);
	ports.shaderSlot12(handle, pipeline);
	ports.shaderSlot13(handle, partition);
	ports.shaderSlot14(handle, partition);
	{ ports.shaderSlot15(handle, record) } -> std::convertible_to<Handle>;
	ports.shaderSlot16(handle, partition);
	ports.shaderSlot17(handle, pipeline);
	{ ports.shaderSlot18(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.shaderSlot19(handle) } -> std::convertible_to<std::uint32_t>;
	ports.cannotRenderPartition(word);
	ports.renderStateSubObjectSlot1();
	ports.renderStateSetDrawFlag(word);
	ports.drawIndexedPrimitive(draw);
	ports.eyeSwitch(eye);
};

// How a call ended (the rebuild's own report; the original returns nothing:
// EAX is whatever the last call left, and the caller 0x007693E0 does not read
// it, 0x007694C2-0x007694C9, read).
enum class Outcome : std::uint8_t {
	kNoSkin,           // B1: no read, no call, no write
	kPipelineRefused,  // B4: slot 10 non-zero; nothing after it
	kRecordMissing,    // B9: slot 15 null; no slot 16/19/17, no flags clear, no draw flag (C13)
	kClosed,           // slot 17 and the draw flag ran
};

// The draws of one partition for one eye: one DrawIndexedPrimitive per array
// of the record (B10..B12), the start index from 0 (0x007676FE) growing by
// count + 2 (C4). The record's +0x44 and +0x48 are re-read per array, as the
// original does (0x00767763, 0x00767708).
template <DrawSkinnedPrimitivePorts P>
void DrawPartitionArrays(P& ports, std::uint32_t primitiveType, Handle partition, Handle record) {
	if (!HasArrays(ports.arrayCount(record))) {
		return;
	}
	std::uint32_t startIndex = 0;
	std::uint32_t index = 0;
	do {
		const Handle lengths = ports.arrayLengths(record);
		const bool hasLengths = lengths != kNull;
		const std::uint32_t primitiveCount = PrimitiveCount(hasLengths,
				hasLengths ? ports.arrayLength(lengths, index) : std::uint16_t{0},
				hasLengths ? 0u : ports.triangleCount(record));
		const Handle device = ports.device();
		const std::uint32_t numVertices = ports.vertexCount(partition);
		const std::uint32_t baseVertexIndex = ports.baseVertexIndex(record);
		ports.drawIndexedPrimitive(DrawIndexedCall{device, primitiveType, baseVertexIndex, kMinVertexIndex,
				numVertices, startIndex, primitiveCount});
		startIndex = NextStartIndex(startIndex, primitiveCount);
		++index;
	} while (LoopContinues(index, ports.arrayCount(record)));
}

// 0x00767520 over ports. With plan.eyes == 1 the port calls are the
// original's, one for one (the 1:1 gate; eyeSwitch is never called). With
// plan.eyes == 2 each partition's draws run a second time between
// eyeSwitch(1) and eyeSwitch(0), after the first eye's draws and before slot
// 16 (StereoDraw.h); the second run re-reads the record and the device the
// way the first did. It runs for a partition without arrays too (the
// contract names no exception; the switch then frames no draw).
template <DrawSkinnedPrimitivePorts P>
Outcome DrawSkinnedPrimitive(P& ports, const DrawArguments& args, stereo_draw::EyePlan plan) {
	if (!HasSkin(args.skin)) {
		return Outcome::kNoSkin;
	}
	const Handle skinPartition = ports.skinPartition(args.skin);
	const Handle partitions = ports.partitions(skinPartition);
	const Handle partition0 = ports.partitionAt(partitions, 0);
	const std::uint32_t primitiveType = PrimitiveTypeFor(ports.stripCount(partition0));

	Handle shader = kNull;
	if (args.geometry != kNull) {
		shader = ports.castToShaderInterface(ports.shaderCandidate(args.geometry));
	}
	if (FallsBackToDefaultShader(args.geometry, shader)) {
		shader = ports.defaultShader();
	}

	// EDI (C8): partition 0's +0x28 (0x00767581), later the last record.
	Handle passRecord = ports.bufferData(partition0);
	const auto pipeline = [&]() {
		const Handle effectState = ports.effectState();
		const Handle propertyState = ports.propertyState();
		return PipelineCall{args.geometry, args.skin, passRecord, propertyState, effectState, args.world,
				args.worldBound};
	};
	if (PipelineRefused(ports.shaderSlot10(shader, pipeline()))) {
		return Outcome::kPipelineRefused;
	}
	ports.shaderSlot11(shader, pipeline());
	const std::uint32_t partitionCount = ports.partitionCount(skinPartition);

	if (PassOpened(ports.shaderSlot18(shader))) {
		do {
			ports.shaderSlot12(shader, pipeline());
			for (std::uint32_t word = 0; word < 4; ++word) {
				ports.setCurrentWorldBoundWord(word, ports.worldBoundWord(args.worldBound, word));
			}
			if (PartitionLoopRuns(partitionCount)) {
				std::uint32_t index = 0;
				do {
					const Handle partition = ports.partitionAt(partitions, index);
					const Handle defaultShader = ports.defaultShader();
					if (shader == defaultShader) {
						const std::uint16_t bones = ports.boneCount(partition);
						if (SkippedByBoneGate(shader, defaultShader, bones, ports.maxBonesWithoutShader())) {
							ports.cannotRenderPartition(bones);
							++index;
							continue;
						}
					}
					const Handle bufferData = ports.bufferData(partition);
					passRecord = bufferData;  // MOV EDI,[EBP+0x28] (0x0076767F)
					{
						const Handle effectState = ports.effectState();
						const Handle propertyState = ports.propertyState();
						ports.shaderSlot13(shader, PartitionCall{args.geometry, args.skin, partition, bufferData,
								propertyState, effectState, args.world, args.worldBound});
					}
					const Handle record = ports.shaderSlot15(shader,
							RecordCall{args.geometry, partition, bufferData, ports.propertyState()});
					passRecord = record;  // MOV EDI,EAX (0x007676B5)
					if (RecordMissing(record)) {
						return Outcome::kRecordMissing;
					}
					const auto partitionCall = [&]() {
						const Handle effectState = ports.effectState();
						const Handle propertyState = ports.propertyState();
						return PartitionCall{args.geometry, args.skin, partition, record, propertyState, effectState,
								args.world, args.worldBound};
					};
					ports.shaderSlot14(shader, partitionCall());
					ports.renderStateSubObjectSlot1();
					DrawPartitionArrays(ports, primitiveType, partition, record);
					if (stereo_draw::DrawsTwice(plan)) {
						ports.eyeSwitch(1);
						DrawPartitionArrays(ports, primitiveType, partition, record);
						ports.eyeSwitch(0);
					}
					ports.shaderSlot16(shader, partitionCall());
					++index;
				} while (LoopContinues(index, partitionCount));
			}
			ports.setDataFlags(args.data, ClearedPassFlags(ports.dataFlags(args.data)));
		} while (PassOpened(ports.shaderSlot19(shader)));
	}

	ports.shaderSlot17(shader, pipeline());
	ports.renderStateSetDrawFlag(0);
	return Outcome::kClosed;
}

} // namespace obng::engine::render::draw_skinned_primitive
