// The occlusion culler ([Route] OcclusionCull): geometries whose world bound
// lay wholly behind the drawn depth in both eyes in one of the last frames
// are left out of the single-pass eye pass. The decisions and the measure's
// reasoning are OcclusionCullLogic.h's; this file is the part that touches
// the game and Direct3D.
//
// Three detours: the accumulator's finish 0x007AE070 (the eye pass is its
// call from ShadowSceneNode::Render 0x007C78D0, return site 0x007C7D56 - read
// in re/functions/0x007C78D0.md, measured in run 20261030-194432);
// DrawRenderPass 0x007A9820, the accumulator's per-record draw step, where a
// skip also saves the record's light constants (re/functions/0x007A9820.md
// steps 3 and 4; the two globals it publishes first are still written); and
// Do_RenderShape 0x007693E0, the renderer's shape draw entry (slots 88/89),
// for shapes that reach it another way. The two direct draw entries (slots
// 90/91) belong to the stereo route; it asks OcclusionCullDraws before each
// of its direct draws. The eye pass needs the route's eye data
// (StereoEyeData): without it - the dual pass, a menu - the pass runs
// untouched.
//
// Per eye pass: the results of the earlier passes' queries are read (no
// wait: what is not ready stays for the next pass), then the finish runs
// with every shape and direct draw asked about - its bound projected into
// both eyes, its verdict looked up, the test for this pass queued - and
// after the finish the queued rectangles are drawn into occlusion queries on
// the pass's finished depth buffer, bracketed by one state block. The
// rectangles are the occlusion probe's (OcclusionProbeLogic.h BoundRect,
// zero contradictions measured), grown by the margin.
//
// The device's Reset (IDirect3DDevice9 vtable slot 16, d3d9.h) is hooked at
// the first eye pass: the queries and the state block are released before
// the engine's Reset runs and made anew, lazily, afterwards; the batches in
// flight are dropped.

#include "OcclusionCull.h"

#include <windows.h>

#include <d3d9.h>
#include <intrin.h>

#include <cstring>

#include "Memory.h"
#include "DrawGroupLogic.h"
#include "OcclusionCullLogic.h"
#include "OcclusionRectDevice.h"
#include "ProbeLogic.h"
#include "RouteStereoDraw.h"
#include "StereoEyesApi.h"

namespace obng::plugin {

namespace {

namespace cull = occlusion_cull;
namespace oc = occlusion;

constexpr UInt32 kFinish = 0x007AE070;
constexpr UInt8 kFinishBytes[] = {0x6A, 0xFF, 0x68, 0xD3, 0xD4, 0x9C, 0x00};  // push -1; push 0x009CD4D3 (the SEH frame)
constexpr UInt32 kEyePassSite = 0x007C7D56;
constexpr UInt32 kRenderShape = 0x007693E0;
constexpr UInt8 kRenderShapeBytes[] = {0x83, 0xEC, 0x4C, 0x53, 0x8B, 0xD9};  // sub esp,4Ch; push ebx; mov ebx,ecx
constexpr UInt32 kDrawRenderPass = 0x007A9820;
constexpr UInt8 kDrawRenderPassBytes[] = {0x53, 0x66, 0x8B, 0x5C, 0x24, 0x0C};  // push ebx; mov bx,[esp+0Ch] (re/export/pass-drivers/0x007A9820/asm.s)
// What DrawRenderPass publishes before it draws (re/functions/0x007A9820.md
// steps 2): the pass id and the record, read by the code below the draw.
constexpr UInt32 kPassIdGlobal = 0x00B42E90;
constexpr UInt32 kPassRecordGlobal = 0x00B42EB8;
constexpr UInt32 kRecordGeometryOffset = 0x00;
constexpr UInt32 kDeviceResetSlot = 16;  // IDirect3DDevice9::Reset, d3d9.h

// The renderer (re/functions/0x007C78D0.md), its device and matrices
// (re/functions/0x00762640.md: view +0x980, projection +0x9C0, inverse view
// +0xA00), the origin global, and the geometry's fields
// (re/functions/0x007693E0.md).
constexpr UInt32 kRendererGlobal = 0x00B3F928;
constexpr UInt32 kRendererDeviceOffset = 0x280;
constexpr UInt32 kRendererViewOffset = 0x980;
constexpr UInt32 kRendererProjOffset = 0x9C0;
constexpr UInt32 kRendererInverseViewOffset = 0xA00;
constexpr UInt32 kOriginGlobal = 0x00B3F92C;
constexpr UInt32 kGeometryBoundOffset = 0x20;
constexpr UInt32 kGeometrySkinOffset = 0xB8;
constexpr UInt32 kObjectNameOffset = 0x08;  // NiObjectNET's name (the probe's)
constexpr UInt32 kObjectParentOffset = 0x1C;  // NiAVObject's parent (the probe's)

static_assert(sizeof(D3DVIEWPORT9) == sizeof(oc::Viewport), "the logic's viewport is D3DVIEWPORT9");

using FinishFn = UInt32(__fastcall*)(void* self, void* edx);
using DrawFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 geometry);
using DrawRenderPassFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 record, UInt32 pass);  // __thiscall, this unused, ret 8
using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters);

