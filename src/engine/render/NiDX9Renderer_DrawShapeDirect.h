#pragma once

// The engine-facing shape of NiDX9Renderer::DrawShapeDirect 0x007694F0
// (proposed name, re/functions/0x007694F0.md): the calling convention, the
// fields and vtable slots the body reads, and the call sites the ports stand
// for. The logic is NiDX9Renderer_DrawShapeDirectLogic.h; the stereo contract
// is StereoDrawDirect.h / StereoDraw.h (included through the logic, not
// redefined).
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts, unverified = not checked. The views
// below hold only what this body touches; pointer fields are std::uint32_t
// (the binary's 32-bit addresses) so the layout holds on any host, and the
// function types are the 32-bit binary's.

#include "engine/render/NiDX9Renderer_DrawShapeDirectLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::draw_shape_direct::engine {

// ---- The function and its callees ------------------------------------------------------

// The function being replaced: __thiscall, ECX = renderer (MOV EBP,ECX at
// 0x007694F4), one stack argument (the geometry), callee-cleaned (RET 0x4 at
// 0x0076965B, 0x007696C5, 0x007696F7), no return value read by its caller
// (read). Two sources for the address: the export's identity (meta.json) and
// vtable 0x00A88EA4's slot 90 entry at .rdata 0x00A8900C (exe scan, map
// "Callers").
inline constexpr std::uint32_t kDrawShapeDirectAddress = 0x007694F0;
inline constexpr std::uint32_t kRendererVtable = 0x00A88EA4;
inline constexpr std::uint32_t kRendererSlotDrawShapeDirect = 0x168;  // slot 90
static_assert(kRendererSlotDrawShapeDirect == 90 * 4, "slot 90");

// The skinned-or-whole decision: __thiscall(renderer; geometry, shader), RET
// 0x8, result in AL (map "The callee 0x00768890", objdump 0x00768890-0x00768952;
// measured in the emulator gate: every case returns with the RET 0x4 balance
// when the stub pops 8 bytes). Pushes 0x0076952E (0) and 0x00769530 (geometry),
// ECX = renderer 0x00769531 (read).
inline constexpr std::uint32_t kIsSkinnedAddress = 0x00768890;

// The RTTI cast, cdecl (rtti, object) -> object or 0, ADD ESP,8 after the call
// (0x00769563, read). NiRTTI_NiD3DShaderInterface is the immediate pushed at
// 0x0076954C (read) and named by data.json and re/symbols/xobse.csv: two
// sources.
inline constexpr std::uint32_t kRttiCastAddress = 0x00560920;
inline constexpr std::uint32_t kNiRttiNiD3DShaderInterface = 0x00B42858;

// The diagnostic, cdecl (format, boneCount), ADD ESP,8 after (0x007695D5); a
// single RET in this build (objdump 0x0060D0A0, map C8). The format string's
// address, pushed at 0x007695CB (read; data.json: "DrawSkinnedPrimitive>
// Cannot render partition with %d bones without a shader!\n").
inline constexpr std::uint32_t kDebugPrintAddress = 0x0060D0A0;
inline constexpr std::uint32_t kCannotRenderPartitionFormat = 0x00A88D90;

// The calls' return addresses (the instruction after each CALL, read in
// asm.s): what the emulator's trace shows as "from".
inline constexpr std::uint32_t kAfterDataVertexCount = 0x0076951D;
inline constexpr std::uint32_t kAfterIsSkinned = 0x0076953C;
inline constexpr std::uint32_t kAfterRttiCast = 0x00769558;
inline constexpr std::uint32_t kAfterShaderSlot18 = 0x00769568;
inline constexpr std::uint32_t kAfterSubObjectSlot1 = 0x0076957B;
inline constexpr std::uint32_t kAfterDebugPrint = 0x007695D5;
inline constexpr std::uint32_t kAfterSkinnedDrawIndexedPrimitive = 0x00769624;
inline constexpr std::uint32_t kAfterWholeDrawIndexedPrimitive = 0x007696A8;
inline constexpr std::uint32_t kAfterDrawPrimitive = 0x007696E6;

