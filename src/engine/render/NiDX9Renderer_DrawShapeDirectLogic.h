#pragma once

// NiDX9Renderer::DrawShapeDirect 0x007694F0 (proposed name: vtable 0x00A88EA4
// slot 90, class read from the COL, method derived - re/functions/0x007694F0.md
// "Identity") rebuilt over plain values and ports, with the stereo contract of
// StereoDrawDirect.h.
//
// The original draws one geometry with the pipeline its caller left on the
// device: unless the renderer's byte +0x6F0 is set or the data's vertex count
// is 0, it asks 0x00768890 whether to draw per skin partition, casts the
// geometry's +0xBC to NiD3DShaderInterface, calls the shader's slot 18 and the
// render state's sub-object slot 1 (both results unused), then issues the
// device draws - one DrawPrimitive, or one DrawIndexedPrimitive per array of
// the buffer record (whole), or per partition and array (skinned, with the
// default shader's bone gate) - and clears the low 12 bits of the data's
// flags word. DrawShapeDirect below makes the same port calls, in the same
// order, with the same arguments; every port read happens where the original
// reads that memory, so a callee that changes it is seen at the same point.
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts (which said), unverified = not checked.
// Names are proposals; the address stays next to each.
//
// No x87, SSE or other floating-point instruction is in the body (read: asm.s
// 0x007694F0-0x007696F7 holds only integer instructions), so the game's x87
// precision control (24-bit, nearest; knowledge/frame-clock.md) touches no
// result of this function (derived from the read fact); there is no rounding
// to model.
//
// Two faults of the original are preconditions here, not refusals (map C4,
// C12, open question 5): the cast result is used without a null test
// (MOV EDX,[EAX] at 0x00769558) and the whole arm's record without one
// (CMP [EDI+0x30],0 at 0x0076965E). The rebuild hands the same null handle
// to the same port (shaderSlot18(0); indexBuffer(0)), so a port backed by
// engine memory faults at the same point as the original; it does not test
// and does not invent an exit the original lacks.
//
// 64-bit clean: engine objects are std::uint32_t handles the ports resolve;
// no pointer is assumed to be four bytes here. The engine shapes (offsets,
// vtable slots, calling conventions) are in NiDX9Renderer_DrawShapeDirect.h.

#include "engine/render/StereoDrawDirect.h"

#include <concepts>
#include <cstdint>

