// The occlusion probe ([Probe] Occlusion): which share of the geometries an
// eye's world pass draws lies wholly behind other geometry. The decisions and
// the measure's reasoning are OcclusionProbeLogic.h's; this file is the part
// that touches the game and Direct3D.
//
// Four detours: the accumulator's finish 0x007AE070 (the eye pass is its
// call from ShadowSceneNode::Render 0x007C78D0, return site 0x007C7D56 -
// read in re/functions/0x007C78D0.md, the site measured in run
// 20261030-194432) and the renderer's three draw entries that take a
// geometry: Do_RenderShape 0x007693E0 (slots 88/89), DrawShapeDirect
// 0x007694F0 (slot 90) and DrawTriStripsDirect 0x00769700 (slot 91). One eye
// pass in N is sampled: every draw inside its finish gets the three queries
// (and the control) of OcclusionProbeLogic.h, the results are read in the following frames,
// and no pass is sampled while they are outstanding.
//
// The rectangle draws change the device's state behind the engine's state
// cache, so each is bracketed by a state block (D3DSBT_ALL) captured before
// and applied after. That makes a sampled pass slow (thousands of captures)
// and is why the probe is a measurement, never on in play. The queries and
// the state block are kept for the process's life and never released: a
// device reset with the probe on would fail (D3D9 wants them released first)
// - one more reason it is off in play.
//
// Everything here runs on the render thread except LogOcclusionSummary,
// which only asks for a line; the next finish writes it.

#include "OcclusionProbe.h"

#include <windows.h>

#include <d3d9.h>
#include <intrin.h>

#include <cstring>

#include "OcclusionProbeLogic.h"
#include "ProbeLogic.h"

