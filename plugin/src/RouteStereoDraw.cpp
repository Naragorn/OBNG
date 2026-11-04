// The stereo draw's route: two entry detours - DrawPrimitive 0x007672F0 and
// DrawSkinnedPrimitive 0x00767520 - that run the rebuilds of the stereo-draw
// slice over the game's own objects (src/engine/render/
// NiDX9Renderer_DrawPrimitive.h, NiDX9Renderer_DrawSkinnedPrimitive.h) with
// the eye plan of [Route] StereoEyes. The originals are not called: the
// rebuilds make the originals' calls themselves, in the originals' order
// (verified against the emulator's traces of the originals in the tests).
//
// The eye switch (StereoEyeSwitchLogic.h) runs between the two draws of a
// partition when eye data exists - set by OBVR through StereoEyesApi.h, or
// the plugin's own [Route] StereoTestEyeShift - and does, in this order: the
// second eye's translation rows into the renderer's view +0x980 and inverse
// view +0xA00 (re/functions/0x00762640.md C2, C3), device SetTransform(VIEW,
// +0x980) as the engine does (K1 there), SetViewport with the second eye's
// rectangle, and the constants re-applied by calling the shader's slot 14
// again with the partition's own arguments (NiD3DShader::SetupShaderPrograms
// 0x0077A1F0: the program bind and both constant maps; the whole call, so
// that the extra-data cursors it resets come first, re/functions/
// 0x0077A1F0.md C12). Then the draws, then the rows and the viewport back.
// Without eye data the switch only counts, so the double draw stays what
// step 3b measured.

#include "RouteStereoDraw.h"

#include <windows.h>

#include <cstdio>

#include "Probes.h"
#include "RouteStereoDrawLogic.h"
#include "ShadowRecorder.h"
#include "StereoEyeSwitchLogic.h"
#include "StereoEyesApi.h"
#include "engine/render/NiDX9Renderer_DrawPrimitive.h"
#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitive.h"

namespace obng::plugin {
namespace {

namespace route = stereo_draw_route;
namespace eye = stereo_eye_switch;
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

// The renderer's matrices and device (re/functions/0x00762640.md: +0x980
// view, +0xA00 inverse view, written there; +0x280 the device, OBVR
// GameAddresses.h and xOBSE agree) and the device's methods by vtable
// offset (Windows SDK d3d9.h IDirect3DDevice9: SetTransform 44 = +0xB0,
// SetViewport 47 = +0xBC, GetViewport 48 = +0xC0; D3DTS_VIEW = 2 in
// d3d9types.h). SetTransform and SetViewport are the engine's own calls K1
// and K6 of 0x00762640; GetViewport only serves the test mode.
constexpr std::uint32_t kRendererDeviceOffset = 0x280;
constexpr std::uint32_t kRendererViewMatrixOffset = 0x980;
constexpr std::uint32_t kRendererInverseViewOffset = 0xA00;
constexpr std::uint32_t kDeviceSetTransformOffset = 0xB0;
constexpr std::uint32_t kDeviceSetViewportOffset = 0xBC;
constexpr std::uint32_t kDeviceGetViewportOffset = 0xC0;
constexpr std::uint32_t kD3DTransformView = 2;
using SetTransformFn = std::int32_t(__stdcall*)(void* device, std::uint32_t state, const float* matrix);
using SetViewportFn = std::int32_t(__stdcall*)(void* device, const eye::Viewport* viewport);
using GetViewportFn = std::int32_t(__stdcall*)(void* device, eye::Viewport* viewport);

DrawPrimitiveEntryFn g_originalDrawPrimitive = nullptr;  // the trampolines, kept but not called
DrawSkinnedEntryFn g_originalDrawSkinned = nullptr;
bool g_on = false;
LogFn g_log = nullptr;
RouteSettings g_settings;

// The eye data: the interface's (render thread only, by its contract) or
// none; the test mode is a setting.
eye::EyeData g_eyes;
bool g_eyesSet = false;

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

bool HasEyeData() {
	return g_eyesSet || eye::TestModeOn(g_settings.stereoTestEyeShift);
}

// The eye switch's engine port: the renderer's two matrices, the device's
// three methods, and the constants re-apply handed in by the draw (each
// draw function captures its own slot-14 call).
struct EyeEnginePort {
	std::uintptr_t renderer;
	void (*reapply)(void* context);
	void* context;