// The body's only stores outside its stack (read): the flags clear, one site
// per exit path.
inline constexpr std::uint32_t kFlagsClearSkinned = 0x0076964E;      // after B10 (the partition loop's end)
inline constexpr std::uint32_t kFlagsClearWholeIndexed = 0x007696B8; // after B14
inline constexpr std::uint32_t kFlagsClearShared = 0x007696EA;       // after B4 (JBE 0x007696E6), B11 (DrawPrimitive), B12

// The flags clear site of each exit (read: the jumps into 0x0076964A,
// 0x007696B4 and 0x007696E6 in asm.s; the skinned arm's zero-partition exit
// JBE 0x007696E6 at 0x007695A2 shares the whole arm's site - measured in the
// emulator gate, case 7); 0 for the two exits without a clear.
constexpr std::uint32_t FlagsClearSite(Outcome outcome) {
	switch (outcome) {
	case Outcome::kSkinned: return kFlagsClearSkinned;
	case Outcome::kWholeIndexed: return kFlagsClearWholeIndexed;
	case Outcome::kSkinnedNoPartitions:
	case Outcome::kWholeDrawPrimitive:
	case Outcome::kWholeNoArrays: return kFlagsClearShared;
	case Outcome::kSuppressed:
	case Outcome::kNoVertices: return 0;
	}
	return 0;
}

// ---- Views (offsets read in 0x007694F0.md "Struct accesses") ---------------------------

// NiDX9Renderer (proposal), the fields this body reads.
struct NiDX9RendererView {
	std::uint8_t unknown00[0x280];
	std::uint32_t device;  // +0x280 IDirect3DDevice9* (0x007695FD, 0x0076968A, 0x007696CE); OBVR GameAddresses.h:597-598
	std::uint8_t unknown284[0x6D8 - 0x284];
	std::uint32_t maxBonesWithoutShader;  // +0x6D8, unsigned (JBE 0x007695C8); meaning from the string
	std::uint8_t unknown6DC[0x6F0 - 0x6DC];
	std::uint8_t drawSuppressed;  // +0x6F0, any non-zero byte returns (0x007694F6); meaning unverified
	std::uint8_t unknown6F1[0x8AC - 0x6F1];
	std::uint32_t renderState;  // +0x8AC, object with a sub-object at +0xFF0 (0x00769568); type unverified
	std::uint8_t unknown8B0[0xA94 - 0x8B0];
	std::uint32_t defaultShader;  // +0xA94 NiD3DShaderInterface* (CMP 0x007695B7), compared only
};
static_assert(offsetof(NiDX9RendererView, device) == 0x280, "device at +0x280");
static_assert(offsetof(NiDX9RendererView, maxBonesWithoutShader) == 0x6D8, "bone limit at +0x6D8");
static_assert(offsetof(NiDX9RendererView, drawSuppressed) == 0x6F0, "suppress byte at +0x6F0");
static_assert(sizeof(NiDX9RendererView::drawSuppressed) == 1, "CMP byte ptr (0x007694F6)");
static_assert(offsetof(NiDX9RendererView, renderState) == 0x8AC, "render state at +0x8AC");
static_assert(offsetof(NiDX9RendererView, defaultShader) == 0xA94, "default shader at +0xA94");
static_assert(sizeof(NiDX9RendererView) == 0xA98, "the view ends after the default shader");

// The render state object at renderer+0x8AC: its +0xFF0 is read at 0x0076956E,
// and that object's vtable +0x04 is called with no stack argument (0x00769579).
inline constexpr std::uint32_t kRenderStateSubObjectOffset = 0xFF0;
inline constexpr std::uint32_t kSubObjectSlot1 = 0x04;

