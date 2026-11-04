#pragma once

// The engine-facing shape of NiDX9Renderer::DrawTriStripsDirect 0x00769700
// (proposed name, re/functions/0x00769700.md): the calling convention, the
// fields and vtable slots the body reads, and the call sites the ports stand
// for. The logic is NiDX9Renderer_DrawTriStripsDirectLogic.h; the stereo
// contract is StereoDrawDirect.h / StereoDraw.h (included through the logic,
// not redefined).
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts, unverified = not checked. The views
// below hold only what this body touches; pointer fields are std::uint32_t
// (the binary's 32-bit addresses) so the layout holds on any host, and the
// function types are the 32-bit binary's.

#include "engine/render/NiDX9Renderer_DrawTriStripsDirectLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::draw_tri_strips_direct::engine {

// ---- The function and its callees ------------------------------------------------------

// The function being replaced: __thiscall, ECX = renderer (MOV EBP,ECX at
// 0x00769704), one stack argument (the geometry), callee-cleaned (RET 0x4 at
// 0x007698B0, 0x00769921, 0x00769953), no return value (read). Two sources for
// the address: the export's identity (meta.json) and the .rdata dword at
// 0x00A89010 = vtable 0x00A88EA4 + 0x16C (exe scan, map C1 - unverified in
// the export, so the vtable slot is a proposal).
inline constexpr std::uint32_t kDrawTriStripsDirectAddress = 0x00769700;
inline constexpr std::uint32_t kRendererVtableSlot91Offset = 0x16C;

// 0x00768890: __thiscall(renderer), (geometry, 0), RET 0x8, result in AL
// (read: ECX = EBP at 0x00769751, no ADD ESP after the CALL at 0x0076975B;
// RET 8 by objdump in the map, C16). Two code paths call it the same way:
// here and slot 90's 0x00769537 (re/export/draw-entries/0x007694F0/asm.s).
inline constexpr std::uint32_t kSelectsPartitionsAddress = 0x00768890;

// The RTTI cast, cdecl (rtti, object) -> object or 0, ADD ESP,8 after the call
// (0x00769785, read). NiRTTI_NiD3DShaderInterface is the immediate pushed at
// 0x00769770 (read) and named in data.json and by xOBSE (NiRTTI_1_2_416.inl
// via re/symbols/xobse.csv, as NiDX9Renderer_DrawSkinnedPrimitive.h has it).
inline constexpr std::uint32_t kRttiCastAddress = 0x00560920;
inline constexpr std::uint32_t kNiRttiNiD3DShaderInterface = 0x00B42858;

// The diagnostic, cdecl (format, boneCount), ADD ESP,8 after (0x00769820,
// read). That its body is a single RET in this build is unverified here (map
// C13; NiDX9Renderer_DrawSkinnedPrimitive.h has it read by its map). The
// format string at 0x00A88D90 (data.json), pushed at 0x00769816; slot 90
// pushes the same pair (0x007695CB / 0x007695D0).
inline constexpr std::uint32_t kDebugPrintAddress = 0x0060D0A0;
inline constexpr std::uint32_t kCannotRenderPartitionFormat = 0x00A88D90;

// The calls' return addresses (the instruction after each CALL, read in
// asm.s): what the emulator's trace shows as "from".
inline constexpr std::uint32_t kAfterDataSlot20 = 0x00769729;
inline constexpr std::uint32_t kAfterSelectsPartitions = 0x00769760;
inline constexpr std::uint32_t kAfterRttiCast = 0x0076977E;
inline constexpr std::uint32_t kAfterShaderSlot18 = 0x00769790;
inline constexpr std::uint32_t kAfterSubObjectSlot1 = 0x007697C7;
inline constexpr std::uint32_t kAfterDebugPrint = 0x00769820;
inline constexpr std::uint32_t kAfterPartitionDrawIndexedPrimitive = 0x00769871;
inline constexpr std::uint32_t kAfterRecordDrawIndexedPrimitive = 0x007698FC;
inline constexpr std::uint32_t kAfterDrawPrimitive = 0x00769942;

