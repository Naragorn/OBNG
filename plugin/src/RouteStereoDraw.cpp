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

#include <intrin.h>

#include <cmath>
#include <cstdio>

#include "ProbeLogic.h"

#include "Probes.h"
#include "RouteStereoDrawLogic.h"
#include "OcclusionProbe.h"
#include "ShadowRecorder.h"
#include "StereoEyeSwitchLogic.h"
#include "StereoEyesApi.h"
#include "UnroutedDrawProbe.h"
#include "engine/render/ConstantReapply.h"
#include "engine/render/NiD3DShaderConstantMap_ApplyConstants.h"
#include "engine/render/NiDX9Renderer_DrawPrimitive.h"
#include "engine/render/NiDX9Renderer_DrawShapeDirect.h"
#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitive.h"
#include "engine/render/NiDX9Renderer_DrawTriStripsDirect.h"

namespace obng::plugin {
namespace {

namespace route = stereo_draw_route;
namespace eye = stereo_eye_switch;
namespace whole = engine::render::draw_primitive;
namespace whole_engine = engine::render::draw_primitive::engine;
namespace skinned = engine::render::draw_skinned_primitive;
namespace skinned_engine = engine::render::draw_skinned_primitive::engine;
namespace apply_engine = engine::render::apply_constants::engine;
namespace shape_direct = engine::render::draw_shape_direct;
namespace shape_direct_engine = engine::render::draw_shape_direct::engine;
namespace strips_direct = engine::render::draw_tri_strips_direct;
namespace strips_direct_engine = engine::render::draw_tri_strips_direct::engine;
using engine::render::constant_reapply::EntryFilter;

// The entries (objdump of the reference EXE, 2026-11-04): whole instructions,
// nothing relative, no jump into them.
constexpr UInt8 kDrawPrimitiveBytes[] = {0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10};  // push ecx; push ebx; push ebp; mov ebp,[esp+10h]
constexpr UInt8 kDrawSkinnedBytes[] = {0x8B, 0x44, 0x24, 0x0C, 0x83, 0xEC, 0x1C};    // mov eax,[esp+0Ch]; sub esp,1Ch
// The renderer's direct draw entries (StereoDrawDirect.h; objdump 2026-11-04).
constexpr UInt8 kDrawShapeDirectBytes[] = {0x83, 0xEC, 0x14, 0x55, 0x8B, 0xE9};      // sub esp,14h; push ebp; mov ebp,ecx
constexpr UInt8 kDrawTriStripsDirectBytes[] = {0x83, 0xEC, 0x24, 0x55, 0x8B, 0xE9};  // sub esp,24h; push ebp; mov ebp,ecx
using DrawDirectEntryFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 geometry);
DrawDirectEntryFn g_originalDrawShapeDirect = nullptr;
DrawDirectEntryFn g_originalDrawTriStripsDirect = nullptr;

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

// The stats are the render thread's alone (every draw and the interface's
// calls run there, by the engine's design and StereoEyesApi.h's contract);
// the summary thread only asks for a line, and the next routed draw writes it
// (FlushSummaryIfRequested). No lock on the draw path: the route's per-call
// lock was 0.19 ms a pass on the fixed scene (docs/subsystems/
// single-pass-stereo.md, step 3).
route::Stats g_stats;
volatile LONG g_summaryRequested = 0;
probe::ClockPair g_summaryClock;  // the clocks at the last summary (0: none yet)

probe::ClockPair ReadClocks() {
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	return probe::ClockPair{static_cast<std::uint64_t>(__rdtsc()), static_cast<std::uint64_t>(qpc.QuadPart)};
}

std::uint64_t ClockFrequency() {
	LARGE_INTEGER f{};
	QueryPerformanceFrequency(&f);
	return static_cast<std::uint64_t>(f.QuadPart);
}

// The summary line, written by the render thread when the summary thread
// asked for one (LogStereoDrawSummary); the counts are since the last line.
void FlushSummaryIfRequested() {
	if (g_summaryRequested == 0) {
		return;
	}
	InterlockedExchange(&g_summaryRequested, 0);
	const probe::ClockPair now = ReadClocks();
	const double ticksPerMillisecond = probe::TicksPerMillisecond(g_summaryClock, now, ClockFrequency());
	g_summaryClock = now;
	char line[1400];
	route::FormatStats(g_stats, ticksPerMillisecond, line, sizeof(line));
	g_stats.Reset();
	g_log("%s", line);
}

// The ticks one draw call's switches took, split by the re-apply.
struct SwitchTicks {
	std::uint64_t all = 0;
	std::uint64_t reapply = 0;
};

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

// Whether the geometry hangs below one of the two roots the scene render
// puts at the camera every pass ([0x00B333D8], [0x00B333DC]: re/functions/
// 0x0040C830.md step 6; the sky's node is attached to the second,
// re/functions/0x00406360.md) - NiAVObject's parent at +0x1C
// (re/functions/0x0055D4C0.md, xOBSE). Such geometry gets the second eye's
// viewport without the view shift (StereoEyeSwitchLogic.h).
constexpr std::uint32_t kFollowerRootA = 0x00B333D8;
constexpr std::uint32_t kFollowerRootB = 0x00B333DC;
constexpr std::uint32_t kObjectParentOffset = 0x1C;
bool FollowsCamera(std::uint32_t geometry) {
	return eye::HasAncestor(geometry, ReadAt<std::uint32_t>(kFollowerRootA), ReadAt<std::uint32_t>(kFollowerRootB),
	                        [](std::uint32_t object) { return ReadAt<std::uint32_t>(object + kObjectParentOffset); });
}

// The first camera followers seen, once each, with what the view shift
// would have made of them: the bound's radius and how far its centre lies
// from the pass camera (the origin global) - the distance the second eye
// would have seen the geometry at.
constexpr std::uint32_t kFollowersLogged = 16;
constexpr std::uint32_t kGeometryBoundOffset = 0x20;
constexpr std::uint32_t kOriginGlobal = 0x00B3F92C;
std::uint32_t g_followersSeen[kFollowersLogged] = {};
std::uint32_t g_followersSeenCount = 0;
void NoteFollower(bool follows, std::uint32_t geometry) {
	if (!follows || g_followersSeenCount == kFollowersLogged) {
		return;
	}
	for (std::uint32_t i = 0; i < g_followersSeenCount; ++i) {
		if (g_followersSeen[i] == geometry) {
			return;
		}
	}
	g_followersSeen[g_followersSeenCount++] = geometry;
	char name[48];
	ReadObjectName(geometry, name, sizeof(name));
	const float cx = ReadAt<float>(geometry + kGeometryBoundOffset) - ReadAt<float>(kOriginGlobal);
	const float cy = ReadAt<float>(geometry + kGeometryBoundOffset + 4) - ReadAt<float>(kOriginGlobal + 4);
	const float cz = ReadAt<float>(geometry + kGeometryBoundOffset + 8) - ReadAt<float>(kOriginGlobal + 8);
	g_log("Route: camera follower \"%s\" (%08X) - bound radius %.0f, its centre %.0f units from the pass camera; drawn without the view shift",
	      name, geometry, static_cast<double>(ReadAt<float>(geometry + kGeometryBoundOffset + 12)),
	      std::sqrt(static_cast<double>(cx) * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz));
}

UInt32 VtableSlot(std::uint32_t object, std::uint32_t slotOffset) {
	const std::uint32_t vtable = ReadAt<std::uint32_t>(object);
	return ReadAt<std::uint32_t>(vtable + slotOffset);
}

// The view re-apply ([Route] StereoReapply=view): what SetupShaderPrograms
// 0x0077A1F0 does for its two maps (V2 / V3: map this+0x2C against
// [this+0x3C]+0x44, map this+0x30 against [this+0x3C]+0x58, the slot-14
// arguments 1..8, this+0x34 as the pass index, 1 - read, 0x0077A1F0.md
// C14 / C19), done by the rebuilt ApplyConstants with the view filter on
// the map(s) whose vtable is NiD3DSCM_Vertex (StereoEyeSwitchLogic.h
// ViewReapplyTargets).
constexpr std::uint32_t kShaderMapAOffset = 0x2C;
constexpr std::uint32_t kShaderMapBOffset = 0x30;
constexpr std::uint32_t kShaderPassIndexOffset = 0x34;
constexpr std::uint32_t kShaderPassObjectOffset = 0x3C;
constexpr std::uint32_t kPassProgramAOffset = 0x44;
constexpr std::uint32_t kPassProgramBOffset = 0x58;
constexpr std::uint32_t kApplyLastArgument = 1;

// What one draw call's re-applies saw, for the route's line.
struct ReapplySeen {
	std::uint32_t shaderVtable = 0;
	std::uint32_t type1 = 0;
};

// The shader whose +0x2C / +0x30 maps the re-apply walks: the shader itself,
// or for SpeedTree branches the manager's type-1 shader
// (StereoEyeSwitchLogic.h MapsSourceFor).
std::uint32_t MapsOwner(std::uint32_t shader, ReapplySeen* seen) {
	const std::uint32_t vtable = ReadAt<std::uint32_t>(shader);
	seen->shaderVtable = vtable;
	if (eye::MapsSourceFor(vtable) == eye::MapsSource::kOwn) {
		return shader;
	}
	const std::uint32_t entry = ReadAt<std::uint32_t>(eye::kShaderManagerTableAddress + 4 * eye::kShaderManagerType1);
	if (entry == 0) {
		return 0;
	}
	++seen->type1;
	return ReadAt<std::uint32_t>(entry + eye::kShaderManagerEntryShaderOffset);
}

void ViewReapply(std::uint32_t shader, const std::uint32_t args[8], ReapplySeen* seen) {
	if (shader == 0) {
		return;
	}
	const std::uint32_t pass = ReadAt<std::uint32_t>(shader + kShaderPassObjectOffset);
	if (pass == 0) {
		return;
	}
	const std::uint32_t owner = MapsOwner(shader, seen);
	if (owner == 0) {
		return;
	}
	const std::uint32_t mapA = ReadAt<std::uint32_t>(owner + kShaderMapAOffset);
	const std::uint32_t mapB = ReadAt<std::uint32_t>(owner + kShaderMapBOffset);
	const std::uint32_t programA = ReadAt<std::uint32_t>(pass + kPassProgramAOffset);
	const std::uint32_t programB = ReadAt<std::uint32_t>(pass + kPassProgramBOffset);
	const std::uint32_t passIndex = ReadAt<std::uint32_t>(shader + kShaderPassIndexOffset);
	const eye::ReapplyTargets targets = eye::ViewReapplyTargets(mapA, programA, mapA != 0 ? ReadAt<std::uint32_t>(mapA) : 0u, mapB, programB,
	                                                             mapB != 0 ? ReadAt<std::uint32_t>(mapB) : 0u);
	for (std::uint8_t i = 0; i < targets.count; ++i) {
		apply_engine::ApplyConstants(apply_engine::FromHandle<apply_engine::ShaderConstantMapView>(targets.target[i].map),
		                             targets.target[i].program, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7],
		                             passIndex, kApplyLastArgument, EntryFilter::kView,
		                             reinterpret_cast<apply_engine::GetExtraDataFn>(static_cast<std::uintptr_t>(apply_engine::kGetExtraDataAddress)),
		                             apply_engine::FromHandle<const char* const>(apply_engine::kExtraDataNameSlotAddress));
	}
}

