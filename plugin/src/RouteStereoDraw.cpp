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

#include "OcclusionCull.h"

#include <windows.h>

#include <intrin.h>

#include <cmath>
#include <cstdio>

#include "ProbeLogic.h"

#include "Probes.h"
#include "RouteStereoDrawLogic.h"
#include "RoutedDrawDevice.h"
#include "EntryDetour.h"
#include "EyeConstantsTraceLogic.h"
#include "GrassFadeFix.h"
#include "GrassProbe.h"
#include "Memory.h"
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
// SetVertexShaderConstantF 94 = +0x178 and GetVertexShaderConstantF 95 =
// +0x17C (d3d9.h's method order), for the carried registers
// (StereoEyeSwitchLogic.h RegisterPairs). The engine creates a pure device
// (behavior flags 0x54), on which Direct3D does not promise the Get; DXVK
// answers it, and a refusal leaves the switch without the carry.
constexpr std::uint32_t kDeviceSetVertexShaderConstantFOffset = 0x178;
constexpr std::uint32_t kDeviceGetVertexShaderConstantFOffset = 0x17C;
using SetVertexShaderConstantFFn = std::int32_t(__stdcall*)(void* device, std::uint32_t start, const float* data, std::uint32_t count);
using GetVertexShaderConstantFFn = std::int32_t(__stdcall*)(void* device, std::uint32_t start, float* data, std::uint32_t count);
constexpr std::uint32_t kD3DTransformView = 2;
using SetTransformFn = std::int32_t(__stdcall*)(void* device, std::uint32_t state, const float* matrix);
using SetViewportFn = std::int32_t(__stdcall*)(void* device, const eye::Viewport* viewport);
using GetViewportFn = std::int32_t(__stdcall*)(void* device, eye::Viewport* viewport);

DrawPrimitiveEntryFn g_originalDrawPrimitive = nullptr;  // the trampolines, kept but not called
DrawSkinnedEntryFn g_originalDrawSkinned = nullptr;
bool g_on = false;
LogFn g_log = nullptr;
RouteSettings g_settings;
constexpr std::uint32_t kShaderCurrentPassOffset = 0x3C;  // what slot 12 0x0077A1B0 calls slot 2 of (pseudocode, re/export/rea-all 0x77a1b0.c)

// The eye data: the interface's (render thread only, by its contract) or
// none; the test mode is a setting.
eye::EyeData g_eyes;
bool g_eyesSet = false;
// The carried registers' pairs ([Route] StereoCarryConstants), the render
// thread's like the eye data.
eye::RegisterPairs g_registerPairs;

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
	if (draw_timer::g_timers.on) {
		draw_timer::g_timers.LogAndReset(ticksPerMillisecond, [](const char* text) { g_log("%s", text); });
	}
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
		const draw_timer::StepTimer timer(route::PortTimes::kReadView);
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
	void WriteRows(const eye::TranslationRows& rows) const {
		const draw_timer::StepTimer timer(route::PortTimes::kWriteRows);
		eye::WriteRows(rows, View(), Inverse());
	}
	void SetViewTransform() const {
		const draw_timer::StepTimer timer(route::PortTimes::kSetTransform);
		const std::uint32_t device = Device();
		reinterpret_cast<SetTransformFn>(VtableSlot(device, kDeviceSetTransformOffset))(
		    reinterpret_cast<void*>(device), kD3DTransformView, View());
	}
	void SetViewport(const eye::Viewport& viewport) const {
		const draw_timer::StepTimer timer(route::PortTimes::kSetViewport);
		const std::uint32_t device = Device();
		reinterpret_cast<SetViewportFn>(VtableSlot(device, kDeviceSetViewportOffset))(reinterpret_cast<void*>(device), &viewport);
	}
	eye::Viewport CurrentViewport() const {
		const draw_timer::StepTimer timer(route::PortTimes::kGetViewport);
		eye::Viewport viewport;
		const std::uint32_t device = Device();
		reinterpret_cast<GetViewportFn>(VtableSlot(device, kDeviceGetViewportOffset))(reinterpret_cast<void*>(device), &viewport);
		return viewport;
	}
	bool ReadRegisters(float* out) const {
		const draw_timer::StepTimer timer(route::PortTimes::kReadRegisters);
		const std::uint32_t device = Device();
		return reinterpret_cast<GetVertexShaderConstantFFn>(VtableSlot(device, kDeviceGetVertexShaderConstantFOffset))(
		           reinterpret_cast<void*>(device), 0, out, eye::kCarriedRegisters) >= 0;
	}
	void WriteRegisters(std::uint32_t start, const float* data, std::uint32_t count) const {
		const draw_timer::StepTimer timer(route::PortTimes::kWriteRegisters);
		const std::uint32_t device = Device();
		reinterpret_cast<SetVertexShaderConstantFFn>(VtableSlot(device, kDeviceSetVertexShaderConstantFOffset))(
		    reinterpret_cast<void*>(device), start, data, count);
	}
	void ReapplyConstants() const {
		const std::uint64_t t0 = draw_timer::g_timers.on ? __rdtsc() : 0;
		SetProbeEyePhase(1);  // the view-dependence probe keys the uploads inside
		reapply(context);
		SetProbeEyePhase(0);
		if (t0 != 0) {
			ticks->reapply += __rdtsc() - t0;
		}
	}
};

