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

#include <cstdio>

#include "Memory.h"
#include "RouteStereoDraw.h"
#include "UnroutedDrawLogic.h"

namespace obng::plugin {

bool g_unroutedDrawProbeOn = false;

namespace {

namespace un = unrouted;

constexpr UInt32 kRendererDeviceOffset = 0x280;  // re/functions/0x00762640.md
constexpr UInt32 kSlotDrawPrimitive = 81;  // then DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP
constexpr UInt32 kSitesListed = 40;
// Oblivion.exe's code (ImageBase 0x00400000; .text ends before the import
// pointers at 0x00A28000, e.g. 0x00A28078 read in re/functions/0x007C78D0.md)
// and how far up the stack the game's call site is looked for behind
// another module's hook.
constexpr std::uint32_t kGameCodeLow = 0x00401000;
constexpr std::uint32_t kGameCodeHigh = 0x00A28000;
constexpr std::uint32_t kStackScanned = 96;

using DrawPrimitiveFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedPrimitiveFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using DrawPrimitiveUPFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedPrimitiveUPFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);

// The hooks stay the outermost. Another plugin that hooks the same slots
// after this probe (FoliageAA.dll does: run obng-single-pass-spell/
// 20261105-193120 saw its forwarding call as the only site) would hide the
// game's call sites, so the route's detours put the hooks back on top
// whenever a slot holds something else. Each time that happens is a layer:
// what stood in the slots then is that layer's originals. A hook entered
// from the game is at level 0 and calls the newest layer's original (the
// other plugin's hook); entered again from inside that, it is at level 1 and
// calls the layer below - down to the driver's method.
constexpr int kMethods = 4;
constexpr int kLayers = 4;
void* g_originals[kLayers][kMethods] = {};
int g_layerCount = 0;
int g_hookLevel = 0;
bool g_hookFailed = false;

void* OriginalFor(int level, int method) {
	const int layer = g_layerCount - 1 - level;
	return g_originals[layer < 0 ? 0 : layer][method];
}
LogFn g_log = nullptr;
UInt32 g_depth = 0;  // the route's detours on the stack
un::Stats g_stats;
volatile LONG g_summaryRequested = 0;

// "Module.dll+0xOFFSET" for an address in a loaded module, else empty.
void NameSite(std::uint32_t site, char* out, std::size_t capacity) {
	out[0] = '\0';
	HMODULE module = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(site)), &module) ||
	    module == nullptr) {
		return;
	}
	char path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameA(module, path, sizeof(path));
	const char* name = path;
	for (DWORD i = 0; i < length; ++i) {
		if (path[i] == '\\' || path[i] == '/') {
			name = path + i + 1;
		}
	}
	std::snprintf(out, capacity, "%s+0x%X", name, static_cast<unsigned>(site - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(module))));
}

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
	for (UInt32 rank = 0; rank < kSitesListed && un::FormatSite(g_stats, rank, line, sizeof(line), &NameSite) > 0; ++rank) {
		g_log("%s", line);
	}
	g_stats.Reset();
}

void Note(IDirect3DDevice9* device, un::Method method, void** returnSlot, UINT primitives) {
	auto site = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(*returnSlot));
	std::uint32_t via = 0;
	if (g_depth == 0 && !un::InImage(site, kGameCodeLow, kGameCodeHigh)) {
		// Another module's hook called through: the game's own call site lies above it on the stack.
		const std::uint32_t game = un::GameCallSite(reinterpret_cast<const std::uint32_t*>(returnSlot + 1), kStackScanned, kGameCodeLow, kGameCodeHigh,
		                                            un::SlotOffset(method),
		                                            [](std::uint32_t address) {
			                                            return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(address - 7));
		                                            });
		if (game != 0) {
			via = site;
			site = game;
		}
	}
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
	g_stats.Add(un::Classify(false, eyesSet, current, zero), method, site, current, primitives, via);
	FlushSummaryIfRequested();
}

// One hook's frame: counts at level 0 only, and names the original to call.
struct HookFrame {
	int level;
	HookFrame(IDirect3DDevice9* device, un::Method method, void** returnSlot, UINT primitives) : level(g_hookLevel++) {
		if (level == 0) {
			Note(device, method, returnSlot, primitives);
		}
	}
	~HookFrame() { --g_hookLevel; }
	HookFrame(const HookFrame&) = delete;
	HookFrame& operator=(const HookFrame&) = delete;
	template <class Fn>
	Fn Original(un::Method method) const {
		return reinterpret_cast<Fn>(OriginalFor(level, static_cast<int>(method)));
	}
};