// The eye data for one draw call: the interface's when the device's
// viewport is eye 0's (StereoEyeSwitchLogic.h, the viewport gate), the test
// mode's otherwise; null when neither. outside counts a gated-out draw.
template <class Port>
const eye::EyeData* EyesForDraw(const Port& port, bool* outside, eye::Viewport* seen) {
	*outside = false;
	if (g_eyesSet) {
		*seen = port.CurrentViewport();
		if (eye::InEyeViewport(*seen, g_eyes)) {
			return &g_eyes;
		}
		*outside = true;
		return nullptr;
	}
	return nullptr;
}

bool TestMode() {
	return eye::TestModeOn(g_settings.stereoTestEyeShift);
}

// The eye switch's engine port: the renderer's two matrices, the device's
// three methods, and the constants re-apply handed in by the draw (each
// draw function captures its own slot-14 call).
struct EyeEnginePort {
	std::uintptr_t renderer;
	void (*reapply)(void* context);
	void* context;
	SwitchTicks* ticks;

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
		const std::uint64_t t0 = __rdtsc();
		SetProbeEyePhase(1);  // the view-dependence probe keys the uploads inside
		reapply(context);
		SetProbeEyePhase(0);
		ticks->reapply += __rdtsc() - t0;
	}
};

using EngineEyeSwitch = eye::EyeSwitch<EyeEnginePort>;

