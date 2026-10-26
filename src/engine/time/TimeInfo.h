#pragma once

// The engine's frame-time record, 0x18 bytes at 0x00B33E90 (xOBSE's
// g_timeInfo), as the update at 0x0047D170 uses it.
//
// Layout read in Oblivion.exe 1.2.0.416 (knowledge/frame-clock.md; OBVR's
// src/game/FrameClockLogic.h carries the same reading and runs against it in
// the game). The offsets are read; the field names are proposals that the
// frame-clock slice (re/ledger.md, 2026-10-26) left standing.
//
// Not a singleton: the LoadingMenu allocates its own instance (0x18 bytes,
// FormHeap_Allocate at 0x005AD2E7) and constructs it through 0x0047D150; the
// global below is the main loop's. The update 0x0047D170 writes this frame's
// tick to 0x00B39DB0 and the previous one to 0x00B39DB4 for every instance.
// The pair 0x0047D120 / 0x0047D0F0 brackets work that must not count as
// frame time (GameMessageBoxA, TESObjectREFR::LoadBaseFormModels); what the
// pair writes is not read yet.

#include <cstddef>
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

// Globals the update reads and writes (read, knowledge/frame-clock.md).
inline constexpr std::uint32_t kTimeInfoAddress = 0x00B33E90;        // the main loop's TimeInfo
inline constexpr std::uint32_t kTimeMultiplierAddress = 0x00B06704;  // float, the value of an INI SettingInfo (name pointer at +0x04)
inline constexpr std::uint32_t kFrameCeilingMsAddress = 0x00A3D14C;  // float 166.67 (1000/6); value unverified in the export
inline constexpr std::uint32_t kCurrentTickAddress = 0x00B39DB0;     // this frame's tick, written by every instance's update
inline constexpr std::uint32_t kPreviousTickAddress = 0x00B39DB4;    // the previous value of the above

// The class's functions (read: call sites in the slice; bodies not yet mapped).
inline constexpr std::uint32_t kTimeInfoConstruct = 0x0047D150;  // thiscall(tick), ret 4 - derived from 0x005AD0D0
inline constexpr std::uint32_t kTimeInfoUpdate = 0x0047D170;     // thiscall(tick), ret 4
inline constexpr std::uint32_t kTimeInfoFreeze = 0x0047D120;     // thiscall, the bracket's first half - name is a proposal
inline constexpr std::uint32_t kTimeInfoUnfreeze = 0x0047D0F0;   // thiscall, the bracket's second half - name is a proposal

} // namespace obng::engine::time