FinishFn g_finish = nullptr;
DrawFn g_renderShape = nullptr;
DrawRenderPassFn g_drawRenderPass = nullptr;
ResetFn g_originalReset = nullptr;
bool g_resetHookFailed = false;
LogFn g_log = nullptr;
bool g_on = false;
cull::Settings g_settings;
cull::Table g_table;
cull::Ring g_ring;
cull::Cameras g_cameras;
cull::Stats g_stats;
cull::TraceSettings g_trace;
UInt32 g_traced = 0;  // passes traced so far
cull::PassIds g_passIds;  // the traced pass's renderer draws by pass id
cull::ListTracker g_lists;  // which record leads its pass list
bool g_forceDraw = false;   // a list's first record is being drawn whatever its verdict
IDirect3DQuery9* g_earlyQueries[cull::kTraceTests] = {};
IDirect3DQuery9* g_queries[cull::kBatches][cull::kMaxTests] = {};
UInt32 g_frame = 0;         // eye passes with eye data, from 1
bool g_active = false;      // inside the eye pass's finish
UInt32 g_inner = 0;         // finishes running inside it
bool g_cameraSet = false;   // this pass's camera is in the ring
cull::Batch* g_batch = nullptr;
UInt32 g_batchIndex = 0;
IDirect3DDevice9* g_device = nullptr;
IDirect3DStateBlock9* g_block = nullptr;
UInt32 g_renderer = 0;
ObngStereoEyes g_eyes{};
oc::Viewport g_ports[2];
UInt32 g_eyeCount = 2;  // this pass's cameras: 2 with the route's eye data, 1 in the flat game
bool g_flat = false;    // [Route] OcclusionCullFlat
// [Route] OcclusionCullStats: every eighth eye pass's draws noted (model
// data, shader, four ancestors, hidden or not) and counted at the pass's end
// (DrawGroupLogic.h): what instancing and a cull at the node could reach.
constexpr UInt32 kGeometryModelDataOffset = 0xB4;  // NiGeometry's data (read 0x00722AE5, SceneUpdate.h)
constexpr UInt32 kGeometryShaderOffset = 0xBC;     // its shader (NiDX9Renderer_DrawSkinnedPrimitive.h)
bool g_groupStats = false;
bool g_notePass = false;
std::vector<draw_groups::DrawNote> g_notes;
draw_groups::Totals g_groupTotals;
volatile LONG g_inEyePass = 0;
// [Route] OcclusionCullNodes: scene nodes left out at the cull walk. The eye
// pass's driver ShadowSceneNode::OnVisible 0x007C78D0 (slot +0x7C; it runs
// the cull walk and then the accumulator's finish, re/functions/
// 0x007C78D0.md) is detoured so that the pass begins before the walk, and
// NiCullingProcess::Cull 0x0070DFB0 (the visit of one object: the frustum
// test, then the object's +0x7C) so that a plain node or fade node whose
// bound is hidden is not visited - what the engine itself does with a node
// outside the frustum. Only candidate nodes are asked (DrawGroupLogic.h
// NodeBook); an asked node is tested like a draw, by its world bound.
constexpr UInt32 kDriver = 0x007C78D0;
constexpr UInt8 kDriverBytes[] = {0x6A, 0xFF, 0x68, 0x5B, 0xE8, 0x9C, 0x00};  // push -1; push 009CE85Bh (read)
constexpr UInt32 kCullVisit = 0x0070DFB0;
constexpr UInt8 kCullVisitBytes[] = {0x51, 0x55, 0x8B, 0xE9, 0x8B, 0x85, 0x8C, 0x00, 0x00, 0x00};  // push ecx; push ebp; mov ebp,ecx; mov eax,[ebp+8Ch] (read)
constexpr UInt32 kSlotOnVisible = 0x7C;
constexpr UInt32 kNiNodeOnVisible = 0x0070AB40;     // NiNode's body of slot +0x7C (read, re/functions/0x007C78D0.md C2)
constexpr UInt32 kBSFadeNodeOnVisible = 0x004A0920;  // BSFadeNode's (knowledge/culling-and-draw-setup.md)
using DriverFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 process);
using CullVisitFn = void(__fastcall*)(void* self, void* edx, UInt32 object);
DriverFn g_driver = nullptr;
CullVisitFn g_cullVisit = nullptr;
bool g_nodeCull = false;
bool g_passBegun = false;   // the eye pass was begun at the driver's entry
bool g_walk = false;        // inside the eye pass's cull walk
unsigned g_driverDepth = 0;
unsigned __int64 g_beginTicks = 0;
draw_groups::NodeBook g_nodeBook;
unsigned __int64 g_nodesAsked = 0;
unsigned __int64 g_nodesLeftOut = 0;
unsigned __int64 g_nodeVisits = 0;
unsigned __int64 g_walkTicks = 0;
unsigned __int64 g_passesWithWalk = 0;
unsigned __int64 g_viewChangedAfterWalk = 0;
float g_walkView[16] = {};
// [Route] OcclusionCullDriverLog: the driver's outermost calls apart by what
// they are (DrawGroupLogic.h DriverBook) - a frame runs more than one where
// the water's reflection renders.
bool g_targetCheck = true;  // [Route] OcclusionCullEyeTarget
cull::Camera g_lastCamera;      // the last eye pass's camera, for OcclusionCullLastCamera
bool g_lastCameraSet = false;
bool g_driverLog = false;
// [Route] OcclusionCullVisibleEvery: a draw found visible is tested in one
// frame of this many (OcclusionCullLogic.h TestDue); alternating by summary
// window for a measurement.
std::uint32_t g_visibleEvery = 1;
bool g_visibleAlternate = false;
bool g_visibleOnNow = true;
unsigned __int64 g_visibleTestsLeftOut = 0;
draw_groups::DriverBook g_driverBook;
unsigned __int64 g_walkVisitsAll = 0;  // objects visited in any outermost driver's walk
unsigned __int64 g_leftOutAll = 0;     // nodes left out, never reset (the summary resets g_nodesLeftOut inside a driver call)
volatile LONG g_eyePassCount = 0;
bool g_refusedLogged = false;
volatile LONG g_summaryRequested = 0;
probe::ClockPair g_summaryClock;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

probe::ClockPair ReadClocks() {
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	return probe::ClockPair{static_cast<std::uint64_t>(__rdtsc()), static_cast<std::uint64_t>(qpc.QuadPart)};
}