// The switch timed as a whole (both Switch calls), for the route's line.
struct TimedEyeSwitch {
	EngineEyeSwitch* inner;
	SwitchTicks* ticks;
	void Switch(std::uint8_t e) {
		const std::uint64_t t0 = __rdtsc();
		inner->Switch(e);
		ticks->all += __rdtsc() - t0;
	}
};

// The skinned rebuild's ports over the game's memory (the views and the
// function types of NiDX9Renderer_DrawSkinnedPrimitive.h); each member is
// the one read or call the original makes at that point. The slot-14 call
// is kept for the eye switch's re-apply.
struct SkinnedEnginePorts {
	skinned_engine::NiDX9RendererView* renderer;
	TimedEyeSwitch* eyes;
	ReapplySeen* seen;
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

	// The re-apply: slot 14 again as the partition had it, or the view
	// re-apply with the same arguments.
	static void Reapply(void* context) {
		auto* self = static_cast<SkinnedEnginePorts*>(context);
		if (self->lastShader == skinned::kNull) {
			return;
		}
		if (g_settings.stereoReapply == StereoReapply::kSlot14) {
			self->Partition(self->lastShader, skinned_engine::kShaderSlotOffset14, self->lastSetup);
			return;
		}
		if (g_settings.stereoReapply == StereoReapply::kNone) {
			return;
		}
		const skinned::PartitionCall& c = self->lastSetup;
		const std::uint32_t args[8] = {c.geometry, c.skin, c.partition, c.record, c.propertyState, c.effectState, c.world, c.worldBound};
		ViewReapply(self->lastShader, args, self->seen);
	}
};
static_assert(skinned::DrawSkinnedPrimitivePorts<SkinnedEnginePorts>, "the engine ports meet the rebuild's concept");