	float* View() const { return reinterpret_cast<float*>(renderer + kRendererViewMatrixOffset); }
	float* Inverse() const { return reinterpret_cast<float*>(renderer + kRendererInverseViewOffset); }
	std::uint32_t Device() const { return ReadAt<std::uint32_t>(renderer + kRendererDeviceOffset); }

	void ReadView(float out[16]) const {
		const volatile float* m = View();
		for (int i = 0; i < 16; ++i) {
			out[i] = m[i];
		}
	}
	void ReadInverseRow(float out[3]) const {
		const volatile float* m = Inverse();
		out[0] = m[12];
		out[1] = m[13];
		out[2] = m[14];
	}
	void WriteRows(const eye::TranslationRows& rows) const { eye::WriteRows(rows, View(), Inverse()); }
	void SetViewTransform() const {
		const std::uint32_t device = Device();
		reinterpret_cast<SetTransformFn>(VtableSlot(device, kDeviceSetTransformOffset))(
		    reinterpret_cast<void*>(device), kD3DTransformView, View());
	}
	void SetViewport(const eye::Viewport& viewport) const {
		const std::uint32_t device = Device();
		reinterpret_cast<SetViewportFn>(VtableSlot(device, kDeviceSetViewportOffset))(reinterpret_cast<void*>(device), &viewport);
	}
	eye::Viewport CurrentViewport() const {
		eye::Viewport viewport;
		const std::uint32_t device = Device();
		reinterpret_cast<GetViewportFn>(VtableSlot(device, kDeviceGetViewportOffset))(reinterpret_cast<void*>(device), &viewport);
		return viewport;
	}
	void ReapplyConstants() const {
		SetProbeEyePhase(1);  // the view-dependence probe keys the uploads inside
		reapply(context);
		SetProbeEyePhase(0);
	}
};

using EngineEyeSwitch = eye::EyeSwitch<EyeEnginePort>;

// The skinned rebuild's ports over the game's memory (the views and the
// function types of NiDX9Renderer_DrawSkinnedPrimitive.h); each member is
// the one read or call the original makes at that point. The slot-14 call
// is kept for the eye switch's re-apply.
struct SkinnedEnginePorts {
	skinned_engine::NiDX9RendererView* renderer;
	EngineEyeSwitch* eyes;
	skinned::Handle lastShader = skinned::kNull;
	skinned::PartitionCall lastSetup{};

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
	void shaderSlot14(Handle s, const skinned::PartitionCall& c) {
		lastShader = s;
		lastSetup = c;
		Partition(s, skinned_engine::kShaderSlotOffset14, c);
	}
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
	void eyeSwitch(std::uint8_t e) { eyes->Switch(e); }

	// The re-apply: slot 14 again, as the partition had it.
	static void Reapply(void* context) {
		auto* self = static_cast<SkinnedEnginePorts*>(context);
		if (self->lastShader != skinned::kNull) {
			self->Partition(self->lastShader, skinned_engine::kShaderSlotOffset14, self->lastSetup);
		}
	}
};
static_assert(skinned::DrawSkinnedPrimitivePorts<SkinnedEnginePorts>, "the engine ports meet the rebuild's concept");

// The whole draw's shader port: the rebuild's own engine port, with the
// slot-14 call kept for the eye switch's re-apply.
struct RecordingShaderPort {
	whole_engine::EnginePorts* engine;
	whole::Handle lastShader = 0;
	whole::ShaderCall lastSetup{};

	std::uint32_t Slot(whole::Handle shader, const whole::ShaderCall& call) {
		if (call.slot == 14) {
			lastShader = shader;
			lastSetup = call;
		}
		return engine->Slot(shader, call);
	}