// NiGeometry (proposal): data, skin and shader candidate.
struct NiGeometryView {
	std::uint8_t unknown00[0xB4];
	std::uint32_t data;             // +0xB4 NiGeometryData* (0x0076950C, re-read 0x0076953C)
	std::uint32_t skin;             // +0xB8 NiSkinInstance* (0x00769526)
	std::uint32_t shaderCandidate;  // +0xBC, cast to NiD3DShaderInterface (0x00769542)
};
static_assert(offsetof(NiGeometryView, data) == 0xB4, "data at +0xB4");
static_assert(offsetof(NiGeometryView, skin) == 0xB8, "skin at +0xB8");
static_assert(offsetof(NiGeometryView, shaderCandidate) == 0xBC, "shader candidate at +0xBC");
static_assert(sizeof(NiGeometryView) == 0xC0, "the view ends after the shader candidate");

// NiGeometryData (proposal): the vtable (slot +0x50, the vertex count in AX),
// the u16 flags word and the whole draw's buffer record.
struct NiGeometryDataView {
	std::uint32_t vtable;  // +0x00 (0x00769512)
	std::uint8_t unknown04[0x2E - 0x04];
	std::uint16_t flags;  // +0x2E, AND word 0xF000 (0x0076964E, 0x007696B8, 0x007696EA)
	std::uint8_t unknown30[0x38 - 0x30];
	std::uint32_t record;  // +0x38, the whole arm's buffer record (0x00769548)
};
static_assert(offsetof(NiGeometryDataView, flags) == 0x2E, "flags word at +0x2E");
static_assert(sizeof(NiGeometryDataView::flags) == 2, "AND word ptr");
static_assert(offsetof(NiGeometryDataView, record) == 0x38, "record at +0x38");
static_assert(sizeof(NiGeometryDataView) == 0x3C, "the view ends after the record");
inline constexpr std::uint32_t kDataVertexCountSlot = 0x50;  // MOV EDX,[EAX+0x50] (0x00769514), ECX = data

// NiSkinInstance (proposal): +0x0C the skin partition (0x00769587).
struct NiSkinInstanceView {
	std::uint8_t unknown00[0x0C];
	std::uint32_t skinPartition;
};
static_assert(offsetof(NiSkinInstanceView, skinPartition) == 0x0C, "skin partition at +0x0C");

// NiSkinPartition (proposal): count and array (0x0076958A, 0x0076958D).
struct NiSkinPartitionView {
	std::uint8_t unknown00[0x08];
	std::uint32_t partitionCount;  // unsigned (JBE 0x007695A2), counted down (0x0076963B)
	std::uint32_t partitions;      // Partition[partitionCount]
};
static_assert(offsetof(NiSkinPartitionView, partitionCount) == 0x08, "count at +0x08");
static_assert(offsetof(NiSkinPartitionView, partitions) == 0x0C, "array at +0x0C");

// NiSkinPartition::Partition (proposal), stride 0x2C (ADD ESI,0x2C at
// 0x00769638). The body walks it with a cursor at +0x20 (LEA ESI,[EAX+0x20]
// 0x007695A8): +0x1C is [cursor-4], +0x28 is [cursor+8] (read).
struct PartitionView {
	std::uint8_t unknown00[0x1C];
	std::uint16_t vertexCount;  // +0x1C, DIP NumVertices (MOVZX 0x00769607)
	std::uint8_t unknown1E[0x02];
	std::uint16_t boneCount;   // +0x20 (MOVZX 0x007695BF)
	std::uint16_t stripCount;  // +0x22, partition 0's only (CMP word 0x00769592)
	std::uint8_t unknown24[0x04];
	std::uint32_t record;      // +0x28 (0x007695DA)
};
static_assert(offsetof(PartitionView, vertexCount) == 0x1C, "vertex count at +0x1C");
static_assert(offsetof(PartitionView, boneCount) == 0x20, "bone count at +0x20");
static_assert(offsetof(PartitionView, stripCount) == 0x22, "strip count at +0x22");
static_assert(offsetof(PartitionView, record) == 0x28, "record at +0x28");
static_assert(sizeof(PartitionView) == 0x2C, "partition stride 0x2C");
inline constexpr std::uint32_t kPartitionStride = sizeof(PartitionView);  // partitionAt(array, i) = array + 0x2C*i
inline constexpr std::uint32_t kPartitionCursorOffset = 0x20;             // the cursor the loop keeps in a1's slot (C21)