namespace obng::engine::render::draw_shape_direct {

using stereo_draw_direct::DrawsTwice;
using stereo_draw_direct::EyePlan;

// An engine object (renderer, geometry, data, shader, partition, record, ...)
// as a 32-bit handle; 0 is null.
using Handle = std::uint32_t;
inline constexpr Handle kNull = 0;

// D3DPRIMITIVETYPE values the skinned arm builds: XOR EDX,EDX / CMP word
// [EAX+0x22],DX / SETNZ DL / ADD EDX,4 (0x00769590-0x00769599, read):
// D3DPT_TRIANGLELIST 4 and D3DPT_TRIANGLESTRIP 5 (d3d9types.h, per
// re/functions/0x00767520.md).
inline constexpr std::uint32_t kTriangleList = 4;
inline constexpr std::uint32_t kTriangleStrip = 5;

// AND word [data+0x2E],0xF000 (0x0076964E, 0x007696B8, 0x007696EA, read).
inline constexpr std::uint16_t kFlagsKept = 0xF000;

// DrawIndexedPrimitive's MinVertexIndex: PUSH 0x0 (0x00769619, 0x00769698, read).
inline constexpr std::uint32_t kMinVertexIndex = 0;

// The second argument 0x00768890 gets: PUSH 0x0 (0x0076952E, read) - "no
// shader", so the callee casts geometry+0xBC itself (map D6).
inline constexpr Handle kNoShaderArgument = 0;

// IDirect3DDevice9::DrawIndexedPrimitive (slot 82, +0x148) in the device's
// argument order (pushes read before 0x00769622 and 0x007696A6).
struct DrawIndexedCall {
	Handle device;                  // renderer +0x280, re-read per draw (0x007695FD, 0x0076968A)
	std::uint32_t primitiveType;    // skinned: 4 or 5 (C15); whole: record +0x38 (0x0076969B)
	std::uint32_t baseVertexIndex;  // record +0x34 (an INT in d3d9.h; passed as its bits)
	std::uint32_t minVertexIndex;   // kMinVertexIndex
	std::uint32_t numVertices;      // skinned: the partition's u16 +0x1C zero-extended (0x00769607); whole: record +0x14
	std::uint32_t startIndex;
	std::uint32_t primitiveCount;
};

// IDirect3DDevice9::DrawPrimitive (slot 81, +0x144), pushes read before
// 0x007696E4.
struct DrawCall {
	Handle device;                 // renderer +0x280 (0x007696CE)
	std::uint32_t primitiveType;   // record +0x38 (0x007696D8)
	std::uint32_t startVertex;     // record +0x34 (0x007696CB)
	std::uint32_t primitiveCount;  // record +0x3C (0x007696C8)
};

// ---- The decisions (pure; each names its branch in 0x007694F0.md) ----------------------

// B1 (CMP byte [EBP+0x6F0],0 at 0x007694F6 / JNZ 0x00769501): any non-zero
// byte returns before anything is read from the geometry.
constexpr bool DrawSuppressed(std::uint8_t suppressByte) {
	return suppressByte != 0;
}

// B2 (TEST AX,AX at 0x0076951D / JZ 0x00769520): only the low 16 bits of the
// data's slot +0x50 result count.
constexpr bool HasVertices(std::uint32_t vertexCountEax) {
	return (vertexCountEax & 0xFFFFu) != 0;
}

// B3 (MOV BL,AL at 0x00769551, TEST BL,BL at 0x0076957B / JZ): only AL of
// 0x00768890's result decides; non-zero is the skinned arm.
constexpr bool DrawsBySkinPartition(std::uint32_t isSkinnedEax) {
	return (isSkinnedEax & 0xFFu) != 0;
}

// C15 (0x00769590-0x00769599): partition 0's u16 +0x22 against 0; one type for
// every partition of the call.
constexpr std::uint32_t PrimitiveTypeFor(std::uint16_t stripCountOfPartition0) {
	return stripCountOfPartition0 != 0 ? kTriangleStrip : kTriangleList;
}

// B4 (TEST ECX,ECX / JBE 0x007695A2): unsigned "below or equal 0" is "equal 0".
constexpr bool PartitionLoopRuns(std::uint32_t partitionCount) {
	return partitionCount != 0;
}

// B5/B6 (CMP EAX,[EBP+0xA94] / JNZ 0x007695BD; MOVZX EAX,word [ESI] / CMP
// EAX,[EBP+0x6D8] / JBE 0x007695C8): only the default shader (by pointer
// identity) is bone-gated; the u16 bone count, zero-extended, against the
// dword limit, unsigned; equal passes.
constexpr bool SkippedByBoneGate(Handle shader, Handle defaultShader, std::uint16_t boneCount,
		std::uint32_t maxBonesWithoutShader) {
	return shader == defaultShader && static_cast<std::uint32_t>(boneCount) > maxBonesWithoutShader;
}

// B7 (CMP [EBX+0x44],EBP with EBP = 0 / JBE 0x007695E4) and B12 (CMP
// [EDI+0x44],ESI with ESI = 0 / JBE 0x0076966B): no array, no draw.
constexpr bool HasArrays(std::uint32_t arrayCount) {
	return arrayCount != 0;
}

// B8 (0x007695EB JZ) and B13 (0x00769675 JZ): with a lengths array the count
// is the u16 length zero-extended minus 2 in 32 bits (MOVZX / SUB 2 at
// 0x007695ED-0x007695F1 and 0x00769677-0x0076967B), so a length below 2 wraps
// to 0xFFFFFFFE or 0xFFFFFFFF and is passed on unchecked; without one it is
// the record's +0x3C.
constexpr std::uint32_t PrimitiveCount(bool hasLengths, std::uint16_t length, std::uint32_t triangleCount) {
	return hasLengths ? static_cast<std::uint32_t>(length) - 2u : triangleCount;
}

// C19 (LEA EDI,[EDI+ESI+2] at 0x0076962A; LEA EBP,[EBX+EBP+2] at 0x007696AE):
// the next array starts count + 2 indices later, modulo 2^32.
constexpr std::uint32_t NextStartIndex(std::uint32_t startIndex, std::uint32_t primitiveCount) {
	return startIndex + primitiveCount + 2u;
}

// B9 (CMP EBP,[EBX+0x44] / JC 0x0076962E) and B14 (CMP ESI,[EDI+0x44] / JC
// 0x007696B2): the incremented index against the re-read count, unsigned below.
constexpr bool ArrayLoopContinues(std::uint32_t nextIndex, std::uint32_t arrayCount) {
	return nextIndex < arrayCount;
}

// B10 (SUB dword [ESP+0x14],1 / JNZ 0x00769644): the partitions left after the
// decrement, 32-bit.
constexpr bool MorePartitions(std::uint32_t partitionsLeftAfterDecrement) {
	return partitionsLeftAfterDecrement != 0;
}

// B11 (CMP dword [EDI+0x30],0 / JZ 0x00769662): an index buffer means indexed
// draws; none means one DrawPrimitive.
constexpr bool IsIndexed(std::uint32_t indexBuffer) {
	return indexBuffer != 0;
}

// C20: the low 12 bits of the data's flags word cleared once, past B2.
constexpr std::uint16_t ClearedFlags(std::uint16_t flags) {
	return static_cast<std::uint16_t>(flags & kFlagsKept);
}

// ---- The ports -------------------------------------------------------------------------

// Each member is one engine read or call of 0x007694F0 (offsets and slots in
// NiDX9Renderer_DrawShapeDirect.h). Reads have no effect; every call member
// stands for exactly one call site of the original.
//   drawSuppressed()              byte [renderer+0x6F0]                    (0x007694F6)
//   geometryData(g)               [g+0xB4]                                 (0x0076950C, re-read 0x0076953C)
//   geometrySkin(g)               [g+0xB8]                                 (0x00769526)
//   geometryShaderCandidate(g)    [g+0xBC]                                 (0x00769542)
//   dataRecord(d)                 [d+0x38]                                 (0x00769548)
//   dataFlags(d)/setDataFlags(d,v) the u16 [d+0x2E]                        (0x0076964E, 0x007696B8, 0x007696EA)
//   defaultShader()               [renderer+0xA94], per partition          (0x007695B7)
//   maxBonesWithoutShader()       [renderer+0x6D8], per gated test          (0x007695C2)
//   device()                      [renderer+0x280], per draw               (0x007695FD, 0x0076968A, 0x007696CE)
//   skinPartition(s)              [s+0x0C]                                 (0x00769587)
//   partitionCount(sp)/partitions(sp)  [sp+0x08] / [sp+0x0C]               (0x0076958A, 0x0076958D)
//   partitionAt(array, i)         array + 0x2C*i                           (ADD ESI,0x2C 0x00769638)
//   stripCount/boneCount/vertexCount(p)  u16 +0x22 / +0x20 / +0x1C
//   partitionRecord(p)            [p+0x28]                                 (0x007695DA)
//   recordVertexCount/indexBuffer/baseVertexIndex/primitiveType/triangleCount/arrayCount/arrayLengths(r)
//                                 [r+0x14/+0x30/+0x34/+0x38/+0x3C/+0x44/+0x48]
//   arrayLength(lengths, i)       u16 lengths[i]                           (0x007695ED, 0x00769677)
//   dataVertexCount(d)            [[d]+0x50], ECX = d; the full EAX        (0x0076951B)
//   isSkinned(g, shader)          0x00768890, thiscall ECX = renderer; EAX (0x00769537)
//   castToShaderInterface(o)      0x00560920(NiRTTI 0x00B42858, o), cdecl  (0x00769553)
//   shaderSlot18(shader)          [[shader]+0x48], ECX = shader; result unused (0x00769566)
//   renderStateSubObjectSlot1()   [[[renderer+0x8AC]+0xFF0]]+0x04          (0x00769579)
//   cannotRenderPartition(n)      0x0060D0A0(0x00A88D90, n): a bare RET in this build (C8)
//   drawPartitionIndexed(call)    [[renderer+0x280]]+0x148, skinned arm    (0x00769622)
//   drawWholeIndexed(call)        [[renderer+0x280]]+0x148, whole arm      (0x007696A6)
//   drawPrimitive(call)           [[renderer+0x280]]+0x144                 (0x007696E4)
//   eyeSwitch(eye)                StereoDraw.h's port; never called with one eye
template <class P>
concept DrawShapeDirectPorts = requires(P& ports, Handle handle, std::uint32_t index, std::uint16_t flags,
		std::uint8_t eye, const DrawIndexedCall& indexed, const DrawCall& plain) {
	{ ports.drawSuppressed() } -> std::convertible_to<std::uint8_t>;
	{ ports.geometryData(handle) } -> std::convertible_to<Handle>;
	{ ports.geometrySkin(handle) } -> std::convertible_to<Handle>;
	{ ports.geometryShaderCandidate(handle) } -> std::convertible_to<Handle>;
	{ ports.dataRecord(handle) } -> std::convertible_to<Handle>;
	{ ports.dataFlags(handle) } -> std::convertible_to<std::uint16_t>;
	ports.setDataFlags(handle, flags);
	{ ports.defaultShader() } -> std::convertible_to<Handle>;
	{ ports.maxBonesWithoutShader() } -> std::convertible_to<std::uint32_t>;
	{ ports.device() } -> std::convertible_to<Handle>;
	{ ports.skinPartition(handle) } -> std::convertible_to<Handle>;
	{ ports.partitionCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.partitions(handle) } -> std::convertible_to<Handle>;
	{ ports.partitionAt(handle, index) } -> std::convertible_to<Handle>;
	{ ports.stripCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.boneCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.vertexCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.partitionRecord(handle) } -> std::convertible_to<Handle>;
	{ ports.recordVertexCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.indexBuffer(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.baseVertexIndex(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.primitiveType(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.triangleCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.arrayCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.arrayLengths(handle) } -> std::convertible_to<Handle>;
	{ ports.arrayLength(handle, index) } -> std::convertible_to<std::uint16_t>;
	{ ports.dataVertexCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.isSkinned(handle, handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.castToShaderInterface(handle) } -> std::convertible_to<Handle>;
	ports.shaderSlot18(handle);
	ports.renderStateSubObjectSlot1();
	ports.cannotRenderPartition(index);
	ports.drawPartitionIndexed(indexed);
	ports.drawWholeIndexed(indexed);
	ports.drawPrimitive(plain);
	ports.eyeSwitch(eye);
};

// How a call ended: one value per exit of the original (the rebuild's own
// report; the original returns nothing: EAX at its exits is the data pointer,
// or the caller's EAX after B1, or the slot +0x50 result after B2 (read, map
// "Calling convention"), and its caller 0x007176D0 does not read it (objdump
// 0x00717706-0x0071770A, per the map)). The four drawing exits clear the
// flags at three different sites (NiDX9Renderer_DrawShapeDirect.h maps them).
enum class Outcome : std::uint8_t {
	kSuppressed,          // B1 -> 0x007696F3: nothing read from the geometry, no call, no write
	kNoVertices,          // B2 -> 0x007696F2: only the vertex count call; no flags clear
	kSkinnedNoPartitions, // B4 taken (JBE 0x007696E6) -> flags clear 0x007696EA
	kSkinned,             // B10 not taken -> flags clear 0x0076964E
	kWholeDrawPrimitive,  // B11 taken: one DrawPrimitive -> flags clear 0x007696EA
	kWholeNoArrays,       // B12 taken: no draw -> flags clear 0x007696EA
	kWholeIndexed,        // B14 not taken: DrawIndexedPrimitive per array -> flags clear 0x007696B8
};

// The skinned arm's draws of one partition for one eye: one
// DrawIndexedPrimitive per array of the partition's record (B7..B9), the start
// index from 0 (XOR EDI,EDI 0x007695DF) growing by count + 2 (C19). The
// record's +0x44 and +0x48 are re-read per array, as the original does
// (0x00769627, 0x007695E6).
template <DrawShapeDirectPorts P>
void DrawPartitionArrays(P& ports, std::uint32_t primitiveType, Handle partition, Handle record) {
	if (!HasArrays(ports.arrayCount(record))) {  // B7
		return;
	}
	std::uint32_t startIndex = 0;
	std::uint32_t index = 0;
	do {
		const Handle lengths = ports.arrayLengths(record);
		const bool hasLengths = lengths != kNull;  // B8
		const std::uint32_t primitiveCount = PrimitiveCount(hasLengths,
				hasLengths ? ports.arrayLength(lengths, index) : std::uint16_t{0},
				hasLengths ? 0u : ports.triangleCount(record));
		const Handle device = ports.device();
		const std::uint32_t numVertices = ports.vertexCount(partition);
		const std::uint32_t baseVertexIndex = ports.baseVertexIndex(record);
		ports.drawPartitionIndexed(DrawIndexedCall{device, primitiveType, baseVertexIndex, kMinVertexIndex,
				numVertices, startIndex, primitiveCount});
		startIndex = NextStartIndex(startIndex, primitiveCount);
		++index;
	} while (ArrayLoopContinues(index, ports.arrayCount(record)));  // B9
}

// The whole arm's draw(s) for one eye: B11..B14. Returns which of the two
// draw kinds B11 chose.
template <DrawShapeDirectPorts P>
Outcome DrawWhole(P& ports, Handle record) {
	if (!IsIndexed(ports.indexBuffer(record))) {  // B11
		const std::uint32_t primitiveCount = ports.triangleCount(record);
		const std::uint32_t startVertex = ports.baseVertexIndex(record);
		const Handle device = ports.device();
		ports.drawPrimitive(DrawCall{device, ports.primitiveType(record), startVertex, primitiveCount});
		return Outcome::kWholeDrawPrimitive;
	}
	if (!HasArrays(ports.arrayCount(record))) {  // B12
		return Outcome::kWholeNoArrays;
	}
	std::uint32_t startIndex = 0;  // XOR EBP,EBP 0x00769666
	std::uint32_t index = 0;
	do {
		const Handle lengths = ports.arrayLengths(record);
		const bool hasLengths = lengths != kNull;  // B13
		const std::uint32_t primitiveCount = PrimitiveCount(hasLengths,
				hasLengths ? ports.arrayLength(lengths, index) : std::uint16_t{0},
				hasLengths ? 0u : ports.triangleCount(record));
		const std::uint32_t numVertices = ports.recordVertexCount(record);
		const Handle device = ports.device();
		const std::uint32_t baseVertexIndex = ports.baseVertexIndex(record);
		ports.drawWholeIndexed(DrawIndexedCall{device, ports.primitiveType(record), baseVertexIndex,
				kMinVertexIndex, numVertices, startIndex, primitiveCount});
		startIndex = NextStartIndex(startIndex, primitiveCount);
		++index;
	} while (ArrayLoopContinues(index, ports.arrayCount(record)));  // B14
	return Outcome::kWholeIndexed;
}

// 0x007694F0 over ports; `geometry` is the one stack argument (a1). With
// plan.eyes == 1 the port calls are the original's, one for one (the 1:1
// gate; eyeSwitch is never called). With plan.eyes == 2 (StereoDrawDirect.h):
// the whole arm's draw(s) run a second time between eyeSwitch(1) and
// eyeSwitch(0) before the flags clear; the skinned arm does the same per
// partition that passes the bone gate. The second run re-reads the record and
// the device the way the first did. It runs for a record without arrays too
// (B7/B12; the contract names no exception, as in DrawSkinnedPrimitive and
// DrawPrimitive) - the switch then frames no draw; a partition the bone gate
// skips draws nothing in either eye and gets no switch.
template <DrawShapeDirectPorts P>
Outcome DrawShapeDirect(P& ports, Handle geometry, EyePlan plan) {
	if (DrawSuppressed(ports.drawSuppressed())) {  // B1
		return Outcome::kSuppressed;
	}
	const Handle data = ports.geometryData(geometry);  // 0x0076950C, kept for the flags clear (C14)
	if (!HasVertices(ports.dataVertexCount(data))) {  // B2
		return Outcome::kNoVertices;
	}
	const Handle skin = ports.geometrySkin(geometry);  // 0x00769526, into the a1 slot (C21)
	const bool skinned = DrawsBySkinPartition(ports.isSkinned(geometry, kNoShaderArgument));
	// 0x0076953C-0x00769548: geometry+0xB4 re-read, +0xBC and the record read
	// before the cast (C13, C14).
	const Handle dataAfterDecision = ports.geometryData(geometry);
	const Handle candidate = ports.geometryShaderCandidate(geometry);
	const Handle wholeRecord = ports.dataRecord(dataAfterDecision);
	const Handle shader = ports.castToShaderInterface(candidate);
	ports.shaderSlot18(shader);  // no null test (C3), result unused (C5)
	ports.renderStateSubObjectSlot1();  // result unused (C5)

	Outcome outcome = Outcome::kSkinnedNoPartitions;
	if (skinned) {  // B3
		const Handle skinPartition = ports.skinPartition(skin);
		const std::uint32_t partitionCount = ports.partitionCount(skinPartition);
		const Handle partitions = ports.partitions(skinPartition);
		// Read before the count test (C15): partition 0 is read with a count of 0.
		const std::uint32_t primitiveType = PrimitiveTypeFor(ports.stripCount(ports.partitionAt(partitions, 0)));
		if (PartitionLoopRuns(partitionCount)) {  // B4
			outcome = Outcome::kSkinned;
			std::uint32_t partitionsLeft = partitionCount;
			std::uint32_t index = 0;
			do {
				const Handle partition = ports.partitionAt(partitions, index);
				const Handle defaultShader = ports.defaultShader();
				bool skipped = false;
				if (shader == defaultShader) {  // B5
					const std::uint16_t bones = ports.boneCount(partition);
					if (SkippedByBoneGate(shader, defaultShader, bones, ports.maxBonesWithoutShader())) {  // B6
						ports.cannotRenderPartition(bones);
						skipped = true;
					}
				}
				if (!skipped) {
					const Handle record = ports.partitionRecord(partition);
					DrawPartitionArrays(ports, primitiveType, partition, record);
					if (DrawsTwice(plan)) {
						ports.eyeSwitch(1);
						DrawPartitionArrays(ports, primitiveType, partition, record);
						ports.eyeSwitch(0);
					}
				}
				++index;
				--partitionsLeft;
			} while (MorePartitions(partitionsLeft));  // B10
		}
	} else {
		outcome = DrawWhole(ports, wholeRecord);
		if (DrawsTwice(plan)) {
			ports.eyeSwitch(1);
			DrawWhole(ports, wholeRecord);
			ports.eyeSwitch(0);
		}
	}
	ports.setDataFlags(data, ClearedFlags(ports.dataFlags(data)));  // C20
	return outcome;
}

} // namespace obng::engine::render::draw_shape_direct
