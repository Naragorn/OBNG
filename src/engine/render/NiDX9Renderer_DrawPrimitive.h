#pragma once

// The engine-facing shape of 0x007672F0 (proposed NiDX9Renderer::DrawPrimitive):
// the views of the bytes it touches, its calling convention, the callees'
// conventions, and the ports of NiDX9Renderer_DrawPrimitiveLogic.h over real
// engine memory. 32-bit only; the decisions live in the logic header.
//
// Evidence: re/functions/0x007672F0.md, re/export/stereo-draw/0x007672F0/asm.s.
// Names are proposals until the stereo-draw slice's refutation round.
//
// No shared header defines these classes; StereoDraw.h states the contract
// and defines no layout. The views below are local to this rebuild and are
// layout requests for a shared header, not definitions of the engine's
// classes: they cover the bytes this function touches, at the offsets the
// instructions use; what the engine keeps in between is unread here.

#include "engine/render/NiDX9Renderer_DrawPrimitiveLogic.h"
#include "engine/render/StereoDraw.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::draw_primitive::engine {

static_assert(sizeof(void*) == 4, "the views mirror a 32-bit binary");

inline constexpr std::uint32_t kDrawPrimitiveAddress = 0x007672F0;
inline constexpr std::uint32_t kShaderInterfaceCastAddress = 0x00560920;  // CALL at 0x0076730B
inline constexpr std::uint32_t kShaderInterfaceRttiAddress = 0x00B42858;  // PUSH at 0x00767306 (data.json:
                                                                          // NiRTTI_NiD3DShaderInterface)

// ---- The callees' conventions -------------------------------------------------

// 0x00560920: cdecl (ADD ESP,0x8 after the call, 0x00767312), arguments
// (rtti, object), result EAX = the object when its RTTI chain holds rtti,
// else 0 (behaviour read in re/functions/0x00767520.md C12).
using ShaderInterfaceCastFn = void*(__cdecl*)(const void* rtti, const void* object);