// The geometry buffer record (fields read 0x007695E1-0x007696D8).
struct BufferRecordView {
	std::uint8_t unknown00[0x14];
	std::uint32_t vertexCount;  // +0x14, whole DIP NumVertices (0x00769683)
	std::uint8_t unknown18[0x30 - 0x18];
	std::uint32_t indexBuffer;      // +0x30, tested for zero only (0x0076965E)
	std::uint32_t baseVertexIndex;  // +0x34 (0x00769616, 0x00769695, 0x007696CB)
	std::uint32_t primitiveType;    // +0x38, whole arm only (0x0076969B, 0x007696D8)
	std::uint32_t triangleCount;    // +0x3C (0x007695F6, 0x00769680, 0x007696C8)
	std::uint8_t unknown40[0x04];
	std::uint32_t arrayCount;    // +0x44, unsigned (0x007695E1, 0x00769627, 0x00769668, 0x007696AB)
	std::uint32_t arrayLengths;  // +0x48, u16* or 0 (0x007695E6, 0x00769670)
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

// NiD3DShaderInterface slot 18: no stack argument, ECX = shader (MOV ECX,EAX
// 0x0076955E; CALL 0x00769566), result unused (C5).
inline constexpr std::uint32_t kShaderSlotOffset18 = 0x48;
static_assert(kShaderSlotOffset18 == 18 * 4, "slot 18");

// IDirect3DDevice9 DrawPrimitive (slot 81) and DrawIndexedPrimitive (slot 82),
// as counted in d3d9.h by re/functions/0x007672F0.md; the argument shapes
// here match (3 and 6 arguments after the COM this; derived).
inline constexpr std::uint32_t kDeviceDrawPrimitiveOffset = 0x144;
inline constexpr std::uint32_t kDeviceDrawIndexedPrimitiveOffset = 0x148;
static_assert(kDeviceDrawPrimitiveOffset == 81 * 4, "slot 81");
static_assert(kDeviceDrawIndexedPrimitiveOffset == 82 * 4, "slot 82");

// ---- Function types (the 32-bit binary's) ---------------------------------------------

// Every call inside the body is callee-cleaned except the two cdecl ones
// (read: ADD ESP,8 only after 0x00560920 and 0x0060D0A0).
using DrawShapeDirectFn = void(__thiscall*)(NiDX9RendererView* self, NiGeometryView* geometry);
using IsSkinnedFn = bool(__thiscall*)(NiDX9RendererView* self, NiGeometryView* geometry, std::uint32_t shader);
using RttiCastFn = std::uint32_t(__cdecl*)(std::uint32_t rtti, std::uint32_t object);
using DebugPrintFn = void(__cdecl*)(const char* format, std::uint32_t boneCount);
using DataVertexCountFn = std::uint16_t(__thiscall*)(NiGeometryDataView* data);  // AX tested (0x0076951D)
using ShaderSlot18Fn = std::uint32_t(__thiscall*)(void* shader);
using SubObjectSlot1Fn = void(__thiscall*)(void* subObject);
using DrawPrimitiveFn = std::int32_t(__stdcall*)(void* device, std::uint32_t primitiveType, std::uint32_t startVertex,
		std::uint32_t primitiveCount);
using DrawIndexedPrimitiveFn = std::int32_t(__stdcall*)(void* device, std::uint32_t primitiveType,
		std::int32_t baseVertexIndex, std::uint32_t minVertexIndex, std::uint32_t numVertices, std::uint32_t startIndex,
		std::uint32_t primitiveCount);

} // namespace obng::engine::render::draw_shape_direct::engine