using EngineEyeSwitch = eye::EyeSwitch<EyeEnginePort>;

// The eye constants trace ([Route] StereoTraceConstants, off in play): in
// every kTraceFrameEvery-th single-pass frame, for every routed draw with a
// view shift, the device's vertex registers c0..c47 read back before the
// second eye's switch, after it and after the switch back
// (GetVertexShaderConstantF, device vtable +0x17C - slot 95 of
// IDirect3DDevice9 in d3d9.h; the device answers it under DXVK), and a line
// per shader class and pass index saying which registers the second eye's
// draw has of its own (EyeConstantsTraceLogic.h). A class and pass with none
// is drawn in the second eye with the first eye's view.
constexpr std::uint32_t kTraceFrameEvery = 16;
constexpr std::uint32_t kTraceShaderCandidateOffset = 0xBC;  // NiGeometry's shader (the draw functions' candidate)
std::uint32_t g_traceFrames = 0;
bool g_traceFrame = false;
eye_trace::ClassBook g_traceBook;

struct EyeConstantsTrace {
	std::uintptr_t renderer = 0;
	bool on = false;
	bool taken[3] = {false, false, false};
	float c[3][eye_trace::kRegisters * 4];  // before the second eye's switch, after it, after the switch back

	void Take(int slot) {
		if (!on || taken[slot]) {
			return;
		}
		const std::uint32_t device = ReadAt<std::uint32_t>(renderer + kRendererDeviceOffset);
		reinterpret_cast<GetVertexShaderConstantFFn>(VtableSlot(device, kDeviceGetVertexShaderConstantFOffset))(
		    reinterpret_cast<void*>(static_cast<std::uintptr_t>(device)), 0, c[slot], eye_trace::kRegisters);
		taken[slot] = true;
	}
};

// One line per constant map of a shader: every live entry as
// register x count : type nibble / predefined id.
void LogShaderMaps(std::uint32_t shader) {
	for (const std::uint32_t mapOffset : {kShaderMapAOffset, kShaderMapBOffset}) {
		const std::uint32_t map = shader != 0 ? ReadAt<std::uint32_t>(shader + mapOffset) : 0u;
		if (map == 0) {
			continue;
		}
		char line[1200];
		int used = std::snprintf(line, sizeof(line), "Route: eye constants -   map +%02X (vtable %08X) entries register x count : type/id -", mapOffset,
		                         ReadAt<std::uint32_t>(map));
		const std::uint32_t entries = ReadAt<std::uint32_t>(map + 0x10);
		const std::uint32_t count = ReadAt<std::uint16_t>(map + 0x16);
		for (std::uint32_t i = 0; entries != 0 && i < count && used > 0 && used < static_cast<int>(sizeof(line)) - 40; ++i) {
			const std::uint32_t entry = ReadAt<std::uint32_t>(entries + 4 * i);
			if (entry == 0) {
				continue;
			}
			if (ReadAt<std::uint8_t>(entry + 0x08) == 0) {
				break;
			}
			used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used), " c%dx%u:%X/%u",
			                      static_cast<int>(ReadAt<std::uint32_t>(entry + 0x1C)), ReadAt<std::uint32_t>(entry + 0x20),
			                      ReadAt<std::uint32_t>(entry + 0x14) >> 28, ReadAt<std::uint32_t>(entry + 0x10) & 0xFFFFu);
		}
		g_log("%s", line);
	}
}