// The whole draw's shader port: the rebuild's own engine port, with the
// slot-14 call kept for the eye switch's re-apply.
struct RecordingShaderPort {
	whole_engine::EnginePorts* engine;
	ReapplySeen* seen;
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
		if (self->lastShader == 0) {
			return;
		}
		if (g_settings.stereoReapply == StereoReapply::kSlot14) {
			self->engine->Slot(self->lastShader, self->lastSetup);
			return;
		}
		if (g_settings.stereoReapply == StereoReapply::kNone) {
			return;
		}
		ViewReapply(self->lastShader, self->lastSetup.arguments, self->seen);
	}
};

UInt32 __fastcall RouteDrawPrimitive(void* self, void* /*edx*/, UInt32 geometry, UInt32 data, UInt32 skin, UInt32 world, UInt32 worldBound,
                                     UInt32 record) {
	const RoutedDrawScope routed{self};
	whole_engine::EnginePorts ports{static_cast<whole_engine::NiDX9RendererView*>(self),
	                                reinterpret_cast<whole_engine::ShaderInterfaceCastFn>(whole_engine::kShaderInterfaceCastAddress)};
	ReapplySeen seen_;
	RecordingShaderPort shader{&ports, &seen_};
	eye::SwitchCounters counters;
	SwitchTicks ticks;
	EyeEnginePort eyePort{reinterpret_cast<std::uintptr_t>(self), &RecordingShaderPort::Reapply, &shader, &ticks};
	bool outside = false;
	eye::Viewport seen;
	const eye::EyeData* eyes = EyesForDraw(eyePort, &outside, &seen);
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	eyeSwitch.viewportOnly = g_settings.stereoFollowers && (eyes != nullptr || (!outside && TestMode())) && FollowsCamera(geometry);
	NoteFollower(eyeSwitch.viewportOnly, geometry);
	TimedEyeSwitch timed{&eyeSwitch, &ticks};
	const whole::DrawInput in{geometry, data, skin, world, worldBound, record};
	const whole::Outcome outcome =
	    whole::Draw(in, route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode())), ports, shader, ports, ports, timed);
	g_stats.AddWhole(outcome);
	g_stats.AddSwitches(counters);
	g_stats.AddTicks(ticks.all, ticks.reapply);
	g_stats.drawsOutsideEye += outside ? 1u : 0u;
	if (outside) {
		g_stats.outside.Add(seen);
	}
	if (counters.applied != 0 && seen_.shaderVtable != 0) {
		g_stats.shaderClasses.Add(seen_.shaderVtable);
		g_stats.reappliesType1 += seen_.type1;
	}
	FlushSummaryIfRequested();
	return 0;
}

UInt32 __fastcall RouteDrawSkinned(void* self, void* /*edx*/, UInt32 geometry, UInt32 data, UInt32 skin, UInt32 world, UInt32 worldBound) {
	const RoutedDrawScope routed{self};
	eye::SwitchCounters counters;
	ReapplySeen seen_;
	SkinnedEnginePorts ports{static_cast<skinned_engine::NiDX9RendererView*>(self), nullptr, &seen_};
	SwitchTicks ticks;
	EyeEnginePort eyePort{reinterpret_cast<std::uintptr_t>(self), &SkinnedEnginePorts::Reapply, &ports, &ticks};
	bool outside = false;
	eye::Viewport seen;
	const eye::EyeData* eyes = EyesForDraw(eyePort, &outside, &seen);
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	eyeSwitch.viewportOnly = g_settings.stereoFollowers && (eyes != nullptr || (!outside && TestMode())) && FollowsCamera(geometry);
	NoteFollower(eyeSwitch.viewportOnly, geometry);
	TimedEyeSwitch timed{&eyeSwitch, &ticks};
	ports.eyes = &timed;
	const skinned::DrawArguments args{static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)), geometry, data, skin, world, worldBound};
	const skinned::Outcome outcome =
	    skinned::DrawSkinnedPrimitive(ports, args, route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode())));
	g_stats.AddSkinned(outcome);
	g_stats.AddSwitches(counters);
	g_stats.AddTicks(ticks.all, ticks.reapply);
	g_stats.drawsOutsideEye += outside ? 1u : 0u;
	if (outside) {
		g_stats.outside.Add(seen);
	}
	if (counters.applied != 0 && seen_.shaderVtable != 0) {
		g_stats.shaderClasses.Add(seen_.shaderVtable);
		g_stats.reappliesType1 += seen_.type1;
	}
	FlushSummaryIfRequested();
	return 0;
}