// The NiD3DShaderInterface slots 10..19 (vtable +0x28..+0x4C), thiscall with
// ECX = the shader (MOV ECX,EBX before each call), callee-cleaned: the body
// never adjusts ESP after them and reuses the same [ESP+n] offsets (read).
// Every argument is one dword; the types are proposals, so the views carry
// them as std::uint32_t - the same ABI on this 32-bit target.
using ShaderSlot0Fn = std::uint32_t(__thiscall*)(void* shader);
using ShaderSlot4Fn = std::uint32_t(__thiscall*)(void* shader, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t);
using ShaderSlot7Fn = std::uint32_t(__thiscall*)(void* shader, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using ShaderSlot8Fn = std::uint32_t(__thiscall*)(void* shader, std::uint32_t, std::uint32_t, std::uint32_t,
	std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);

struct ShaderInterfaceVtableView {
	void* unread00[10];
	ShaderSlot7Fn slot10;  // +0x28 0x0076733B (result tested, B3)
	ShaderSlot7Fn slot11;  // +0x2C 0x00767365
	ShaderSlot7Fn slot12;  // +0x30 0x00767398
	ShaderSlot8Fn slot13;  // +0x34 0x007673BA
	ShaderSlot8Fn slot14;  // +0x38 0x007673F1
	ShaderSlot4Fn slot15;  // +0x3C 0x007673CB (result = the buffer record)
	ShaderSlot8Fn slot16;  // +0x40 0x007674BB
	ShaderSlot7Fn slot17;  // +0x44 0x007674F0
	ShaderSlot0Fn slot18;  // +0x48 0x00767371 (result tested, B4)
	ShaderSlot0Fn slot19;  // +0x4C 0x007674C9 (result tested, B9)
};
static_assert(offsetof(ShaderInterfaceVtableView, slot10) == 0x28, "slot 10 at +0x28");
static_assert(offsetof(ShaderInterfaceVtableView, slot15) == 0x3C, "slot 15 at +0x3C");
static_assert(offsetof(ShaderInterfaceVtableView, slot19) == 0x4C, "slot 19 at +0x4C");
static_assert(sizeof(ShaderInterfaceVtableView) == 0x50, "the view ends after slot 19");

struct ShaderInterfaceView {
	const ShaderInterfaceVtableView* vtable;  // +0x00 (MOV EAX,[EBX] before each slot call)
};

// IDirect3DDevice9 slots 81 and 82 (Windows SDK d3d9.h, IDirect3DDevice9
// declaration: DrawPrimitive(D3DPRIMITIVETYPE, UINT StartVertex, UINT
// PrimitiveCount) and DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT
// BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex,
// UINT primCount)); COM stdcall, the device pushed first (PUSH EAX at
// 0x00767461 / 0x007674A0). The HRESULT is ignored (read: EAX is not tested).
using DrawPrimitiveDeviceFn = std::int32_t(__stdcall*)(void* device, std::uint32_t type, std::uint32_t startVertex,
	std::uint32_t primitiveCount);
using DrawIndexedPrimitiveDeviceFn = std::int32_t(__stdcall*)(void* device, std::uint32_t type,
	std::int32_t baseVertexIndex, std::uint32_t minVertexIndex, std::uint32_t numVertices, std::uint32_t startIndex,
	std::uint32_t primitiveCount);

struct DeviceVtableView {
	void* unread000[81];
	DrawPrimitiveDeviceFn drawPrimitive;                // +0x144 (0x00767491)
	DrawIndexedPrimitiveDeviceFn drawIndexedPrimitive;  // +0x148 (0x0076744B)
};
static_assert(offsetof(DeviceVtableView, drawPrimitive) == 0x144, "DrawPrimitive at +0x144 (slot 81)");
static_assert(offsetof(DeviceVtableView, drawIndexedPrimitive) == 0x148, "DrawIndexedPrimitive at +0x148 (slot 82)");

struct DeviceView {
	const DeviceVtableView* vtable;  // +0x00 (MOV EDX,[EAX] 0x00767449 / 0x0076748F)
};

// [renderer+0x8AC] (render state, proposal NiDX9RenderState, unverified) and
// its +0xFF0 sub-object. Slot +0x04 of the sub-object: thiscall, no stack
// argument (0x00767403..0x0076740E). Slot +0xFC of the render state: thiscall,
// one dword, called with 0 (0x00767502..0x0076750C).
using SubObjectSlot1Fn = void(__thiscall*)(void* subObject);
using SetDrawFlagFn = void(__thiscall*)(void* renderState, std::uint32_t value);

struct RenderSubObjectVtableView {
	void* slot0;
	SubObjectSlot1Fn slot1;  // +0x04 (MOV EAX,[EDX+0x4] 0x0076740B)
};
static_assert(offsetof(RenderSubObjectVtableView, slot1) == 0x04, "slot 1 at +0x04");

struct RenderSubObjectView {
	const RenderSubObjectVtableView* vtable;  // +0x00 (MOV EDX,[ECX] 0x00767409)
};

struct RenderStateVtableView {
	void* unread00[63];
	SetDrawFlagFn setDrawFlag;  // +0xFC (MOV EDX,[EAX+0xFC] 0x00767502); name a proposal
};
static_assert(offsetof(RenderStateVtableView, setDrawFlag) == 0xFC, "draw flag at +0xFC");

struct RenderStateView {
	const RenderStateVtableView* vtable;  // +0x000 (MOV EAX,[EDI] 0x00767500)
	std::uint8_t unread004[0xFEC];
	RenderSubObjectView* subObject;       // +0xFF0 (MOV ECX,[EAX+0xFF0] 0x00767403)
};
static_assert(offsetof(RenderStateView, subObject) == 0xFF0, "sub-object at +0xFF0");

// this.
struct NiDX9RendererView {
	std::uint8_t unread000[0x0C];
	std::uint32_t propertyState;    // +0x0C (MOV EDX,[EDI+0xC] e.g. 0x00767332); NiPropertyState* (proposal)
	std::uint32_t effectState;      // +0x10 (MOV ECX,[EDI+0x10] e.g. 0x0076732E); meaning unverified
	std::uint8_t unread014[0x26C];
	DeviceView* device;             // +0x280 (0x00767443, 0x00767489); OBVR GameAddresses.h and xOBSE agree
	std::uint8_t unread284[0x628];
	RenderStateView* renderState;   // +0x8AC (0x007673FD, 0x007674FA)
	std::uint8_t unread8B0[0x1E4];
	ShaderInterfaceView* defaultShader;  // +0xA94 (MOV EBX,[EDI+0xA94] 0x00767319)
};
static_assert(offsetof(NiDX9RendererView, propertyState) == 0x0C, "property state at +0x0C");
static_assert(offsetof(NiDX9RendererView, effectState) == 0x10, "effect state at +0x10");
static_assert(offsetof(NiDX9RendererView, device) == 0x280, "device at +0x280");
static_assert(offsetof(NiDX9RendererView, renderState) == 0x8AC, "render state at +0x8AC");
static_assert(offsetof(NiDX9RendererView, defaultShader) == 0xA94, "default shader at +0xA94");
static_assert(sizeof(NiDX9RendererView) == 0xA98, "the view ends after the default shader");

// a1.
struct NiGeometryView {
	std::uint8_t unread00[0xBC];
	void* shader;  // +0xBC (MOV EAX,[EBP+0xBC] 0x007672FF), cast to NiD3DShaderInterface
};
static_assert(offsetof(NiGeometryView, shader) == 0xBC, "shader at +0xBC");

// a6 and the slot 15 results (GeometryBufferData, re/functions/0x00763130.md).
struct GeometryBufferDataView {
	std::uint8_t unread00[0x14];
	std::uint32_t vertexCount;            // +0x14 (0x00767453)
	std::uint8_t unread18[0x18];
	std::uint32_t indexBuffer;            // +0x30 (0x00767412), IDirect3DIndexBuffer9*, tested only
	std::uint32_t baseVertexIndex;        // +0x34 (0x00767457, 0x00767498)
	std::uint32_t primitiveType;          // +0x38 (0x0076745D, 0x0076749C)
	std::uint32_t triangleCount;          // +0x3C (0x0076743C, 0x00767486)
	std::uint8_t unread40[0x04];
	std::uint32_t arrayCount;             // +0x44 (0x00767417, 0x00767473)
	const std::uint16_t* arrayLengths;    // +0x48 (0x00767428)
};
static_assert(offsetof(GeometryBufferDataView, vertexCount) == 0x14, "vertex count at +0x14");
static_assert(offsetof(GeometryBufferDataView, indexBuffer) == 0x30, "index buffer at +0x30");
static_assert(offsetof(GeometryBufferDataView, baseVertexIndex) == 0x34, "base vertex at +0x34");
static_assert(offsetof(GeometryBufferDataView, primitiveType) == 0x38, "primitive type at +0x38");
static_assert(offsetof(GeometryBufferDataView, triangleCount) == 0x3C, "triangle count at +0x3C");
static_assert(offsetof(GeometryBufferDataView, arrayCount) == 0x44, "array count at +0x44");
static_assert(offsetof(GeometryBufferDataView, arrayLengths) == 0x48, "lengths at +0x48");
static_assert(sizeof(GeometryBufferDataView) == 0x4C, "the view ends after the lengths");

// 0x007672F0 itself: thiscall, ECX = this (moved to EDI at 0x007672FB), six
// dword stack arguments, callee-cleaned (RET 0x18 at 0x00767513); no
// meaningful return (EAX is the last callee's, ignored by both callers).
using DrawPrimitiveFn = void(__thiscall*)(NiDX9RendererView* self, NiGeometryView* geometry, void* data, void* skin,
	const void* world, const void* worldBound, GeometryBufferDataView* record);

// ---- The ports over engine memory ------------------------------------------------

inline Handle ToHandle(const void* pointer) {
	return static_cast<Handle>(reinterpret_cast<std::uintptr_t>(pointer));
}

template <class T>
T* FromHandle(Handle handle) {
	return reinterpret_cast<T*>(static_cast<std::uintptr_t>(handle));
}

// memory, shader, renderState and device of the logic's Draw, for one call on
// one renderer. Every read happens when the logic asks for it, as the
// original's loads do.
struct EnginePorts {
	NiDX9RendererView* renderer;
	ShaderInterfaceCastFn cast;  // 0x00560920 in the game

	Handle GeometryShaderCandidate(Handle geometry) const {
		return ToHandle(FromHandle<NiGeometryView>(geometry)->shader);
	}
	Handle CastToShaderInterface(Handle object) const {
		return ToHandle(cast(FromHandle<const void>(kShaderInterfaceRttiAddress), FromHandle<const void>(object)));
	}
	Handle DefaultShader() const {
		return ToHandle(renderer->defaultShader);
	}
	RendererStates States() const {
		return RendererStates{renderer->propertyState, renderer->effectState};
	}
	BufferRecord ReadRecord(Handle record) const {
		const auto* view = FromHandle<const GeometryBufferDataView>(record);
		return BufferRecord{view->vertexCount, view->indexBuffer, view->baseVertexIndex, view->primitiveType,
			view->triangleCount, view->arrayCount, ToHandle(view->arrayLengths)};
	}
	std::uint16_t ReadStripLength(Handle lengths, std::uint32_t index) const {
		return FromHandle<const std::uint16_t>(lengths)[index];
	}

	std::uint32_t Slot(Handle shaderHandle, const ShaderCall& call) const {
		auto* shader = FromHandle<ShaderInterfaceView>(shaderHandle);
		const ShaderInterfaceVtableView& vt = *shader->vtable;
		const Handle* a = call.arguments;
		switch (call.slot) {
		case 10: return vt.slot10(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
		case 11: return vt.slot11(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
		case 12: return vt.slot12(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
		case 13: return vt.slot13(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
		case 14: return vt.slot14(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
		case 15: return vt.slot15(shader, a[0], a[1], a[2], a[3]);
		case 16: return vt.slot16(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
		case 17: return vt.slot17(shader, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
		case 18: return vt.slot18(shader);
		case 19: return vt.slot19(shader);
		default: return 0;  // the logic issues slots 10..19 only
		}
	}

	void SubObjectSlot1() const {
		RenderSubObjectView* sub = renderer->renderState->subObject;  // re-read per call, as 0x007673FD..0x00767403
		sub->vtable->slot1(sub);
	}
	void SetDrawFlag(std::uint32_t value) const {
		RenderStateView* state = renderer->renderState;  // 0x007674FA
		state->vtable->setDrawFlag(state, value);
	}

	void DrawIndexedPrimitive(const IndexedDraw& d) const {
		DeviceView* device = renderer->device;  // re-read per DIP (0x00767443)
		device->vtable->drawIndexedPrimitive(device, d.primitiveType, static_cast<std::int32_t>(d.baseVertexIndex),
			d.minVertexIndex, d.vertexCount, d.startIndex, d.primitiveCount);
	}
	void DrawPrimitive(const PlainDraw& d) const {
		DeviceView* device = renderer->device;  // 0x00767489
		device->vtable->drawPrimitive(device, d.primitiveType, d.startVertex, d.primitiveCount);
	}
};

// The rebuild with the original's signature, minus the calling convention:
// the plugin's thiscall detour forwards here. eyeSwitch is the port of
// StereoDraw.h (a no-op with a plan of one eye).
template <class EyeSwitch>
Outcome DrawPrimitive(NiDX9RendererView* self, NiGeometryView* geometry, void* data, void* skin, const void* world,
	const void* worldBound, GeometryBufferDataView* record, stereo_draw::EyePlan plan, EyeSwitch& eyeSwitch,
	ShaderInterfaceCastFn cast) {
	EnginePorts ports{self, cast};
	const DrawInput in{ToHandle(geometry), ToHandle(data), ToHandle(skin), ToHandle(world), ToHandle(worldBound),
		ToHandle(record)};
	return Draw(in, plan, ports, ports, ports, ports, eyeSwitch);
}

} // namespace obng::engine::render::draw_primitive::engine
