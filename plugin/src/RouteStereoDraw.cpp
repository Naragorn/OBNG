// The stereo draw's route: two entry detours - DrawPrimitive 0x007672F0 and
// DrawSkinnedPrimitive 0x00767520 - that run the rebuilds of the stereo-draw
// slice over the game's own objects (src/engine/render/
// NiDX9Renderer_DrawPrimitive.h, NiDX9Renderer_DrawSkinnedPrimitive.h) with
// the eye plan of [Route] StereoEyes. The originals are not called: the
// rebuilds make the originals' calls themselves, in the originals' order
// (verified against the emulator's traces of the originals in the tests).
// The eye switch counts and does nothing else until OBVR's double-wide
// target exists (docs/subsystems/single-pass-stereo.md, step 3b).

#include "RouteStereoDraw.h"

#include <windows.h>

#include <cstdio>

#include "RouteStereoDrawLogic.h"
#include "ShadowRecorder.h"
#include "engine/render/NiDX9Renderer_DrawPrimitive.h"
#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitive.h"

namespace obng::plugin {
namespace {

namespace route = stereo_draw_route;
namespace whole = engine::render::draw_primitive;
namespace whole_engine = engine::render::draw_primitive::engine;
namespace skinned = engine::render::draw_skinned_primitive;
namespace skinned_engine = engine::render::draw_skinned_primitive::engine;

// The entries (objdump of the reference EXE, 2026-11-04): whole instructions,
// nothing relative, no jump into them.
constexpr UInt8 kDrawPrimitiveBytes[] = {0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10};  // push ecx; push ebx; push ebp; mov ebp,[esp+10h]
constexpr UInt8 kDrawSkinnedBytes[] = {0x8B, 0x44, 0x24, 0x0C, 0x83, 0xEC, 0x1C};    // mov eax,[esp+0Ch]; sub esp,1Ch

// __fastcall with a dummy edx mirrors __thiscall; the callee cleans the
// stack arguments as the originals do (RET 0x18 and RET 0x14, read).
using DrawPrimitiveEntryFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32);
using DrawSkinnedEntryFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32);

DrawPrimitiveEntryFn g_originalDrawPrimitive = nullptr;  // the trampolines, kept but not called
DrawSkinnedEntryFn g_originalDrawSkinned = nullptr;
bool g_on = false;
LogFn g_log = nullptr;
engine::render::stereo_draw::EyePlan g_plan{1};

SRWLOCK g_statsLock = SRWLOCK_INIT;
route::Stats g_stats;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

UInt32 VtableSlot(std::uint32_t object, std::uint32_t slotOffset) {
	const std::uint32_t vtable = ReadAt<std::uint32_t>(object);
	return ReadAt<std::uint32_t>(vtable + slotOffset);
}

// The skinned rebuild's ports over the game's memory (the views and the
// function types of NiDX9Renderer_DrawSkinnedPrimitive.h); each member is
// the one read or call the original makes at that point.
struct SkinnedEnginePorts {
	skinned_engine::NiDX9RendererView* renderer;
	route::Stats* stats;