// ---- The direct draw entries (slots 90 / 91) ---------------------------------------
//
// No shader slot 14 runs in these; the eye switch's re-apply gets its
// arguments from the geometry as Do_RenderShape hands them to the shader
// slots (StereoDrawDirect.h): geometry, skin = geometry+0xB8, the partition
// (0 for the whole draw), the record, property / effect state = renderer
// +0x0C / +0x10, world = geometry+0x64, worldBound = geometry+0x20.
constexpr std::uint32_t kRendererPropertyStateOffset = 0x0C;
constexpr std::uint32_t kRendererEffectStateOffset = 0x10;
constexpr std::uint32_t kGeometryWorldBoundOffset = 0x20;
constexpr std::uint32_t kGeometryWorldOffset = 0x64;
constexpr std::uint32_t kGeometryDataOffset = 0xB4;
constexpr std::uint32_t kGeometrySkinOffset = 0xB8;
constexpr std::uint32_t kGeometryShaderOffset = 0xBC;
constexpr std::uint32_t kDataFlagsOffset = 0x2E;
constexpr std::uint32_t kDataRecordOffset = 0x38;
constexpr std::uint32_t kSkinPartitionOffset = 0x0C;
constexpr std::uint32_t kSkinPartitionCountOffset = 0x08;
constexpr std::uint32_t kSkinPartitionsOffset = 0x0C;
constexpr std::uint32_t kPartitionVertexCountOffset = 0x1C;
constexpr std::uint32_t kPartitionBoneCountOffset = 0x20;
constexpr std::uint32_t kPartitionStripCountOffset = 0x22;
constexpr std::uint32_t kPartitionRecordOffset = 0x28;
constexpr std::uint32_t kRecordVertexCountOffset = 0x14;
constexpr std::uint32_t kRecordIndexBufferOffset = 0x30;
constexpr std::uint32_t kRecordBaseVertexOffset = 0x34;
constexpr std::uint32_t kRecordPrimitiveTypeOffset = 0x38;
constexpr std::uint32_t kRecordTriangleCountOffset = 0x3C;
constexpr std::uint32_t kRecordArrayCountOffset = 0x44;
constexpr std::uint32_t kRecordArrayLengthsOffset = 0x48;

// What the current direct draw is drawing, for the re-apply.
struct DirectContext {
	std::uintptr_t renderer = 0;
	std::uint32_t geometry = 0;
	std::uint32_t shader = 0;     // the cast result
	std::uint32_t partition = 0;  // the partition being drawn, 0 for the whole draw
	std::uint32_t record = 0;     // its record
	ReapplySeen seen;

	static void Reapply(void* context) {
		auto* self = static_cast<DirectContext*>(context);
		if (self->shader == 0 || g_settings.stereoReapply == StereoReapply::kNone) {
			return;
		}
		const std::uint32_t args[8] = {self->geometry, ReadAt<std::uint32_t>(self->geometry + kGeometrySkinOffset), self->partition,
		                               self->record, ReadAt<std::uint32_t>(self->renderer + kRendererPropertyStateOffset),
		                               ReadAt<std::uint32_t>(self->renderer + kRendererEffectStateOffset),
		                               static_cast<std::uint32_t>(self->geometry + kGeometryWorldOffset),
		                               static_cast<std::uint32_t>(self->geometry + kGeometryWorldBoundOffset)};
		ViewReapply(self->shader, args, &self->seen);
	}
};

// The reads and calls both direct rebuilds share, over the game's memory.
struct DirectEngineBase {
	std::uintptr_t renderer;
	DirectContext* context;
	TimedEyeSwitch* eyes;

