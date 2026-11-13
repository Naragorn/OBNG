#pragma once

// The voice lines' probe ([Probe] VoiceLines; VoiceProbe.cpp) - the line it
// writes for one played voice file and the window's tally, over plain values.
//
// 64-bit clean: plain values only.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::voice {

constexpr std::size_t kPathAtMost = 200;

// The path as the log takes it: at most kPathAtMost characters, anything
// that is not printable ASCII as '?', "(none)" for no path. Returns the
// characters written (without the terminator); `capacity` 0 writes nothing.
inline std::size_t CleanPath(const char* path, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return 0;
	}
	if (path == nullptr) {
		return static_cast<std::size_t>(std::snprintf(out, capacity, "(none)"));
	}
	std::size_t n = 0;
	for (; path[n] != 0 && n < kPathAtMost && n + 1 < capacity; ++n) {
		const unsigned char c = static_cast<unsigned char>(path[n]);
		out[n] = c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
	}
	out[n] = 0;
	return n;
}

// "Probe: voice line N.NNN ms - <path>".
inline int FormatLine(const char* cleanPath, std::uint64_t ticks, std::uint64_t ticksPerSecond, char* out, std::size_t capacity) {
	const double ms = ticksPerSecond != 0 ? static_cast<double>(ticks) * 1000.0 / static_cast<double>(ticksPerSecond) : 0.0;
	return std::snprintf(out, capacity, "Probe: voice line %.3f ms - %s", ms, cleanPath);
}

// Whether a call's line is written: its time at or over the threshold and
// the window's lines not used up.
constexpr bool Writes(std::uint64_t ticks, std::uint64_t thresholdTicks, std::uint32_t written, std::uint32_t atMost) {
	return ticks >= thresholdTicks && written < atMost;
}

}  // namespace obng::plugin::voice