// The body's own stores outside its stack (read): the world bound's four
// dwords (in this order) and the flags word at one of three exits.
inline constexpr std::uint32_t kWorldBoundStoreSites[4] = {0x0076979C, 0x007697A2, 0x007697AE, 0x007697B4};

constexpr std::uint32_t FlagsClearSite(Outcome exit) {
	switch (exit) {
	case Outcome::kPartitionLoopExit: return 0x007698A3;
	case Outcome::kArrayLoopExit: return 0x00769914;
	case Outcome::kSharedDrawExit: return 0x00769946;
	default: return 0;  // B1 and B2 clear nothing
	}
}

// ---- Views (offsets read in 0x00769700.md "Struct accesses") ---------------------------

// NiDX9Renderer (proposal), the fields this body reads or writes.
struct NiDX9RendererView {
	std::uint8_t unknown000[0x280];
	std::uint32_t device;                // +0x280 IDirect3DDevice9* (0x00769852, 0x007698DE, 0x0076992A)
	std::uint8_t unknown284[0x658 - 0x284];
	std::uint32_t currentWorldBound[4];  // +0x658..+0x664, dwords stored after slot 18 (0x0076979C..0x007697B4)
	std::uint8_t unknown668[0x6D8 - 0x668];
	std::uint32_t maxBonesWithoutShader;  // +0x6D8, unsigned (JBE 0x00769813)
	std::uint8_t unknown6DC[0x6F0 - 0x6DC];
	std::uint8_t skipGeometryDraws;      // +0x6F0, any non-zero byte returns (0x00769706); name unverified
	std::uint8_t unknown6F1[0x8AC - 0x6F1];
	std::uint32_t renderState;           // +0x8AC, object with a sub-object at +0xFF0; type unverified
	std::uint8_t unknown8B0[0xA94 - 0x8B0];
	std::uint32_t defaultShader;         // +0xA94 NiD3DShaderInterface* (0x00769802)
};
static_assert(offsetof(NiDX9RendererView, device) == 0x280, "device at +0x280");
static_assert(offsetof(NiDX9RendererView, currentWorldBound) == 0x658, "current world bound at +0x658");
static_assert(sizeof(NiDX9RendererView::currentWorldBound) == 0x10, "four dwords, +0x658..+0x664");
static_assert(offsetof(NiDX9RendererView, maxBonesWithoutShader) == 0x6D8, "bone limit at +0x6D8");
static_assert(offsetof(NiDX9RendererView, skipGeometryDraws) == 0x6F0, "gate byte at +0x6F0");
static_assert(offsetof(NiDX9RendererView, renderState) == 0x8AC, "render state at +0x8AC");
static_assert(offsetof(NiDX9RendererView, defaultShader) == 0xA94, "default shader at +0xA94");
static_assert(sizeof(NiDX9RendererView) == 0xA98, "the view ends after the default shader");
inline constexpr std::uint32_t kCurrentWorldBoundOffset = offsetof(NiDX9RendererView, currentWorldBound);

// The render state object at renderer+0x8AC: its +0xFF0 is read at 0x007697BA.
inline constexpr std::uint32_t kRenderStateSubObjectOffset = 0xFF0;
inline constexpr std::uint32_t kSubObjectSlot1 = 0x04;  // [[sub]]+0x04, no stack argument (0x007697C2)

// NiGeometry (proposal), the fields this body reads.
struct NiGeometryView {
	std::uint8_t unknown00[0x20];
	std::uint32_t worldBound[4];  // +0x20..+0x2C, centre x, y, z, radius as dwords (0x0076973F..0x00769747)
	std::uint8_t unknown30[0xB4 - 0x30];
	std::uint32_t data;            // +0xB4 NiGeometryData*, read twice (0x00769718, 0x00769760)
	std::uint32_t skin;            // +0xB8 NiSkinInstance*, dereferenced only in the skinned arm (0x00769732)
	std::uint32_t shaderCandidate; // +0xBC, cast to NiD3DShaderInterface (0x00769766)
};
static_assert(offsetof(NiGeometryView, worldBound) == 0x20, "world bound at +0x20");
static_assert(sizeof(NiGeometryView::worldBound) == 0x10, "four dwords");
static_assert(offsetof(NiGeometryView, data) == 0xB4, "data at +0xB4");
static_assert(offsetof(NiGeometryView, skin) == 0xB8, "skin at +0xB8");
static_assert(offsetof(NiGeometryView, shaderCandidate) == 0xBC, "shader candidate at +0xBC");
static_assert(sizeof(NiGeometryView) == 0xC0, "the view ends after the shader candidate");