	using Handle = std::uint32_t;
	static void* Object(Handle h) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(h)); }

	Handle geometryData(Handle g) const { return ReadAt<Handle>(g + kGeometryDataOffset); }
	Handle skinPartition(Handle s) const { return ReadAt<Handle>(s + kSkinPartitionOffset); }
	std::uint32_t partitionCount(Handle sp) const { return ReadAt<std::uint32_t>(sp + kSkinPartitionCountOffset); }
	Handle partitions(Handle sp) const { return ReadAt<Handle>(sp + kSkinPartitionsOffset); }
	Handle partitionAt(Handle array, std::uint32_t i) const { return array + shape_direct_engine::kPartitionStride * i; }
	std::uint16_t stripCount(Handle p) const { return ReadAt<std::uint16_t>(p + kPartitionStripCountOffset); }
	std::uint16_t boneCount(Handle p) const { return ReadAt<std::uint16_t>(p + kPartitionBoneCountOffset); }
	std::uint16_t vertexCount(Handle p) const { return ReadAt<std::uint16_t>(p + kPartitionVertexCountOffset); }
	Handle defaultShader() const { return ReadAt<Handle>(renderer + 0xA94); }
	std::uint32_t maxBonesWithoutShader() const { return ReadAt<std::uint32_t>(renderer + 0x6D8); }
	Handle device() const { return ReadAt<Handle>(renderer + kRendererDeviceOffset); }
	std::uint32_t recordVertexCount(Handle r) const { return ReadAt<std::uint32_t>(r + kRecordVertexCountOffset); }
	std::uint32_t indexBuffer(Handle r) const { return ReadAt<std::uint32_t>(r + kRecordIndexBufferOffset); }
	std::uint32_t baseVertexIndex(Handle r) const { return ReadAt<std::uint32_t>(r + kRecordBaseVertexOffset); }
	std::uint32_t primitiveType(Handle r) const { return ReadAt<std::uint32_t>(r + kRecordPrimitiveTypeOffset); }
	std::uint32_t triangleCount(Handle r) const { return ReadAt<std::uint32_t>(r + kRecordTriangleCountOffset); }
	std::uint32_t arrayCount(Handle r) const { return ReadAt<std::uint32_t>(r + kRecordArrayCountOffset); }
	Handle arrayLengths(Handle r) const { return ReadAt<Handle>(r + kRecordArrayLengthsOffset); }
	std::uint16_t arrayLength(Handle lengths, std::uint32_t i) const { return ReadAt<std::uint16_t>(lengths + 2 * i); }
	std::uint16_t dataFlags(Handle d) const { return ReadAt<std::uint16_t>(d + kDataFlagsOffset); }
	void writeDataFlags(Handle d, std::uint16_t v) const {
		*reinterpret_cast<volatile std::uint16_t*>(static_cast<std::uintptr_t>(d) + kDataFlagsOffset) = v;
	}
	Handle castToShaderInterface(Handle o) {
		const Handle shader = reinterpret_cast<shape_direct_engine::RttiCastFn>(shape_direct_engine::kRttiCastAddress)(
		    shape_direct_engine::kNiRttiNiD3DShaderInterface, o);
		context->shader = shader;
		return shader;
	}
	void shaderSlot18(Handle shader) const {
		reinterpret_cast<shape_direct_engine::ShaderSlot18Fn>(VtableSlot(shader, shape_direct_engine::kShaderSlotOffset18))(Object(shader));
	}
	void renderStateSubObjectSlot1() const {
		const std::uint32_t sub = ReadAt<std::uint32_t>(ReadAt<std::uint32_t>(renderer + 0x8AC) + shape_direct_engine::kRenderStateSubObjectOffset);
		reinterpret_cast<shape_direct_engine::SubObjectSlot1Fn>(VtableSlot(sub, shape_direct_engine::kSubObjectSlot1))(Object(sub));
	}
	void cannotRenderPartition(std::uint32_t bones) const {
		reinterpret_cast<shape_direct_engine::DebugPrintFn>(shape_direct_engine::kDebugPrintAddress)(
		    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(shape_direct_engine::kCannotRenderPartitionFormat)), bones);
	}
	void drawIndexed(Handle device_, std::uint32_t type, std::uint32_t base, std::uint32_t minIndex, std::uint32_t numVertices,
	                 std::uint32_t start, std::uint32_t count) const {
		reinterpret_cast<shape_direct_engine::DrawIndexedPrimitiveFn>(VtableSlot(device_, shape_direct_engine::kDeviceDrawIndexedPrimitiveOffset))(
		    Object(device_), type, static_cast<std::int32_t>(base), minIndex, numVertices, start, count);
	}
	void drawPlain(Handle device_, std::uint32_t type, std::uint32_t startVertex, std::uint32_t count) const {
		reinterpret_cast<shape_direct_engine::DrawPrimitiveFn>(VtableSlot(device_, shape_direct_engine::kDeviceDrawPrimitiveOffset))(
		    Object(device_), type, startVertex, count);
	}
	void eyeSwitch(std::uint8_t e) { eyes->Switch(e); }
};