void FlushSummaryIfRequested() {
	if (g_summaryRequested == 0) {
		return;
	}
	InterlockedExchange(&g_summaryRequested, 0);
	const probe::ClockPair now = ReadClocks();
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	const double ticksPerMillisecond = probe::TicksPerMillisecond(g_summaryClock, now, static_cast<std::uint64_t>(frequency.QuadPart));
	g_summaryClock = now;
	char line[1400];
	cull::FormatSummary(g_stats, g_settings, ticksPerMillisecond, now.qpc, line, sizeof(line));
	g_log("%s", line);
	g_stats.Reset();
	if (g_visibleEvery > 1) {
		g_log("Route: occlusion cull visible draws - tested in one frame of %u (%s in this window): %llu tests of draws found visible left out",
		      g_visibleEvery, g_visibleOnNow ? "on" : "off", g_visibleTestsLeftOut);
		g_visibleTestsLeftOut = 0;
		if (g_visibleAlternate) {
			g_visibleOnNow = !g_visibleOnNow;
		}
	}
	if (g_nodeCull && g_passesWithWalk > 0) {
		const double per = 1.0 / static_cast<double>(g_passesWithWalk);
		g_log("Route: occlusion cull nodes - %llu passes begun at the driver; per pass %.0f objects visited in the walk, %.1f candidate nodes asked, "
		      "%.1f left out; the driver (walk and finish) %.3f ms a pass; the view differed between the walk and the finish in %llu passes",
		      g_passesWithWalk, static_cast<double>(g_nodeVisits) * per, static_cast<double>(g_nodesAsked) * per, static_cast<double>(g_nodesLeftOut) * per,
		      ticksPerMillisecond > 0.0 ? static_cast<double>(g_walkTicks) / ticksPerMillisecond * per : 0.0, g_viewChangedAfterWalk);
		if (g_driverLog) {
			for (unsigned i = 0; i < g_driverBook.Count(); ++i) {
				const draw_groups::DriverBook::Kind& k = g_driverBook.At(i);
				const double perCall = 1.0 / static_cast<double>(k.calls);
				g_log("Route: occlusion cull driver - called from 0x%08X, process vtable 0x%08X, target %ux%u: %llu calls, an eye pass begun at %llu; "
				      "per call %.3f ms, %.0f objects visited, %.1f nodes left out",
				      k.site, k.processVtable, k.width, k.height, k.calls, k.begun,
				      ticksPerMillisecond > 0.0 ? static_cast<double>(k.ticks) / ticksPerMillisecond * perCall : 0.0,
				      static_cast<double>(k.visits) * perCall, static_cast<double>(k.leftOut) * perCall);
			}
			if (g_driverBook.Dropped() != 0) {
				g_log("Route: occlusion cull driver - %llu calls of further kinds not noted (the table holds %u)", g_driverBook.Dropped(),
				      draw_groups::DriverBook::kKinds);
			}
			g_driverBook.Reset();
		}
		g_passesWithWalk = 0;
		g_nodeVisits = 0;
		g_nodesAsked = 0;
		g_nodesLeftOut = 0;
		g_walkTicks = 0;
		g_viewChangedAfterWalk = 0;
	}
	if (g_groupStats && g_groupTotals.passes > 0) {
		const draw_groups::Totals& t = g_groupTotals;
		const double per = 1.0 / static_cast<double>(t.passes);
		const double unskinned = static_cast<double>(t.drawn - t.drawnSkinned);
		g_log("Route: occlusion cull draw groups - %llu passes noted; per pass %.0f draws asked, %.0f hidden, %.0f drawn (%.0f skinned); of the "
		      "drawn unskinned: %.0f meshes, %.0f (mesh, shader) pairs - instancing by pair would leave %.0f draws of %.0f (%.0f %% less), %.0f "
		      "draws share their pair with another, %.0f sit in groups of four or more, the largest group %u; hidden draws below a node whose "
		      "noted draws are all hidden (two or more), by level above the geometry: 1: %.0f in %.0f of %.0f nodes, 2: %.0f in %.0f of %.0f, 3: "
		      "%.0f in %.0f of %.0f, 4: %.0f in %.0f of %.0f",
		      t.passes, static_cast<double>(t.draws) * per, static_cast<double>(t.hidden) * per, static_cast<double>(t.drawn) * per,
		      static_cast<double>(t.drawnSkinned) * per, static_cast<double>(t.meshes) * per, static_cast<double>(t.meshAndShader) * per,
		      static_cast<double>(t.meshAndShader) * per, unskinned * per,
		      unskinned > 0.0 ? 100.0 * (unskinned - static_cast<double>(t.meshAndShader)) / unskinned : 0.0, static_cast<double>(t.inTwo) * per,
		      static_cast<double>(t.inFour) * per, t.largest, static_cast<double>(t.hiddenBelow[0]) * per, static_cast<double>(t.hiddenNodes[0]) * per,
		      static_cast<double>(t.nodes[0]) * per, static_cast<double>(t.hiddenBelow[1]) * per, static_cast<double>(t.hiddenNodes[1]) * per,
		      static_cast<double>(t.nodes[1]) * per, static_cast<double>(t.hiddenBelow[2]) * per, static_cast<double>(t.hiddenNodes[2]) * per,
		      static_cast<double>(t.nodes[2]) * per, static_cast<double>(t.hiddenBelow[3]) * per, static_cast<double>(t.hiddenNodes[3]) * per,
		      static_cast<double>(t.nodes[3]) * per);
		g_groupTotals = draw_groups::Totals{};
	}
}

// The geometry's name for the trace; a pointer that cannot be read gives the
// empty name (the probe's ReadName).
void ReadName(UInt32 geometry, char out[oc::kNameWidth]) {
	char raw[oc::kNameWidth] = {};
	__try {
		const char* name = *reinterpret_cast<const char* const*>(static_cast<std::uintptr_t>(geometry + kObjectNameOffset));
		for (std::size_t i = 0; name != nullptr && i + 1 < oc::kNameWidth && name[i] != '\0'; ++i) {
			raw[i] = name[i];
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		raw[0] = '\0';
	}
	oc::CopyName(raw, out);
}

// The trace's view of the device: the render target, the depth-stencil and
// the viewport bound right now.
void LogSurfaces(const char* when) {
	D3DSURFACE_DESC target{};
	D3DSURFACE_DESC depth{};
	bool haveTarget = false;
	bool haveDepth = false;
	IDirect3DSurface9* surface = nullptr;
	if (SUCCEEDED(g_device->GetRenderTarget(0, &surface)) && surface != nullptr) {
		haveTarget = SUCCEEDED(surface->GetDesc(&target));
		surface->Release();
	}
	surface = nullptr;
	if (SUCCEEDED(g_device->GetDepthStencilSurface(&surface)) && surface != nullptr) {
		haveDepth = SUCCEEDED(surface->GetDesc(&depth));
		surface->Release();
	}
	D3DVIEWPORT9 port{};
	const bool havePort = SUCCEEDED(g_device->GetViewport(&port));
	DWORD zEnable = 0;
	g_device->GetRenderState(D3DRS_ZENABLE, &zEnable);
	g_log("Route: occlusion cull trace pass %u %s - target %ux%u format %u samples %u (%s), depth %ux%u format %u samples %u (%s), viewport %lu,%lu "
	      "%lux%lu z %.2f..%.2f (%s), ZENABLE %lu",
	      g_frame, when, target.Width, target.Height, static_cast<unsigned>(target.Format), static_cast<unsigned>(target.MultiSampleType),
	      haveTarget ? "read" : "none", depth.Width, depth.Height, static_cast<unsigned>(depth.Format), static_cast<unsigned>(depth.MultiSampleType),
	      haveDepth ? "read" : "none", port.X, port.Y, port.Width, port.Height, static_cast<double>(port.MinZ), static_cast<double>(port.MaxZ),
	      havePort ? "read" : "none", static_cast<unsigned long>(zEnable));
}

void RefuseOnce(const char* what, HRESULT result) {
	if (!g_refusedLogged) {
		g_refusedLogged = true;
		g_log("Route: occlusion cull - %s refused (%08lX); the pass runs untouched", what, static_cast<unsigned long>(result));
	}
}

// Before the engine's Reset: the queries and the state block released (D3D9
// wants every default-pool object gone), the batches in flight dropped; the
// next eye pass makes them anew.
void ReleaseDeviceObjects() {
	for (auto& batch : g_queries) {
		for (IDirect3DQuery9*& query : batch) {
			if (query != nullptr) {
				query->Release();
				query = nullptr;
			}
		}
	}
	for (IDirect3DQuery9*& query : g_earlyQueries) {
		if (query != nullptr) {
			query->Release();
			query = nullptr;
		}
	}
	if (g_block != nullptr) {
		g_block->Release();
		g_block = nullptr;
	}
	g_stats.batchesLost += g_ring.DropAll();
}

HRESULT __stdcall HookedReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters) {
	if (self == g_device) {
		ReleaseDeviceObjects();
		++g_stats.deviceResets;
	}
	return g_originalReset(self, parameters);
}

