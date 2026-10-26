#pragma once

// The values 0x009DC2D0 (proposed StaticInit_RegisterIniSetting_B06704) passes
// on, for RunStaticInit in StaticInit_RegisterIniSetting_B06704Logic.h.
//
// Evidence: re/functions/0x009DC2D0.md, re/export/frame-clock/0x009DC2D0/asm.s.
// Names are proposals until the frame-clock slice's refutation round.
//
// Only addresses with two sources are engine constants here (AGENTS.md):
//   0x00B07BF0 - this function's ECX (0x009DC2F5) and xOBSE's
//                g_IniSettingCollection (obse/GameAPI.cpp:130) / OBVR's
//                kIniSettingCollection (GameAddresses.h:708).
//   0x00B06704 - this function's push (0x009DC2F0) and the fmul at 0x0047D247
//                (TimeInfo.h's kTimeMultiplierAddress).
// The exit stub 0x00A18690 has one source so far (the push at 0x009DC307,
// read). It is carried in kTargets as a plain value, because the body passes
// it on and the rebuild must pass the same, but it is not an engine constant
// until a second source agrees. The callees 0x004040E0 and _atexit 0x00981FB4
// and the EH handler 0x009AF43A have one source as well and stay comments.

#include "engine/time/StaticInit_RegisterIniSetting_B06704Logic.h"
#include "engine/time/TimeInfo.h"

#include <cstdint>

namespace obng::engine::time::static_init_b06704::engine {

inline constexpr std::uint32_t kIniSettingCollectionAddress = 0x00B07BF0;
inline constexpr std::uint32_t kSettingAddress = kTimeMultiplierAddress;  // 0x00B06704

inline constexpr Targets kTargets{
	kIniSettingCollectionAddress,
	kSettingAddress,
	0x00A18690,  // exit stub, single source: PUSH 0x00A18690 at 0x009DC307 (read)
};

} // namespace obng::engine::time::static_init_b06704::engine