// Whether this draw is traced: the setting, a trace frame, eye data, a view
// shift (a camera follower has none by design).
bool TraceThisDraw(bool eyesKnown, bool viewportOnly) {
	return g_settings.stereoTraceConstants && g_traceFrame && eyesKnown && !viewportOnly;
}

// The line for one traced draw, when the book wants it. restoreExpected:
// the route re-applies the first eye's constants after the switch back (the
// direct entries); the other routes upload everything again before their
// next draw and are not judged on it.
void FinishTrace(const EyeConstantsTrace& trace, const char* route, std::uint32_t geometry, std::uint32_t shader, bool restoreExpected) {
	if (!trace.on || !trace.taken[0] || !trace.taken[1] || !trace.taken[2]) {
		return;
	}
	const std::uint32_t vtable = shader != 0 ? ReadAt<std::uint32_t>(shader) : 0u;
	const std::uint32_t passIndex = shader != 0 ? ReadAt<std::uint32_t>(shader + kShaderPassIndexOffset) : 0u;
	const std::uint64_t changed = eye_trace::ChangedRegisters(trace.c[0], trace.c[1]);
	const std::uint64_t left = eye_trace::ChangedRegisters(trace.c[0], trace.c[2]);
	const std::uint32_t key = ((vtable - 0x00A80000u) & 0x1FFFFu) | (passIndex << 17);
	const bool firstOfClass = g_traceBook.Find(key) == nullptr;
	if (!g_traceBook.Note(key, changed != 0, !restoreExpected || left == 0)) {
		return;
	}
	char name[48];
	ReadObjectName(geometry, name, sizeof(name));
	char changedText[200];
	char leftText[200];
	eye_trace::FormatRegisters(changed, changedText, sizeof(changedText));
	eye_trace::FormatRegisters(left, leftText, sizeof(leftText));
	g_log("Route: eye constants - %s, shader class %08X pass %u, \"%s\" (%08X): the second eye's draw has its own %s; after the switch back "
	      "differing from before: %s%s",
	      route, vtable, passIndex, name, geometry, changedText, leftText, changed == 0 ? " - NO REGISTER OF ITS OWN" : "");
	if (firstOfClass || changed == 0) {
		LogShaderMaps(shader);
	}
	if (changed == 0 && shader != 0) {
		// What the view re-apply had to work with: the pass object and its
		// two programs, each map's last program, and whether the device has
		// a vertex shader bound at all (GetVertexShader, device vtable
		// +0x174 - slot 93; null is the fixed-function pipeline, which takes
		// the view from SetTransform).
		const std::uint32_t pass = ReadAt<std::uint32_t>(shader + kShaderPassObjectOffset);
		const std::uint32_t mapA = ReadAt<std::uint32_t>(shader + kShaderMapAOffset);
		const std::uint32_t mapB = ReadAt<std::uint32_t>(shader + kShaderMapBOffset);
		const std::uint32_t device = ReadAt<std::uint32_t>(trace.renderer + kRendererDeviceOffset);
		void* bound = nullptr;
		using GetVertexShaderFn = std::int32_t(__stdcall*)(void* device, void** shader);
		reinterpret_cast<GetVertexShaderFn>(VtableSlot(device, 0x174))(reinterpret_cast<void*>(static_cast<std::uintptr_t>(device)), &bound);
		if (bound != nullptr) {
			using ReleaseFn = std::uint32_t(__stdcall*)(void* self);
			reinterpret_cast<ReleaseFn>(VtableSlot(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bound)), 0x08))(bound);
		}
		g_log("Route: eye constants -   shader %08X, pass object %08X with programs %08X / %08X, map +2C's last program %08X, map +30's %08X; "
		      "the device's vertex shader %08X",
		      shader, pass, pass != 0 ? ReadAt<std::uint32_t>(pass + kPassProgramAOffset) : 0u,
		      pass != 0 ? ReadAt<std::uint32_t>(pass + kPassProgramBOffset) : 0u, mapA != 0 ? ReadAt<std::uint32_t>(mapA + 0x20) : 0u,
		      mapB != 0 ? ReadAt<std::uint32_t>(mapB + 0x20) : 0u, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bound)));
	}
}

