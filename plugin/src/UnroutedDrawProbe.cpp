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

// ---- The image space step's draws, written out ---------------------------------
//
// 0x007B48E0 (cdecl(renderer, target, arg); it loads the image space manager
// [0x00B42D7C] and calls 0x008037D0 - objdump 2026-11-05) runs the frame's
// screen effects (the blur shader's bloom, HDR) at the end of the scene
// render. On the first few calls made while eye data is set, every device
// draw inside it is written with where it goes and what it reads: the
// viewport, the render target's and the first texture's size, the blend
// state, the scissor, and whether the stereo route issued it. That is what
// says why a screen effect reaches one eye only.
constexpr UInt32 kImageSpace = 0x007B48E0;
constexpr UInt8 kImageSpaceBytes[] = {0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x4C, 0x24, 0x08};  // mov eax,[esp+0Ch]; mov ecx,[esp+8]
constexpr UInt32 kWorldSceneGraph = 0x00B333CC;        // re/functions/0x0040C830.md; OBVR GameAddresses.h kWorldSceneGraph
constexpr UInt32 kSceneGraphCameraOffset = 0xDC;       // OBVR GameAddresses.h:1084
constexpr UInt32 kCameraPortOffset = 0x110;            // re/functions/0x0040C830.md (left, right, top, bottom)
constexpr UInt32 kImageSpaceCallsTraced = 3;
using ImageSpaceFn = void(__cdecl*)(UInt32 renderer, UInt32 target, UInt32 argument);
ImageSpaceFn g_imageSpace = nullptr;
UInt32 g_imageSpaceTraced = 0;
bool g_tracing = false;
UInt32 g_traceDraw = 0;

void TraceDraw(IDirect3DDevice9* device, un::Method method, std::uint32_t site, UINT primitives) {
	D3DVIEWPORT9 port{};
	device->GetViewport(&port);
	D3DSURFACE_DESC target{};
	IDirect3DSurface9* surface = nullptr;
	if (SUCCEEDED(device->GetRenderTarget(0, &surface)) && surface != nullptr) {
		surface->GetDesc(&target);
		surface->Release();
	}
	D3DSURFACE_DESC source{};
	IDirect3DBaseTexture9* base = nullptr;
	if (SUCCEEDED(device->GetTexture(0, &base)) && base != nullptr) {
		if (base->GetType() == D3DRTYPE_TEXTURE) {
			static_cast<IDirect3DTexture9*>(base)->GetLevelDesc(0, &source);
		}
		base->Release();
	}
	DWORD blend = 0, sourceBlend = 0, destBlend = 0, scissor = 0, colorWrite = 0, depth = 0;
	device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
	device->GetRenderState(D3DRS_SRCBLEND, &sourceBlend);
	device->GetRenderState(D3DRS_DESTBLEND, &destBlend);
	device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
	device->GetRenderState(D3DRS_COLORWRITEENABLE, &colorWrite);
	device->GetRenderState(D3DRS_ZENABLE, &depth);
	RECT cut{};
	device->GetScissorRect(&cut);
	IDirect3DVertexShader9* vertexShader = nullptr;
	IDirect3DPixelShader9* pixelShader = nullptr;
	device->GetVertexShader(&vertexShader);
	device->GetPixelShader(&pixelShader);
	char name[96];
	NameSite(site, name, sizeof(name));
	g_log("Probe: image space draw %u - %s %u primitives from %08X (%s), %s; viewport %lu,%lu %lux%lu; target %ux%u (samples %u); texture 0 %ux%u; "
	      "blend %lu (%lu -> %lu), colour write %lX, depth %lu, scissor %lu (%ld,%ld..%ld,%ld), vertex shader %s, pixel shader %s",
	      ++g_traceDraw, un::MethodName(method), primitives, site, name, g_depth != 0 ? "inside the route" : "outside the route",
	      static_cast<unsigned long>(port.X), static_cast<unsigned long>(port.Y), static_cast<unsigned long>(port.Width),
	      static_cast<unsigned long>(port.Height), target.Width, target.Height, static_cast<unsigned>(target.MultiSampleType), source.Width,
	      source.Height, static_cast<unsigned long>(blend), static_cast<unsigned long>(sourceBlend), static_cast<unsigned long>(destBlend),
	      static_cast<unsigned long>(colorWrite), static_cast<unsigned long>(depth), static_cast<unsigned long>(scissor), cut.left, cut.top,
	      cut.right, cut.bottom, vertexShader != nullptr ? "set" : "none", pixelShader != nullptr ? "set" : "none");
	if (vertexShader != nullptr) {
		vertexShader->Release();
	}
	if (pixelShader != nullptr) {
		pixelShader->Release();
	}
}

void __cdecl ProbeImageSpace(UInt32 renderer, UInt32 target, UInt32 argument) {
	ObngStereoViewport eyeZero{};
	const bool trace = g_imageSpaceTraced < kImageSpaceCallsTraced && StereoEyeZeroViewport(&eyeZero);
	if (trace) {
		++g_imageSpaceTraced;
		g_traceDraw = 0;
		float port[4] = {};
		const UInt32 sceneGraph = *reinterpret_cast<volatile const UInt32*>(static_cast<std::uintptr_t>(kWorldSceneGraph));
		const UInt32 camera = sceneGraph != 0 ? *reinterpret_cast<volatile const UInt32*>(static_cast<std::uintptr_t>(sceneGraph + kSceneGraphCameraOffset)) : 0;
		if (camera != 0) {
			for (int i = 0; i < 4; ++i) {
				port[i] = *reinterpret_cast<volatile const float*>(static_cast<std::uintptr_t>(camera + kCameraPortOffset + 4 * i));
			}
		}
		g_log("Probe: image space call %u (renderer %08X, target %08X, argument %08X) with eye data set - eye 0's viewport %u,%u %ux%u, the main "
		      "camera's port %.3f %.3f %.3f %.3f",
		      g_imageSpaceTraced, renderer, target, argument, eyeZero.x, eyeZero.y, eyeZero.width, eyeZero.height, static_cast<double>(port[0]),
		      static_cast<double>(port[1]), static_cast<double>(port[2]), static_cast<double>(port[3]));
		g_tracing = true;
	}
	g_imageSpace(renderer, target, argument);
	if (trace) {
		g_tracing = false;
		g_log("Probe: image space call %u done - %u device draws", g_imageSpaceTraced, g_traceDraw);
	}
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
	if (g_tracing) {
		TraceDraw(device, method, site, primitives);
	}
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

UInt32 InstallUnroutedDrawProbe(LogFn log) {
	g_log = log;
	g_unroutedDrawProbeOn = true;
	return InstallEntryDetour("the image space step 0x007B48E0 (device draws: its draws written out)", kImageSpace, kImageSpaceBytes,
	                          sizeof(kImageSpaceBytes), reinterpret_cast<const void*>(&ProbeImageSpace), reinterpret_cast<void**>(&g_imageSpace), log)
	           ? 1u
	           : 0u;
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