// The hook on the device's Reset, once per vtable (the vtable is the device
// class's). A slot already holding the hook is left; a write that fails is
// logged once and the culler runs on without the hook.
void EnsureResetHook(IDirect3DDevice9* device) {
	if (g_resetHookFailed) {
		return;
	}
	void** vtable = *reinterpret_cast<void***>(device);
	void* hook = reinterpret_cast<void*>(&HookedReset);
	if (vtable[kDeviceResetSlot] == hook || g_originalReset != nullptr) {  // once: a re-hook over another's hook would call itself
		return;
	}
	g_originalReset = reinterpret_cast<ResetFn>(vtable[kDeviceResetSlot]);
	if (!SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&vtable[kDeviceResetSlot])), &hook, sizeof(hook))) {
		g_resetHookFailed = true;
		g_log("Route: occlusion cull - the device's vtable at %p could not be written; a device reset with the culler on will fail",
		      static_cast<void*>(vtable));
		return;
	}
	g_log("Route: occlusion cull - Reset hooked on the device's vtable at %p (slot %u held %p)", static_cast<void*>(vtable), kDeviceResetSlot,
	      reinterpret_cast<void*>(g_originalReset));
}

// The device of the pass's renderer with its state block; false when there
// is none. A device other than the last one's starts over without touching
// the old one's objects.
bool BeginPass() {
	g_renderer = ReadAt<UInt32>(kRendererGlobal);
	if (g_renderer == 0) {
		return false;
	}
	auto* device = reinterpret_cast<IDirect3DDevice9*>(static_cast<std::uintptr_t>(ReadAt<UInt32>(g_renderer + kRendererDeviceOffset)));
	if (device == nullptr) {
		return false;
	}
	if (device != g_device) {
		std::memset(g_queries, 0, sizeof(g_queries));
		g_block = nullptr;
		g_device = device;
	}
	EnsureResetHook(device);
	if (g_block == nullptr) {
		const HRESULT made = device->CreateStateBlock(D3DSBT_ALL, &g_block);
		if (FAILED(made) || g_block == nullptr) {
			g_block = nullptr;
			RefuseOnce("the state block", made);
			return false;
		}
	}
	return true;
}

// A surface's size; false when it cannot be read.
bool SurfaceSize(IDirect3DSurface9* surface, UInt32* width, UInt32* height) {
	if (surface == nullptr) {
		return false;
	}
	D3DSURFACE_DESC desc{};
	const bool read = SUCCEEDED(surface->GetDesc(&desc));
	surface->Release();
	if (!read || desc.Width == 0 || desc.Height == 0) {
		return false;
	}
	*width = desc.Width;
	*height = desc.Height;
	return true;
}

// The render target's size, for the flat game's port.
bool TargetSize(UInt32* width, UInt32* height) {
	IDirect3DSurface9* surface = nullptr;
	if (FAILED(g_device->GetRenderTarget(0, &surface))) {
		return false;
	}
	return SurfaceSize(surface, width, height);
}

// The back buffer's size: what a flat pass of the frame renders into.
bool BackBufferSize(UInt32* width, UInt32* height) {
	IDirect3DSurface9* surface = nullptr;
	if (FAILED(g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface))) {
		return false;
	}
	return SurfaceSize(surface, width, height);
}

IDirect3DQuery9* EnsureQuery(IDirect3DQuery9** slot) {
	if (*slot == nullptr) {
		const HRESULT made = g_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, slot);
		if (FAILED(made) || *slot == nullptr) {
			*slot = nullptr;
			RefuseOnce("an occlusion query", made);
		}
	}
	return *slot;
}

// The pass's camera for the gate, from the renderer's matrices at the first
// draw (the pass has set them by then).
void NoteCamera() {
	if (g_cameraSet) {
		return;
	}
	g_cameraSet = true;
	float view[16];
	float inverseRow[3];
	std::memcpy(view, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererViewOffset)), sizeof(view));
	std::memcpy(inverseRow, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererInverseViewOffset + 12 * sizeof(float))),
	            sizeof(inverseRow));
	g_lastCamera = cull::CameraOf(view, inverseRow);
	g_lastCameraSet = true;
	g_cameras.Set(g_frame, g_lastCamera);
	if (g_batch != nullptr && g_batch->traced) {
		float proj[16];
		std::memcpy(proj, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererProjOffset)), sizeof(proj));
		char line[600];
		cull::FormatTraceCamera(g_frame, g_cameras.At(g_frame), proj, g_ports, line, sizeof(line));
		g_log("%s", line);
	}
}

