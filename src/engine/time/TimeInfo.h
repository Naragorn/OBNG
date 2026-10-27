#pragma once

// The engine's frame-time record, 0x18 bytes, as the five functions of the
// class use it (read in Oblivion.exe 1.2.0.416; time-info slice 2026-10-27,
// re/functions/0x0047D1{50,70,20,F0}.md, 0x009DC2A0.md; re/ledger.md).
//
// Instances: the main loop's at 0x00B33E90 (xOBSE's g_timeInfo), built by
// 0x009DC2A0 with GetTickCount as the start tick; the LoadingMenu's own,
// allocated with FormHeap_Allocate(0x18) (PUSH at 0x005AD2E7, call at
// 0x005AD2E9) and initialised through 0x0047D150.
//
// The names are the slice's, decided by the instructions: +0x00 is a
// nesting counter (ADD CL,1 at 0x0047D127, SUB AL,1 at 0x0047D0F9), not a
// flag; +0x04 is the fixed step and whether iFPSClamp writes it is
// unverified, so it is not called a clamp.

#include <cstddef>
#include <cstdint>

namespace obng::engine::time {

struct TimeInfo {
	std::uint8_t freezeDepth;  // +0x00  nesting depth of Freeze/Unfreeze; nonzero: Update writes 0 seconds
	std::uint8_t pad[3];       // +0x01  never written by the class
	float fixedStepMs;         // +0x04  nonzero: fixed-step mode, the step in ms; the tick is ignored then
	float fixedStepCarryMs;    // +0x08  the fixed step's carried fraction (rounded to float every frame)
	float frameSeconds;        // +0x0C  the frame's length, what everything advances by
	std::uint32_t clockMs;     // +0x10  variable mode: tick - startTick at the last update; fixed mode: a counter advanced by the step
	std::uint32_t startTick;   // +0x14  GetTickCount at construction; across a Freeze/Unfreeze bracket both +0x10 and +0x14 are rewritten as now - field, so the frozen stretch is shifted out of the clock
};
static_assert(sizeof(TimeInfo) == 0x18, "TimeInfo is 0x18 bytes in Oblivion.exe 1.2.0.416");
static_assert(offsetof(TimeInfo, fixedStepMs) == 0x04, "fixedStepMs at +0x04");
static_assert(offsetof(TimeInfo, frameSeconds) == 0x0C, "frameSeconds at +0x0C");
static_assert(offsetof(TimeInfo, clockMs) == 0x10, "clockMs at +0x10");
static_assert(offsetof(TimeInfo, startTick) == 0x14, "startTick at +0x14");

// Globals the class reads and writes (read; values from the data export).
inline constexpr std::uint32_t kTimeInfoAddress = 0x00B33E90;         // the main loop's TimeInfo
inline constexpr std::uint32_t kTimeMultiplierAddress = 0x00B06704;   // fGlobalTimeMultiplier:General, a SettingInfo (value 1.0f, name pointer 0x00A3D150 at +0x04)
inline constexpr std::uint32_t kFrameCeilingAddress = 0x00A3D14C;     // float 166.66667: milliseconds in Update, compared against seconds in PlanHavokSteps 0x00889810
inline constexpr std::uint32_t kMillisecondsToSecondsAddress = 0x00A30E40;  // double 0.0010000000474974513 = (double)0.001f
inline constexpr std::uint32_t kTwoPow32Address = 0x00A2FC78;         // float 4294967296.0, the unsigned-to-float fix-up
inline constexpr std::uint32_t kClockMsAddress = 0x00B39DB0;          // written by every instance's Update: its clockMs of this update
inline constexpr std::uint32_t kPreviousClockMsAddress = 0x00B39DB4;  // the previous value of the above; both read by BSFaceGenNiNode::UpdateDownwardPass 0x0055D4C0

// The class's functions (read: bodies mapped in the time-info slice).
inline constexpr std::uint32_t kTimeInfoInit = 0x0047D150;      // thiscall(tick), ret 4: zeroes +0x00..+0x10, +0x14 = tick; returns the tick in EAX, not this (constructor or init method: open)
inline constexpr std::uint32_t kTimeInfoUpdate = 0x0047D170;    // thiscall(tick), ret 4
inline constexpr std::uint32_t kTimeInfoFreeze = 0x0047D120;    // thiscall: ++freezeDepth; on 0->1 rewrites +0x10/+0x14 as now - field
inline constexpr std::uint32_t kTimeInfoUnfreeze = 0x0047D0F0;  // thiscall: --freezeDepth; on 1->0 rewrites +0x10/+0x14 as now - field
inline constexpr std::uint32_t kTimeInfoBuildGlobal = 0x009DC2A0;  // void(): the stores of Init inlined on 0x00B33E90 with GetTickCount; a CRT initialiser entry is the reading, unverified

} // namespace obng::engine::time
