#pragma once

// The engine-facing shape of NiDX9Renderer::DrawSkinnedPrimitive 0x00767520
// (proposed name, re/functions/0x00767520.md): the calling convention, the
// fields and vtable slots the body reads, and the call sites the ports stand
// for. The logic is NiDX9Renderer_DrawSkinnedPrimitiveLogic.h; the stereo
// contract is StereoDraw.h (included through the logic, not redefined).
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts, unverified = not checked. The views
// below hold only what this body touches; pointer fields are std::uint32_t
// (the binary's 32-bit addresses) so the layout holds on any host, and the
// function types are the 32-bit binary's.

#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitiveLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::draw_skinned_primitive::engine {

// ---- The function and its callees ------------------------------------------------------

// The function being replaced: __thiscall, ECX = renderer (MOV ESI,ECX at
// 0x0076752A), five stack arguments, callee-cleaned (RET 0x14 at 0x0076780B),
// no return value read by its caller (read). Two sources for the address: the
// export's identity (meta.json) and the caller's CALL at 0x007694BD in
// 0x007693E0 (read, 0x00767520.md "Callers").
inline constexpr std::uint32_t kDrawSkinnedPrimitiveAddress = 0x00767520;

// The RTTI cast, cdecl (rtti, object) -> object or 0, ADD ESP,8 after the call
// (0x0076756D, read); body read by the map (C12). NiRTTI_NiD3DShaderInterface
// is the immediate pushed at 0x00767561 (read) and named by xOBSE
// (NiRTTI_1_2_416.inl:283 via re/symbols/xobse.csv): two sources.
inline constexpr std::uint32_t kRttiCastAddress = 0x00560920;
inline constexpr std::uint32_t kNiRttiNiD3DShaderInterface = 0x00B42858;

// The diagnostic, cdecl (format, boneCount), ADD ESP,8 after (0x0076766F); a
// single RET (byte C3) in this build (C11, read by the map in the
// md5-matching binary). The format string's address, pushed at 0x00767665.
inline constexpr std::uint32_t kDebugPrintAddress = 0x0060D0A0;
inline constexpr std::uint32_t kCannotRenderPartitionFormat = 0x00A88D90;

// The calls' return addresses (the instruction after each CALL, read in
// asm.s): what the emulator's trace shows as "from".
inline constexpr std::uint32_t kAfterRttiCast = 0x0076756B;
inline constexpr std::uint32_t kAfterSlot10 = 0x007675A4;
inline constexpr std::uint32_t kAfterSlot11 = 0x007675CE;
inline constexpr std::uint32_t kAfterSlot18 = 0x007675E2;
inline constexpr std::uint32_t kAfterSlot12 = 0x00767612;
inline constexpr std::uint32_t kAfterDebugPrint = 0x0076766F;
inline constexpr std::uint32_t kAfterSlot13 = 0x007676A1;
inline constexpr std::uint32_t kAfterSlot15 = 0x007676B5;
inline constexpr std::uint32_t kAfterSlot14 = 0x007676E6;
inline constexpr std::uint32_t kAfterSubObjectSlot1 = 0x007676F9;
inline constexpr std::uint32_t kAfterDrawIndexedPrimitive = 0x00767750;
inline constexpr std::uint32_t kAfterSlot16 = 0x00767797;
inline constexpr std::uint32_t kAfterSlot19 = 0x007677C6;
inline constexpr std::uint32_t kAfterSlot17 = 0x007677F0;
inline constexpr std::uint32_t kAfterSetDrawFlag = 0x00767804;

// The body's own stores outside its stack (read): the world bound's four
// dwords and the flags word.
inline constexpr std::uint32_t kWorldBoundStoreSites[4] = {0x0076761A, 0x00767623, 0x0076762C, 0x00767635};
inline constexpr std::uint32_t kFlagsClearSite = 0x007677B7;

// ---- Views (offsets read in 0x00767520.md "Struct accesses") ---------------------------