// DrawShapeDirect 0x007694F0's ports (NiDX9Renderer_DrawShapeDirectLogic.h).
struct ShapeDirectEnginePorts : DirectEngineBase {
	std::uint8_t drawSuppressed() const { return ReadAt<std::uint8_t>(renderer + 0x6F0); }
	Handle geometrySkin(Handle g) const { return ReadAt<Handle>(g + kGeometrySkinOffset); }
	Handle geometryShaderCandidate(Handle g) const { return ReadAt<Handle>(g + kGeometryShaderOffset); }
	Handle dataRecord(Handle d) {
		const Handle record = ReadAt<Handle>(d + kDataRecordOffset);
		context->partition = 0;
		context->record = record;
		return record;
	}
	void setDataFlags(Handle d, std::uint16_t v) const { writeDataFlags(d, v); }
	Handle partitionRecord(Handle p) {
		const Handle record = ReadAt<Handle>(p + kPartitionRecordOffset);
		context->partition = p;
		context->record = record;
		return record;
	}
	std::uint32_t dataVertexCount(Handle d) const {
		return reinterpret_cast<shape_direct_engine::DataVertexCountFn>(VtableSlot(d, shape_direct_engine::kDataVertexCountSlot))(
		    static_cast<shape_direct_engine::NiGeometryDataView*>(Object(d)));
	}
	std::uint32_t isSkinned(Handle g, Handle shader) const {
		return reinterpret_cast<shape_direct_engine::IsSkinnedFn>(shape_direct_engine::kIsSkinnedAddress)(
		    reinterpret_cast<shape_direct_engine::NiDX9RendererView*>(renderer), static_cast<shape_direct_engine::NiGeometryView*>(Object(g)), shader) ? 1u : 0u;
	}
	void drawPartitionIndexed(const shape_direct::DrawIndexedCall& c) const {
		drawIndexed(c.device, c.primitiveType, c.baseVertexIndex, c.minVertexIndex, c.numVertices, c.startIndex, c.primitiveCount);
	}
	void drawWholeIndexed(const shape_direct::DrawIndexedCall& c) const { drawPartitionIndexed(c); }
	void drawPrimitive(const shape_direct::DrawCall& c) const { drawPlain(c.device, c.primitiveType, c.startVertex, c.primitiveCount); }
};
static_assert(shape_direct::DrawShapeDirectPorts<ShapeDirectEnginePorts>, "the engine ports meet the rebuild's concept");

// DrawTriStripsDirect 0x00769700's ports (NiDX9Renderer_DrawTriStripsDirectLogic.h).
struct StripsDirectEnginePorts : DirectEngineBase {
	std::uint8_t skipsGeometryDraws() const { return ReadAt<std::uint8_t>(renderer + 0x6F0); }
	Handle skinInstance(Handle g) const { return ReadAt<Handle>(g + kGeometrySkinOffset); }
	std::uint32_t worldBoundWord(Handle g, std::uint32_t k) const { return ReadAt<std::uint32_t>(g + kGeometryWorldBoundOffset + 4 * k); }
	Handle shaderCandidate(Handle g) const { return ReadAt<Handle>(g + kGeometryShaderOffset); }
	Handle bufferRecord(Handle d) {
		const Handle record = ReadAt<Handle>(d + kDataRecordOffset);
		context->partition = 0;
		context->record = record;
		return record;
	}
	Handle bufferData(Handle p) {
		const Handle record = ReadAt<Handle>(p + kPartitionRecordOffset);
		context->partition = p;
		context->record = record;
		return record;
	}
	void setDataFlags(Handle d, std::uint16_t v, strips_direct::Outcome) const { writeDataFlags(d, v); }
	void setCurrentWorldBoundWord(std::uint32_t k, std::uint32_t v) const {
		*reinterpret_cast<volatile std::uint32_t*>(renderer + strips_direct_engine::kCurrentWorldBoundOffset + 4 * k) = v;
	}
	std::uint32_t geometryDataSlot20(Handle d) const {
		return reinterpret_cast<strips_direct_engine::DataVertexCountFn>(VtableSlot(d, strips_direct_engine::kDataVertexCountSlot))(
		    static_cast<strips_direct_engine::NiGeometryDataView*>(Object(d)));
	}
	std::uint32_t selectsPartitions(Handle g) const {
		return reinterpret_cast<strips_direct_engine::SelectsPartitionsFn>(strips_direct_engine::kSelectsPartitionsAddress)(
		    reinterpret_cast<strips_direct_engine::NiDX9RendererView*>(renderer), static_cast<strips_direct_engine::NiGeometryView*>(Object(g)), 0) ? 1u : 0u;
	}
	void drawIndexedPrimitiveForPartition(const strips_direct::DrawIndexedCall& c) const {
		drawIndexed(c.device, c.primitiveType, c.baseVertexIndex, c.minVertexIndex, c.numVertices, c.startIndex, c.primitiveCount);
	}
	void drawIndexedPrimitiveForRecord(const strips_direct::DrawIndexedCall& c) const { drawIndexedPrimitiveForPartition(c); }
	void drawPrimitive(const strips_direct::DrawCall& c) const { drawPlain(c.device, c.primitiveType, c.startVertex, c.primitiveCount); }
};
static_assert(strips_direct::DrawTriStripsDirectPorts<StripsDirectEnginePorts>, "the engine ports meet the rebuild's concept");