namespace obng::plugin {

namespace {

namespace oc = occlusion;

constexpr UInt32 kFinish = 0x007AE070;
constexpr UInt8 kFinishBytes[] = {0x6A, 0xFF, 0x68, 0xD3, 0xD4, 0x9C, 0x00};  // push -1; push 0x009CD4D3 (the SEH frame)
constexpr UInt32 kEyePassSite = 0x007C7D56;
constexpr UInt32 kRenderShape = 0x007693E0;
constexpr UInt8 kRenderShapeBytes[] = {0x83, 0xEC, 0x4C, 0x53, 0x8B, 0xD9};  // sub esp,4Ch; push ebx; mov ebx,ecx
constexpr UInt32 kShapeDirect = 0x007694F0;
constexpr UInt8 kShapeDirectBytes[] = {0x83, 0xEC, 0x14, 0x55, 0x8B, 0xE9};  // sub esp,14h; push ebp; mov ebp,ecx
constexpr UInt32 kStripsDirect = 0x00769700;
constexpr UInt8 kStripsDirectBytes[] = {0x83, 0xEC, 0x24, 0x55, 0x8B, 0xE9};  // sub esp,24h; push ebp; mov ebp,ecx

// The renderer (the global read in re/functions/0x007C78D0.md; the draw
// entries' `this` is the same object), its device and matrices
// (re/functions/0x00762640.md), the origin global, and the geometry's fields
// (re/functions/0x007693E0.md).
constexpr UInt32 kRendererGlobal = 0x00B3F928;
constexpr UInt32 kRendererDeviceOffset = 0x280;
constexpr UInt32 kRendererViewOffset = 0x980;
constexpr UInt32 kRendererProjOffset = 0x9C0;
constexpr UInt32 kOriginGlobal = 0x00B3F92C;
constexpr UInt32 kGeometryBoundOffset = 0x20;
constexpr UInt32 kGeometrySkinOffset = 0xB8;
// For the classification of the draws: the shader pass id the accumulator
// publishes before it draws a pass's list (re/functions/0x007A9820.md C3,
// C4), the object's name and parent (NiObjectNET +0x08: OBVR
// GameAddresses.h kNiObjectNameOffset and xOBSE NiNodes.h; NiAVObject +0x1C:
// re/functions/0x0055D4C0.md and xOBSE), and the player's two scene roots
// (OBVR GameAddresses.h: kPlayerPointer, TESObjectREFR::niNode +0x3C,
// kPlayerFirstPersonNodeOffset).
constexpr UInt32 kPassIdGlobal = 0x00B42E90;
constexpr UInt32 kObjectNameOffset = 0x08;
constexpr UInt32 kObjectParentOffset = 0x1C;
constexpr UInt32 kPlayerGlobal = 0x00B333C4;
constexpr UInt32 kPlayerThirdPersonNodeOffset = 0x3C;
constexpr UInt32 kPlayerFirstPersonNodeOffset = 0x5D0;
constexpr UInt32 kNamesListed = 16;

static_assert(sizeof(D3DVIEWPORT9) == sizeof(oc::Viewport), "the logic's viewport is D3DVIEWPORT9");

using FinishFn = UInt32(__fastcall*)(void* self, void* edx);
using DrawFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 geometry);

FinishFn g_finish = nullptr;
DrawFn g_renderShape = nullptr;
DrawFn g_shapeDirect = nullptr;
DrawFn g_stripsDirect = nullptr;
LogFn g_log = nullptr;
bool g_on = false;

enum class Phase : UInt8 { Idle, Sampling, Pending };

struct Entry {
	oc::Sample sample;
	oc::ScreenRect rect{};
	oc::Target target;
	IDirect3DQuery9* atDraw = nullptr;  // A
	IDirect3DQuery9* draw = nullptr;    // B
	IDirect3DQuery9* atEnd = nullptr;   // C
	IDirect3DQuery9* control = nullptr; // D
};

Entry g_entries[oc::kMaxDraws];
UInt32 g_count = 0;    // entries of the batch
UInt32 g_cursor = 0;   // the first entry whose results are not read yet
UInt32 g_waited = 0;   // eye passes since the batch was issued
UInt32 g_inner = 0;    // finishes running inside the sampled one
Phase g_phase = Phase::Idle;
oc::Sampler g_sampler;
oc::Stats g_stats;
IDirect3DDevice9* g_device = nullptr;
IDirect3DStateBlock9* g_block = nullptr;
UInt32 g_renderer = 0;  // the sampled pass's renderer
UInt32 g_playerThird = 0;  // the player's scene roots at the sampled pass's start (0: none)
UInt32 g_playerFirst = 0;
bool g_refusedLogged = false;
bool g_firstLogged = false;
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
	char line[900];
	oc::FormatHead(g_stats, g_sampler.every, now.qpc, line, sizeof(line));
	g_log("%s", line);
	for (int kind = 0; kind < static_cast<int>(oc::DrawKind::kCount); ++kind) {
		if (oc::FormatKind(g_stats, static_cast<oc::DrawKind>(kind), ticksPerMillisecond, line, sizeof(line)) > 0) {
			g_log("%s", line);
		}
	}
	for (int kind = 0; kind < static_cast<int>(oc::DrawKind::kCount); ++kind) {
		if (oc::FormatClasses(g_stats, static_cast<oc::DrawKind>(kind), ticksPerMillisecond, line, sizeof(line)) > 0) {
			g_log("%s", line);
		}
	}
	for (UInt32 row = 0; oc::FormatPass(g_stats, row, ticksPerMillisecond, line, sizeof(line)) > 0; ++row) {
		g_log("%s", line);
	}
	if (oc::FormatPassesBeyond(g_stats, ticksPerMillisecond, line, sizeof(line)) > 0) {
		g_log("%s", line);
	}
	if (oc::FormatRadius(g_stats, ticksPerMillisecond, line, sizeof(line)) > 0) {
		g_log("%s", line);
	}
	for (int kind = 0; kind < static_cast<int>(oc::DrawKind::kCount); ++kind) {
		for (int owner = 0; owner < static_cast<int>(oc::Owner::kCount); ++owner) {
			if (oc::FormatOwner(g_stats, static_cast<oc::DrawKind>(kind), static_cast<oc::Owner>(owner), ticksPerMillisecond, line, sizeof(line)) > 0) {
				g_log("%s", line);
			}
		}
	}
	for (UInt32 rank = 0; rank < kNamesListed && oc::FormatName(g_stats, rank, ticksPerMillisecond, line, sizeof(line)) > 0; ++rank) {
		g_log("%s", line);
	}
	if (oc::FormatNamesBeyond(g_stats, ticksPerMillisecond, line, sizeof(line)) > 0) {
		g_log("%s", line);
	}
	g_stats.Reset();
}

// The geometry's name into a table key; a pointer that cannot be read gives
// the empty name (the name is the engine's own string, but nothing here has
// proved the pointer).
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

UInt32 ParentOf(std::uint32_t object) {
	return ReadAt<UInt32>(object + kObjectParentOffset);
}

void RefuseOnce(const char* what, HRESULT result) {
	if (!g_refusedLogged) {
		g_refusedLogged = true;
		g_log("Probe: occlusion - %s refused (%08lX); no pass is sampled", what, static_cast<unsigned long>(result));
	}
}