HRESULT __stdcall HookDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT start, UINT count) {
	const HookFrame frame{device, un::Method::kDrawPrimitive, static_cast<void**>(_AddressOfReturnAddress()), count};
	return frame.Original<DrawPrimitiveFn>(un::Method::kDrawPrimitive)(device, type, start, count);
}

HRESULT __stdcall HookDrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT vertices, UINT start,
                                           UINT count) {
	const HookFrame frame{device, un::Method::kDrawIndexedPrimitive, static_cast<void**>(_AddressOfReturnAddress()), count};
	return frame.Original<DrawIndexedPrimitiveFn>(un::Method::kDrawIndexedPrimitive)(device, type, base, minIndex, vertices, start, count);
}

HRESULT __stdcall HookDrawPrimitiveUP(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT count, const void* data, UINT stride) {
	const HookFrame frame{device, un::Method::kDrawPrimitiveUP, static_cast<void**>(_AddressOfReturnAddress()), count};
	return frame.Original<DrawPrimitiveUPFn>(un::Method::kDrawPrimitiveUP)(device, type, count, data, stride);
}

HRESULT __stdcall HookDrawIndexedPrimitiveUP(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, UINT minIndex, UINT vertices, UINT count,
                                             const void* indices, D3DFORMAT format, const void* data, UINT stride) {
	const HookFrame frame{device, un::Method::kDrawIndexedPrimitiveUP, static_cast<void**>(_AddressOfReturnAddress()), count};
	return frame.Original<DrawIndexedPrimitiveUPFn>(un::Method::kDrawIndexedPrimitiveUP)(device, type, minIndex, vertices, count, indices, format, data,
	                                                                                   stride);
}

// Puts the four hooks on the vtable of the renderer's device, and back on
// top when another module has hooked a slot since (a new layer).
void EnsureHooks(void* renderer) {
	if (g_hookFailed) {
		return;
	}
	auto* device = *reinterpret_cast<IDirect3DDevice9**>(reinterpret_cast<std::uintptr_t>(renderer) + kRendererDeviceOffset);
	if (device == nullptr) {
		return;
	}
	void** vtable = *reinterpret_cast<void***>(device);
	void* const hooks[kMethods] = {reinterpret_cast<void*>(&HookDrawPrimitive), reinterpret_cast<void*>(&HookDrawIndexedPrimitive),
	                               reinterpret_cast<void*>(&HookDrawPrimitiveUP), reinterpret_cast<void*>(&HookDrawIndexedPrimitiveUP)};
	bool onTop = true;
	for (int m = 0; m < kMethods; ++m) {
		onTop = onTop && vtable[kSlotDrawPrimitive + m] == hooks[m];
	}
	if (onTop) {
		return;
	}
	if (g_layerCount == kLayers) {
		g_hookFailed = true;
		g_log("Probe: device draws - the hooks were replaced %d times; left as they are", kLayers);
		return;
	}
	void* found[kMethods];
	for (int m = 0; m < kMethods; ++m) {
		found[m] = vtable[kSlotDrawPrimitive + m];
		// A slot that still holds this probe's hook keeps the layer below's original.
		g_originals[g_layerCount][m] = found[m] == hooks[m] && g_layerCount != 0 ? g_originals[g_layerCount - 1][m] : found[m];
	}
	++g_layerCount;
	for (int m = 0; m < kMethods; ++m) {
		if (found[m] != hooks[m] &&
		    !SafeWrite(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&vtable[kSlotDrawPrimitive + m])), &hooks[m], sizeof(hooks[m]))) {
			g_hookFailed = true;
			g_log("Probe: device draws - the device's vtable at %p could not be written", static_cast<void*>(vtable));
			return;
		}
	}
	char names[kMethods][96];
	for (int m = 0; m < kMethods; ++m) {
		NameSite(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(found[m])), names[m], sizeof(names[m]));
	}
	g_log("Probe: device draws - hooks on the device's vtable at %p, layer %d; slots 81..84 held %p (%s) %p (%s) %p (%s) %p (%s)",
	      static_cast<void*>(vtable), g_layerCount, found[0], names[0], found[1], names[1], found[2], names[2], found[3], names[3]);
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