// The two direct detours share everything but the rebuild they call.
template <class Ports, class Run>
UInt32 RouteDirect(void* self, UInt32 geometry, Run&& run, std::uint64_t route::Stats::*counter) {
	const RoutedDrawScope routed{self};
	eye::SwitchCounters counters;
	SwitchTicks ticks;
	DirectContext context;
	context.renderer = reinterpret_cast<std::uintptr_t>(self);
	context.geometry = geometry;
	EyeEnginePort eyePort{reinterpret_cast<std::uintptr_t>(self), &DirectContext::Reapply, &context, &ticks};
	bool outside = false;
	eye::Viewport seen;
	const eye::EyeData* eyes = EyesForDraw(eyePort, &outside, &seen);
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	eyeSwitch.viewportOnly = g_settings.stereoFollowers && (eyes != nullptr || (!outside && TestMode())) && FollowsCamera(geometry);
	NoteFollower(eyeSwitch.viewportOnly, geometry);
	TimedEyeSwitch timed{&eyeSwitch, &ticks};
	Ports ports{{reinterpret_cast<std::uintptr_t>(self), &context, &timed}};
	run(ports, route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode())));
	++(g_stats.*counter);
	g_stats.AddSwitches(counters);
	g_stats.AddTicks(ticks.all, ticks.reapply);
	g_stats.drawsOutsideEye += outside ? 1u : 0u;
	if (outside) {
		g_stats.outside.Add(seen);
	}
	if (counters.applied != 0 && context.seen.shaderVtable != 0) {
		g_stats.shaderClasses.Add(context.seen.shaderVtable);
		g_stats.reappliesType1 += context.seen.type1;
	}
	FlushSummaryIfRequested();
	return 0;
}

UInt32 __fastcall RouteDrawShapeDirect(void* self, void* /*edx*/, UInt32 geometry) {
	return RouteDirect<ShapeDirectEnginePorts>(
	    self, geometry, [geometry](ShapeDirectEnginePorts& ports, engine::render::stereo_draw::EyePlan plan) {
		    shape_direct::DrawShapeDirect(ports, geometry, plan);
	    },
	    &route::Stats::directShapeDraws);
}

UInt32 __fastcall RouteDrawTriStripsDirect(void* self, void* /*edx*/, UInt32 geometry) {
	return RouteDirect<StripsDirectEnginePorts>(
	    self, geometry, [self, geometry](StripsDirectEnginePorts& ports, engine::render::stereo_draw::EyePlan plan) {
		    strips_direct::DrawTriStripsDirect(ports, strips_direct::DrawArguments{static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)), geometry}, plan);
	    },
	    &route::Stats::directStripsDraws);
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
	made += InstallEntryDetour("NiDX9Renderer::DrawShapeDirect", shape_direct_engine::kDrawShapeDirectAddress, kDrawShapeDirectBytes,
	                           sizeof(kDrawShapeDirectBytes), reinterpret_cast<const void*>(&RouteDrawShapeDirect),
	                           reinterpret_cast<void**>(&g_originalDrawShapeDirect), log, DetourRole::Route) ? 1 : 0;
	made += InstallEntryDetour("NiDX9Renderer::DrawTriStripsDirect", strips_direct_engine::kDrawTriStripsDirectAddress, kDrawTriStripsDirectBytes,
	                           sizeof(kDrawTriStripsDirectBytes), reinterpret_cast<const void*>(&RouteDrawTriStripsDirect),
	                           reinterpret_cast<void**>(&g_originalDrawTriStripsDirect), log, DetourRole::Route) ? 1 : 0;
	g_on = made == kRouteStereoDrawDetours;
	g_summaryClock = ReadClocks();
	log("Route: stereo draw %s, eyes %u, test eye shift %.3f (%s), re-apply %s, camera followers %s",
	    g_on ? "on" : "off (not all four detours made)", static_cast<unsigned>(settings.stereoEyes),
	    static_cast<double>(settings.stereoTestEyeShift),
	    eye::TestModeOn(settings.stereoTestEyeShift) ? "the viewport split in halves" : "off: eye data from OBVR or none",
	    StereoReapplyName(settings.stereoReapply), settings.stereoFollowers ? "without the view shift" : "shifted like everything else");
	return made;
}

UInt32 StereoPlan() {
	return eye::ReportedPlan(g_on, g_settings.stereoEyes);
}

UInt32 SetStereoEyes(const ObngStereoEyes* eyes) {
	const ObngStereoResult verdict = eye::Validate(eyes, StereoPlan());
	if (verdict == kObngStereoOk) {
		++g_stats.eyeDataSets;
	} else {
		++g_stats.eyeDataRefused;
	}
	if (verdict != kObngStereoOk) {
		return verdict;
	}
	g_eyes = eye::FromApi(*eyes);
	g_eyesSet = true;
	return kObngStereoOk;
}

bool StereoEyeZeroViewport(ObngStereoViewport* out) {
	if (!g_eyesSet) {
		return false;
	}
	const eye::Viewport& zero = g_eyes.viewport[0];
	*out = ObngStereoViewport{zero.x, zero.y, zero.width, zero.height, zero.minZ, zero.maxZ};
	return true;
}

void ClearStereoEyes() {
	g_eyesSet = false;
}

void LogStereoDrawSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	InterlockedExchange(&g_summaryRequested, 1);
}

}  // namespace obng::plugin
