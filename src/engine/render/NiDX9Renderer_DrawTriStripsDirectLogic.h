#pragma once

// NiDX9Renderer::DrawTriStripsDirect 0x00769700 (proposed name; NiDX9Renderer
// vtable 0x00A88EA4 slot 91, the class by exe scan and the method by the
// NiTriStrips caller pairing, both unverified - re/functions/0x00769700.md
// "Identity") rebuilt over plain values and ports, with the stereo contract of
// StereoDrawDirect.h.
//
// The original draws one geometry with the pipeline its caller left on the
// device: gate byte renderer+0x6F0 (B1), the data's vertex count through data
// slot 20 (B2), the world bound read, skinned-or-whole from 0x00768890, the
// record read, the shader cast and its slot 18, the bound stored to
// renderer+0x658, the render state's sub-object slot 1, then the device draws
// - per partition (bone gate, one DrawIndexedPrimitive per array) or for the
// whole record (DrawPrimitive, or one DrawIndexedPrimitive per array) - and
// the data's flags clear. DrawTriStripsDirect below makes the same port calls,
// in the same order, with the same arguments; every port read happens where
// the original reads that memory, so a callee that changes it is seen at the
// same point (C6: the bound is read before 0x00768890 and stored after it; C9:
// geometry+0xB4 is read again after 0x00768890 for the record, while the flags
// clear uses the first read).
//
// Faults are not refused here: the original dereferences the cast result
// (C7, 0x00769780), the record (0x007698B3), the skin (0x007697D6) and the
// partition records (0x0076982A) without a null test, so the rebuild hands
// the same handles to the same ports; an engine port faults where the
// original does. No branch is added that the original does not have.
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts (which said), unverified = not checked.
// Names are proposals; the address stays next to each.
//
// No x87, SSE or other floating-point instruction is in the body (read:
// asm.s 0x00769700-0x00769953 holds only integer instructions); the world
// bound travels as four dwords in EBX and stack slots (MOV, 0x0076973F-
// 0x007697B4), so its bits pass unchanged, NaN payloads included. The x87
// precision control touches no result of this function (derived from the
// two read facts).
//
// 64-bit clean: engine objects are std::uint32_t handles the ports resolve;
// no pointer is assumed to be four bytes here. The engine shapes (offsets,
// vtable slots, calling conventions) are in NiDX9Renderer_DrawTriStripsDirect.h.

#include "engine/render/StereoDrawDirect.h"

#include <concepts>
#include <cstdint>