// The switch timed as a whole (both Switch calls), for the route's line.
struct TimedEyeSwitch {
	EngineEyeSwitch* inner;
	SwitchTicks* ticks;
	EyeConstantsTrace* trace = nullptr;
	void Switch(std::uint8_t e) {
		if (trace != nullptr && e == 1) {
			trace->Take(0);
		}
		// Timed only for the measurement: two reads of the clock a call are about
		// 4 % of the switch (2026-11-12).
		if (!draw_timer::g_timers.on) {
			inner->Switch(e);
		} else {
			const std::uint64_t t0 = __rdtsc();
			inner->Switch(e);
			ticks->all += __rdtsc() - t0;
		}
		if (trace != nullptr) {
			trace->Take(e == 1 ? 1 : 2);
		}
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
		if (!draw_timer::g_timers.on) {
			return engine->Slot(shader, call);
		}
		if (call.slot == 12 && shader != 0) {
			draw_timer::g_timers.repeats.Note(ReadAt<std::uint32_t>(shader + kShaderCurrentPassOffset), call.arguments[3], call.arguments[4]);
			draw_timer::g_timers.NotePassSetup(call.arguments[0], shader, call.arguments[3], call.arguments[4]);
		}
		const std::uint64_t t0 = __rdtsc();
		const std::uint32_t result = engine->Slot(shader, call);
		draw_timer::g_timers.steps.AddSlot(call.slot, __rdtsc() - t0);
		return result;
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
	OcclusionCullNoteDraw(geometry);
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
	if (g_grassProbeOn) {
		GrassProbeNoteDraw(geometry, eyes != nullptr || !g_eyesSet);
	}
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	eyeSwitch.viewportOnly = g_settings.stereoFollowers && (eyes != nullptr || (!outside && TestMode())) && FollowsCamera(geometry);
	NoteFollower(eyeSwitch.viewportOnly, geometry);
	eyeSwitch.pairs = g_settings.stereoCarryConstants ? &g_registerPairs : nullptr;
	EyeConstantsTrace trace;
	trace.renderer = reinterpret_cast<std::uintptr_t>(self);
	trace.on = TraceThisDraw(eyes != nullptr, eyeSwitch.viewportOnly);
	TimedEyeSwitch timed{&eyeSwitch, &ticks, &trace};
	const whole::DrawInput in{geometry, data, skin, world, worldBound, record};
	const auto plan = route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode()));
	draw_timer::g_timers.eyeDraw = eyes != nullptr;
	const RoutedDevice<whole_engine::EnginePorts, whole::IndexedDraw, whole::PlainDraw> routedDevice{&ports, &shader.lastShader, geometry};
	const std::uint64_t wholeStart = __rdtsc();
	const whole::Outcome outcome = RoutedDeviceWanted() ? whole::Draw(in, plan, ports, shader, routedDevice, routedDevice, timed)
	                                                     : whole::Draw(in, plan, ports, shader, ports, ports, timed);
	if (draw_timer::g_timers.on) {
		draw_timer::g_timers.steps.wholeTicks += __rdtsc() - wholeStart;
		++draw_timer::g_timers.steps.wholeCalls;
	}
	g_stats.AddWhole(outcome);
	FinishTrace(trace, "whole draw", geometry, geometry != 0 ? ReadAt<std::uint32_t>(geometry + kTraceShaderCandidateOffset) : 0u, false);
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
	SkinnedCensusNote(geometry, skin);  // the culler's draw note, and [Probe] SkinnedCensus
	eye::SwitchCounters counters;
	ReapplySeen seen_;
	SkinnedEnginePorts ports{static_cast<skinned_engine::NiDX9RendererView*>(self), nullptr, &seen_};
	SwitchTicks ticks;
	EyeEnginePort eyePort{reinterpret_cast<std::uintptr_t>(self), &SkinnedEnginePorts::Reapply, &ports, &ticks};
	bool outside = false;
	eye::Viewport seen;
	const eye::EyeData* eyes = EyesForDraw(eyePort, &outside, &seen);
	if (g_grassProbeOn) {
		GrassProbeNoteDraw(geometry, eyes != nullptr || !g_eyesSet);
	}
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	eyeSwitch.viewportOnly = g_settings.stereoFollowers && (eyes != nullptr || (!outside && TestMode())) && FollowsCamera(geometry);
	NoteFollower(eyeSwitch.viewportOnly, geometry);
	eyeSwitch.pairs = g_settings.stereoCarryConstants ? &g_registerPairs : nullptr;
	EyeConstantsTrace trace;
	trace.renderer = reinterpret_cast<std::uintptr_t>(self);
	trace.on = TraceThisDraw(eyes != nullptr, eyeSwitch.viewportOnly);
	TimedEyeSwitch timed{&eyeSwitch, &ticks, &trace};
	ports.eyes = &timed;
	const skinned::DrawArguments args{static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)), geometry, data, skin, world, worldBound};
	const skinned::Outcome outcome =
	    skinned::DrawSkinnedPrimitive(ports, args, route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode())));
	g_stats.AddSkinned(outcome);
	FinishTrace(trace, "skinned draw", geometry, geometry != 0 ? ReadAt<std::uint32_t>(geometry + kTraceShaderCandidateOffset) : 0u, false);
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