// NiDX9Renderer (proposal), the fields this body reads or writes.
struct NiDX9RendererView {
	std::uint8_t unknown00[0x0C];
	std::uint32_t propertyState;  // +0x0C (0x00767590, ...), meaning derived
	std::uint32_t effectState;    // +0x10 (0x0076757E, ...), meaning unverified
	std::uint8_t unknown14[0x280 - 0x14];
	std::uint32_t device;         // +0x280 IDirect3DDevice9* (0x00767729); OBVR GameAddresses.h:598 + xOBSE NiRenderer.h
	std::uint8_t unknown284[0x658 - 0x284];
	std::uint32_t currentWorldBound[4];  // +0x658..+0x664, dwords copied from a5 per pass (0x0076761A..)
	std::uint8_t unknown668[0x6D8 - 0x668];
	std::uint32_t maxBonesWithoutShader;  // +0x6D8, unsigned (JBE 0x00767662)
	std::uint8_t unknown6DC[0x8AC - 0x6DC];
	std::uint32_t renderState;    // +0x8AC, object with a vtable (+0xFC) and a sub-object at +0xFF0; type unverified
	std::uint8_t unknown8B0[0xA94 - 0x8B0];
	std::uint32_t defaultShader;  // +0xA94 NiD3DShaderInterface* (0x00767574, 0x00767650)
};
static_assert(offsetof(NiDX9RendererView, propertyState) == 0x0C, "property state at +0x0C");
static_assert(offsetof(NiDX9RendererView, effectState) == 0x10, "effect state at +0x10");
static_assert(offsetof(NiDX9RendererView, device) == 0x280, "device at +0x280");
static_assert(offsetof(NiDX9RendererView, currentWorldBound) == 0x658, "current world bound at +0x658");
static_assert(sizeof(NiDX9RendererView::currentWorldBound) == 0x10, "four dwords, +0x658..+0x664");
static_assert(offsetof(NiDX9RendererView, maxBonesWithoutShader) == 0x6D8, "bone limit at +0x6D8");
static_assert(offsetof(NiDX9RendererView, renderState) == 0x8AC, "render state at +0x8AC");
static_assert(offsetof(NiDX9RendererView, defaultShader) == 0xA94, "default shader at +0xA94");
static_assert(sizeof(NiDX9RendererView) == 0xA98, "the view ends after the default shader");

// The render state object at renderer+0x8AC: its +0xFF0 is read at 0x007676EC.
inline constexpr std::uint32_t kRenderStateSubObjectOffset = 0xFF0;
inline constexpr std::uint32_t kSubObjectSlot1 = 0x04;         // [[sub]]+0x04, no arguments (0x007676F4)
inline constexpr std::uint32_t kRenderStateSetDrawFlagSlot = 0xFC;  // [[rs]]+0xFC, one dword (0x007677F8)

// NiGeometry (proposal): +0xBC, the shader candidate cast at 0x0076755A.
inline constexpr std::uint32_t kGeometryShaderOffset = 0xBC;

// NiGeometryData (proposal): the u16 flags word +0x2E (0x007677B7).
struct NiGeometryDataView {
	std::uint8_t unknown00[0x2E];
	std::uint16_t flags;
};
static_assert(offsetof(NiGeometryDataView, flags) == 0x2E, "flags word at +0x2E");
static_assert(sizeof(NiGeometryDataView::flags) == 2, "AND word ptr (0x007677B7)");

// NiSkinInstance (proposal): +0x0C the skin partition (0x00767532).
struct NiSkinInstanceView {
	std::uint8_t unknown00[0x0C];
	std::uint32_t skinPartition;
};
static_assert(offsetof(NiSkinInstanceView, skinPartition) == 0x0C, "skin partition at +0x0C");

// NiSkinPartition (proposal): count and array (0x007675D2, 0x0076753B).
struct NiSkinPartitionView {
	std::uint8_t unknown00[0x08];
	std::uint32_t partitionCount;  // unsigned (JBE 0x00767643, JC 0x007677A9)
	std::uint32_t partitions;      // Partition[partitionCount]
};
static_assert(offsetof(NiSkinPartitionView, partitionCount) == 0x08, "count at +0x08");
static_assert(offsetof(NiSkinPartitionView, partitions) == 0x0C, "array at +0x0C");

// NiSkinPartition::Partition (proposal), stride 0x2C (ADD EBP,0x2C at 0x0076779E).
struct PartitionView {
	std::uint8_t unknown00[0x1C];
	std::uint16_t vertexCount;  // +0x1C, DIP NumVertices (MOVZX 0x0076773D)
	std::uint8_t unknown1E[0x02];
	std::uint16_t boneCount;    // +0x20 (MOVZX 0x00767658)
	std::uint16_t stripCount;   // +0x22, partition 0's only (CMP word 0x00767540)
	std::uint16_t unknown24;    // +0x24, not read here (0x007C9230 reads it)
	std::uint8_t unknown26[0x02];
	std::uint32_t bufferData;   // +0x28 (0x00767581, 0x0076767F)
};
static_assert(offsetof(PartitionView, vertexCount) == 0x1C, "vertex count at +0x1C");
static_assert(offsetof(PartitionView, boneCount) == 0x20, "bone count at +0x20");
static_assert(offsetof(PartitionView, stripCount) == 0x22, "strip count at +0x22");
static_assert(offsetof(PartitionView, bufferData) == 0x28, "buffer data at +0x28");
static_assert(sizeof(PartitionView) == 0x2C, "partition stride 0x2C");
inline constexpr std::uint32_t kPartitionStride = sizeof(PartitionView);  // partitionAt(array, i) = array + 0x2C*i

