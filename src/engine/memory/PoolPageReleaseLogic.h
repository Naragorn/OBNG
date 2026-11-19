#pragma once
// The memory pools' page release held back at a cell crossing - the decisions and the counts, over plain values
// ([Route] PoolPageRelease in the plugin's PoolPageRelease.cpp).
//
// Read 2026-11-19 (the disassembly of Oblivion.exe 1.2.0.416 and the rea-all pseudocode):
// - The grid's move 0x00444FB0 calls the purge 0x0043FC20 twice when the player crosses into a new cell: at
//   0x00445371 when the buffer of interior cells was emptied (0x0043FE30 returned non-zero), and at 0x004454EF
//   every time. 0x004455E0 calls it three more times; other callers are the loads and the cell changes.
// - The purge, under the critical section 0x00B35380: 0x0055E390(1) twice, 0x007B84E0, 0x004A25F0 when a flag
//   is set, and between them `MemoryPool::FreeUnusedPagesForAllPools()` 0x00402740 (its string). That one walks
//   the 129 pool slots at 0x00B33080 (27 pools are made, 0x0040E3xx: element sizes 8 to 0x108 bytes, 174 MB
//   reserved in all) under the allocator's lock 0x00B32B80 - the same lock every MemoryHeap::Allocate 0x00401AA0
//   takes - and for each pool 0x00402640: the free list sorted by address (0x00402190), then every page whose
//   count is zero cut out of it and decommitted, VirtualFree(page, 0x1000, MEM_DECOMMIT).
// - A decommitted page stays reserved: the pool's address range is reserved once at its making (VirtualAlloc
//   MEM_RESERVE in 0x00402280) and its pages are committed again on demand (0x00402480, VirtualAlloc
//   MEM_COMMIT). So the release gives back committed memory, never address space.
//
// Measured 2026-11-18 (knowledge/background-loading.md): the release takes 1.2 to 19.4 ms of a crossing's purge
// in every second to third crossing, on the main thread, while every other thread's allocation waits on the lock.
//
// The route skips the release when the purge comes from one of the crossing's two calls; every other purge (the
// loads, the cell changes behind a loading screen) releases as before. Derived: what is kept committed meanwhile is
// bounded by the pools' 174 MB reservation, which the 32-bit address space carries in either case. Class B: the
// free list's order and so which addresses the pools hand out differ from the original's; nothing else changes.
//
// 64-bit clean: return addresses and thread ids are std::uint32_t values; no engine pointer.
#include <cstdint>

namespace obng::engine::memory::pool_page_release {

constexpr std::uint64_t kPageBytes = 0x1000;

// The return addresses of the grid's move's two calls of the purge (0x00445371 + 5, 0x004454EF + 5; read).
constexpr std::uint32_t kCrossingReturns[] = {0x00445376, 0x004454F4};

enum class Mode : std::uint8_t { Off, Skip, Measure };

inline Mode ModeFrom(unsigned value) {
	return value == 1 ? Mode::Skip : value == 2 ? Mode::Measure : Mode::Off;
}

inline bool FromCrossing(std::uint32_t returnAddress) {
	for (const std::uint32_t r : kCrossingReturns) {
		if (r == returnAddress) {
			return true;
		}
	}
	return false;
}

enum class Act : std::uint8_t { Original, Skip };

// crossingThread: the thread inside a crossing's purge, 0 when none.
inline Act Decide(Mode mode, std::uint32_t crossingThread, std::uint32_t thisThread) {
	return mode == Mode::Skip && crossingThread != 0 && crossingThread == thisThread ? Act::Skip : Act::Original;
}

// One pool's page counts (read in 0x00402280 / 0x00402480 / 0x00402640): +0x10C the pages reserved, +0x118 the
// pages committed, +0x104 the most pages committed at once.
struct PoolFields {
	std::uint32_t reservedPages = 0;
	std::uint32_t committedPages = 0;
	std::uint32_t peakPages = 0;
};

struct PoolTotals {
	std::uint32_t pools = 0;
	std::uint64_t reservedBytes = 0;
	std::uint64_t committedBytes = 0;
	std::uint64_t peakBytes = 0;

	void Add(const PoolFields& pool) {
		++pools;
		reservedBytes += pool.reservedPages * kPageBytes;
		committedBytes += pool.committedPages * kPageBytes;
		peakBytes += pool.peakPages * kPageBytes;
	}
};

struct Counts {
	std::uint64_t crossingPurges = 0;
	std::uint64_t otherPurges = 0;
	std::uint64_t skipped = 0;
	std::uint64_t released = 0;
	double crossingMs = 0.0;
	double longestCrossingMs = 0.0;
	double otherMs = 0.0;
	double longestOtherMs = 0.0;
	double releaseMs = 0.0;
	double longestReleaseMs = 0.0;

	void NotePurge(bool crossing, double ms) {
		++(crossing ? crossingPurges : otherPurges);
		(crossing ? crossingMs : otherMs) += ms;
		double& longest = crossing ? longestCrossingMs : longestOtherMs;
		if (ms > longest) {
			longest = ms;
		}
	}
	void NoteRelease(Act act, double ms) {
		if (act == Act::Skip) {
			++skipped;
			return;
		}
		++released;
		releaseMs += ms;
		if (ms > longestReleaseMs) {
			longestReleaseMs = ms;
		}
	}
};

}  // namespace obng::engine::memory::pool_page_release