// The grass probe's fade sample ([Probe] Grass): for a draw of the world
// pass whose shader is the grass's (TallGrassShader, vtable 0x00A9202C -
// read: its Complete Object Locator names .?AVTallGrassShader@@), the
// constants the device holds - AlphaParam c5, the clip rows c9..c12, the
// instances' positions c20..c42 (read in GRASS20xx.vso) - and the
// projection's _11 and _22 (renderer +0x9C0, 0x00762640.md).
constexpr std::uint32_t kTallGrassShaderVtable = 0x00A9202C;
constexpr std::uint32_t kRendererProjectionOffset = 0x9C0;
// InstanceData: the grass map's entry c20 x 23 (the eye constants trace, run
// 20261106-083722). The engine uploads instances beyond those 23 - c245 held
// one's position (run obng-single-pass-meadow/20261106-120731); the 23 are a
// sample.
constexpr int kGrassInstanceRegister = 20;
constexpr int kGrassInstanceRegisters = 23;
void NoteGrassFade(const EyeEnginePort& port, std::uint32_t geometry, bool worldViewport) {
	if (!worldViewport || geometry == 0) {
		return;
	}
	const std::uint32_t shader = ReadAt<std::uint32_t>(geometry + kGeometryShaderOffset);
	if (shader == 0 || ReadAt<std::uint32_t>(shader) != kTallGrassShaderVtable) {
		return;
	}
	float c[eye::kCarriedRegisters * 4];
	if (!port.ReadRegisters(c)) {
		return;
	}
	const float p11 = ReadAt<float>(port.renderer + kRendererProjectionOffset);
	const float p22 = ReadAt<float>(port.renderer + kRendererProjectionOffset + 5 * sizeof(float));
	GrassProbeNoteFade(c + 5 * 4, c + 9 * 4, c + kGrassInstanceRegister * 4, kGrassInstanceRegisters, p11, p22, GrassFadeFixBoundTwin());
}

