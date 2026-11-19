#include "PoolPageRelease.h"

#include <windows.h>

#include <intrin.h>
#include <atomic>
#include <cstring>

#include "engine/memory/PoolPageReleaseLogic.h"

namespace obng::plugin {

namespace {

namespace ppr = obng::engine::memory::pool_page_release;

// __thiscall(char all), RET 4 at 0x0043FCC6 (read 2026-11-19).
constexpr UInt32 kPurge = 0x0043FC20;
constexpr UInt8 kPurgeBytes[] = {0x80, 0x3D, 0xD5, 0x50, 0xB3, 0x00, 0x00};  // cmp byte ptr [0x00B350D5], 0 (read)
// No arguments; it sets ECX itself and ends with a jump to LeaveCriticalSection's thunk 0x00401040 (read).
constexpr UInt32 kRelease = 0x00402740;
constexpr UInt8 kReleaseBytes[] = {0x56, 0x68, 0xA8, 0xF8, 0xA2, 0x00};  // push esi; push 0x00A2F8A8 (read)
// The pool slots by element size / 4 (0x00402740 walks 0x204 bytes of them; read).
constexpr UInt32 kPoolSlots = 0x00B33080;
constexpr UInt32 kPoolSlotCount = 0x81;

using PurgeFn = void(__thiscall*)(void* self, char all);
using ReleaseFn = void(__cdecl*)();

void* g_purgeOriginal = nullptr;
void* g_releaseOriginal = nullptr;
ppr::Mode g_mode = ppr::Mode::Off;
LogFn g_log = nullptr;
std::atomic<UInt32> g_crossingThread{0};
SRWLOCK g_lock = SRWLOCK_INIT;
ppr::Counts g_counts;  // under g_lock
double g_msPerTick = 0.0;

double Since(const LARGE_INTEGER& start) {
	LARGE_INTEGER now{};
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart - start.QuadPart) * g_msPerTick;
}

// The entry's replacement: __fastcall with an unused second register takes the __thiscall's this; one stack
// argument, callee-cleaned like the original's RET 4.
void __fastcall Purge(void* self, void*, char all) {
	const bool crossing = ppr::FromCrossing(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress())));
	if (crossing) {
		g_crossingThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
	}
	LARGE_INTEGER start{};
	QueryPerformanceCounter(&start);
	reinterpret_cast<PurgeFn>(g_purgeOriginal)(self, all);
	const double ms = Since(start);
	if (crossing) {
		g_crossingThread.store(0, std::memory_order_relaxed);
	}
	AcquireSRWLockExclusive(&g_lock);
	g_counts.NotePurge(crossing, ms);
	ReleaseSRWLockExclusive(&g_lock);
}

void __cdecl Release() {
	const ppr::Act act = ppr::Decide(g_mode, g_crossingThread.load(std::memory_order_relaxed), GetCurrentThreadId());
	double ms = 0.0;
	if (act == ppr::Act::Original) {
		LARGE_INTEGER start{};
		QueryPerformanceCounter(&start);
		reinterpret_cast<ReleaseFn>(g_releaseOriginal)();
		ms = Since(start);
	}
	AcquireSRWLockExclusive(&g_lock);
	g_counts.NoteRelease(act, ms);
	ReleaseSRWLockExclusive(&g_lock);
}

// The pools' page counts, read without the allocator's lock: a summary's numbers, not a decision's.
ppr::PoolTotals ReadPools() {
	ppr::PoolTotals totals;
	for (UInt32 i = 0; i < kPoolSlotCount; ++i) {
		UInt32 pool = 0;
		std::memcpy(&pool, reinterpret_cast<const void*>(kPoolSlots + i * 4), 4);
		if (pool == 0) {
			continue;
		}
		ppr::PoolFields fields;
		std::memcpy(&fields.reservedPages, reinterpret_cast<const void*>(pool + 0x10C), 4);
		std::memcpy(&fields.committedPages, reinterpret_cast<const void*>(pool + 0x118), 4);
		std::memcpy(&fields.peakPages, reinterpret_cast<const void*>(pool + 0x104), 4);
		totals.Add(fields);
	}
	return totals;
}

}  // namespace

void InstallPoolPageRelease(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = ppr::ModeFrom(GetPrivateProfileIntA("Route", "PoolPageRelease", 0, iniPath));
	if (g_mode == ppr::Mode::Off) {
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	g_msPerTick = 1000.0 / static_cast<double>(frequency.QuadPart);
	// Route in both modes: the measure mode compares nothing, so the shadow's install line would mislead.
	const DetourRole role = DetourRole::Route;
	if (!InstallEntryDetour("the purge 0x0043FC20 (pool page release: who calls)", kPurge, kPurgeBytes, sizeof(kPurgeBytes),
	                        reinterpret_cast<const void*>(&Purge), &g_purgeOriginal, log, role)) {
		g_mode = ppr::Mode::Off;
		return;
	}
	if (!InstallEntryDetour("MemoryPool::FreeUnusedPagesForAllPools 0x00402740 (pool page release)", kRelease, kReleaseBytes, sizeof(kReleaseBytes),
	                        reinterpret_cast<const void*>(&Release), &g_releaseOriginal, log, role)) {
		// The purge's detour stays and only counts: with no release detour nothing is skipped.
		g_mode = ppr::Mode::Measure;
		log("Route: pool page release - the release's entry refused; the purges are only timed");
		return;
	}
	log(g_mode == ppr::Mode::Skip ? "Route: pool page release - the pools' unused pages kept at a cell crossing, released at every other purge"
	                              : "Route: pool page release - measure: the purges and the release timed, nothing skipped");
}

void LogPoolPageReleaseSummary() {
	if (g_mode == ppr::Mode::Off) {
		return;
	}
	AcquireSRWLockExclusive(&g_lock);
	const ppr::Counts c = g_counts;
	g_counts = ppr::Counts{};
	ReleaseSRWLockExclusive(&g_lock);
	const ppr::PoolTotals pools = ReadPools();
	constexpr double kMb = 1024.0 * 1024.0;
	g_log("Route: pool page release (%s) - purges at a cell crossing %llu (%.3f ms, longest %.3f ms), elsewhere %llu (%.3f ms, longest %.3f ms); "
	      "the release skipped %llu, run %llu (%.3f ms, longest %.3f ms); %u pools: %.1f MB reserved, %.1f MB committed, peak %.1f MB",
	      g_mode == ppr::Mode::Skip ? "skip" : "measure", c.crossingPurges, c.crossingMs, c.longestCrossingMs, c.otherPurges, c.otherMs, c.longestOtherMs,
	      c.skipped, c.released, c.releaseMs, c.longestReleaseMs, pools.pools, static_cast<double>(pools.reservedBytes) / kMb,
	      static_cast<double>(pools.committedBytes) / kMb, static_cast<double>(pools.peakBytes) / kMb);
}

}  // namespace obng::plugin
