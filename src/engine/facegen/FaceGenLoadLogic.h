#pragma once
// [Route] FaceGenLoadOutside and [Probe] FaceGenLoads: the reads of a FaceGen model's .egm and .egt files taken out of
// the model cache's lock - the decisions over plain values (the plugin's FaceGenLoad.cpp).
//
// Read 2026-11-20: a model of the cache `[[0x00B39B80] + 0xDAC]` is made with its mesh and the paths of its two data
// files only (0x00559330, 0x00556A00); the files themselves are read by the first find of the model. A find -
// 0x005515B0 (both kinds), 0x005516B0 (the shape modes), 0x005517F0 (the texture modes) - enters the cache's lock
// 0x00B39C00, looks the entry up, stamps it, makes room (the purge 0x005506B0) and then, still under the lock, calls
// 0x00559190 (-> 0x00558770, the .egm) and 0x00558520 (-> 0x00557C80, the .egt): a model without its data reads the
// file there, for a head's .egt 9.4 MB. Every other find on every other thread waits for it.
//
// The data's users (the texture synthesis 0x00557E60, the morph 0x00558840) take the model's own flags (+0x14 the
// shapes', +0x18 the textures') around each use and check the data again under the flag; the eviction (0x00559BA0,
// 0x00559C40) frees under the same flag. Both users also read a file with no lock at all when the model is not in the
// cache. So the data pointer is already written and read across threads without the cache's lock.
//
// The route: when a find's load would read a file and the lock is entered exactly once, the lock is given up for the
// read and taken again after it. Loads of one model take turns on one of sixteen locks, so no file is read twice at
// once. Class B: the same loads with the same results, no longer one at a time across models.
//
// Measured 2026-11-20 (runs obng-profile-ride-anvil-late/20261120-175137 and -175504): the reads are on the loader's
// thread, a head's .egm 8 - 22 ms and .egt 5 - 51 ms, and with the reads alone moved the main thread still waits 5 -
// 11 ms: 17 of 107 loads sat under a lock entered twice. Read: the head's texture synthesis 0x005551C0 (QueuedHead's
// run on the loader's thread, 0x00435300 -> 0x00523220 -> 0x0052DED0) wraps each call of the synthesis 0x00557E60 in
// the cache's lock (`mov ecx, 0x00B39C00` before the enter 0x0043F2E0 and the leave 0x0043F300, twice) - the find
// with its file read, the synthesis and the texture's making all under it. The body textures' path 0x0052D2C0 calls
// the same synthesis with no lock. So the route also gives the head's synthesis a lock of its own in place of the
// cache's: the syntheses of 0x005551C0 still take turns among themselves, and no longer hold up the cache.
// 64-bit clean: a model as a 32-bit handle, times in counter ticks.
#include <cstddef>
#include <cstdint>