namespace obng::engine::render::draw_tri_strips_direct {

// An engine object (renderer, geometry, data, shader, partition, record, ...)
// as a 32-bit handle; 0 is null.
using Handle = std::uint32_t;
inline constexpr Handle kNull = 0;

// D3DPRIMITIVETYPE values (d3d9types.h; the map's C10): D3DPT_TRIANGLELIST 4,
// D3DPT_TRIANGLESTRIP 5, built as SETNZ DL / ADD EDX,4 (0x007697E5, 0x007697E8).
inline constexpr std::uint32_t kTriangleList = 4;
inline constexpr std::uint32_t kTriangleStrip = 5;

// AND word [data+0x2E],0xF000 (0x007698A3, 0x00769914, 0x00769946; read).
inline constexpr std::uint16_t kFlagsKept = 0xF000;

// DrawIndexedPrimitive's MinVertexIndex: PUSH 0x0 (0x00769866, 0x007698F2; read).
inline constexpr std::uint32_t kMinVertexIndex = 0;

// The world bound: four dwords, geometry +0x20..+0x2C -> renderer +0x658..+0x664.
inline constexpr std::uint32_t kWorldBoundWords = 4;

// ECX (the renderer, MOV EBP,ECX 0x00769704) and the one stack argument
// (the geometry, [ESP0+4] read at 0x00769714; RET 0x4).
struct DrawArguments {
	Handle renderer = kNull;
	Handle geometry = kNull;
};

// IDirect3DDevice9::DrawIndexedPrimitive (slot 82, +0x148) in the device's
// argument order (pushes read at 0x00769860-0x0076986E and 0x007698EC-0x007698F9).
struct DrawIndexedCall {
	Handle device;                  // renderer +0x280, re-read per draw (0x00769852, 0x007698DE)
	std::uint32_t primitiveType;    // skinned: PrimitiveTypeFor (C10); whole: record +0x38
	std::uint32_t baseVertexIndex;  // record +0x34 (an INT in d3d9.h; passed as its bits)
	std::uint32_t minVertexIndex;   // kMinVertexIndex
	std::uint32_t numVertices;      // skinned: the partition's u16 +0x1C zero-extended; whole: record +0x14
	std::uint32_t startIndex;
	std::uint32_t primitiveCount;
};

// IDirect3DDevice9::DrawPrimitive (slot 81, +0x144), pushes 0x00769933-0x0076993F.
struct DrawCall {
	Handle device;                // renderer +0x280 (0x0076992A)
	std::uint32_t primitiveType;  // record +0x38
	std::uint32_t startVertex;    // record +0x34
	std::uint32_t primitiveCount; // record +0x3C
};

// How a call ended: the original's five exits (read; the map's "Exits").
enum class Outcome : std::uint8_t {
	kSuppressed,         // B1, 0x0076994F: no read of the geometry, no call, no write
	kNoVertices,         // B2, 0x0076994E: after data slot 20 only
	kPartitionLoopExit,  // flags clear at 0x007698A3: skinned arm, >= 1 partition
	kArrayLoopExit,      // flags clear at 0x00769914: whole indexed arm, >= 1 array
	kSharedDrawExit,     // flags clear at 0x00769946: DrawPrimitive, B4 taken, or B10 taken
};

// What the rebuild reports: the exit, and what the original leaves in EAX
// (it returns nothing; the caller 0x00719BA0 ignoring EAX is unverified).
struct Result {
	Outcome outcome;
	std::uint32_t vertexCountResult;  // data slot 20's EAX (0 when B1 returned first)
	Handle data;                      // geometry+0xB4 as first read (0x00769718; 0 when B1 returned first)
};

// ---- The decisions (pure; each names its branch in 0x00769700.md) -----------------------

// B1 (CMP byte [EBP+0x6F0],0 / JNZ 0x0076970D).
constexpr bool DrawsSuppressed(std::uint8_t skipGeometryDraws) {
	return skipGeometryDraws != 0;
}

// B2 (TEST AX,AX / JZ 0x0076972C): only the low word of slot 20's EAX.
constexpr bool HasVertices(std::uint32_t slot20Eax) {
	return (slot20Eax & 0xFFFFu) != 0;
}

// B3: AL of 0x00768890's result is stored as a byte (MOV byte [ESP+0x40],AL
// 0x00769775) and tested (CMP byte 0x007697C7 / JZ 0x007697CC).
constexpr bool UsesPartitions(std::uint32_t selectsEax) {
	return (selectsEax & 0xFFu) != 0;
}

// C10 (0x007697DF-0x007697E8): partition 0's u16 +0x22 against 0, one type
// for every partition of the call.
constexpr std::uint32_t PrimitiveTypeFor(std::uint16_t stripCountOfPartition0) {
	return stripCountOfPartition0 != 0 ? kTriangleStrip : kTriangleList;
}

// B4 (TEST ECX,ECX / JBE 0x007697F1): unsigned "below or equal 0" is "equal 0".
constexpr bool PartitionLoopRuns(std::uint32_t partitionCount) {
	return partitionCount != 0;
}

// B5 (CMP ESI,[EBP+0xA94] / JNZ 0x00769808): only the default shader, by
// identity, is bone-gated; the bone count is read only then.
constexpr bool IsBoneGated(Handle shader, Handle defaultShader) {
	return shader == defaultShader;
}

// B6 (MOVZX EAX,word [EDX] / CMP EAX,[EBP+0x6D8] / JBE 0x00769813): the u16
// count zero-extended against the dword limit, unsigned; equal passes.
constexpr bool ExceedsBoneLimit(std::uint16_t boneCount, std::uint32_t maxBonesWithoutShader) {
	return static_cast<std::uint32_t>(boneCount) > maxBonesWithoutShader;
}

// B7 (CMP [EBX+0x44],EDI=0 / JBE 0x00769831) and B10 (CMP [EDI+0x44],EBX=0 /
// JBE 0x007698C2).
constexpr bool HasArrays(std::uint32_t arrayCount) {
	return arrayCount != 0;
}

// B8a/B11a (0x00769838, 0x007698C9 JZ): with a lengths array the count is the
// u16 length zero-extended minus 2 in 32 bits (MOVZX ESI / SUB ESI,2), so a
// length below 2 wraps and is passed on unchecked; without one the record's +0x3C.
constexpr std::uint32_t PrimitiveCount(bool hasLengths, std::uint16_t length, std::uint32_t triangleCount) {
	return hasLengths ? static_cast<std::uint32_t>(length) - 2u : triangleCount;
}

// C14 (LEA EDI,[EDI+ESI+2] 0x0076987B; LEA EBX,[EBX+ESI+2] 0x00769906),
// modulo 2^32.
constexpr std::uint32_t NextStartIndex(std::uint32_t startIndex, std::uint32_t primitiveCount) {
	return startIndex + primitiveCount + 2u;
}

// B8b/B11b (ADD EAX,1 / CMP EAX,[+0x44] / JC 0x00769883, 0x0076990E): the
// incremented index against the re-read count, unsigned below.
constexpr bool LoopContinues(std::uint32_t nextIndex, std::uint32_t arrayCount) {
	return nextIndex < arrayCount;
}

// B8c (SUB dword [ESP+0x14],1 / JNZ 0x00769899): the partition countdown.
constexpr std::uint32_t NextCountdown(std::uint32_t remaining) {
	return remaining - 1u;
}

// B9 (CMP dword [EDI+0x30],0 / JZ 0x007698B7).
constexpr bool IsIndexed(std::uint32_t indexBuffer) {
	return indexBuffer != 0;
}

// Step 9: the low 12 bits of the data's flags word cleared.
constexpr std::uint16_t ClearedFlags(std::uint16_t flags) {
	return static_cast<std::uint16_t>(flags & kFlagsKept);
}

// EAX at the original's exit (read): B1 jumps past every call to 0x0076994F,
// so EAX is the caller's; B2 leaves slot 20's EAX (no write between
// 0x00769727 and 0x0076994E); every draw exit loads the data from [ESP+0x20]
// into EAX for the flags clear (0x0076989F, 0x00769910, 0x00769942).
constexpr std::uint32_t ExitEax(const Result& result, std::uint32_t incomingEax) {
	switch (result.outcome) {
	case Outcome::kSuppressed: return incomingEax;
	case Outcome::kNoVertices: return result.vertexCountResult;
	default: return result.data;
	}
}

// ---- The ports -------------------------------------------------------------------------

// Each member is one engine read or call of 0x00769700 (offsets and slots in
// NiDX9Renderer_DrawTriStripsDirect.h). Reads have no effect; every call
// member stands for exactly one call site of the original.
//   skipsGeometryDraws()          byte [renderer+0x6F0]                   (0x00769706)
//   geometryData(g)               [g+0xB4]                                (0x00769718, again 0x00769760)
//   skinInstance(g)               [g+0xB8]                                (0x00769732)
//   worldBoundWord(g, k)          [g+0x20+4k]                             (0x0076973F, 0x00769738, 0x0076973B, 0x00769747)
//   shaderCandidate(g)            [g+0xBC]                                (0x00769766)
//   bufferRecord(d)               [d+0x38]                                (0x0076976C)
//   skinPartition(s)              [s+0x0C]                                (0x007697D6)
//   partitionCount(sp)/partitions(sp)  [sp+0x08] / [sp+0x0C]              (0x007697D9, 0x007697DC)
//   partitionAt(array, i)         array + 0x2C*i                          (ADD EDX,0x2C 0x0076988D)
//   stripCount/boneCount/vertexCount(p)  u16 +0x22 / +0x20 / +0x1C        (0x007697E1, 0x0076980A, 0x0076984E)
//   bufferData(p)                 [p+0x28]                                (0x00769825)
//   defaultShader()               [renderer+0xA94], per partition         (0x00769802)
//   maxBonesWithoutShader()       [renderer+0x6D8], per gated partition   (0x0076980D)
//   device()                      [renderer+0x280], per draw              (0x00769852, 0x007698DE, 0x0076992A)
//   indexBuffer/recordVertexCount/baseVertexIndex/primitiveType/triangleCount/arrayCount/arrayLengths(r)
//                                 [r+0x30/+0x14/+0x34/+0x38/+0x3C/+0x44/+0x48]
//   arrayLength(lengths, i)       u16 lengths[i]                          (0x0076983E, 0x007698CF)
//   dataFlags(d)/setDataFlags(d, v, exit)  the u16 [d+0x2E]; the exit names the AND's site
//   setCurrentWorldBoundWord(k,v) [renderer+0x658+4k] = v                 (0x0076979C..0x007697B4)
//   geometryDataSlot20(d)         [[d]+0x50], ECX = d, EAX returned       (0x00769727)
//   selectsPartitions(g)          0x00768890(g, 0), ECX = renderer, EAX returned (0x0076975B)
//   castToShaderInterface(o)      0x00560920(NiRTTI 0x00B42858, o)        (0x00769779)
//   shaderSlot18(shader)          [[shader]+0x48], ECX = shader; result ignored (C8) (0x0076978E)
//   renderStateSubObjectSlot1()   [[[renderer+0x8AC]+0xFF0]]+0x04         (0x007697C5)
//   cannotRenderPartition(n)      0x0060D0A0(0x00A88D90, n)               (0x0076981B)
//   drawIndexedPrimitiveForPartition(call)  [[renderer+0x280]]+0x148     (0x0076986F)
//   drawIndexedPrimitiveForRecord(call)     [[renderer+0x280]]+0x148     (0x007698FA)
//   drawPrimitive(call)           [[renderer+0x280]]+0x144                (0x00769940)
//   eyeSwitch(eye)                StereoDraw.h's port; never called with one eye
template <class P>
concept DrawTriStripsDirectPorts = requires(P& ports, Handle handle, std::uint32_t index, std::uint32_t word,
		std::uint16_t flags, std::uint8_t eye, Outcome exit, const DrawIndexedCall& indexed, const DrawCall& plain) {
	{ ports.skipsGeometryDraws() } -> std::convertible_to<std::uint8_t>;
	{ ports.geometryData(handle) } -> std::convertible_to<Handle>;
	{ ports.skinInstance(handle) } -> std::convertible_to<Handle>;
	{ ports.worldBoundWord(handle, index) } -> std::convertible_to<std::uint32_t>;
	{ ports.shaderCandidate(handle) } -> std::convertible_to<Handle>;
	{ ports.bufferRecord(handle) } -> std::convertible_to<Handle>;
	{ ports.skinPartition(handle) } -> std::convertible_to<Handle>;
	{ ports.partitionCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.partitions(handle) } -> std::convertible_to<Handle>;
	{ ports.partitionAt(handle, index) } -> std::convertible_to<Handle>;
	{ ports.stripCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.boneCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.vertexCount(handle) } -> std::convertible_to<std::uint16_t>;
	{ ports.bufferData(handle) } -> std::convertible_to<Handle>;
	{ ports.defaultShader() } -> std::convertible_to<Handle>;
	{ ports.maxBonesWithoutShader() } -> std::convertible_to<std::uint32_t>;
	{ ports.device() } -> std::convertible_to<Handle>;
	{ ports.indexBuffer(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.recordVertexCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.baseVertexIndex(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.primitiveType(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.triangleCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.arrayCount(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.arrayLengths(handle) } -> std::convertible_to<Handle>;
	{ ports.arrayLength(handle, index) } -> std::convertible_to<std::uint16_t>;
	{ ports.dataFlags(handle) } -> std::convertible_to<std::uint16_t>;
	ports.setDataFlags(handle, flags, exit);
	ports.setCurrentWorldBoundWord(index, word);
	{ ports.geometryDataSlot20(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.selectsPartitions(handle) } -> std::convertible_to<std::uint32_t>;
	{ ports.castToShaderInterface(handle) } -> std::convertible_to<Handle>;
	ports.shaderSlot18(handle);
	ports.renderStateSubObjectSlot1();
	ports.cannotRenderPartition(word);
	ports.drawIndexedPrimitiveForPartition(indexed);
	ports.drawIndexedPrimitiveForRecord(indexed);
	ports.drawPrimitive(plain);
	ports.eyeSwitch(eye);
};

// The draws of one partition for one eye (B7, B8a, B8b): the start index from
// 0 (XOR EDI,EDI 0x00769828), one DrawIndexedPrimitive per array of the
// partition's record, growing by count + 2. The record's +0x44 and +0x48 are
// re-read per array, the device per draw, as the original does.
template <DrawTriStripsDirectPorts P>
void DrawPartitionArrays(P& ports, std::uint32_t primitiveType, Handle partition, Handle record) {
	if (!HasArrays(ports.arrayCount(record))) {  // B7
		return;
	}
	std::uint32_t startIndex = 0;
	std::uint32_t index = 0;
	do {
		const Handle lengths = ports.arrayLengths(record);  // 0x00769833
		const bool hasLengths = lengths != kNull;            // B8a
		const std::uint32_t primitiveCount = PrimitiveCount(hasLengths,
				hasLengths ? ports.arrayLength(lengths, index) : std::uint16_t{0},
				hasLengths ? 0u : ports.triangleCount(record));
		const std::uint32_t numVertices = ports.vertexCount(partition);  // 0x0076984E
		const Handle device = ports.device();                             // 0x00769852
		const std::uint32_t baseVertexIndex = ports.baseVertexIndex(record);  // 0x00769863
		ports.drawIndexedPrimitiveForPartition(DrawIndexedCall{device, primitiveType, baseVertexIndex, kMinVertexIndex,
				numVertices, startIndex, primitiveCount});
		startIndex = NextStartIndex(startIndex, primitiveCount);
		++index;
	} while (LoopContinues(index, ports.arrayCount(record)));  // B8b
}

// The skinned arm (B4..B8c). Every partition the bone gate lets through is one
// stereo unit: its draws, and with two eyes eyeSwitch(1), the draws again,
// eyeSwitch(0) - also for a partition without arrays (the contract names no
// exception; the switch then frames no draw, as in DrawSkinnedPrimitive).
template <DrawTriStripsDirectPorts P>
Outcome DrawPartitions(P& ports, Handle skin, Handle shader, stereo_draw_direct::EyePlan plan) {
	const Handle skinPartition = ports.skinPartition(skin);
	const std::uint32_t partitionCount = ports.partitionCount(skinPartition);
	const Handle partitions = ports.partitions(skinPartition);
	// C10: partition 0's +0x22 is read before the count test.
	const std::uint32_t primitiveType = PrimitiveTypeFor(ports.stripCount(ports.partitionAt(partitions, 0)));
	if (!PartitionLoopRuns(partitionCount)) {  // B4
		return Outcome::kSharedDrawExit;
	}
	std::uint32_t remaining = partitionCount;
	std::uint32_t index = 0;
	do {
		const Handle partition = ports.partitionAt(partitions, index);
		bool skipped = false;
		if (IsBoneGated(shader, ports.defaultShader())) {  // B5
			const std::uint16_t bones = ports.boneCount(partition);
			if (ExceedsBoneLimit(bones, ports.maxBonesWithoutShader())) {  // B6
				ports.cannotRenderPartition(bones);
				skipped = true;
			}
		}
		if (!skipped) {
			const Handle record = ports.bufferData(partition);
			DrawPartitionArrays(ports, primitiveType, partition, record);
			if (stereo_draw_direct::DrawsTwice(plan)) {
				ports.eyeSwitch(1);
				DrawPartitionArrays(ports, primitiveType, partition, record);
				ports.eyeSwitch(0);
			}
		}
		++index;
		remaining = NextCountdown(remaining);
	} while (remaining != 0);  // B8c
	return Outcome::kPartitionLoopExit;
}

// The whole arm for one eye (B9..B11b): DrawPrimitive without an index buffer,
// else one DrawIndexedPrimitive per array of the record. Returns the exit.
template <DrawTriStripsDirectPorts P>
Outcome DrawWholeOnce(P& ports, Handle record) {
	if (!IsIndexed(ports.indexBuffer(record))) {  // B9
		const std::uint32_t primitiveCount = ports.triangleCount(record);  // 0x00769924
		const std::uint32_t startVertex = ports.baseVertexIndex(record);   // 0x00769927
		const Handle device = ports.device();                              // 0x0076992A
		const std::uint32_t primitiveType = ports.primitiveType(record);   // 0x00769934
		ports.drawPrimitive(DrawCall{device, primitiveType, startVertex, primitiveCount});
		return Outcome::kSharedDrawExit;
	}
	if (!HasArrays(ports.arrayCount(record))) {  // B10
		return Outcome::kSharedDrawExit;
	}
	std::uint32_t startIndex = 0;
	std::uint32_t index = 0;
	do {
		const Handle lengths = ports.arrayLengths(record);  // 0x007698C4
		const bool hasLengths = lengths != kNull;            // B11a
		const std::uint32_t primitiveCount = PrimitiveCount(hasLengths,
				hasLengths ? ports.arrayLength(lengths, index) : std::uint16_t{0},
				hasLengths ? 0u : ports.triangleCount(record));
		const std::uint32_t numVertices = ports.recordVertexCount(record);    // 0x007698DB
		const Handle device = ports.device();                                 // 0x007698DE
		const std::uint32_t baseVertexIndex = ports.baseVertexIndex(record);  // 0x007698EF
		const std::uint32_t primitiveType = ports.primitiveType(record);      // 0x007698F5
		ports.drawIndexedPrimitiveForRecord(DrawIndexedCall{device, primitiveType, baseVertexIndex, kMinVertexIndex,
				numVertices, startIndex, primitiveCount});
		startIndex = NextStartIndex(startIndex, primitiveCount);
		++index;
	} while (LoopContinues(index, ports.arrayCount(record)));  // B11b
	return Outcome::kArrayLoopExit;
}

// 0x00769700 over ports. With plan.eyes == 1 the port calls are the
// original's, one for one (the 1:1 gate; eyeSwitch is never called). With
// plan.eyes == 2 each unit's draws (the whole record; each partition the bone
// gate lets through) run a second time between eyeSwitch(1) and eyeSwitch(0),
// right after the first eye's and before the flags clear (StereoDrawDirect.h);
// the second run re-reads the record and the device the way the first did.
// The bound store, slot 18 and the sub-object call stay single.
template <DrawTriStripsDirectPorts P>
Result DrawTriStripsDirect(P& ports, const DrawArguments& args, stereo_draw_direct::EyePlan plan) {
	if (DrawsSuppressed(ports.skipsGeometryDraws())) {  // B1
		return Result{Outcome::kSuppressed, 0, kNull};
	}
	const Handle data = ports.geometryData(args.geometry);  // 0x00769718, kept at [ESP0-0x14]
	const std::uint32_t vertexCountResult = ports.geometryDataSlot20(data);
	if (!HasVertices(vertexCountResult)) {  // B2
		return Result{Outcome::kNoVertices, vertexCountResult, data};
	}
	const Handle skin = ports.skinInstance(args.geometry);  // 0x00769732
	std::uint32_t bound[kWorldBoundWords];                    // C5/C6: read before 0x00768890
	for (std::uint32_t word = 0; word < kWorldBoundWords; ++word) {
		bound[word] = ports.worldBoundWord(args.geometry, word);
	}
	const bool usePartitions = UsesPartitions(ports.selectsPartitions(args.geometry));  // B3's byte
	const Handle record = ports.bufferRecord(ports.geometryData(args.geometry));         // C9: the second read
	const Handle shader = ports.castToShaderInterface(ports.shaderCandidate(args.geometry));
	ports.shaderSlot18(shader);  // C7: no null test; C8: result ignored
	for (std::uint32_t word = 0; word < kWorldBoundWords; ++word) {
		ports.setCurrentWorldBoundWord(word, bound[word]);
	}
	ports.renderStateSubObjectSlot1();

	Outcome exit;
	if (usePartitions) {  // B3
		exit = DrawPartitions(ports, skin, shader, plan);
	} else {
		exit = DrawWholeOnce(ports, record);
		if (stereo_draw_direct::DrawsTwice(plan)) {
			ports.eyeSwitch(1);
			DrawWholeOnce(ports, record);
			ports.eyeSwitch(0);
		}
	}
	ports.setDataFlags(data, ClearedFlags(ports.dataFlags(data)), exit);
	return Result{exit, vertexCountResult, data};
}

} // namespace obng::engine::render::draw_tri_strips_direct