	using Handle = skinned::Handle;
	static void* Object(Handle h) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(h)); }

	Handle shaderCandidate(Handle geometry) const { return ReadAt<Handle>(geometry + skinned_engine::kGeometryShaderOffset); }
	Handle castToShaderInterface(Handle object) const {
		return reinterpret_cast<skinned_engine::RttiCastFn>(skinned_engine::kRttiCastAddress)(skinned_engine::kNiRttiNiD3DShaderInterface, object);
	}
	Handle defaultShader() const { return renderer->defaultShader; }
	Handle propertyState() const { return renderer->propertyState; }
	Handle effectState() const { return renderer->effectState; }
	Handle skinPartition(Handle skin) const { return ReadAt<Handle>(skin + offsetof(skinned_engine::NiSkinInstanceView, skinPartition)); }
	Handle partitions(Handle sp) const { return ReadAt<Handle>(sp + offsetof(skinned_engine::NiSkinPartitionView, partitions)); }
	std::uint32_t partitionCount(Handle sp) const { return ReadAt<std::uint32_t>(sp + offsetof(skinned_engine::NiSkinPartitionView, partitionCount)); }
	Handle partitionAt(Handle array, std::uint32_t index) const { return array + skinned_engine::kPartitionStride * index; }
	std::uint16_t stripCount(Handle p) const { return ReadAt<std::uint16_t>(p + offsetof(skinned_engine::PartitionView, stripCount)); }
	std::uint16_t boneCount(Handle p) const { return ReadAt<std::uint16_t>(p + offsetof(skinned_engine::PartitionView, boneCount)); }
	std::uint16_t vertexCount(Handle p) const { return ReadAt<std::uint16_t>(p + offsetof(skinned_engine::PartitionView, vertexCount)); }
	Handle bufferData(Handle p) const { return ReadAt<Handle>(p + offsetof(skinned_engine::PartitionView, bufferData)); }
	std::uint32_t maxBonesWithoutShader() const { return renderer->maxBonesWithoutShader; }
	std::uint32_t worldBoundWord(Handle bound, std::uint32_t k) const { return ReadAt<std::uint32_t>(bound + 4 * k); }
	void setCurrentWorldBoundWord(std::uint32_t k, std::uint32_t v) const { renderer->currentWorldBound[k] = v; }
	std::uint16_t dataFlags(Handle data) const { return ReadAt<std::uint16_t>(data + offsetof(skinned_engine::NiGeometryDataView, flags)); }
	void setDataFlags(Handle data, std::uint16_t v) const {
		*reinterpret_cast<volatile std::uint16_t*>(static_cast<std::uintptr_t>(data) + offsetof(skinned_engine::NiGeometryDataView, flags)) = v;
	}
	std::uint32_t arrayCount(Handle r) const { return ReadAt<std::uint32_t>(r + offsetof(skinned_engine::BufferRecordView, arrayCount)); }
	Handle arrayLengths(Handle r) const { return ReadAt<Handle>(r + offsetof(skinned_engine::BufferRecordView, arrayLengths)); }
	std::uint16_t arrayLength(Handle lengths, std::uint32_t i) const { return ReadAt<std::uint16_t>(lengths + 2 * i); }
	std::uint32_t triangleCount(Handle r) const { return ReadAt<std::uint32_t>(r + offsetof(skinned_engine::BufferRecordView, triangleCount)); }
	std::uint32_t baseVertexIndex(Handle r) const { return ReadAt<std::uint32_t>(r + offsetof(skinned_engine::BufferRecordView, baseVertexIndex)); }
	Handle device() const { return renderer->device; }

	std::uint32_t Pipeline(Handle shader, std::uint32_t slot, const skinned::PipelineCall& c) const {
		return reinterpret_cast<skinned_engine::ShaderPipelineFn>(VtableSlot(shader, slot))(Object(shader), c.geometry, c.skin, c.record,
		                                                                                    c.propertyState, c.effectState, c.world, c.worldBound);
	}
	void Partition(Handle shader, std::uint32_t slot, const skinned::PartitionCall& c) const {
		reinterpret_cast<skinned_engine::ShaderPartitionFn>(VtableSlot(shader, slot))(Object(shader), c.geometry, c.skin, c.partition, c.record,
		                                                                              c.propertyState, c.effectState, c.world, c.worldBound);
	}
	std::uint32_t shaderSlot10(Handle s, const skinned::PipelineCall& c) const { return Pipeline(s, skinned_engine::kShaderSlotOffset10, c); }
	void shaderSlot11(Handle s, const skinned::PipelineCall& c) const { Pipeline(s, skinned_engine::kShaderSlotOffset11, c); }
	void shaderSlot12(Handle s, const skinned::PipelineCall& c) const { Pipeline(s, skinned_engine::kShaderSlotOffset12, c); }
	void shaderSlot13(Handle s, const skinned::PartitionCall& c) const { Partition(s, skinned_engine::kShaderSlotOffset13, c); }
	void shaderSlot14(Handle s, const skinned::PartitionCall& c) const { Partition(s, skinned_engine::kShaderSlotOffset14, c); }
	Handle shaderSlot15(Handle s, const skinned::RecordCall& c) const {
		return reinterpret_cast<skinned_engine::ShaderRecordFn>(VtableSlot(s, skinned_engine::kShaderSlotOffset15))(Object(s), c.geometry, c.partition,
		                                                                                                              c.bufferData, c.propertyState);
	}
	void shaderSlot16(Handle s, const skinned::PartitionCall& c) const { Partition(s, skinned_engine::kShaderSlotOffset16, c); }
	void shaderSlot17(Handle s, const skinned::PipelineCall& c) const { Pipeline(s, skinned_engine::kShaderSlotOffset17, c); }
	std::uint32_t shaderSlot18(Handle s) const {
		return reinterpret_cast<skinned_engine::ShaderPassFn>(VtableSlot(s, skinned_engine::kShaderSlotOffset18))(Object(s));
	}
	std::uint32_t shaderSlot19(Handle s) const {
		return reinterpret_cast<skinned_engine::ShaderPassFn>(VtableSlot(s, skinned_engine::kShaderSlotOffset19))(Object(s));
	}
	void cannotRenderPartition(std::uint32_t bones) const {
		reinterpret_cast<skinned_engine::DebugPrintFn>(skinned_engine::kDebugPrintAddress)(
		    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(skinned_engine::kCannotRenderPartitionFormat)), bones);
	}
	void renderStateSubObjectSlot1() const {
		const std::uint32_t sub = ReadAt<std::uint32_t>(renderer->renderState + skinned_engine::kRenderStateSubObjectOffset);
		reinterpret_cast<skinned_engine::SubObjectSlot1Fn>(VtableSlot(sub, skinned_engine::kSubObjectSlot1))(Object(sub));
	}
	void renderStateSetDrawFlag(std::uint32_t v) const {
		reinterpret_cast<skinned_engine::SetDrawFlagFn>(VtableSlot(renderer->renderState, skinned_engine::kRenderStateSetDrawFlagSlot))(
		    Object(renderer->renderState), v);
	}
	void drawIndexedPrimitive(const skinned::DrawIndexedCall& d) const {
		reinterpret_cast<skinned_engine::DrawIndexedPrimitiveFn>(VtableSlot(d.device, skinned_engine::kDeviceDrawIndexedPrimitiveOffset))(
		    Object(d.device), d.primitiveType, static_cast<std::int32_t>(d.baseVertexIndex), d.minVertexIndex, d.numVertices, d.startIndex,
		    d.primitiveCount);
	}
	void eyeSwitch(std::uint8_t eye) const {
		route::CountingEyeSwitch{stats}.Switch(eye);
	}
};
static_assert(skinned::DrawSkinnedPrimitivePorts<SkinnedEnginePorts>, "the engine ports meet the rebuild's concept");