// The decision for one draw inside the eye pass; a skipped geometry's test
// of this pass is marked, so the result says whether the skip was confirmed.
// A geometry asked about before in this frame gets the frame's decision at
// once: a later pass of it counts as a repeat (countRepeat), Do_RenderShape
// under a DrawRenderPass that decided already counts as nothing - the record
// was the draw.
cull::Decision DecideDraw(UInt32 geometry, cull::DrawKind kind, bool countRepeat, bool node = false) {
	const unsigned __int64 t0 = __rdtsc();
	if (cull::Verdict* seen = g_table.Find(geometry); seen != nullptr && seen->seenFrame == g_frame) {
		if (countRepeat) {
			const oc::RectKind none[2] = {oc::RectKind::kTestable, oc::RectKind::kTestable};
			cull::Count(&g_stats, kind, seen->decision, cull::TestKind::kTest, false, none);
		}
		g_stats.decideTicks += __rdtsc() - t0;
		return seen->decision;
	}
	D3DVIEWPORT9 port{};
	if (FAILED(g_device->GetViewport(&port)) || port.X != g_ports[0].x || port.Y != g_ports[0].y || port.Width != g_ports[0].width ||
	    port.Height != g_ports[0].height) {
		++g_stats.outsideEyeViewport;
		g_stats.decideTicks += __rdtsc() - t0;
		return cull::Decision::kDraw;
	}
	NoteCamera();
	oc::Bound bound;
	std::memcpy(&bound, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(geometry + kGeometryBoundOffset)), sizeof(bound));
	float origin[3];
	float view0[16];
	float view1[16];
	float proj[16];
	std::memcpy(origin, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kOriginGlobal)), sizeof(origin));
	std::memcpy(view0, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererViewOffset)), sizeof(view0));
	std::memcpy(proj, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererProjOffset)), sizeof(proj));
	if (g_eyeCount == 2) {
		cull::SecondEyeView(view0, g_eyes.secondEyeOffset, view1);
	}
	cull::EyeRects rects;
	oc::RectKind kinds[2];
	const cull::TestKind testKind =
	    cull::RectsFor(bound, origin, view0, view1, proj, g_ports, static_cast<float>(g_settings.margin), &rects, kinds, g_eyeCount);
	const cull::Decided decided = cull::Decide(g_table, geometry, g_frame, testKind, g_settings, g_cameras, &g_stats);
	if (!node) {
		cull::Count(&g_stats, kind, decided.decision, testKind, decided.newThisFrame, kinds);
	}
	if (g_nodeCull && !node && decided.newThisFrame) {
		// The draw noted at its three nearest ancestors, for the walk's candidates.
		UInt32 ancestor = geometry;
		for (unsigned level = 0; level < 3; ++level) {
			ancestor = ReadAt<UInt32>(ancestor + kObjectParentOffset);
			if (ancestor == 0) {
				break;
			}
			g_nodeBook.Note(ancestor, g_frame, decided.decision == cull::Decision::kSkip);
		}
	}
	if (g_notePass && decided.newThisFrame && !node) {
		draw_groups::DrawNote note;
		note.data = ReadAt<UInt32>(geometry + kGeometryModelDataOffset);
		note.shader = ReadAt<UInt32>(geometry + kGeometryShaderOffset);
		UInt32 node = geometry;
		for (unsigned level = 0; level < draw_groups::kLevels && node != 0; ++level) {
			node = ReadAt<UInt32>(node + kObjectParentOffset);
			note.ancestor[level] = node;
		}
		note.hidden = decided.decision == cull::Decision::kSkip;
		note.skinned = kind == cull::DrawKind::kSkinned;
		g_notes.push_back(note);
	}
	const std::uint32_t every = g_visibleOnNow ? g_visibleEvery : 1;
	const bool due = decided.verdict == nullptr || cull::TestDue(*decided.verdict, decided.decision, g_frame, every);
	if (decided.newThisFrame && testKind == cull::TestKind::kTest && !due) {
		++g_visibleTestsLeftOut;
	}
	if (decided.newThisFrame && testKind == cull::TestKind::kTest && due) {
		if (g_batch->count < cull::kMaxTests) {
			if (decided.verdict != nullptr) {
				decided.verdict->testedFrame = g_frame;
			}
			cull::Test& test = g_batch->tests[g_batch->count];
			test.geometry = geometry;
			test.rects = rects;
			test.skipped = decided.decision == cull::Decision::kSkip;
			test.control = false;
			test.kind = static_cast<std::uint8_t>(kind);
			if (g_batch->traced && g_batch->count < cull::kTraceTests) {
				ReadName(geometry, g_batch->names[g_batch->count]);
				ReadName(ReadAt<UInt32>(geometry + kObjectParentOffset), g_batch->parents[g_batch->count]);
				g_batch->bounds[g_batch->count] = bound;
			}
			++g_batch->count;
			++g_stats.testsIssued;
		} else {
			++g_stats.testsBeyondCapacity;
		}
	}
	g_stats.decideTicks += __rdtsc() - t0;
	return decided.decision;
}

// After the eye pass's last draw: every queued rectangle pair into its
// query, on the finished depth buffer.
// The traced pass's first tests once more at the pass's end, the rectangles
// at the bound's farthest depth: whether the depth in front lies before the
// whole bound or within it. Inside EndTests' state block.
void IssueFarTests() {
	g_batch->earlyIssued = true;
	const UInt32 count = g_batch->count < cull::kTraceTests ? g_batch->count : cull::kTraceTests;
	float vertices[cull::kMaxRectVertices * 4];
	UInt32 issued = 0;
	for (UInt32 i = 0; i < count; ++i) {
		if (g_batch->tests[i].control) {
			break;
		}
		IDirect3DQuery9* query = EnsureQuery(&g_earlyQueries[i]);
		if (query == nullptr) {
			break;
		}
		if (g_trace.probeDepth >= 0.0f) {
			for (int eye = 0; eye < 2; ++eye) {
				g_batch->tests[i].rects.rect[eye].farDepth = g_trace.probeDepth;
			}
		}
		const UInt32 vertexCount = cull::RectTriangles(g_batch->tests[i].rects, vertices, true);
		query->Issue(D3DISSUE_BEGIN);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, vertexCount / 3, vertices, 4 * sizeof(float));
		query->Issue(D3DISSUE_END);
		++issued;
	}
	g_batch->earlyCount = issued;
}

void EndTests() {
	if (g_batch->traced) {
		char ids[1400];
		cull::FormatPassIds(g_frame, g_passIds, *g_batch, ids, sizeof(ids));
		g_log("%s", ids);
		LogSurfaces("at the tests");
		for (float depth : cull::kControlDepths) {
			if (g_batch->count < cull::kMaxTests) {
				cull::Test& control = g_batch->tests[g_batch->count];
				control = cull::Test{};
				control.control = true;
				control.rects = cull::ControlRects(g_ports, depth);
				++g_batch->count;
			}
		}
	}
	if (g_batch->count == 0) {
		return;
	}
	const unsigned __int64 t0 = __rdtsc();
	g_block->Capture();
	occlusion_device::SetTestState(g_device);
	// Both eyes' ports in one viewport: the pass ends with eye 0's viewport, which
	// would clip the second eye's rectangles away (measured 2026-11-09).
	D3DVIEWPORT9 both{};
	cull::UnionViewport(g_ports, reinterpret_cast<oc::Viewport*>(&both));
	g_device->SetViewport(&both);
	float vertices[cull::kMaxRectVertices * 4];
	for (UInt32 i = 0; i < g_batch->count; ++i) {
		IDirect3DQuery9* query = EnsureQuery(&g_queries[g_batchIndex][i]);
		if (query == nullptr) {
			g_batch->count = i;  // the rest never had a query: dropped from this batch
			break;
		}
		const UInt32 count = cull::RectTriangles(g_batch->tests[i].rects, vertices);
		query->Issue(D3DISSUE_BEGIN);
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, vertices, 4 * sizeof(float));
		query->Issue(D3DISSUE_END);
	}
	if (g_batch->traced) {
		IssueFarTests();
	}
	g_block->Apply();
	g_batch->pending = g_batch->count != 0;
	g_stats.endTestsTicks += __rdtsc() - t0;
}

