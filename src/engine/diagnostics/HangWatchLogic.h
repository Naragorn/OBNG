#pragma once
// [Probe] HangWatch: when the main thread's update steps stop coming, every thread's place and the holders of the
// locks OBNG knows are written to the log once - the decision over plain values (the plugin's HangWatch.cpp).
//
// Why: a route that moves work between threads can deadlock, and a frozen game says nothing about where. Measured
// 2026-11-20 (runs obng-profile-ride-anvil-late/20261120-182929 and -184133): two of three rides froze with the
// FaceGen routes on, OBVR's watchdog named the render thread only (waiting on a lock inside Blockhead.dll).
// 64-bit clean: counts and milliseconds.
#include <cstdint>

namespace obng::engine::diagnostics::hang_watch {

constexpr std::uint32_t kStallMs = 10000;  // no update step for this long is a stall (a loading screen can be one too)
constexpr unsigned kMaxDumps = 3;          // a run's log holds at most this many dumps

struct State {
	std::uint64_t steps = 0;       // the step count seen last
	std::uint64_t sinceMs = 0;     // when it was first seen
	bool started = false;
	bool dumped = false;           // this stall has its dump
	unsigned dumps = 0;
};

// Called about once a second with the main thread's step count: true when the threads should be dumped now.
constexpr bool Decide(State& state, std::uint64_t steps, std::uint64_t nowMs) {
	if (!state.started || steps != state.steps) {
		state.started = true;
		state.steps = steps;
		state.sinceMs = nowMs;
		state.dumped = false;
		return false;
	}
	if (state.dumped || state.dumps >= kMaxDumps || nowMs - state.sinceMs < kStallMs) {
		return false;
	}
	state.dumped = true;
	++state.dumps;
	return true;
}

// Whether a stack word is worth a place in the dump: inside the game's code, or inside another module's image.
constexpr std::uint32_t kExeCodeBegin = 0x00401000, kExeCodeEnd = 0x00A28000;

constexpr bool InExeCode(std::uint32_t word) {
	return word >= kExeCodeBegin && word < kExeCodeEnd;
}

}  // namespace obng::engine::diagnostics::hang_watch