// The device of the pass's renderer with its state block; false when there
// is none. A device other than the last one's (never seen in a run) starts
// over without touching the old one's objects.
bool BeginPass() {
	g_renderer = ReadAt<UInt32>(kRendererGlobal);
	if (g_renderer == 0) {
		return false;
	}
	auto* device = reinterpret_cast<IDirect3DDevice9*>(static_cast<std::uintptr_t>(ReadAt<UInt32>(g_renderer + kRendererDeviceOffset)));
	if (device == nullptr) {
		return false;
	}
	const UInt32 player = ReadAt<UInt32>(kPlayerGlobal);
	g_playerThird = player != 0 ? ReadAt<UInt32>(player + kPlayerThirdPersonNodeOffset) : 0;
	g_playerFirst = player != 0 ? ReadAt<UInt32>(player + kPlayerFirstPersonNodeOffset) : 0;
	if (device != g_device) {
		for (Entry& e : g_entries) {
			e.atDraw = e.draw = e.atEnd = e.control = nullptr;
		}
		g_block = nullptr;
		g_device = device;
	}
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

bool EnsureQueries(Entry& e) {
	if (e.control != nullptr) {
		return true;
	}
	IDirect3DQuery9** const slots[] = {&e.atDraw, &e.draw, &e.atEnd, &e.control};
	for (IDirect3DQuery9** slot : slots) {
		if (*slot != nullptr) {
			continue;
		}
		const HRESULT made = g_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, slot);
		if (FAILED(made) || *slot == nullptr) {
			*slot = nullptr;
			RefuseOnce("an occlusion query", made);
			return false;
		}
	}
	return true;
}

oc::Target CurrentTarget() {
	oc::Target target;
	D3DVIEWPORT9 port{};
	if (FAILED(g_device->GetViewport(&port))) {
		return target;
	}
	std::memcpy(&target.port, &port, sizeof(port));
	IDirect3DSurface9* depth = nullptr;
	if (SUCCEEDED(g_device->GetDepthStencilSurface(&depth)) && depth != nullptr) {
		target.depthBuffer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(depth));
		depth->Release();
	}
	return target;
}

// The state of a rectangle draw: fixed function, pre-transformed vertices,
// depth test without a depth write, nothing written to any colour target.
// The caller captured the device's state before and applies it after.
void SetTestState() {
	g_device->SetVertexShader(nullptr);
	g_device->SetPixelShader(nullptr);
	g_device->SetFVF(D3DFVF_XYZRHW);
	g_device->SetTexture(0, nullptr);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	const struct {
		D3DRENDERSTATETYPE state;
		DWORD value;
	} states[] = {
	    {D3DRS_ZENABLE, D3DZB_TRUE},        {D3DRS_ZWRITEENABLE, FALSE},      {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
	    {D3DRS_COLORWRITEENABLE, 0},        {D3DRS_COLORWRITEENABLE1, 0},     {D3DRS_COLORWRITEENABLE2, 0},
	    {D3DRS_COLORWRITEENABLE3, 0},       {D3DRS_ALPHATESTENABLE, FALSE},   {D3DRS_ALPHABLENDENABLE, FALSE},
	    {D3DRS_CULLMODE, D3DCULL_NONE},     {D3DRS_STENCILENABLE, FALSE},     {D3DRS_FOGENABLE, FALSE},
	    {D3DRS_SCISSORTESTENABLE, FALSE},   {D3DRS_CLIPPLANEENABLE, 0},       {D3DRS_FILLMODE, D3DFILL_SOLID},
	    {D3DRS_DEPTHBIAS, 0},               {D3DRS_SLOPESCALEDEPTHBIAS, 0},   {D3DRS_LIGHTING, FALSE},
	};
	for (const auto& s : states) {
		g_device->SetRenderState(s.state, s.value);
	}
}

void DrawRect(IDirect3DQuery9* query, const oc::ScreenRect& rect) {
	float vertices[16];
	oc::QuadVertices(rect, vertices);
	query->Issue(D3DISSUE_BEGIN);
	g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, 4 * sizeof(float));
	query->Issue(D3DISSUE_END);
}