cull::Read ReadQuery(IDirect3DQuery9* query, std::uint32_t* pixels) {
	if (query == nullptr) {
		return cull::Read::kFailed;
	}
	DWORD value = 0;
	const HRESULT result = query->GetData(&value, sizeof(value), D3DGETDATA_FLUSH);
	if (result == S_OK) {
		*pixels = value;
		return cull::Read::kDone;
	}
	return result == S_FALSE ? cull::Read::kNotYet : cull::Read::kFailed;
}

void Collect() {
	const unsigned __int64 t0 = __rdtsc();
	cull::Collect(&g_ring, &g_table, &g_stats, [](const cull::Batch& batch, UInt32 test, std::uint32_t* pixels) {
		const UInt32 index = static_cast<UInt32>(&batch - g_ring.batches);
		const cull::Read read = ReadQuery(g_queries[index][test], pixels);
		// The trace logs the controls and every hidden or skipped test it kept.
		if (batch.traced && read == cull::Read::kDone &&
		    (batch.tests[test].control || (test < cull::kTraceTests && (*pixels == 0 || batch.tests[test].skipped)))) {
			char line[800];
			const bool named = test < cull::kTraceTests && !batch.tests[test].control;
			long long farPixels = -1;
			if (named && batch.earlyIssued && test < batch.earlyCount) {
				std::uint32_t pixelsAtFar = 0;
				farPixels = ReadQuery(g_earlyQueries[test], &pixelsAtFar) == cull::Read::kDone ? static_cast<long long>(pixelsAtFar) : -1;
			}
			cull::FormatTraceTest(batch.frame, test, batch.tests[test], named ? batch.names[test] : "", named ? batch.parents[test] : "",
			                      named ? batch.bounds[test] : oc::Bound{}, *pixels, line, sizeof(line), farPixels);
			g_log("%s", line);
		}
		return read;
	});
	g_stats.collectTicks += __rdtsc() - t0;
}

// Everything the eye pass needs before its first decision: the eyes and
// their ports, the device, the frame's number, the results that came in,
// the frame's batch. False: this is no pass the culler acts in. Called at
// the driver's entry with the node cull on (the walk's decisions need it),
// otherwise at the finish.
bool BeginEyePass() {
	const bool eyesGiven = StereoEyeData(&g_eyes);
	if (!eyesGiven && !g_flat) {
		++g_stats.passesWithoutEyes;
		return false;
	}
	if (!BeginPass()) {
		return false;
	}
	if (eyesGiven) {
		g_eyeCount = 2;
		for (int eye = 0; eye < 2; ++eye) {
			std::memcpy(&g_ports[eye], &g_eyes.viewport[eye], sizeof(oc::Viewport));
		}
		// Only the frame's own pass: the water's reflection comes through the
		// same driver and finish into its own map (EyeTargetIsFrame).
		UInt32 width = 0;
		UInt32 height = 0;
		if (g_targetCheck && TargetSize(&width, &height) && !cull::EyeTargetIsFrame(width, height, g_ports)) {
			++g_stats.otherTargetPasses;
			return false;
		}
	} else {
		// The flat game: the one camera's port is the render target itself -
		// when that is the frame's (the back buffer's size); a reflection or
		// shadow map's pass is left alone (FlatTargetIsFrame).
		UInt32 width = 0;
		UInt32 height = 0;
		UInt32 backWidth = 0;
		UInt32 backHeight = 0;
		if (!TargetSize(&width, &height) || !BackBufferSize(&backWidth, &backHeight)) {
			++g_stats.passesWithoutEyes;
			return false;
		}
		if (!cull::FlatTargetIsFrame(width, height, backWidth, backHeight)) {
			++g_stats.otherTargetPasses;
			return false;
		}
		g_eyeCount = 1;
		cull::FlatPorts(width, height, g_ports);
		++g_stats.flatPasses;
	}
	const unsigned __int64 t0 = __rdtsc();
	++g_frame;
	++g_stats.eyePasses;
	Collect();
	bool lost = false;
	g_batchIndex = g_frame % cull::kBatches;
	g_batch = &g_ring.Begin(g_frame, &lost);
	if (lost) {
		++g_stats.batchesLost;
	}
	g_batch->traced = cull::TraceDue(g_frame, g_trace, g_traced);
	g_batch->earlyIssued = false;
	g_batch->earlyCount = 0;
	if (g_batch->traced) {
		++g_traced;
		g_passIds.Reset();
		LogSurfaces("at the pass");
	}
	g_cameraSet = false;
	g_lists.Reset();
	g_beginTicks = __rdtsc() - t0;
	return true;
}

UInt32 __fastcall CullFinish(void* self, void* /*edx*/) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	if (!g_on) {
		return g_finish(self, nullptr);  // the second detour was refused: the finish stays the game's
	}
	FlushSummaryIfRequested();
	if (g_active || g_driverDepth > 1) {
		// A pass inside the eye pass (its draws go to another target), or the
		// finish of a driver that runs inside the eye pass's walk.
		++g_inner;
		const UInt32 inner = g_finish(self, nullptr);
		--g_inner;
		return inner;
	}
	if (site != kEyePassSite) {
		return g_finish(self, nullptr);
	}
	if (!g_passBegun && !BeginEyePass()) {
		return g_finish(self, nullptr);
	}
	if (g_passBegun) {
		// The view the walk decided with must be the view the draws decide with.
		float now[16];
		std::memcpy(now, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererViewOffset)), sizeof(now));
		if (std::memcmp(now, g_walkView, sizeof(now)) != 0) {
			++g_viewChangedAfterWalk;
		}
	}
	g_passBegun = false;
	g_walk = false;
	const unsigned __int64 t0 = __rdtsc();
	g_notePass = g_groupStats && (g_frame % 8) == 0;
	g_active = true;
	InterlockedExchange(&g_inEyePass, 1);
	const UInt32 eax = g_finish(self, nullptr);
	InterlockedExchange(&g_inEyePass, 0);
	InterlockedIncrement(&g_eyePassCount);
	g_active = false;
	if (g_notePass) {
		g_groupTotals.Add(draw_groups::Count(g_notes));
		g_notes.clear();
		g_notePass = false;
	}
	if (!g_cameraSet) {
		NoteCamera();  // no draw asked: the ring still gets this frame's camera
	}
	EndTests();
	g_stats.passTicks += __rdtsc() - t0 + g_beginTicks;
	return eax;
}

