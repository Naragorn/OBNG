// The unrouted-draw probe ([Probe] UnroutedDraws): which device draws of a
// single-pass frame the stereo route does not double, and who issues them.
// The decisions are UnroutedDrawLogic.h's.
//
// The four draw methods of the device (IDirect3DDevice9 slots 81..84, read
// in the Windows SDK's d3d9.h) are replaced in the device's vtable at the
// first routed draw - the vtable is the device class's, so every device of
// it is covered, and whatever stood in the slots (the driver's method, or
// another mod's hook) is called through. Each call is sorted by the route's
// depth (the route brackets its detours' bodies), the eye data the route
// holds, and the device's current viewport; the return address is the
// calling site.
//
// Render thread only, except LogUnroutedDrawSummary, which asks for the
// lines; the next device draw writes them.

#include "UnroutedDrawProbe.h"

#include <windows.h>

#include <d3d9.h>
#include <intrin.h>

#include "Memory.h"
#include "RouteStereoDraw.h"
#include "UnroutedDrawLogic.h"

namespace obng::plugin {

bool g_unroutedDrawProbeOn = false;

namespace {

namespace un = unrouted;

constexpr UInt32 kRendererDeviceOffset = 0x280;  // re/functions/0x00762640.md
constexpr UInt32 kSlotDrawPrimitive = 81;
constexpr UInt32 kSlotDrawIndexedPrimitive = 82;
constexpr UInt32 kSlotDrawPrimitiveUP = 83;
constexpr UInt32 kSlotDrawIndexedPrimitiveUP = 84;
constexpr UInt32 kSitesListed = 40;

using DrawPrimitiveFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedPrimitiveFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using DrawPrimitiveUPFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedPrimitiveUPFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);

DrawPrimitiveFn g_drawPrimitive = nullptr;
DrawIndexedPrimitiveFn g_drawIndexedPrimitive = nullptr;
DrawPrimitiveUPFn g_drawPrimitiveUP = nullptr;
DrawIndexedPrimitiveUPFn g_drawIndexedPrimitiveUP = nullptr;
void** g_hookedVtable = nullptr;
bool g_hookFailed = false;
LogFn g_log = nullptr;
UInt32 g_depth = 0;  // the route's detours on the stack
un::Stats g_stats;
volatile LONG g_summaryRequested = 0;

void FlushSummaryIfRequested() {
	if (g_summaryRequested == 0) {
		return;
	}
	InterlockedExchange(&g_summaryRequested, 0);
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	char line[400];
	un::FormatHead(g_stats, static_cast<std::uint64_t>(qpc.QuadPart), line, sizeof(line));
	g_log("%s", line);
	for (UInt32 rank = 0; rank < kSitesListed && un::FormatSite(g_stats, rank, line, sizeof(line)) > 0; ++rank) {
		g_log("%s", line);
	}
	g_stats.Reset();
}

void Note(IDirect3DDevice9* device, un::Method method, void* returnAddress, UINT primitives) {
	const auto site = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(returnAddress));
	if (g_depth != 0) {
		g_stats.Add(un::Where::kRouted, method, site, un::Rect{}, primitives);
		return;
	}
	ObngStereoViewport eyeZero{};
	const bool eyesSet = StereoEyeZeroViewport(&eyeZero);
	D3DVIEWPORT9 port{};
	device->GetViewport(&port);
	const un::Rect current{port.X, port.Y, port.Width, port.Height};
	const un::Rect zero{eyeZero.x, eyeZero.y, eyeZero.width, eyeZero.height};
	g_stats.Add(un::Classify(false, eyesSet, current, zero), method, site, current, primitives);
	FlushSummaryIfRequested();
}

HRESULT __stdcall HookDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT start, UINT count) {
	Note(device, un::Method::kDrawPrimitive, _ReturnAddress(), count);
	return g_drawPrimitive(device, type, start, count);
}

HRESULT __stdcall HookDrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT vertices, UINT start,
                                           UINT count) {
	Note(device, un::Method::kDrawIndexedPrimitive, _ReturnAddress(), count);
	return g_drawIndexedPrimitive(device, type, base, minIndex, vertices, start, count);
}

HRESULT __stdcall HookDrawPrimitiveUP(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT count, const void* data, UINT stride) {
	Note(device, un::Method::kDrawPrimitiveUP, _ReturnAddress(), count);
	return g_drawPrimitiveUP(device, type, count, data, stride);
}

HRESULT __stdcall HookDrawIndexedPrimitiveUP(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT minIndex, UINT vertices, UINT count,
                                             const void* indices, D3DFORMAT format, const void* data, UINT stride) {
	Note(device, un::Method::kDrawIndexedPrimitiveUP, _ReturnAddress(), count);
	return g_drawIndexedPrimitiveUP(device, type, minIndex, vertices, count, indices, format, data, stride);
}

bool Replace(void** vtable, UInt32 slot, const void* hook, void** original) {
	*original = vtable[slot];
	return SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&vtable[slot])), &hook, sizeof(hook));
}

// Puts the four hooks on the vtable of the renderer's device, once.
void EnsureHooks(void* renderer) {
	if (g_hookedVtable != nullptr || g_hookFailed) {
		return;
	}
	auto* device = *reinterpret_cast<IDirect3DDevice9**>(reinterpret_cast<std::uintptr_t>(renderer) + kRendererDeviceOffset);
	if (device == nullptr) {
		return;
	}
	void** vtable = *reinterpret_cast<void***>(device);
	const bool ok = Replace(vtable, kSlotDrawPrimitive, reinterpret_cast<const void*>(&HookDrawPrimitive), reinterpret_cast<void**>(&g_drawPrimitive)) &&
	                Replace(vtable, kSlotDrawIndexedPrimitive, reinterpret_cast<const void*>(&HookDrawIndexedPrimitive),
	                        reinterpret_cast<void**>(&g_drawIndexedPrimitive)) &&
	                Replace(vtable, kSlotDrawPrimitiveUP, reinterpret_cast<const void*>(&HookDrawPrimitiveUP), reinterpret_cast<void**>(&g_drawPrimitiveUP)) &&
	                Replace(vtable, kSlotDrawIndexedPrimitiveUP, reinterpret_cast<const void*>(&HookDrawIndexedPrimitiveUP),
	                        reinterpret_cast<void**>(&g_drawIndexedPrimitiveUP));
	if (!ok) {
		g_hookFailed = true;
		g_log("Probe: device draws - the device's vtable at %p could not be written; nothing is counted", static_cast<void*>(vtable));
		return;
	}
	g_hookedVtable = vtable;
	g_log("Probe: device draws - hooks on the device's vtable at %p (slots 81..84 were %p %p %p %p)", static_cast<void*>(vtable),
	      reinterpret_cast<void*>(g_drawPrimitive), reinterpret_cast<void*>(g_drawIndexedPrimitive), reinterpret_cast<void*>(g_drawPrimitiveUP),
	      reinterpret_cast<void*>(g_drawIndexedPrimitiveUP));
}

}  // namespace

void InstallUnroutedDrawProbe(LogFn log) {
	g_log = log;
	g_unroutedDrawProbeOn = true;
}

void LogUnroutedDrawSummary() {
	if (g_unroutedDrawProbeOn) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

void UnroutedDrawRouteEnter(void* renderer) {
	EnsureHooks(renderer);
	++g_depth;
}

void UnroutedDrawRouteLeave() {
	--g_depth;
}

}  // namespace obng::plugin