// NiGeometryData (proposal): the vtable, the flags word, the buffer record.
struct NiGeometryDataView {
	std::uint32_t vtable;       // slot 20 (+0x50) returns the vertex count in AX (0x00769720)
	std::uint8_t unknown04[0x2E - 0x04];
	std::uint16_t flags;        // +0x2E, &= 0xF000 at the draw exits
	std::uint8_t unknown30[0x38 - 0x30];
	std::uint32_t bufferRecord; // +0x38 (0x0076976C)
};
static_assert(offsetof(NiGeometryDataView, flags) == 0x2E, "flags word at +0x2E");
static_assert(sizeof(NiGeometryDataView::flags) == 2, "AND word ptr (0x007698A3)");
static_assert(offsetof(NiGeometryDataView, bufferRecord) == 0x38, "buffer record at +0x38");
inline constexpr std::uint32_t kDataFlagsOffset = offsetof(NiGeometryDataView, flags);
inline constexpr std::uint32_t kDataVertexCountSlot = 0x50;  // slot 20, ECX = data, no stack argument (0x00769727)
static_assert(kDataVertexCountSlot == 20 * 4, "slot 20");

// NiSkinInstance (proposal): +0x0C the skin partition (0x007697D6).
struct NiSkinInstanceView {
	std::uint8_t unknown00[0x0C];
	std::uint32_t skinPartition;
};
static_assert(offsetof(NiSkinInstanceView, skinPartition) == 0x0C, "skin partition at +0x0C");

// NiSkinPartition (proposal): count and array (0x007697D9, 0x007697DC).
struct NiSkinPartitionView {
	std::uint8_t unknown00[0x08];
	std::uint32_t partitionCount;  // unsigned (JBE 0x007697F1), counted down (0x00769890)
	std::uint32_t partitions;      // Partition[partitionCount]
};
static_assert(offsetof(NiSkinPartitionView, partitionCount) == 0x08, "count at +0x08");
static_assert(offsetof(NiSkinPartitionView, partitions) == 0x0C, "array at +0x0C");

// NiSkinPartition::Partition (proposal), stride 0x2C (ADD EDX,0x2C at 0x0076988D).
struct PartitionView {
	std::uint8_t unknown00[0x1C];
	std::uint16_t vertexCount;  // +0x1C, DIP NumVertices (MOVZX ECX,word [ECX-4] 0x0076984E)
	std::uint8_t unknown1E[0x02];
	std::uint16_t boneCount;    // +0x20 (MOVZX 0x0076980A)
	std::uint16_t stripCount;   // +0x22, partition 0's only (CMP word 0x007697E1)
	std::uint8_t unknown24[0x04];
	std::uint32_t bufferData;   // +0x28 (MOV EBX,[EDX+8] 0x00769825, EDX = partition+0x20)
};
static_assert(offsetof(PartitionView, vertexCount) == 0x1C, "vertex count at +0x1C");
static_assert(offsetof(PartitionView, boneCount) == 0x20, "bone count at +0x20");
static_assert(offsetof(PartitionView, stripCount) == 0x22, "strip count at +0x22");
static_assert(offsetof(PartitionView, bufferData) == 0x28, "buffer data at +0x28");
static_assert(sizeof(PartitionView) == 0x2C, "partition stride 0x2C");
inline constexpr std::uint32_t kPartitionStride = sizeof(PartitionView);  // partitionAt(array, i) = array + 0x2C*i