UInt32 SampleDraw(DrawFn original, oc::DrawKind kind, void* self, UInt32 geometry) {
	if (g_phase != Phase::Sampling) {
		++g_stats.drawsUnsampled;
		return original(self, nullptr, geometry);
	}
	if (g_inner != 0) {
		++g_stats.drawsInInnerPasses;
		return original(self, nullptr, geometry);
	}
	if (g_count == oc::kMaxDraws) {
		++g_stats.drawsBeyondCapacity;
		return original(self, nullptr, geometry);
	}
	Entry& e = g_entries[g_count];
	if (geometry == 0 || static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self)) != g_renderer || !EnsureQueries(e)) {
		++g_stats.drawsUnsampled;
		return original(self, nullptr, geometry);
	}
	e.sample = oc::Sample{};
	e.sample.kind = kind == oc::DrawKind::kShape && ReadAt<UInt32>(geometry + kGeometrySkinOffset) != 0 ? oc::DrawKind::kSkinned : kind;
	e.target = CurrentTarget();
	oc::Bound bound;
	std::memcpy(&bound, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(geometry + kGeometryBoundOffset)), sizeof(bound));
	float origin[3];
	float view[16];
	float proj[16];
	std::memcpy(origin, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kOriginGlobal)), sizeof(origin));
	std::memcpy(view, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererViewOffset)), sizeof(view));
	std::memcpy(proj, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_renderer + kRendererProjOffset)), sizeof(proj));
	e.sample.passId = ReadAt<UInt32>(kPassIdGlobal);
	e.sample.radius = bound.radius;
	e.sample.owner = oc::OwnerOf(geometry, g_playerThird, g_playerFirst, &ParentOf);
	ReadName(geometry, e.sample.name);
	e.sample.rect = e.target.depthBuffer == 0 ? oc::RectKind::kViewport : oc::BoundRect(bound, origin, view, proj, e.target.port, &e.rect);
	if (!g_firstLogged) {
		g_firstLogged = true;
		g_log("Probe: occlusion - the first sampled draw: viewport %lu,%lu %lux%lu depth %.3f..%.3f, depth buffer %08X, origin %.1f %.1f %.1f, "
		      "view row 4 %.3f %.3f %.3f, projection _11 %.4f _22 %.4f _33 %.6f _34 %.1f _43 %.4f _44 %.1f",
		      static_cast<unsigned long>(e.target.port.x), static_cast<unsigned long>(e.target.port.y),
		      static_cast<unsigned long>(e.target.port.width), static_cast<unsigned long>(e.target.port.height),
		      static_cast<double>(e.target.port.minZ), static_cast<double>(e.target.port.maxZ), e.target.depthBuffer,
		      static_cast<double>(origin[0]), static_cast<double>(origin[1]), static_cast<double>(origin[2]), static_cast<double>(view[12]),
		      static_cast<double>(view[13]), static_cast<double>(view[14]), static_cast<double>(proj[0]), static_cast<double>(proj[5]),
		      static_cast<double>(proj[10]), static_cast<double>(proj[11]), static_cast<double>(proj[14]), static_cast<double>(proj[15]));
	}
	if (e.sample.rect == oc::RectKind::kTestable) {
		g_block->Capture();
		SetTestState();
		DrawRect(e.atDraw, e.rect);
		g_block->Apply();
	}
	e.draw->Issue(D3DISSUE_BEGIN);
	const unsigned __int64 start = __rdtsc();
	const UInt32 eax = original(self, nullptr, geometry);
	e.sample.ticks = __rdtsc() - start;
	e.draw->Issue(D3DISSUE_END);
	++g_count;
	return eax;
}

// After the sampled pass's last draw: every testable rectangle again, on
// the finished depth buffer, where the target is still the draw's.
void EndTests() {
	const oc::Target now = CurrentTarget();
	bool any = false;
	for (UInt32 i = 0; i < g_count; ++i) {
		Entry& e = g_entries[i];
		e.sample.endTested = e.sample.rect == oc::RectKind::kTestable && oc::SameTarget(e.target, now);
		any = any || e.sample.endTested;
	}
	if (!any) {
		return;
	}
	g_block->Capture();
	SetTestState();
	for (UInt32 i = 0; i < g_count; ++i) {
		const Entry& e = g_entries[i];
		if (e.sample.endTested) {
			DrawRect(e.atEnd, e.rect);
			oc::ScreenRect nearest = e.rect;
			nearest.depth = 0.0f;
			DrawRect(e.control, nearest);
		}
	}
	g_block->Apply();
}

enum class Read : UInt8 { Done, NotYet, Failed };

Read ReadQuery(IDirect3DQuery9* query, std::uint32_t* pixels) {
	DWORD value = 0;
	const HRESULT result = query->GetData(&value, sizeof(value), D3DGETDATA_FLUSH);
	if (result == S_OK) {
		*pixels = value;
		return Read::Done;
	}
	return result == S_FALSE ? Read::NotYet : Read::Failed;
}