	static void Reapply(void* context) {
		auto* self = static_cast<RecordingShaderPort*>(context);
		if (self->lastShader != 0) {
			self->engine->Slot(self->lastShader, self->lastSetup);
		}
	}
};

UInt32 __fastcall RouteDrawPrimitive(void* self, void* /*edx*/, UInt32 geometry, UInt32 data, UInt32 skin, UInt32 world, UInt32 worldBound,
                                     UInt32 record) {
	whole_engine::EnginePorts ports{static_cast<whole_engine::NiDX9RendererView*>(self),
	                                reinterpret_cast<whole_engine::ShaderInterfaceCastFn>(whole_engine::kShaderInterfaceCastAddress)};
	RecordingShaderPort shader{&ports};
	eye::SwitchCounters counters;
	EyeEnginePort eyePort{reinterpret_cast<std::uintptr_t>(self), &RecordingShaderPort::Reapply, &shader};
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, g_eyesSet ? &g_eyes : nullptr, g_settings.stereoTestEyeShift};
	const whole::DrawInput in{geometry, data, skin, world, worldBound, record};
	const whole::Outcome outcome = whole::Draw(in, route::PlanFor(g_settings, HasEyeData()), ports, shader, ports, ports, eyeSwitch);
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.AddWhole(outcome);
	g_stats.AddSwitches(counters);
	ReleaseSRWLockExclusive(&g_statsLock);
	return 0;
}

UInt32 __fastcall RouteDrawSkinned(void* self, void* /*edx*/, UInt32 geometry, UInt32 data, UInt32 skin, UInt32 world, UInt32 worldBound) {
	eye::SwitchCounters counters;
	SkinnedEnginePorts ports{static_cast<skinned_engine::NiDX9RendererView*>(self), nullptr};
	EyeEnginePort eyePort{reinterpret_cast<std::uintptr_t>(self), &SkinnedEnginePorts::Reapply, &ports};
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, g_eyesSet ? &g_eyes : nullptr, g_settings.stereoTestEyeShift};
	ports.eyes = &eyeSwitch;
	const skinned::DrawArguments args{static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)), geometry, data, skin, world, worldBound};
	const skinned::Outcome outcome = skinned::DrawSkinnedPrimitive(ports, args, route::PlanFor(g_settings, HasEyeData()));
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.AddSkinned(outcome);
	g_stats.AddSwitches(counters);
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
	g_settings = settings;
	g_stats.eyes = settings.stereoEyes;
	UInt32 made = 0;
	made += InstallEntryDetour("NiDX9Renderer::DrawPrimitive", whole_engine::kDrawPrimitiveAddress, kDrawPrimitiveBytes, sizeof(kDrawPrimitiveBytes),
	                           reinterpret_cast<const void*>(&RouteDrawPrimitive), reinterpret_cast<void**>(&g_originalDrawPrimitive), log,
	                           DetourRole::Route) ? 1 : 0;
	made += InstallEntryDetour("NiDX9Renderer::DrawSkinnedPrimitive", skinned_engine::kDrawSkinnedPrimitiveAddress, kDrawSkinnedBytes,
	                           sizeof(kDrawSkinnedBytes), reinterpret_cast<const void*>(&RouteDrawSkinned),
	                           reinterpret_cast<void**>(&g_originalDrawSkinned), log, DetourRole::Route) ? 1 : 0;
	g_on = made == 2;
	log("Route: stereo draw %s, eyes %u, test eye shift %.3f (%s)", g_on ? "on" : "off (not both detours made)",
	    static_cast<unsigned>(settings.stereoEyes), static_cast<double>(settings.stereoTestEyeShift),
	    eye::TestModeOn(settings.stereoTestEyeShift) ? "the viewport split in halves" : "off: eye data from OBVR or none");
	return made;
}

UInt32 StereoPlan() {
	return eye::ReportedPlan(g_on, g_settings.stereoEyes);
}

UInt32 SetStereoEyes(const ObngStereoEyes* eyes) {
	const ObngStereoResult verdict = eye::Validate(eyes, StereoPlan());
	AcquireSRWLockExclusive(&g_statsLock);
	if (verdict == kObngStereoOk) {
		++g_stats.eyeDataSets;
	} else {
		++g_stats.eyeDataRefused;
	}
	ReleaseSRWLockExclusive(&g_statsLock);
	if (verdict != kObngStereoOk) {
		return verdict;
	}
	g_eyes = eye::FromApi(*eyes);
	g_eyesSet = true;
	return kObngStereoOk;
}

void ClearStereoEyes() {
	g_eyesSet = false;
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