// The accumulator's per-record draw: a skipped record still publishes the
// pass id and itself, as the original does first, and nothing else runs.
UInt32 __fastcall CullDrawRenderPass(void* self, void* /*edx*/, UInt32 record, UInt32 pass) {
	if (!g_active || g_inner != 0 || record == 0) {
		return g_drawRenderPass(self, nullptr, record, pass);
	}
	const UInt32 geometry = ReadAt<UInt32>(record + kRecordGeometryOffset);
	if (geometry == 0) {
		return g_drawRenderPass(self, nullptr, record, pass);
	}
	const cull::DrawKind kind = ReadAt<UInt32>(geometry + kGeometrySkinOffset) != 0 ? cull::DrawKind::kSkinned : cull::DrawKind::kShape;
	const bool first = g_lists.First(pass & 0xFFFFu);
	if (DecideDraw(geometry, kind, true) == cull::Decision::kDraw) {
		return g_drawRenderPass(self, nullptr, record, pass);
	}
	if (first) {
		// The list's first record is drawn inside the pass's shader state (RenderPassList
		// steps 2 and 5): left out, the records after it lose their textures.
		++g_stats.firstOfListKept;
		g_forceDraw = true;
		const UInt32 eax = g_drawRenderPass(self, nullptr, record, pass);
		g_forceDraw = false;
		return eax;
	}
	*reinterpret_cast<volatile UInt32*>(static_cast<std::uintptr_t>(kPassIdGlobal)) = pass & 0xFFFFu;
	*reinterpret_cast<volatile UInt32*>(static_cast<std::uintptr_t>(kPassRecordGlobal)) = record;
	++g_stats.skippedAtRecord;
	return 0;  // the original writes no EAX its callers read (re/functions/0x007A9820.md)
}

UInt32 __fastcall CullRenderShape(void* self, void* /*edx*/, UInt32 geometry) {
	if (!g_active || g_inner != 0 || geometry == 0 || static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)) != g_renderer) {
		return g_renderShape(self, nullptr, geometry);
	}
	const cull::DrawKind kind = ReadAt<UInt32>(geometry + kGeometrySkinOffset) != 0 ? cull::DrawKind::kSkinned : cull::DrawKind::kShape;
	if (DecideDraw(geometry, kind, false) == cull::Decision::kDraw || g_forceDraw) {
		return g_renderShape(self, nullptr, geometry);
	}
	return 0;  // Do_RenderShape returns nothing its callers read (re/functions/0x007693E0.md)
}

// The eye pass's driver: with the node cull on the pass begins here, before
// the cull walk, so that the walk can ask about nodes. A driver inside
// another (depth above one) is left alone.
UInt32 __fastcall CullDriver(void* self, void* /*edx*/, UInt32 process) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	if (!g_on || !g_nodeCull || g_driverDepth != 0 || g_active) {
		++g_driverDepth;
		const UInt32 inner = g_driver(self, nullptr, process);
		--g_driverDepth;
		return inner;
	}
	++g_driverDepth;
	g_passBegun = BeginEyePass();
	g_walk = g_passBegun;
	const bool begun = g_passBegun;
	if (g_passBegun) {
		++g_passesWithWalk;
		std::memcpy(g_walkView, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererViewOffset)), sizeof(g_walkView));
	}
	UInt32 targetWidth = 0;
	UInt32 targetHeight = 0;
	if (g_driverLog && g_device != nullptr) {
		TargetSize(&targetWidth, &targetHeight);
	}
	const unsigned __int64 visitsBefore = g_walkVisitsAll;
	const unsigned __int64 leftOutBefore = g_leftOutAll;
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 eax = g_driver(self, nullptr, process);
	const unsigned __int64 ticks = __rdtsc() - t0;
	g_walkTicks += ticks;
	if (g_driverLog) {
		g_driverBook.Add(site, process != 0 ? ReadAt<UInt32>(process) : 0, targetWidth, targetHeight, begun, ticks, g_walkVisitsAll - visitsBefore,
		                 g_leftOutAll - leftOutBefore);
	}
	g_walk = false;
	if (g_passBegun) {
		// The driver returned without its finish at the eye pass's site: the
		// pass that was begun is closed with what it has.
		g_passBegun = false;
		if (!g_cameraSet) {
			NoteCamera();
		}
		EndTests();
	}
	--g_driverDepth;
	return eax;
}

// [Route] OcclusionCullNodeLog: a node's first leaving out, by name - the
// node, its vtable, its bound, three parents and its first children (the
// layout as GrassProbe.cpp reads it: the child array at +0xB0, its count at
// +0xB6). For the question of what the node cull leaves out in a place.
constexpr UInt32 kNodeChildrenOffset = 0xB0;
constexpr UInt32 kNodeChildCountOffset = 0xB6;
constexpr UInt32 kNodeLogChildren = 8;
UInt32 g_nodeLogLeft = 0;

void LogNodeLeftOut(UInt32 node) {
	--g_nodeLogLeft;
	char line[768];
	char name[oc::kNameWidth];
	ReadName(node, name);
	oc::Bound bound;
	std::memcpy(&bound, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(node + kGeometryBoundOffset)), sizeof(bound));
	int at = std::snprintf(line, sizeof(line), "Route: occlusion cull node left out - frame %u node 0x%08X '%s' vtable 0x%08X bound (%.0f, %.0f, %.0f) r %.0f; parents",
	                       g_frame, node, name, ReadAt<UInt32>(node), static_cast<double>(bound.centre[0]), static_cast<double>(bound.centre[1]),
	                       static_cast<double>(bound.centre[2]), static_cast<double>(bound.radius));
	UInt32 parent = node;
	for (unsigned level = 0; level < 3 && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++level) {
		parent = ReadAt<UInt32>(parent + kObjectParentOffset);
		if (parent == 0) {
			break;
		}
		ReadName(parent, name);
		at += std::snprintf(line + at, sizeof(line) - at, " '%s'", name);
	}
	const UInt32 children = ReadAt<UInt32>(node + kNodeChildrenOffset);
	const UInt32 count = ReadAt<UInt16>(node + kNodeChildCountOffset);
	if (at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
		at += std::snprintf(line + at, sizeof(line) - at, "; %u children:", count);
	}
	for (UInt32 i = 0; children != 0 && i < count && i < kNodeLogChildren && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++i) {
		const UInt32 child = ReadAt<UInt32>(children + i * 4);
		if (child == 0) {
			continue;
		}
		ReadName(child, name);
		at += std::snprintf(line + at, sizeof(line) - at, " '%s' 0x%08X", name, ReadAt<UInt32>(child));
	}
	g_log("%s", line);
}

