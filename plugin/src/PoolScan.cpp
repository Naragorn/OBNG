#include "PoolScan.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Memory.h"
#include "engine/memory/PoolScanLogic.h"

namespace obng::plugin {

namespace {

namespace ps = obng::engine::memory::pool_scan;

LogFn g_log = nullptr;
ps::Mode g_mode = ps::Mode::Off;
UInt32 g_resume = ps::kResume;
SRWLOCK g_lock = SRWLOCK_INIT;  // the counts: the search's thread against the summary's
ps::Counts g_counts;
UInt32 g_mismatchPool = 0, g_mismatchFast = 0, g_mismatchWalked = 0;  // the last mismatch, under the lock

template <typename T>
T At(UInt32 base, std::size_t offset) {
	T value;
	std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base) + offset), sizeof(T));
	return value;
}

// In place of the pool's walk: the page to commit, or none. Runs inside the game's allocation (0x00401AA0 holds
// its lock): nothing here allocates or logs.
std::uint32_t __fastcall FirstFreePage(UInt32 pool) {
	const UInt32 pages = At<UInt32>(pool, ps::kReservedPages);
	const UInt32 committed = At<UInt32>(pool, ps::kCommittedPages);
	const UInt32 table = At<UInt32>(pool, ps::kPageTable);
	const auto entry = [table](std::uint32_t i) { return At<UInt16>(table, static_cast<std::size_t>(i) * 2); };
	const bool noneLeft = ps::NoneLeft(committed, pages);
	std::uint32_t answer = ps::kNone;
	bool agrees = true;
	if (g_mode == ps::Mode::Check) {
		answer = ps::FirstFree(pages, entry);
		agrees = ps::Agrees(noneLeft ? ps::kNone : answer, answer);
	} else if (!noneLeft) {
		answer = ps::FirstFree(pages, entry);
	}
	AcquireSRWLockExclusive(&g_lock);
	g_counts.Note(At<UInt32>(pool, ps::kElementSize), pages, answer, noneLeft);
	if (!agrees) {
		g_counts.NoteMismatch();
		g_mismatchPool = pool;
		g_mismatchFast = ps::kNone;
		g_mismatchWalked = answer;
	}
	ReleaseSRWLockExclusive(&g_lock);
	return answer;
}

// From 0x00402493, the free list empty: ESI the pool. The index goes to EDI as the walk leaves it, and the game's
// own compare at 0x004024C2 takes it from there. EAX, ECX and EDX are written before they are read on both ways on.
__declspec(naked) void SearchSite() {
	__asm {
		mov ecx, esi
		call FirstFreePage
		mov edi, eax
		jmp dword ptr [g_resume]
	}
}

}  // namespace

void InstallPoolScan(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = ps::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "PoolScan", ps::Shipped(), iniPath)));
	if (g_mode == ps::Mode::Off) {
		return;
	}
	g_log = log;
	if (!Verify(ps::kSite, ps::kSiteBytes, sizeof(ps::kSiteBytes)) || !Verify(ps::kResume, ps::kResumeBytes, sizeof(ps::kResumeBytes))) {
		char have[64];
		DescribeBytes(ps::kSite, have, sizeof(have));
		log("Route: pool scan - the pool allocation's walk at %08X reads %s, not the game's own bytes; not routed", ps::kSite, have);
		g_mode = ps::Mode::Off;
		return;
	}
	UInt8 patch[sizeof(ps::kSiteBytes)];
	std::memset(patch, 0x90, sizeof(patch));
	patch[0] = 0xE9;
	const UInt32 relative = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&SearchSite)) - (ps::kSite + 5);
	std::memcpy(&patch[1], &relative, sizeof(relative));
	if (!SafeWrite(ps::kSite, patch, sizeof(patch))) {
		log("Route: pool scan - the walk at %08X could not be written; not routed", ps::kSite);
		g_mode = ps::Mode::Off;
		return;
	}
	log(g_mode == ps::Mode::Route
	        ? "Route: pool scan (route) - a pool with every page committed answers \"no page left\" from its count; the page table 0x00402480 walks is "
	          "read only while a page is left"
	        : "Route: pool scan (check) - the pool's walk for a page to commit runs as the game's, its answer compared with the count's");
}

void LogPoolScanSummary() {
	if (g_mode == ps::Mode::Off) {
		return;
	}
	AcquireSRWLockExclusive(&g_lock);
	const ps::Counts c = g_counts;
	const UInt32 pool = g_mismatchPool, fast = g_mismatchFast, walked = g_mismatchWalked;
	g_counts = ps::Counts{};
	ReleaseSRWLockExclusive(&g_lock);
	char sizes[256] = "";
	int at = 0;
	const ps::Top top = c.Busiest();
	for (std::uint32_t i = 0; i < top.used && at < static_cast<int>(sizeof(sizes)) - 1; ++i) {
		const ps::SlotCounts& s = c.bySlot[top.slot[i]];
		at += std::snprintf(sizes + at, sizeof(sizes) - at, "%s%u bytes %llu (none left %llu, %u pages)", i == 0 ? "" : ", ", top.slot[i] * 4, s.calls, s.none, s.pages);
	}
	const bool route = g_mode == ps::Mode::Route;
	g_log("Route: pool scan (%s) - in this window %llu searches with a pool's free list empty: no page left %llu (%s %llu), a page committed %llu; "
	      "the game's walks %llu entries (%s %llu); mismatches %llu; by element size: %s",
	      route ? "route" : "check", c.calls, c.none, route ? "the walk left out" : "the count says so without a walk", c.leftOut, c.found,
	      c.walkedOriginal, route ? "not read" : "the route would not read", c.walkedSaved, c.mismatches, top.used != 0 ? sizes : "none");
	if (c.mismatches != 0) {
		g_log("Route: pool scan - MISMATCH: the last for the pool %08X, the count's answer %08X, the walk's %08X", pool, fast, walked);
	}
}

}  // namespace obng::plugin
