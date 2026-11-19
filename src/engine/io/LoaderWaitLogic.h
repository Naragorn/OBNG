#pragma once
// [Probe] LoaderWaits: who makes the main thread wait for the background loader - the decisions over plain values
// (the plugin's LoaderWaitProbe.cpp).
//
// Read 2026-11-19: 0x00432860 is "pause the loader": its first instruction calls 0x004322B0 (pause every loader
// thread, each waited for with WaitForSingleObject INFINITE) and, when that was the first pause, it jumps to 0x00431F50
// (wait for the IO thread) with nothing left on the stack. So at 0x004322B0's entry the return address is 0x00432865
// and the word above it is 0x00432860's own caller; at 0x00431F50's entry the return address is that caller already.
// 64-bit clean: addresses are 32-bit handles.
#include <cstdint>

namespace obng::engine::io::loader_wait {

constexpr std::uint32_t kPauseAllReturn = 0x00432865;  // after 0x00432860's call of 0x004322B0 (read)

// The function that asked for the pause: 0x00432860's caller when 0x004322B0 came through it, else the direct caller.
inline std::uint32_t CallerOf(std::uint32_t returnAddress, std::uint32_t wordAbove) {
	return returnAddress == kPauseAllReturn ? wordAbove : returnAddress;
}

// A record is kept when the wait is long enough to matter and room is left.
inline bool Keep(double ms, double minimumMs, std::uint32_t kept, std::uint32_t capacity) {
	return ms >= minimumMs && kept < capacity;
}

}  // namespace obng::engine::io::loader_wait
