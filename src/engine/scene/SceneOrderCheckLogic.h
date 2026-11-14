#pragma once
// [Probe] SceneOrderCheck: what the flat skeleton update's check of its order
// against the tree (SceneUpdateLogic.h Matches) costs, and how much of that is
// the memory's. With 2 the same check runs again at once: the second finds the
// tree's nodes in the cache, so first - second is the time spent fetching them
// - which the run right after the check would pay itself if the check were
// left out (derived). A horde at Anvil's gate spent 1.05 ms a frame in Matches
// (render sampler, 2026-11-14); this says whether leaving the check out could
// save it or would only move it. Pure; SceneOrderCheckLogicTest.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::engine::scene::order_check {

enum class Mode : std::uint8_t { kOff = 0, kTime = 1, kTwice = 2 };

constexpr Mode ModeFromIni(int value) {
	return value == 1 ? Mode::kTime : value == 2 ? Mode::kTwice : Mode::kOff;
}

struct Counts {
	std::uint64_t checks = 0;
	std::uint64_t entries = 0;
	std::uint64_t firstTicks = 0;
	std::uint64_t secondTicks = 0;  // Mode::kTwice only
	std::uint64_t differed = 0;     // the second check's answer was not the first's (a tree changed between them: another thread)

	void Add(std::uint32_t entryCount, std::uint64_t first, std::uint64_t second, bool sameAnswer) {
		++checks;
		entries += entryCount;
		firstTicks += first;
		secondTicks += second;
		differed += sameAnswer ? 0u : 1u;
	}
};

// "Probe: scene order check (twice) - 300 checks of 18000 entries: the first 1.200 ms in all (4.00 us a check,
// 66.7 ns an entry), the same again at once 0.300 ms - 75 % of the first is fetching the tree; 0 answers differed"
inline int Format(const Counts& c, Mode mode, double ticksPerMs, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return 0;
	}
	const double ms = ticksPerMs > 0.0 ? static_cast<double>(c.firstTicks) / ticksPerMs : 0.0;
	const double checks = c.checks == 0 ? 1.0 : static_cast<double>(c.checks);
	const double entries = c.entries == 0 ? 1.0 : static_cast<double>(c.entries);
	int at = std::snprintf(out, capacity, "Probe: scene order check (%s) - %llu checks of %llu entries: the first %.3f ms in all (%.2f us a check, %.1f ns an entry)",
	                       mode == Mode::kTwice ? "twice" : "timed", static_cast<unsigned long long>(c.checks), static_cast<unsigned long long>(c.entries), ms,
	                       1000.0 * ms / checks, 1.0e6 * ms / entries);
	if (mode == Mode::kTwice && at >= 0 && static_cast<std::size_t>(at) < capacity) {
		const double second = ticksPerMs > 0.0 ? static_cast<double>(c.secondTicks) / ticksPerMs : 0.0;
		const double share = ms > 0.0 && second < ms ? 100.0 * (ms - second) / ms : 0.0;
		at += std::snprintf(out + at, capacity - static_cast<std::size_t>(at), ", the same again at once %.3f ms - %.0f %% of the first is fetching the tree; %llu answers differed",
		                    second, share, static_cast<unsigned long long>(c.differed));
	}
	return at;
}

}  // namespace obng::engine::scene::order_check