// The geometry buffer record slot 15 returns (fields read 0x00767708-0x00767763).
struct BufferRecordView {
	std::uint8_t unknown00[0x34];
	std::uint32_t baseVertexIndex;  // +0x34 (0x00767742)
	std::uint8_t unknown38[0x04];
	std::uint32_t triangleCount;    // +0x3C (0x00767720)
	std::uint8_t unknown40[0x04];
	std::uint32_t arrayCount;       // +0x44, unsigned (0x007676FB, 0x00767763)
	std::uint32_t arrayLengths;     // +0x48, u16* or 0 (0x00767708, 0x00767713)
};
static_assert(offsetof(BufferRecordView, baseVertexIndex) == 0x34, "base vertex index at +0x34");
static_assert(offsetof(BufferRecordView, triangleCount) == 0x3C, "triangle count at +0x3C");
static_assert(offsetof(BufferRecordView, arrayCount) == 0x44, "array count at +0x44");
static_assert(offsetof(BufferRecordView, arrayLengths) == 0x48, "array lengths at +0x48");
static_assert(sizeof(BufferRecordView) == 0x4C, "the view ends after the lengths pointer");

// The bound copied per pass: four dwords (0x00767617-0x00767635).
inline constexpr std::uint32_t kWorldBoundWords = 4;

// ---- Vtable slots (byte offsets; read at the CALLs) ----------------------------------

// NiD3DShaderInterface: slots 10..19. Names recalled from Gamebryo NiShader
// (C17, unverified); the numbers, order and argument lists are read.
inline constexpr std::uint32_t kShaderSlotOffset10 = 0x28;  // 7 args, dword result (0x00767586)
inline constexpr std::uint32_t kShaderSlotOffset11 = 0x2C;  // 7 args (0x007675C6)
inline constexpr std::uint32_t kShaderSlotOffset12 = 0x30;  // 7 args (0x007675F9)
inline constexpr std::uint32_t kShaderSlotOffset13 = 0x34;  // 8 args (0x00767684)
inline constexpr std::uint32_t kShaderSlotOffset14 = 0x38;  // 8 args (0x007676C9)
inline constexpr std::uint32_t kShaderSlotOffset15 = 0x3C;  // 4 args, the record (0x007676AA)
inline constexpr std::uint32_t kShaderSlotOffset16 = 0x40;  // 8 args (0x0076777A)
inline constexpr std::uint32_t kShaderSlotOffset17 = 0x44;  // 7 args (0x007677D7)
inline constexpr std::uint32_t kShaderSlotOffset18 = 0x48;  // no args, dword result (0x007675D7)
inline constexpr std::uint32_t kShaderSlotOffset19 = 0x4C;  // no args, dword result (0x007677BF)
static_assert(kShaderSlotOffset10 == 10 * 4 && kShaderSlotOffset19 == 19 * 4, "slot n at 4n");

// IDirect3DDevice9::DrawIndexedPrimitive, slot 82 (d3d9.h, counted by the map;
// the SDK header line 516 reads (D3DPRIMITIVETYPE, INT BaseVertexIndex, UINT
// MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)).
inline constexpr std::uint32_t kDeviceDrawIndexedPrimitiveOffset = 0x148;
static_assert(kDeviceDrawIndexedPrimitiveOffset == 82 * 4, "slot 82");

// ---- Function types (the 32-bit binary's) ---------------------------------------------

// Every call inside the body is callee-cleaned except the two cdecl ones
// (read: no ADD ESP after any virtual call; measured in the emulator gate:
// every case returns with the RET 0x14 balance when the stubs clean their own
// arguments).
using DrawSkinnedPrimitiveFn = void(__thiscall*)(NiDX9RendererView* self, std::uint32_t geometry, NiGeometryDataView* data,
		NiSkinInstanceView* skin, std::uint32_t world, const std::uint32_t* worldBound);
using RttiCastFn = std::uint32_t(__cdecl*)(std::uint32_t rtti, std::uint32_t object);
using DebugPrintFn = void(__cdecl*)(const char* format, std::uint32_t boneCount);
using ShaderPipelineFn = std::uint32_t(__thiscall*)(void* shader, std::uint32_t geometry, std::uint32_t skin,
		std::uint32_t record, std::uint32_t propertyState, std::uint32_t effectState, std::uint32_t world,
		std::uint32_t worldBound);  // slots 10, 11, 12, 17 (only slot 10's result is read)
using ShaderPartitionFn = void(__thiscall*)(void* shader, std::uint32_t geometry, std::uint32_t skin,
		std::uint32_t partition, std::uint32_t record, std::uint32_t propertyState, std::uint32_t effectState,
		std::uint32_t world, std::uint32_t worldBound);  // slots 13, 14, 16
using ShaderRecordFn = std::uint32_t(__thiscall*)(void* shader, std::uint32_t geometry, std::uint32_t partition,
		std::uint32_t bufferData, std::uint32_t propertyState);  // slot 15
using ShaderPassFn = std::uint32_t(__thiscall*)(void* shader);  // slots 18, 19
using SubObjectSlot1Fn = void(__thiscall*)(void* subObject);
using SetDrawFlagFn = void(__thiscall*)(void* renderState, std::uint32_t flag);
using DrawIndexedPrimitiveFn = std::int32_t(__stdcall*)(void* device, std::uint32_t primitiveType,
		std::int32_t baseVertexIndex, std::uint32_t minVertexIndex, std::uint32_t numVertices, std::uint32_t startIndex,
		std::uint32_t primitiveCount);

} // namespace obng::engine::render::draw_skinned_primitive::engine