// Reads the results that are there; the batch ends when all are, or is
// dropped when one fails.
void Collect() {
	while (g_cursor < g_count) {
		Entry& e = g_entries[g_cursor];
		Read read = ReadQuery(e.draw, &e.sample.drawPixels);
		if (read == Read::Done && e.sample.rect == oc::RectKind::kTestable) {
			read = ReadQuery(e.atDraw, &e.sample.boundAtDraw);
		}
		if (read == Read::Done && e.sample.endTested) {
			read = ReadQuery(e.atEnd, &e.sample.boundAtEnd);
		}
		if (read == Read::Done && e.sample.endTested) {
			read = ReadQuery(e.control, &e.sample.controlAtEnd);
		}
		if (read == Read::NotYet) {
			return;
		}
		if (read == Read::Failed) {
			++g_stats.batchesLost;
			g_phase = Phase::Idle;
			return;
		}
		g_stats.Add(e.sample);
		++g_cursor;
	}
	g_phase = Phase::Idle;
}

UInt32 __fastcall ProbeFinish(void* self, void* /*edx*/) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	FlushSummaryIfRequested();
	if (g_phase == Phase::Sampling) {
		// A pass inside the sampled one (its draws go to another target).
		++g_inner;
		const UInt32 inner = g_finish(self, nullptr);
		--g_inner;
		return inner;
	}
	if (g_phase == Phase::Pending) {
		Collect();
	}
	if (site != kEyePassSite) {
		return g_finish(self, nullptr);
	}
	if (g_phase == Phase::Pending && oc::GiveUp(++g_waited)) {
		++g_stats.batchesLost;
		g_phase = Phase::Idle;
	}
	if (!g_sampler.Take(g_phase == Phase::Pending) || !BeginPass()) {
		return g_finish(self, nullptr);
	}
	g_phase = Phase::Sampling;
	g_count = 0;
	const UInt32 eax = g_finish(self, nullptr);
	EndTests();
	++g_stats.passesSampled;
	g_cursor = 0;
	g_waited = 0;
	g_phase = g_count != 0 ? Phase::Pending : Phase::Idle;
	return eax;
}

UInt32 __fastcall ProbeRenderShape(void* self, void* /*edx*/, UInt32 geometry) {
	return SampleDraw(g_renderShape, oc::DrawKind::kShape, self, geometry);
}
UInt32 __fastcall ProbeShapeDirect(void* self, void* /*edx*/, UInt32 geometry) {
	return SampleDraw(g_shapeDirect, oc::DrawKind::kDirectShape, self, geometry);
}
UInt32 __fastcall ProbeStripsDirect(void* self, void* /*edx*/, UInt32 geometry) {
	return SampleDraw(g_stripsDirect, oc::DrawKind::kDirectStrips, self, geometry);
}

}  // namespace

void ReadObjectName(UInt32 object, char* out, UInt32 capacity) {
	char name[oc::kNameWidth];
	ReadName(object, name);
	UInt32 i = 0;
	for (; capacity != 0 && i + 1 < capacity && i < oc::kNameWidth && name[i] != '\0'; ++i) {
		out[i] = name[i];
	}
	if (capacity != 0) {
		out[i] = '\0';
	}
}

UInt32 InstallOcclusionProbe(UInt32 every, LogFn log) {
	g_log = log;
	g_sampler.every = every;
	UInt32 made = 0;
	made += InstallEntryDetour("FinishAccumulating 0x007AE070 (occlusion: the eye pass)", kFinish, kFinishBytes, sizeof(kFinishBytes),
	                           reinterpret_cast<const void*>(&ProbeFinish), reinterpret_cast<void**>(&g_finish), log) ? 1 : 0;
	if (made == 0) {
		// Without the finish no pass is ever sampled: the draw entries stay the game's.
		return 0;
	}
	made += InstallEntryDetour("Do_RenderShape 0x007693E0 (occlusion)", kRenderShape, kRenderShapeBytes, sizeof(kRenderShapeBytes),
	                           reinterpret_cast<const void*>(&ProbeRenderShape), reinterpret_cast<void**>(&g_renderShape), log) ? 1 : 0;
	made += InstallEntryDetour("DrawShapeDirect 0x007694F0 (occlusion)", kShapeDirect, kShapeDirectBytes, sizeof(kShapeDirectBytes),
	                           reinterpret_cast<const void*>(&ProbeShapeDirect), reinterpret_cast<void**>(&g_shapeDirect), log) ? 1 : 0;
	made += InstallEntryDetour("DrawTriStripsDirect 0x00769700 (occlusion)", kStripsDirect, kStripsDirectBytes, sizeof(kStripsDirectBytes),
	                           reinterpret_cast<const void*>(&ProbeStripsDirect), reinterpret_cast<void**>(&g_stripsDirect), log) ? 1 : 0;
	g_on = true;
	return made;
}

void LogOcclusionSummary() {
	if (g_on) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
