#pragma once
// The small-block pool's search for a page to commit - the decisions over plain values ([Route] PoolScan in the
// plugin's PoolScan.cpp). Class A: the same answer, without the walk.
//
// MemoryHeap's pool allocation 0x00402480 (read 2026-11-24, the instructions): a block comes off the pool's free
// list (+0x44). With the list empty the pool looks for a page to commit: it walks its page table (+0x108, one
// 16-bit word a page of its reserved range: 0xFFFF a page not committed, else the page's blocks in use) from the
// first entry, +0x10C entries (0x004024A3 - 0x004024B6), commits the first 0xFFFF page it finds (0x004024D1 on:
// GlobalMemoryStatus, VirtualAlloc MEM_COMMIT, the entry 0, the page's blocks onto the free list, +0x118 one up)
// and returns 0 when there is none - the caller 0x00401AA0 then takes the block from the heap's size classes.
//
// So a pool with every reserved page committed and every block in use walks its whole table at every allocation
// of its size, in vain. In Nadi's Windows session of 2026-11-23 that walk was the top Oblivion.exe function of
// the scene pass: 0.35 ms a frame in a calm stretch, 0.72 ms in the tutorial's fight (the pass lists' records and
// nodes allocated as the lists are built anew), 0.10 ms in the same place under Linux.
//
// The counter +0x118 is the number of entries that are not 0xFFFF (read): the pool's making 0x00402280 sets every
// entry to 0xFFFF and the counter to 0, the commit sets one entry to 0 and adds one (0x00402557, 0x00402591), the
// release 0x00402640 sets one to 0xFFFF and takes one off (0x004026FE, 0x00402704); the use counts of a page stay
// far under 0xFFFF (at most 512 blocks a page). With the counter at the table's length there is no 0xFFFF entry
// and the walk's answer is "none" without walking.
//
// The site: 0x00402493 - 0x004024A2, sixteen bytes after the free list's test (`xor edi, edi`, the test for a pool
// without pages, the table's load), replaced by a jump to the search; ESI is the pool, EDI takes the index, and
// 0x004024C2 (`cmp edi, -1`) goes on to the commit or to the return of 0 as the original does.
//
// 64-bit clean: plain values and an entry reader, no pointers.

#include <cstdint>

namespace obng::engine::memory::pool_scan {

inline constexpr std::uint32_t kAllocate = 0x00402480;  // __thiscall(pool), the block or 0 (read)
inline constexpr std::uint32_t kSite = 0x00402493;
inline constexpr std::uint8_t kSiteBytes[16] = {0x33, 0xFF, 0x39, 0xBE, 0x0C, 0x01, 0x00, 0x00, 0x76, 0x2A, 0x8B, 0x86, 0x08, 0x01, 0x00, 0x00};
inline constexpr std::uint32_t kResume = 0x004024C2;  // cmp edi, -1 / jne the commit / the return of 0
inline constexpr std::uint8_t kResumeBytes[5] = {0x83, 0xFF, 0xFF, 0x75, 0x0A};
inline constexpr std::uint32_t kElementSize = 0x100, kPageTable = 0x108, kReservedPages = 0x10C, kCommittedPages = 0x118;  // the pool's fields (read)
inline constexpr std::uint16_t kFreePage = 0xFFFF;
inline constexpr std::uint32_t kNone = 0xFFFFFFFFu;
inline constexpr std::uint32_t kSlots = 129;  // the game's own table of pools by element size in words, 0x00B33080

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

// 1 the search without the vain walk, 2 the check: both searches, compared, the original's answer used.
enum class Mode { Off, Route, Check };

constexpr Mode ModeOf(int ini) {
	return ini == 1 ? Mode::Route : ini == 2 ? Mode::Check : Mode::Off;
}

// The original's walk: the lowest page whose entry is 0xFFFF, or none. entry(i) is the table's word i.
template <typename Entry>
constexpr std::uint32_t FirstFree(std::uint32_t pages, Entry entry) {
	for (std::uint32_t i = 0; i < pages; ++i) {
		if (entry(i) == kFreePage) {
			return i;
		}
	}
	return kNone;
}

// Whether the table has no 0xFFFF entry by the pool's own count of committed pages.
constexpr bool NoneLeft(std::uint32_t committed, std::uint32_t pages) {
	return committed >= pages;
}

template <typename Entry>
constexpr std::uint32_t FirstFreeFast(std::uint32_t pages, std::uint32_t committed, Entry entry) {
	return NoneLeft(committed, pages) ? kNone : FirstFree(pages, entry);
}

// The check mode's comparison.
constexpr bool Agrees(std::uint32_t fast, std::uint32_t walked) {
	return fast == walked;
}

// The entries the original's walk reads for an answer.
constexpr std::uint32_t Walked(std::uint32_t answer, std::uint32_t pages) {
	return answer == kNone ? pages : answer + 1;
}

constexpr std::uint32_t SlotOf(std::uint32_t elementSize) {
	const std::uint32_t slot = elementSize / 4;
	return slot < kSlots ? slot : kSlots - 1;
}

struct SlotCounts {
	std::uint64_t calls = 0;
	std::uint64_t none = 0;
	std::uint32_t pages = 0;  // the pool's reserved pages at its last search
};

// The four element sizes with the most searches.
struct Top {
	std::uint32_t slot[4] = {};
	std::uint32_t used = 0;
};

// What a run's searches did, for the summary.
struct Counts {
	std::uint64_t calls = 0;
	std::uint64_t none = 0;
	std::uint64_t found = 0;
	std::uint64_t leftOut = 0;         // walks not made
	std::uint64_t walkedOriginal = 0;  // the entries the original reads for these searches
	std::uint64_t walkedSaved = 0;     // of them not read
	std::uint64_t mismatches = 0;
	SlotCounts bySlot[kSlots] = {};

	constexpr void Note(std::uint32_t elementSize, std::uint32_t pages, std::uint32_t answer, bool walkLeftOut) {
		++calls;
		++(answer == kNone ? none : found);
		const std::uint32_t walked = Walked(answer, pages);
		walkedOriginal += walked;
		if (walkLeftOut) {
			++leftOut;
			walkedSaved += walked;
		}
		SlotCounts& slot = bySlot[SlotOf(elementSize)];
		++slot.calls;
		slot.none += answer == kNone ? 1 : 0;
		slot.pages = pages;
	}
	constexpr void NoteMismatch() {
		++mismatches;
	}
	constexpr Top Busiest() const {
		Top top;
		for (std::uint32_t place = 0; place < 4; ++place) {
			std::uint32_t best = kSlots;
			for (std::uint32_t i = 0; i < kSlots; ++i) {
				bool taken = false;
				for (std::uint32_t k = 0; k < place; ++k) {
					taken = taken || top.slot[k] == i;
				}
				if (!taken && bySlot[i].calls != 0 && (best == kSlots || bySlot[i].calls > bySlot[best].calls)) {
					best = i;
				}
			}
			if (best == kSlots) {
				break;
			}
			top.slot[place] = best;
			top.used = place + 1;
		}
		return top;
	}
};

}  // namespace obng::engine::memory::pool_scan
