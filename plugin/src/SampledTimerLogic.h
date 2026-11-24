#pragma once
// A timer for a section that runs thousands of times a frame: it reads the clock in one frame of four only, and a
// timed frame's ticks count for the four.
//
// Why: the routes time their own per-draw work for their summary lines and, the occlusion culler, for its
// stand-down - two `rdtsc` a record or draw decision, in play too. `rdtsc` is microcoded on the player's processor
// (Zen 4): in the tutorial's fight the two reads in draw instancing's record decision and in the culler's draw
// decision were the two slowest instructions of OBNG.dll's part of the scene pass, 73 - 79 cycles from tag to
// completion each and 2.9 % of all modules' operation latency in the pass (perf, IBS operation samples,
// 2026-11-24, knowledge/render-thread-hardware-profile.md). About 10 000 reads a frame there.
//
// What it changes: the sums are estimates from a quarter of the frames. A summary's "ms a frame" is as before over
// a window of hundreds of frames; the culler's stand-down judges a window of 32 passes on eight timed ones.
//
// 64-bit clean: plain values.

#include <cstdint>

namespace obng::plugin::sampled_timer {

inline constexpr std::uint32_t kEvery = 4;

// Whether the sections of this frame (or pass) read the clock.
constexpr bool Times(std::uint64_t frame) {
	return frame % kEvery == 0;
}

// The ticks a timed frame's measurement stands for.
constexpr std::uint64_t Whole(std::uint64_t measuredTicks) {
	return measuredTicks * kEvery;
}

}  // namespace obng::plugin::sampled_timer