// The geometry buffer record (data+0x38 and partition+0x28; fields read
// 0x0076982A-0x00769878 and 0x007698B3-0x00769934).
struct BufferRecordView {
	std::uint8_t unknown00[0x14];
	std::uint32_t vertexCount;      // +0x14, the whole arm's DIP NumVertices (0x007698DB)
	std::uint8_t unknown18[0x30 - 0x18];
	std::uint32_t indexBuffer;      // +0x30, tested for zero only (0x007698B3)
	std::uint32_t baseVertexIndex;  // +0x34 (0x00769863, 0x007698EF, 0x00769927)
	std::uint32_t primitiveType;    // +0x38, the whole arm's type (0x007698F5, 0x00769934)
	std::uint32_t triangleCount;    // +0x3C (0x00769847, 0x007698D8, 0x00769924)
	std::uint8_t unknown40[0x04];
	std::uint32_t arrayCount;       // +0x44, unsigned (0x0076982A, 0x007698BB, re-read per array)
	std::uint32_t arrayLengths;     // +0x48, u16* or 0 (0x00769833, 0x007698C4)
};
static_assert(offsetof(BufferRecordView, vertexCount) == 0x14, "vertex count at +0x14");
static_assert(offsetof(BufferRecordView, indexBuffer) == 0x30, "index buffer at +0x30");
static_assert(offsetof(BufferRecordView, baseVertexIndex) == 0x34, "base vertex index at +0x34");
static_assert(offsetof(BufferRecordView, primitiveType) == 0x38, "primitive type at +0x38");
static_assert(offsetof(BufferRecordView, triangleCount) == 0x3C, "triangle count at +0x3C");
static_assert(offsetof(BufferRecordView, arrayCount) == 0x44, "array count at +0x44");
static_assert(offsetof(BufferRecordView, arrayLengths) == 0x48, "array lengths at +0x48");
static_assert(sizeof(BufferRecordView) == 0x4C, "the view ends after the lengths pointer");

// ---- Vtable slots (byte offsets; read at the CALLs) ----------------------------------

// NiD3DShaderInterface slot 18 (+0x48), no stack argument, result ignored (0x00769782).
inline constexpr std::uint32_t kShaderSlotOffset18 = 0x48;
static_assert(kShaderSlotOffset18 == 18 * 4, "slot 18");

// IDirect3DDevice9 slots 81 / 82 (d3d9.h order; offsets read at 0x00769938 and
// 0x0076985A / 0x007698E6).
inline constexpr std::uint32_t kDeviceDrawPrimitiveOffset = 0x144;
inline constexpr std::uint32_t kDeviceDrawIndexedPrimitiveOffset = 0x148;
static_assert(kDeviceDrawPrimitiveOffset == 81 * 4 && kDeviceDrawIndexedPrimitiveOffset == 82 * 4, "slots 81, 82");

// ---- Function types (the 32-bit binary's) ---------------------------------------------

// Every virtual call inside the body is callee-cleaned (read: no ADD ESP after
// any of them; measured in the emulator gate: every case returns with the RET
// 0x4 balance when the stubs clean their own arguments); the two cdecl calls
// are followed by ADD ESP,8.
using DrawTriStripsDirectFn = void(__thiscall*)(NiDX9RendererView* self, NiGeometryView* geometry);
using DataVertexCountFn = std::uint16_t(__thiscall*)(NiGeometryDataView* data);  // AX is what is tested
using SelectsPartitionsFn = bool(__thiscall*)(NiDX9RendererView* self, NiGeometryView* geometry,
		std::uint32_t zero);  // AL is what is kept
using RttiCastFn = std::uint32_t(__cdecl*)(std::uint32_t rtti, std::uint32_t object);
using DebugPrintFn = void(__cdecl*)(const char* format, std::uint32_t boneCount);
using ShaderSlot18Fn = std::uint32_t(__thiscall*)(void* shader);
using SubObjectSlot1Fn = void(__thiscall*)(void* subObject);
using DrawPrimitiveFn = std::int32_t(__stdcall*)(void* device, std::uint32_t primitiveType, std::uint32_t startVertex,
		std::uint32_t primitiveCount);
using DrawIndexedPrimitiveFn = std::int32_t(__stdcall*)(void* device, std::uint32_t primitiveType,
		std::int32_t baseVertexIndex, std::uint32_t minVertexIndex, std::uint32_t numVertices, std::uint32_t startIndex,
		std::uint32_t primitiveCount);

} // namespace obng::engine::render::draw_tri_strips_direct::engine