namespace obng::engine::facegen::load {

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

constexpr bool OnOf(int ini) {
	return ini == 1;
}

// The engine's string keeps its length as 16 bits; 0xFFFF says "count the characters" (0x00404FD0, read).
constexpr std::uint32_t PathLength(std::uint16_t stored, std::uint32_t counted) {
	return stored == 0xFFFF ? counted : stored;
}

// One of a model's two data objects (the shapes' at model +8, the textures' at +0xC): path at [0], data at [2].
struct Part {
	bool present = false;
	bool loaded = false;
	std::uint32_t pathLength = 0;
};

// Whether the engine's load would read the file: the same three tests as 0x00559190 and 0x00558520.
constexpr bool Needs(const Part& part) {
	return part.present && !part.loaded && part.pathLength != 0;
}

enum class Where { UnderCacheLock, Outside };

// depth: the engine's own count of the lock's holders at 0x00B39C7C; recursion: the critical section's count. Both
// must say "entered once", or giving the lock up would not release it and the wait for the model's lock could
// deadlock with a thread that waits for the cache's.
constexpr Where WhereOf(bool routed, bool needs, std::uint32_t depth, std::uint32_t recursion) {
	return routed && needs && depth == 1 && recursion == 1 ? Where::Outside : Where::UnderCacheLock;
}

// The four `mov ecx, 0x00B39C00` of the head synthesis's two brackets (read), and the engine lock's shape the enter
// and leave helpers 0x0043F2E0 / 0x0043F300 expect: a critical section, the holder's thread at +0x78, the depth at +0x7C.
constexpr std::uint32_t kSynthesisLockSites[] = {0x0055547E, 0x00555496, 0x00555504, 0x00555520};
constexpr std::uint32_t kCacheLock = 0x00B39C00;
constexpr std::size_t kLockOwner = 0x78, kLockDepth = 0x7C, kLockSize = 0x80;

// `mov ecx, value`.
constexpr void MovEcx(std::uint32_t value, std::uint8_t (&out)[5]) {
	out[0] = 0xB9;
	out[1] = static_cast<std::uint8_t>(value);
	out[2] = static_cast<std::uint8_t>(value >> 8);
	out[3] = static_cast<std::uint8_t>(value >> 16);
	out[4] = static_cast<std::uint8_t>(value >> 24);
}

constexpr std::uint32_t kStripes = 16;

// The lock a model's loads take in turn. Models are heap objects; their low address bits are the heap's alignment.
constexpr std::uint32_t StripeOf(std::uint32_t model) {
	return ((model >> 5) ^ (model >> 9) ^ (model >> 13)) & (kStripes - 1);
}

enum class Kind : std::uint8_t { Egm = 0, Egt = 1 };

// One load that read a file: its time, and the time it waited for the model's lock and for the cache's again.
struct Load {
	Kind kind = Kind::Egm;
	bool loaderThread = false;  // on the thread the engine takes for the loader's, [[0x00B33A10] + 0x30]
	bool outside = false;
	std::uint64_t ticks = 0;
	std::uint64_t waitTicks = 0;
};

struct Cell {
	std::uint64_t loads = 0;
	std::uint64_t ticks = 0;
	std::uint64_t longest = 0;
};

struct Totals {
	Cell of[2][2];  // [kind][0 the loader's thread, 1 another (the main thread, mostly)]
	std::uint64_t outside = 0;
	std::uint64_t waitTicks = 0;
	std::uint64_t longestWait = 0;
	std::uint64_t nested = 0;
};

inline void Add(Totals& totals, const Load& load) {
	Cell& cell = totals.of[static_cast<std::size_t>(load.kind)][load.loaderThread ? 0 : 1];
	++cell.loads;
	cell.ticks += load.ticks;
	cell.longest = load.ticks > cell.longest ? load.ticks : cell.longest;
	if (load.outside) {
		++totals.outside;
		totals.waitTicks += load.waitTicks;
		totals.longestWait = load.waitTicks > totals.longestWait ? load.waitTicks : totals.longestWait;
	}
}

inline void CountNested(Totals& totals) {
	++totals.nested;
}

constexpr bool IsLong(std::uint64_t ticks, std::uint64_t limit) {
	return ticks >= limit;
}

constexpr std::size_t kNameSize = 56;
constexpr unsigned kLongLoads = 48;

struct LongLoad {
	Load load;
	std::uint32_t thread = 0;
	std::uint64_t endedAt = 0;
	char name[kNameSize] = {};
};

struct LongLoads {
	LongLoad loads[kLongLoads];
	unsigned count = 0;
	unsigned dropped = 0;
};

// The end of a path, as much as the name holds.
inline void Tail(const char* path, char (&out)[kNameSize]) {
	out[0] = '\0';
	if (path == nullptr) {
		return;
	}
	std::size_t length = 0;
	while (path[length] != '\0') {
		++length;
	}
	const std::size_t from = length > kNameSize - 1 ? length - (kNameSize - 1) : 0;
	std::size_t i = 0;
	for (; path[from + i] != '\0'; ++i) {
		out[i] = path[from + i];
	}
	out[i] = '\0';
}

inline void Keep(LongLoads& list, const LongLoad& one) {
	if (list.count >= kLongLoads) {
		++list.dropped;
		return;
	}
	list.loads[list.count++] = one;
}

}  // namespace obng::engine::facegen::load