// One object's visit in a cull walk. In the eye pass's walk a candidate node
// (a plain node or a fade node, by its +0x7C) is asked about like a draw;
// hidden, it is not visited.
void __fastcall CullVisit(void* self, void* /*edx*/, UInt32 object) {
	if (g_driverLog && g_driverDepth == 1) {
		++g_walkVisitsAll;
	}
	if (g_walk && g_driverDepth == 1 && object != 0) {
		++g_nodeVisits;
		const UInt32 onVisible = ReadAt<UInt32>(ReadAt<UInt32>(object) + kSlotOnVisible);
		if ((onVisible == kNiNodeOnVisible || onVisible == kBSFadeNodeOnVisible) && g_nodeBook.Candidate(object, g_frame, g_settings.maxAge)) {
			++g_nodesAsked;
			if (DecideDraw(object, cull::DrawKind::kShape, false, true) == cull::Decision::kSkip) {
				++g_nodesLeftOut;
				++g_leftOutAll;
				if (g_nodeBook.LeftOut(object, g_frame) && g_nodeLogLeft != 0) {
					LogNodeLeftOut(object);
				}
				return;
			}
		}
	}
	g_cullVisit(self, nullptr, object);
}

}  // namespace

bool OcclusionCullNodeLeftOut(UInt32 node) {
	return g_on && g_nodeCull && node != 0 && g_nodeBook.LeftOutWithin(node, g_frame, 2);
}

bool OcclusionCullLastCamera(float position[3], float direction[3]) {
	if (!g_on || !g_lastCameraSet) {
		return false;
	}
	std::memcpy(position, g_lastCamera.position, sizeof(g_lastCamera.position));
	std::memcpy(direction, g_lastCamera.direction, sizeof(g_lastCamera.direction));
	return true;
}

void OcclusionCullNoteDraw(UInt32 geometry) {
	if (!g_on || !g_active || g_inner != 0 || g_batch == nullptr || !g_batch->traced) {
		return;
	}
	DWORD blend = 0;
	DWORD test = 0;
	DWORD zWrite = 0;
	DWORD colour = 0;
	g_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
	g_device->GetRenderState(D3DRS_ALPHATESTENABLE, &test);
	g_device->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
	g_device->GetRenderState(D3DRS_COLORWRITEENABLE, &colour);
	char name[oc::kNameWidth];
	ReadName(geometry, name);
	g_passIds.Note(ReadAt<UInt32>(kPassIdGlobal) & 0xFFFFu, blend, test, zWrite, name, colour);
}

bool OcclusionCullDraws(UInt32 geometry, bool strips) {
	if (!g_on || !g_active || g_inner != 0 || geometry == 0) {
		return true;
	}
	return DecideDraw(geometry, strips ? cull::DrawKind::kDirectStrips : cull::DrawKind::kDirectShape, true) == cull::Decision::kDraw;
}

UInt32 InstallOcclusionCull(const RouteSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.occlusionCull) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_settings.maxAge = settings.occlusionCullMaxAge;
	g_settings.margin = settings.occlusionCullMargin;
	g_visibleEvery = settings.occlusionCullVisibleEvery;
	g_visibleAlternate = settings.occlusionCullVisibleAlternate;
	g_settings.maxMoveUnits = settings.occlusionCullMaxMove;
	g_settings.maxTurnDegrees = settings.occlusionCullMaxTurn;
	g_nodeLogLeft = settings.occlusionCullNodeLog;
	g_driverLog = settings.occlusionCullDriverLog;
	g_targetCheck = settings.occlusionCullEyeTarget;
	g_trace.passes = settings.occlusionCullTrace;
	g_trace.probeDepth = settings.occlusionCullTraceDepth;
	g_flat = settings.occlusionCullFlat;
	g_groupStats = settings.occlusionCullStats;
	UInt32 made = 0;
	made += InstallEntryDetour("FinishAccumulating 0x007AE070 (occlusion cull: the eye pass)", kFinish, kFinishBytes, sizeof(kFinishBytes),
	                           reinterpret_cast<const void*>(&CullFinish), reinterpret_cast<void**>(&g_finish), log, DetourRole::Route) ? 1 : 0;
	if (made != 0) {
		made += InstallEntryDetour("Do_RenderShape 0x007693E0 (occlusion cull)", kRenderShape, kRenderShapeBytes, sizeof(kRenderShapeBytes),
		                           reinterpret_cast<const void*>(&CullRenderShape), reinterpret_cast<void**>(&g_renderShape), log, DetourRole::Route)
		            ? 1
		            : 0;
		made += InstallEntryDetour("DrawRenderPass 0x007A9820 (occlusion cull)", kDrawRenderPass, kDrawRenderPassBytes, sizeof(kDrawRenderPassBytes),
		                           reinterpret_cast<const void*>(&CullDrawRenderPass), reinterpret_cast<void**>(&g_drawRenderPass), log,
		                           DetourRole::Route)
		            ? 1
		            : 0;
	}
	g_on = made == kRouteOcclusionCullDetours;
	if (g_on && settings.occlusionCullNodes) {
		const bool driver = InstallEntryDetour("ShadowSceneNode::OnVisible 0x007C78D0 (occlusion cull: the pass begins before the walk)", kDriver, kDriverBytes,
		                                       sizeof(kDriverBytes), reinterpret_cast<const void*>(&CullDriver), reinterpret_cast<void**>(&g_driver), log,
		                                       DetourRole::Route);
		const bool visit = driver && InstallEntryDetour("NiCullingProcess::Cull 0x0070DFB0 (occlusion cull: nodes)", kCullVisit, kCullVisitBytes,
		                                                sizeof(kCullVisitBytes), reinterpret_cast<const void*>(&CullVisit),
		                                                reinterpret_cast<void**>(&g_cullVisit), log, DetourRole::Route);
		g_nodeCull = driver && visit;
		log(g_nodeCull ? "Route: occlusion cull - nodes: a candidate node whose bound is hidden is left out at the cull walk"
		               : "Route: occlusion cull - nodes: not installed (an entry is not the game's own); the draws are culled as before");
	}
	g_summaryClock = ReadClocks();
	log("Route: occlusion cull %s - a hidden verdict holds %u frame%s, rectangles grown %u px, camera gate %.0f units / %.0f degrees, trace %u passes, "
	    "%s",
	    g_on ? "on" : "off (not both detours made)", g_settings.maxAge, g_settings.maxAge == 1 ? "" : "s", g_settings.margin,
	    static_cast<double>(g_settings.maxMoveUnits), static_cast<double>(g_settings.maxTurnDegrees), g_trace.passes,
	    g_flat ? "without eye data the flat game's one camera (the render target as its port)" : "without eye data the pass runs untouched");
	return made;
}

bool OcclusionCullInEyePass() { return g_inEyePass != 0; }
UInt32 OcclusionCullEyePasses() { return static_cast<UInt32>(g_eyePassCount); }

void LogOcclusionCullSummary() {
	if (g_on) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
