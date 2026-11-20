#pragma once
// [Route] FaceGenCacheEgmMB / FaceGenCacheEgtMB: the limits of FaceGen's model cache - the decision over plain values
// (the plugin's FaceGenCacheBudget.cpp).
//
// Read 2026-11-20: FaceGen keeps each model's shape modes (the .egm file) and texture modes (the .egt file) in a
// cache, `[[0x00B39B80] + 0xDAC]`, made on first use (0x00553620 and three more) with two limits copied from the
// settings uiFaceGenMaxEGMDataSize (the value at 0x00B120EC -> cache +0x18) and uiFaceGenMaxEGTDataSize (0x00B120F4
// -> cache +0x1C), 64 MB each as shipped. Every find 0x005515B0 and insert 0x00551450 runs the purge 0x005506B0
// under the cache's lock 0x00B39C00: while a kind's data (summed by a walk of the map, 0x0054F890) is over its limit,
// 0x0054FC30 drops the oldest entry's data; the entry stays, and the next find of it reads the file again
// (0x00559190 -> 0x00558770, 0x00558520 -> 0x00557C80).
//
// Measured 2026-11-20 (run obng-profile-ride-anvil-late/20261120-161036, Nadi's mod list: 415 loose .egm files of
// 511 MB, 26 loose .egt files of 161 MB, a head's .egt 9.4 MB): the main thread's find waits on the lock 4.7 - 13.0 ms
// five times a ride while another thread is in the purge (the holder's enter site 0x005506C3) - in the frame a queued
// character's body texture 0x0052D2C0 is made.
//
// The route raises the two limits to OBNG's own (never below the game's setting), so the purge has nothing to drop
// while the data in use fits. Class B: the same data, kept longer; the price is memory.
#include <cstdint>

namespace obng::engine::facegen::cache_budget {

constexpr std::uint32_t kMb = 1024u * 1024u;
constexpr int kMaxMb = 1024;  // a quarter of the 32-bit address space; more is never right

// What OBNG runs with when OBNG.ini has no key: 0 until the data in use and the address space are measured.
constexpr int kShippedEgmMb = 0;
constexpr int kShippedEgtMb = 0;

struct Budget {
	bool routed = false;  // false: the game's own setting
	std::uint32_t bytes = 0;
};

// The INI's value in megabytes: 0 or less leaves the game's setting; anything else is the limit, at most kMaxMb.
inline Budget BudgetOf(int megabytes) {
	Budget budget;
	if (megabytes <= 0) {
		return budget;
	}
	budget.routed = true;
	budget.bytes = static_cast<std::uint32_t>(megabytes > kMaxMb ? kMaxMb : megabytes) * kMb;
	return budget;
}

// The limit to give the cache: OBNG's when it is routed and larger than the game's, else the game's.
inline std::uint32_t LimitOf(Budget budget, std::uint32_t gameBytes) {
	return budget.routed && budget.bytes > gameBytes ? budget.bytes : gameBytes;
}

inline std::uint32_t MbOf(std::uint32_t bytes) {
	return bytes / kMb;
}

}  // namespace obng::engine::facegen::cache_budget
