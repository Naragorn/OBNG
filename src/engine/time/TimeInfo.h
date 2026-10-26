#pragma once

// The engine's frame-time record, 0x18 bytes at 0x00B33E90 (xOBSE's
// g_timeInfo), as the update at 0x0047D170 uses it.
//
// Layout read in Oblivion.exe 1.2.0.416 (knowledge/frame-clock.md; OBVR's
// src/game/FrameClockLogic.h carries the same reading and runs against it in
// the game). Field names are proposals until the frame-clock slice confirms
// them; the offsets are read.

#include <cstdint>

namespace obng::engine::time {

struct TimeInfo {
	std::uint8_t frozen;      // +0x00  nonzero: the frame's seconds are written as zero (read)
	std::uint8_t pad[3];
	float clampMs;            // +0x04  1000 / iFPSClamp, or zero: the fixed step when set (read)
	float clampCarryMs;       // +0x08  the fixed step's carried fraction (read)
	float frameSeconds;       // +0x0C  the frame's length, what everything advances by (read)
	std::uint32_t lastMs;     // +0x10  the previous frame's milliseconds since the start (read)
	std::uint32_t startTick;  // +0x14  GetTickCount at the start (read)
};
static_assert(sizeof(TimeInfo) == 0x18, "TimeInfo is 0x18 bytes in Oblivion.exe 1.2.0.416");
static_assert(offsetof(TimeInfo, frameSeconds) == 0x0C, "frameSeconds at +0x0C");
static_assert(offsetof(TimeInfo, startTick) == 0x14, "startTick at +0x14");

// Globals the update reads (read, knowledge/frame-clock.md).
inline constexpr std::uint32_t kTimeInfoAddress = 0x00B33E90;        // the TimeInfo
inline constexpr std::uint32_t kTimeMultiplierAddress = 0x00B06704;  // float, the global time multiplier
inline constexpr std::uint32_t kFrameCeilingMsAddress = 0x00A3D14C;  // float 166.67 (1000/6)

} // namespace obng::engine::time
