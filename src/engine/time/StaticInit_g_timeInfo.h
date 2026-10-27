#pragma once

// The engine-facing shape of 0x009DC2A0 (proposed StaticInit_g_timeInfo), for
// RunStaticInit in StaticInit_g_timeInfoLogic.h.
//
// Evidence: re/functions/0x009DC2A0.md, re/export/time-info/0x009DC2A0/asm.s.
// Names are proposals until the time-info slice's refutation round.
//
// Calling convention: no parameters (no [ESP+n] read), no this (ECX is zeroed
// at 0x009DC2AE before any read), plain RET at 0x009DC2CD (read). That is
// void __cdecl (void); a no-argument function is the same under stdcall
// (derived). EAX still holds GetTickCount's result at the return (read); a
// caller of a void() initialiser does not use it (unverified: no caller is in
// the export).
//
// Only addresses with two sources are engine constants here (AGENTS.md):
//   0x00B33E90 - this function's byte store at 0x009DC2B6 and xOBSE's
//                g_timeInfo (TimeInfo.h's kTimeInfoAddress).
// The function's own address 0x009DC2A0 is its identity (meta.json entry).
// The GetTickCount import slot 0x00A280D0 has one source so far (CALL at
// 0x009DC2A0 with Ghidra's import naming, read) and stays a comment.

#include "engine/time/StaticInit_g_timeInfoLogic.h"
#include "engine/time/TimeInfo.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::static_init_g_time_info::engine {

// The stores this body makes, against the shared layout (read, the addresses
// minus 0x00B33E90; the constructor 0x0047D150 uses the same offsets on ECX).
static_assert(offsetof(TimeInfo, freezeDepth) == 0x00, "freezeDepth at +0x00 (0x009DC2B6)");
static_assert(sizeof(TimeInfo::freezeDepth) == 1, "freezeDepth is one byte (MOV byte ptr, 0x009DC2B6)");
static_assert(offsetof(TimeInfo, fixedStepMs) == 0x04, "fixedStepMs at +0x04 (0x009DC2A8)");
static_assert(offsetof(TimeInfo, fixedStepCarryMs) == 0x08, "fixedStepCarryMs at +0x08 (0x009DC2B0)");
static_assert(offsetof(TimeInfo, frameSeconds) == 0x0C, "frameSeconds at +0x0C (0x009DC2BC)");
static_assert(offsetof(TimeInfo, clockMs) == 0x10, "clockMs at +0x10 (0x009DC2C2)");
static_assert(offsetof(TimeInfo, startTick) == 0x14, "startTick at +0x14 (0x009DC2C8)");
static_assert(sizeof(TimeInfo::fixedStepMs) == 4 && sizeof(TimeInfo::fixedStepCarryMs) == 4 &&
		sizeof(TimeInfo::frameSeconds) == 4,
	"m32fp stores at +0x04/+0x08/+0x0C");
static_assert(sizeof(TimeInfo::clockMs) == 4 && sizeof(TimeInfo::startTick) == 4, "dword stores at +0x10/+0x14");
static_assert(sizeof(TimeInfo) == 0x18, "TimeInfo is 0x18 bytes (FormHeap_Allocate(0x18) at 0x005AD2E7)");

inline constexpr std::uint32_t kStaticInitAddress = 0x009DC2A0;
inline constexpr std::uint32_t kTargetAddress = kTimeInfoAddress;  // 0x00B33E90

// The original's signature: void __cdecl (void), plain RET (0x009DC2CD).
using StaticInitFunction = void(__cdecl*)();

} // namespace obng::engine::time::static_init_g_time_info::engine
