#include "MusicGraphProbe.h"

#include <windows.h>

#include <intrin.h>
#include <atomic>
#include <cstring>

#include "Memory.h"

namespace obng::plugin {

namespace {

// The music player's seven calls of CoCreateInstance(CLSID_FilterGraph 0x00A78FB4, null, CLSCTX_INPROC_SERVER,
// IID_IGraphBuilder 0x00A78F64, this+0x70), each followed by IGraphBuilder::RenderFile (vtable +0x34) on the track's
// MP3 (read 2026-11-19: 0x006AB160 once, 0x006ACD10 once, 0x006AD030 five times; `call [0x00A283C8]`).
constexpr UInt32 kSites[] = {0x006AB366, 0x006ACF46, 0x006AD216, 0x006AD4F8, 0x006AD806, 0x006ADB76, 0x006ADD56};
constexpr UInt8 kSiteBytes[] = {0xFF, 0x15, 0xC8, 0x83, 0xA2, 0x00};
constexpr UInt32 kCoCreateInstanceSlot = 0x00A283C8;  // the import's slot (read)
constexpr UInt32 kRenderFileSlot = 0x34 / 4;          // IGraphBuilder::RenderFile (IUnknown 3, IFilterGraph 8, Connect, Render)
constexpr UInt32 kLines = 64;

using CreateFn = HRESULT(__stdcall*)(const CLSID*, void*, DWORD, const IID*, void**);
using RenderFileFn = HRESULT(__stdcall*)(void* graph, const wchar_t* file, const wchar_t* playList);

LogFn g_log = nullptr;
CreateFn g_create = nullptr;
RenderFileFn g_renderFile = nullptr;
double g_ticksPerMs = 0.0;
std::atomic<unsigned> g_lines{0};
unsigned __int64 g_createTicks = 0;  // the last CoCreateInstance's time, written with its RenderFile's line
UInt32 g_createSite = 0;

double TicksPerMs() {
	LARGE_INTEGER q0, q1, f;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&q0);
	const unsigned __int64 t0 = __rdtsc();
	Sleep(30);
	QueryPerformanceCounter(&q1);
	const unsigned __int64 t1 = __rdtsc();
	return static_cast<double>(t1 - t0) / (static_cast<double>(q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart));
}

HRESULT __stdcall RenderFile(void* graph, const wchar_t* file, const wchar_t* playList) {
	const unsigned __int64 t0 = __rdtsc();
	const HRESULT result = g_renderFile(graph, file, playList);
	const double ms = static_cast<double>(__rdtsc() - t0) / g_ticksPerMs;
	if (g_lines.fetch_add(1, std::memory_order_relaxed) < kLines) {
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		char path[200] = {};
		WideCharToMultiByte(CP_ACP, 0, file != nullptr ? file : L"", -1, path, sizeof(path) - 1, nullptr, nullptr);
		g_log("Probe: music graph - site %08X: CoCreateInstance %.3f ms, RenderFile %.3f ms (%08lX) \"%s\", thread %lu, ending at tick %lld",
		      g_createSite, static_cast<double>(g_createTicks) / g_ticksPerMs, ms, static_cast<unsigned long>(result), path, GetCurrentThreadId(),
		      now.QuadPart);
	}
	return result;
}

// The vtable's RenderFile slot of the first graph made, swapped for the timing one (the vtable is quartz's, shared by
// every FilterGraph; only the game makes them here).
void HookRenderFile(void* graph) {
	if (g_renderFile != nullptr || graph == nullptr) {
		return;
	}
	UInt32* slot = *reinterpret_cast<UInt32**>(graph) + kRenderFileSlot;
	g_renderFile = reinterpret_cast<RenderFileFn>(*slot);
	const UInt32 replacement = reinterpret_cast<UInt32>(&RenderFile);
	if (!SafeWrite(reinterpret_cast<UInt32>(slot), &replacement, sizeof(replacement))) {
		g_log("Probe: music graph - the RenderFile slot could not be written; CoCreateInstance timed only");
		g_renderFile = nullptr;
	}
}

HRESULT __stdcall Create(const CLSID* clsid, void* outer, DWORD context, const IID* iid, void** out) {
	const UInt32 site = reinterpret_cast<UInt32>(_ReturnAddress()) - 5;
	const unsigned __int64 t0 = __rdtsc();
	const HRESULT result = g_create(clsid, outer, context, iid, out);
	g_createTicks = __rdtsc() - t0;
	g_createSite = site;
	if (SUCCEEDED(result) && out != nullptr) {
		HookRenderFile(*out);
	}
	return result;
}

}  // namespace

void InstallMusicGraphProbe(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Probe", "MusicGraph", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	for (const UInt32 site : kSites) {
		if (!Verify(site, kSiteBytes, sizeof(kSiteBytes))) {
			log("Probe: music graph - the call at %08X is not the game's own; not installed", site);
			return;
		}
	}
	g_create = *reinterpret_cast<CreateFn*>(kCoCreateInstanceSlot);
	g_ticksPerMs = TicksPerMs();
	unsigned patched = 0;
	for (const UInt32 site : kSites) {
		UInt8 patch[sizeof(kSiteBytes)] = {0xE8, 0, 0, 0, 0, 0x90};
		const UInt32 rel = reinterpret_cast<UInt32>(&Create) - (site + 5);
		std::memcpy(patch + 1, &rel, 4);
		patched += SafeWrite(site, patch, sizeof(patch)) ? 1 : 0;
	}
	log("Probe: music graph - %u of %u CoCreateInstance calls of the music player timed, RenderFile timed from the first graph on", patched,
	    static_cast<unsigned>(sizeof(kSites) / sizeof(kSites[0])));
}

}  // namespace obng::plugin