UInt32 __fastcall RouteDrawPrimitive(void* self, void* /*edx*/, UInt32 geometry, UInt32 data, UInt32 skin, UInt32 world, UInt32 worldBound,
                                     UInt32 record) {
	route::Stats local;
	local.eyes = g_plan.eyes;
	route::CountingEyeSwitch eyeSwitch{&local};
	const whole::Outcome outcome = whole_engine::DrawPrimitive(
	    static_cast<whole_engine::NiDX9RendererView*>(self), whole_engine::FromHandle<whole_engine::NiGeometryView>(geometry),
	    whole_engine::FromHandle<void>(data), whole_engine::FromHandle<void>(skin), whole_engine::FromHandle<const void>(world),
	    whole_engine::FromHandle<const void>(worldBound), whole_engine::FromHandle<whole_engine::GeometryBufferDataView>(record), g_plan, eyeSwitch,
	    reinterpret_cast<whole_engine::ShaderInterfaceCastFn>(whole_engine::kShaderInterfaceCastAddress));
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.AddWhole(outcome);
	g_stats.eyeSwitches += local.eyeSwitches;
	ReleaseSRWLockExclusive(&g_statsLock);
	return 0;
}

UInt32 __fastcall RouteDrawSkinned(void* self, void* /*edx*/, UInt32 geometry, UInt32 data, UInt32 skin, UInt32 world, UInt32 worldBound) {
	route::Stats local;
	local.eyes = g_plan.eyes;
	SkinnedEnginePorts ports{static_cast<skinned_engine::NiDX9RendererView*>(self), &local};
	const skinned::DrawArguments args{static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)), geometry, data, skin, world, worldBound};
	const skinned::Outcome outcome = skinned::DrawSkinnedPrimitive(ports, args, g_plan);
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.AddSkinned(outcome);
	g_stats.eyeSwitches += local.eyeSwitches;
	ReleaseSRWLockExclusive(&g_statsLock);
	return 0;
}

}  // namespace

UInt32 InstallRouteStereoDraw(const RouteSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.stereoDraw) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_plan = route::PlanFor(settings);
	g_stats.eyes = settings.stereoEyes;
	UInt32 made = 0;
	made += InstallEntryDetour("NiDX9Renderer::DrawPrimitive", whole_engine::kDrawPrimitiveAddress, kDrawPrimitiveBytes, sizeof(kDrawPrimitiveBytes),
	                           reinterpret_cast<const void*>(&RouteDrawPrimitive), reinterpret_cast<void**>(&g_originalDrawPrimitive), log,
	                           DetourRole::Route) ? 1 : 0;
	made += InstallEntryDetour("NiDX9Renderer::DrawSkinnedPrimitive", skinned_engine::kDrawSkinnedPrimitiveAddress, kDrawSkinnedBytes,
	                           sizeof(kDrawSkinnedBytes), reinterpret_cast<const void*>(&RouteDrawSkinned),
	                           reinterpret_cast<void**>(&g_originalDrawSkinned), log, DetourRole::Route) ? 1 : 0;
	g_on = made == 2;
	log("Route: stereo draw %s, eyes %u", g_on ? "on" : "off (not both detours made)", static_cast<unsigned>(settings.stereoEyes));
	return made;
}

void LogStereoDrawSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	AcquireSRWLockExclusive(&g_statsLock);
	const route::Stats stats = g_stats;
	g_stats.Reset();
	ReleaseSRWLockExclusive(&g_statsLock);
	char line[400];
	route::FormatStats(stats, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
