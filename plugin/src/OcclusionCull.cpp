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
	if (vtable[kDeviceResetSlot] == hook) {
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
	g_cameras.Set(g_frame, cull::CameraOf(view, inverseRow));
}

// The decision for one draw inside the eye pass; a skipped geometry's test
// of this pass is marked, so the result says whether the skip was confirmed.
// A geometry asked about before in this frame gets the frame's decision at
// once: a later pass of it counts as a repeat (countRepeat), Do_RenderShape
// under a DrawRenderPass that decided already counts as nothing - the record
// was the draw.
cull::Decision DecideDraw(UInt32 geometry, cull::DrawKind kind, bool countRepeat) {
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
	cull::SecondEyeView(view0, g_eyes.secondEyeOffset, view1);
	cull::EyeRects rects;
	oc::RectKind kinds[2];
	const cull::TestKind testKind =
	    cull::RectsFor(bound, origin, view0, view1, proj, g_ports, static_cast<float>(g_settings.margin), &rects, kinds);
	const cull::Decided decided = cull::Decide(g_table, geometry, g_frame, testKind, g_settings, g_cameras, &g_stats);
	cull::Count(&g_stats, kind, decided.decision, testKind, decided.newThisFrame, kinds);
	if (decided.newThisFrame && testKind == cull::TestKind::kTest) {
		if (g_batch->count < cull::kMaxTests) {
			cull::Test& test = g_batch->tests[g_batch->count];
			test.geometry = geometry;
			test.rects = rects;
			test.skipped = decided.decision == cull::Decision::kSkip;
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
void EndTests() {
	if (g_batch->count == 0) {
		return;
	}
	const unsigned __int64 t0 = __rdtsc();
	g_block->Capture();
	occlusion_device::SetTestState(g_device);
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
		return ReadQuery(g_queries[index][test], pixels);
	});
	g_stats.collectTicks += __rdtsc() - t0;
}

UInt32 __fastcall CullFinish(void* self, void* /*edx*/) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	if (!g_on) {
		return g_finish(self, nullptr);  // the second detour was refused: the finish stays the game's
	}
	FlushSummaryIfRequested();
	if (g_active) {
		// A pass inside the eye pass (its draws go to another target).
		++g_inner;
		const UInt32 inner = g_finish(self, nullptr);
		--g_inner;
		return inner;
	}
	if (site != kEyePassSite) {
		return g_finish(self, nullptr);
	}
	if (!StereoEyeData(&g_eyes)) {
		++g_stats.passesWithoutEyes;
		return g_finish(self, nullptr);
	}
	if (!BeginPass()) {
		return g_finish(self, nullptr);
	}
	const unsigned __int64 t0 = __rdtsc();
	++g_frame;
	++g_stats.eyePasses;
	for (int eye = 0; eye < 2; ++eye) {
		std::memcpy(&g_ports[eye], &g_eyes.viewport[eye], sizeof(oc::Viewport));
	}
	Collect();
	bool lost = false;
	g_batchIndex = g_frame % cull::kBatches;
	g_batch = &g_ring.Begin(g_frame, &lost);
	if (lost) {
		++g_stats.batchesLost;
	}
	g_cameraSet = false;
	g_active = true;
	const UInt32 eax = g_finish(self, nullptr);
	g_active = false;
	if (!g_cameraSet) {
		NoteCamera();  // no draw asked: the ring still gets this frame's camera
	}
	EndTests();
	g_stats.passTicks += __rdtsc() - t0;
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
	if (DecideDraw(geometry, kind, true) == cull::Decision::kDraw) {
		return g_drawRenderPass(self, nullptr, record, pass);
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
	if (DecideDraw(geometry, kind, false) == cull::Decision::kDraw) {
		return g_renderShape(self, nullptr, geometry);
	}
	return 0;  // Do_RenderShape returns nothing its callers read (re/functions/0x007693E0.md)
}

}  // namespace

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
	g_settings.maxMoveUnits = settings.occlusionCullMaxMove;
	g_settings.maxTurnDegrees = settings.occlusionCullMaxTurn;
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
	g_summaryClock = ReadClocks();
	log("Route: occlusion cull %s - a hidden verdict holds %u frame%s, rectangles grown %u px, camera gate %.0f units / %.0f degrees",
	    g_on ? "on" : "off (not both detours made)", g_settings.maxAge, g_settings.maxAge == 1 ? "" : "s", g_settings.margin,
	    static_cast<double>(g_settings.maxMoveUnits), static_cast<double>(g_settings.maxTurnDegrees));
	return made;
}

void LogOcclusionCullSummary() {
	if (g_on) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
