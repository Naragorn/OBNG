#include "HeapProbe.h"

#include "OcclusionCull.h"

#include <windows.h>

#include <intrin.h>

#include <cstdio>

namespace obng::plugin {
namespace {

// MemoryHeap's own two methods, which every wrapper and 79 direct callers reach (read): Allocate
// 0x00401AA0 __thiscall(size, flag) RET 8, Free 0x00401D40 __thiscall(pointer) RET 4.
constexpr UInt32 kAllocAddress = 0x00401AA0;
constexpr UInt32 kFreeAddress = 0x00401D40;
constexpr UInt8 kAllocBytes[] = {0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x0C, 0x00};  // push esi; mov esi,ecx; cmp [esi+0Ch],0 (read)
constexpr UInt8 kFreeBytes[] = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C};         // push esi; push edi; mov edi,[esp+0Ch] (read)
using AllocFn = void*(__fastcall*)(void* heap, void* edx, UInt32 size, UInt32 flag);
using FreeFn = void(__fastcall*)(void* heap, void* edx, void* pointer);

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
// The other threads' calls (the loader's, mostly), interlocked.
volatile LONG64 g_otherAllocs = 0;
volatile LONG64 g_otherFrees = 0;
volatile LONG64 g_otherTicks = 0;
// The render thread's frames by length, each with the heap's time in it: a
// frame ends when the culler's eye pass count moves.
constexpr double kLongFrameMs = 30.0;
UInt32 g_framePasses = 0;
unsigned __int64 g_frameStart = 0;
unsigned __int64 g_frameHeapTicks = 0;
unsigned __int64 g_frameHeapCalls = 0;
double g_ticksPerMs = 0.0;  // from the last summary
unsigned __int64 g_longFrames = 0;
unsigned __int64 g_longTicks = 0;
unsigned __int64 g_longHeapTicks = 0;
unsigned __int64 g_longHeapCalls = 0;
unsigned __int64 g_longest = 0;
unsigned __int64 g_longestHeap = 0;

void FrameEdge(unsigned __int64 now) {
	const UInt32 passes = OcclusionCullEyePasses();
	if (passes == g_framePasses) {
		return;
	}
	if (g_frameStart != 0 && g_ticksPerMs > 0.0) {
		const unsigned __int64 length = now - g_frameStart;
		if (static_cast<double>(length) > kLongFrameMs * g_ticksPerMs) {
			++g_longFrames;
			g_longTicks += length;
			g_longHeapTicks += g_frameHeapTicks;
			g_longHeapCalls += g_frameHeapCalls;
			if (length > g_longest) {
				g_longest = length;
				g_longestHeap = g_frameHeapTicks;
			}
		}
	}
	g_framePasses = passes;
	g_frameStart = now;
	g_frameHeapTicks = 0;
	g_frameHeapCalls = 0;
}
unsigned __int64 g_lastTsc = 0;
LARGE_INTEGER g_lastQpc{};
UInt32 g_lastPasses = 0;

unsigned SizeClass(UInt32 size) { return size <= 16 ? 0 : size <= 64 ? 1 : size <= 256 ? 2 : size <= 1024 ? 3 : size <= 16384 ? 4 : 5; }

void* __fastcall AllocReplacement(void* heap, void* /*edx*/, UInt32 size, UInt32 flag) {
	if (g_thread == 0 && OcclusionCullInEyePass()) {
		g_thread = GetCurrentThreadId();  // the render thread is the one that allocates inside an eye pass
	}
	const unsigned __int64 t0 = __rdtsc();
	void* result = reinterpret_cast<AllocFn>(g_allocOriginal)(heap, nullptr, size, flag);
	const unsigned __int64 t1 = __rdtsc();
	if (GetCurrentThreadId() != g_thread) {
		InterlockedIncrement64(&g_otherAllocs);
		InterlockedAdd64(&g_otherTicks, static_cast<LONG64>(t1 - t0));
		return result;
	}
	FrameEdge(t0);
	Side& side = g_side[OcclusionCullInEyePass() ? 1 : 0];
	side.allocTicks += t1 - t0;
	++side.allocs;
	side.bytes += size;
	++side.bySize[SizeClass(size)];
	g_frameHeapTicks += t1 - t0;
	++g_frameHeapCalls;
	return result;
}

void __fastcall FreeReplacement(void* heap, void* /*edx*/, void* pointer) {
	const unsigned __int64 t0 = __rdtsc();
	reinterpret_cast<FreeFn>(g_freeOriginal)(heap, nullptr, pointer);
	const unsigned __int64 t1 = __rdtsc();
	if (GetCurrentThreadId() != g_thread) {
		InterlockedIncrement64(&g_otherFrees);
		InterlockedAdd64(&g_otherTicks, static_cast<LONG64>(t1 - t0));
		return;
	}
	FrameEdge(t0);
	Side& side = g_side[OcclusionCullInEyePass() ? 1 : 0];
	side.freeTicks += t1 - t0;
	++side.frees;
	g_frameHeapTicks += t1 - t0;
	++g_frameHeapCalls;
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
	log("Probe: heap calls - MemoryHeap's Allocate 0x00401AA0 and Free 0x00401D40 counted and timed on the render thread (the one that allocates inside an eye pass; needs [Route] OcclusionCull)");
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
	g_ticksPerMs = ticksPerMs;
	const UInt32 passes = OcclusionCullEyePasses();
	const UInt32 frames = passes - g_lastPasses;
	g_lastTsc = tsc;
	g_lastQpc = qpc;
	g_lastPasses = passes;
	Side side[2] = {g_side[0], g_side[1]};
	g_side[0] = Side{};
	g_side[1] = Side{};
	const unsigned __int64 others = 0;
	if (frames == 0 || ticksPerMs <= 0.0) {
		return;
	}
	{
		const LONG64 otherAllocs = InterlockedExchange64(&g_otherAllocs, 0);
		const LONG64 otherFrees = InterlockedExchange64(&g_otherFrees, 0);
		const LONG64 otherTicks = InterlockedExchange64(&g_otherTicks, 0);
		g_log("Probe: heap calls - %u frames; other threads: %lld allocations, %lld releases, %.1f ms in them; the render thread's frames over "
		      "%.0f ms: %llu, %.0f ms in all with %.1f ms (%.1f %%) in the heap over %llu calls; the longest %.0f ms with %.1f ms in the heap",
		      frames, otherAllocs, otherFrees, static_cast<double>(otherTicks) / ticksPerMs, kLongFrameMs, g_longFrames,
		      static_cast<double>(g_longTicks) / ticksPerMs, static_cast<double>(g_longHeapTicks) / ticksPerMs,
		      g_longTicks > 0 ? 100.0 * static_cast<double>(g_longHeapTicks) / static_cast<double>(g_longTicks) : 0.0, g_longHeapCalls,
		      static_cast<double>(g_longest) / ticksPerMs, static_cast<double>(g_longestHeap) / ticksPerMs);
		g_longFrames = 0;
		g_longTicks = 0;
		g_longHeapTicks = 0;
		g_longHeapCalls = 0;
		g_longest = 0;
		g_longestHeap = 0;
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