// The two direct detours share everything but the rebuild they call.
template <class Ports, class Run>
UInt32 RouteDirect(void* self, UInt32 geometry, bool strips, Run&& run, std::uint64_t route::Stats::*counter) {
	if (!OcclusionCullDraws(geometry, strips)) {
		return 0;  // the culler leaves this geometry out of the eye pass
	}
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
	if (g_grassProbeOn) {
		GrassProbeNoteDraw(geometry, eyes != nullptr || !g_eyesSet);
		NoteGrassFade(eyePort, geometry, eyes != nullptr || !g_eyesSet);
	}
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	eyeSwitch.viewportOnly = g_settings.stereoFollowers && (eyes != nullptr || (!outside && TestMode())) && FollowsCamera(geometry);
	NoteFollower(eyeSwitch.viewportOnly, geometry);
	eyeSwitch.pairs = g_settings.stereoCarryConstants ? &g_registerPairs : nullptr;
	eyeSwitch.reapplyOnRestore = g_settings.stereoRestoreDirect;
	EyeConstantsTrace trace;
	trace.renderer = reinterpret_cast<std::uintptr_t>(self);
	trace.on = TraceThisDraw(eyes != nullptr, eyeSwitch.viewportOnly);
	TimedEyeSwitch timed{&eyeSwitch, &ticks, &trace};
	Ports ports{{reinterpret_cast<std::uintptr_t>(self), &context, &timed}};
	run(ports, route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode())));
	FinishTrace(trace, "direct draw", geometry, context.shader, g_settings.stereoRestoreDirect);
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
	    self, geometry, false, [geometry](ShapeDirectEnginePorts& ports, engine::render::stereo_draw::EyePlan plan) {
		    shape_direct::DrawShapeDirect(ports, geometry, plan);
	    },
	    &route::Stats::directShapeDraws);
}

UInt32 __fastcall RouteDrawTriStripsDirect(void* self, void* /*edx*/, UInt32 geometry) {
	return RouteDirect<StripsDirectEnginePorts>(
	    self, geometry, true, [self, geometry](StripsDirectEnginePorts& ports, engine::render::stereo_draw::EyePlan plan) {
		    strips_direct::DrawTriStripsDirect(ports, strips_direct::DrawArguments{static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)), geometry}, plan);
	    },
	    &route::Stats::directStripsDraws);
}

// ---- The particle renderer's device call ---------------------------------------
//
// NiDX9Renderer's vtable slot 92 (0x00765990, read in the vtable
// 0x00A88EA4; thiscall(geometry), RET 4) builds a particle system's quads
// into a dynamic buffer and draws them through the renderer's own shader at
// +0xA94 with the device's transforms (it sets D3DTS_WORLD itself at
// 0x00765B63): 1 050 instructions around one DrawIndexedPrimitive, the
// `mov eax,[ecx+148h]; call eax` at 0x007667F6..0x007667FD inside its pass
// loop (re/export/draw-particles/0x00765990/asm.s; nothing jumps into those
// eight bytes). The unrouted-draw probe found it as the one device draw of
// a single-pass frame outside the route (run obng-single-pass-spell/
// 20261105-193815): spell effects and sparks reached the first eye only.
//
// It is routed at that call, not rebuilt: the eight bytes become a call to
// RouteParticleDraw, which is entered as the device's method was (the seven
// arguments pushed, stdcall) and makes the call - for two eyes twice, with
// the eye switch between. The quads face the pass camera in both eyes, as
// the other camera-facing geometry does. No constants are re-applied: the
// draw runs on the device's VIEW transform, which the switch sets.
constexpr UInt32 kParticleDrawSite = 0x007667F6;
constexpr UInt8 kParticleDrawSiteBytes[] = {0x8B, 0x81, 0x48, 0x01, 0x00, 0x00, 0xFF, 0xD0};  // mov eax,[ecx+148h]; call eax
constexpr UInt32 kRendererGlobal = 0x00B3F928;  // re/functions/0x007C78D0.md
constexpr std::uint32_t kDeviceDrawIndexedPrimitiveOffset = 0x148;  // slot 82, d3d9.h
using DrawIndexedPrimitiveFn =
    std::int32_t(__stdcall*)(void* device, UInt32 type, std::int32_t baseVertex, UInt32 minIndex, UInt32 vertices, UInt32 start, UInt32 count);

