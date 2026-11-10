#include "HeapProbe.h"

#include "OcclusionCull.h"

#include <windows.h>

#include <intrin.h>

#include <cstdio>

namespace obng::plugin {
namespace {

constexpr UInt32 kAllocAddress = 0x00401F00;
constexpr UInt32 kFreeAddress = 0x00401F20;
constexpr UInt8 kAllocBytes[] = {0x8B, 0x44, 0x24, 0x04, 0x6A, 0x01};  // mov eax,[esp+4]; push 1 (read)
constexpr UInt8 kFreeBytes[] = {0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0};   // mov eax,[esp+4]; test eax,eax (read)
using AllocFn = void*(__cdecl*)(UInt32 size);
using FreeFn = void(__cdecl*)(void* pointer);

void* g_allocOriginal = nullptr;
void* g_freeOriginal = nullptr;
LogFn g_log = nullptr;
DWORD g_thread = 0;
bool g_installed = false;

// Written by the render thread, read by the summary's thread at the end of
// a window (a probe: a torn read is accepted).
constexpr unsigned kSizeClasses = 6;  // <=16, <=64, <=256, <=1024, <=16384, more
struct Side {
	unsigned __int64 allocs = 0;
	unsigned __int64 frees = 0;
	unsigned __int64 allocTicks = 0;
	unsigned __int64 freeTicks = 0;
	unsigned __int64 bytes = 0;
	unsigned __int64 bySize[kSizeClasses] = {};
};
Side g_side[2];  // outside the eye pass, inside it
unsigned __int64 g_otherThreads = 0;
unsigned __int64 g_lastTsc = 0;
LARGE_INTEGER g_lastQpc{};
UInt32 g_lastPasses = 0;

unsigned SizeClass(UInt32 size) { return size <= 16 ? 0 : size <= 64 ? 1 : size <= 256 ? 2 : size <= 1024 ? 3 : size <= 16384 ? 4 : 5; }

void* __cdecl AllocReplacement(UInt32 size) {
	if (g_thread == 0 && OcclusionCullInEyePass()) {
		g_thread = GetCurrentThreadId();  // the render thread is the one that allocates inside an eye pass
	}
	if (GetCurrentThreadId() != g_thread) {
		++g_otherThreads;
		return reinterpret_cast<AllocFn>(g_allocOriginal)(size);
	}
	const unsigned __int64 t0 = __rdtsc();
	void* result = reinterpret_cast<AllocFn>(g_allocOriginal)(size);
	Side& side = g_side[OcclusionCullInEyePass() ? 1 : 0];
	side.allocTicks += __rdtsc() - t0;
	++side.allocs;
	side.bytes += size;
	++side.bySize[SizeClass(size)];
	return result;
}

void __cdecl FreeReplacement(void* pointer) {
	if (GetCurrentThreadId() != g_thread) {
		reinterpret_cast<FreeFn>(g_freeOriginal)(pointer);
		return;
	}
	const unsigned __int64 t0 = __rdtsc();
	reinterpret_cast<FreeFn>(g_freeOriginal)(pointer);
	Side& side = g_side[OcclusionCullInEyePass() ? 1 : 0];
	side.freeTicks += __rdtsc() - t0;
	++side.frees;
}

}  // namespace

void InstallHeapProbe(const ShadowSettings& settings, LogFn log) {
	if (g_installed || !settings.heapCalls) {
		return;
	}
	g_log = log;
	if (!InstallEntryDetour("MemoryHeap allocate", kAllocAddress, kAllocBytes, sizeof(kAllocBytes), reinterpret_cast<const void*>(&AllocReplacement),
	                        &g_allocOriginal, log, DetourRole::Shadow) ||
	    !InstallEntryDetour("MemoryHeap free", kFreeAddress, kFreeBytes, sizeof(kFreeBytes), reinterpret_cast<const void*>(&FreeReplacement),
	                        &g_freeOriginal, log, DetourRole::Shadow)) {
		log("Probe: heap calls - an entry is not the game's own (another plugin has it?), nothing counted");
		return;
	}
	g_lastTsc = __rdtsc();
	QueryPerformanceCounter(&g_lastQpc);
	g_installed = true;
	log("Probe: heap calls - the allocator's entries 0x00401F00 and 0x00401F20 counted and timed on the render thread (the one that allocates inside an eye pass; needs [Route] OcclusionCull)");
}

void LogHeapProbeSummary() {
	if (!g_installed || g_log == nullptr) {
		return;
	}
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = frequency.QuadPart > 0 ? static_cast<double>(qpc.QuadPart - g_lastQpc.QuadPart) / static_cast<double>(frequency.QuadPart) : 0.0;
	const double ticksPerMs = seconds > 0.0 ? static_cast<double>(tsc - g_lastTsc) / (seconds * 1000.0) : 0.0;
	const UInt32 passes = OcclusionCullEyePasses();
	const UInt32 frames = passes - g_lastPasses;
	g_lastTsc = tsc;
	g_lastQpc = qpc;
	g_lastPasses = passes;
	Side side[2] = {g_side[0], g_side[1]};
	g_side[0] = Side{};
	g_side[1] = Side{};
	const unsigned __int64 others = g_otherThreads;
	g_otherThreads = 0;
	if (frames == 0 || ticksPerMs <= 0.0) {
		return;
	}
	const double per = 1.0 / static_cast<double>(frames);
	static const char* const kWhere[2] = {"outside the eye pass", "inside it"};
	for (unsigned i = 0; i < 2; ++i) {
		const Side& s = side[i];
		const double allocMs = static_cast<double>(s.allocTicks) / ticksPerMs;
		const double freeMs = static_cast<double>(s.freeTicks) / ticksPerMs;
		g_log("Probe: heap calls %s - %u frames: per frame %.0f allocations in %.3f ms (%.0f ns each), %.0f releases in %.3f ms (%.0f ns each), "
		      "%.1f KB asked for; by size <=16: %.0f, <=64: %.0f, <=256: %.0f, <=1024: %.0f, <=16384: %.0f, more: %.0f%s",
		      kWhere[i], frames, static_cast<double>(s.allocs) * per, allocMs * per, s.allocs > 0 ? allocMs * 1.0e6 / static_cast<double>(s.allocs) : 0.0,
		      static_cast<double>(s.frees) * per, freeMs * per, s.frees > 0 ? freeMs * 1.0e6 / static_cast<double>(s.frees) : 0.0,
		      static_cast<double>(s.bytes) * per / 1024.0, static_cast<double>(s.bySize[0]) * per, static_cast<double>(s.bySize[1]) * per,
		      static_cast<double>(s.bySize[2]) * per, static_cast<double>(s.bySize[3]) * per, static_cast<double>(s.bySize[4]) * per,
		      static_cast<double>(s.bySize[5]) * per, i == 0 && others > 0 ? " (other threads allocate too, not counted)" : "");
	}
}

}  // namespace obng::plugin