std::int32_t __stdcall RouteParticleDraw(void* device, UInt32 type, std::int32_t baseVertex, UInt32 minIndex, UInt32 vertices, UInt32 start,
                                         UInt32 count) {
	struct DeviceDraw {
		void* device;
		UInt32 type;
		std::int32_t baseVertex;
		UInt32 minIndex;
		UInt32 vertices;
		UInt32 start;
		UInt32 count;
		std::int32_t Draw() const {
			const std::uint32_t handle = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(device));
			return reinterpret_cast<DrawIndexedPrimitiveFn>(VtableSlot(handle, kDeviceDrawIndexedPrimitiveOffset))(device, type, baseVertex,
			                                                                                                     minIndex, vertices, start, count);
		}
	};
	DeviceDraw draw{device, type, baseVertex, minIndex, vertices, start, count};
	const std::uint32_t renderer = ReadAt<std::uint32_t>(kRendererGlobal);
	if (renderer == 0) {
		return draw.Draw();
	}
	const RoutedDrawScope routed{reinterpret_cast<void*>(static_cast<std::uintptr_t>(renderer))};
	eye::SwitchCounters counters;
	SwitchTicks ticks;
	EyeEnginePort eyePort{renderer, [](void*) {}, nullptr, &ticks};
	bool outside = false;
	eye::Viewport seen;
	const eye::EyeData* eyes = EyesForDraw(eyePort, &outside, &seen);
	EngineEyeSwitch eyeSwitch{&eyePort, &counters, eyes, eyes != nullptr ? 0.0f : g_settings.stereoTestEyeShift};
	TimedEyeSwitch timed{&eyeSwitch, &ticks};
	const std::int32_t result = route::DrawWithPlan(route::PlanFor(g_settings, eyes != nullptr || (!outside && TestMode())), draw, timed);
	++g_stats.particleDraws;
	g_stats.AddSwitches(counters);
	g_stats.AddTicks(ticks.all, ticks.reapply);
	g_stats.drawsOutsideEye += outside ? 1u : 0u;
	if (outside) {
		g_stats.outside.Add(seen);
	}
	FlushSummaryIfRequested();
	return result;
}

// Patches the particle draw's device call; false when the bytes there are
// not the game's own or could not be written (the particles then stay as
// they were: in the first eye only).
bool InstallParticleDraw(LogFn log) {
	if (!Verify(kParticleDrawSite, kParticleDrawSiteBytes, sizeof(kParticleDrawSiteBytes))) {
		char have[32];
		DescribeBytes(kParticleDrawSite, have, sizeof(have));
		log("Route particle draw: the call at %08X reads %s, not the game's own bytes; particles stay in the first eye only", kParticleDrawSite,
		    have);
		return false;
	}
	UInt8 patch[sizeof(kParticleDrawSiteBytes)];
	if (BuildCallPatch(patch, sizeof(patch), kParticleDrawSite, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&RouteParticleDraw)),
	                   sizeof(patch)) == 0 ||
	    !SafeWrite(kParticleDrawSite, patch, sizeof(patch))) {
		log("Route particle draw: the call at %08X could not be patched; particles stay in the first eye only", kParticleDrawSite);
		return false;
	}
	log("Route particle draw: on at %08X - the particle renderer's device call runs through the eye plan", kParticleDrawSite);
	return true;
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
	draw_timer::g_timers.on = settings.stereoDrawTimer;
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
	if (g_on && settings.stereoParticles) {
		InstallParticleDraw(log);
	}
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
	g_traceFrame = g_settings.stereoTraceConstants && g_traceFrames++ % kTraceFrameEvery == 0;
	if (draw_timer::g_timers.on) {
		draw_timer::g_timers.FrameBoundary();
	}
	if (g_grassProbeOn) {
		GrassProbePassBoundary();  // the draws counted so far are the pass before this frame's
	}
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

bool StereoEyeData(ObngStereoEyes* out) {
	if (!g_eyesSet) {
		return false;
	}
	out->size = sizeof(ObngStereoEyes);
	out->version = kObngStereoEyesVersion;
	out->secondEyeOffset[0] = g_eyes.secondEyeOffset.x;
	out->secondEyeOffset[1] = g_eyes.secondEyeOffset.y;
	out->secondEyeOffset[2] = g_eyes.secondEyeOffset.z;
	for (int eye = 0; eye < 2; ++eye) {
		const eye::Viewport& v = g_eyes.viewport[eye];
		out->viewport[eye] = ObngStereoViewport{v.x, v.y, v.width, v.height, v.minZ, v.maxZ};
	}
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
