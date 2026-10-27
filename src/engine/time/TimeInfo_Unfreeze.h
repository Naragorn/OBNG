#pragma once

// The engine-facing shape of 0x0047D0F0 (proposed TimeInfo::Unfreeze): the
// TimeInfo fields it touches, its calling convention and its tick source. The
// decisions live in TimeInfo_UnfreezeLogic.h.
//
// Evidence: re/functions/0x0047D0F0.md, re/export/time-info/0x0047D0F0/asm.s.
// Names are proposals until the time-info slice's refutation round.
//
// Only addresses with two sources are constants here (AGENTS.md):
//   0x0047D0F0 - this body, and the call sites CALL 0x0047D0F0 at 0x004E18C6
//                (in 0x004E1580) and 0x00494E5E (in 0x004949E0), both with
//                ECX = 0x00B33E90 (read, re/functions/0x0047D0F0.md claim 11);
//                TimeInfo.h's kTimeInfoUnfreeze.
//   0x00A280D0 - the GetTickCount IAT slot: CALL [0x00A280D0] at 0x0047D0FF
//                here and at 0x0047D130 in the first half 0x0047D120 (read in
//                both exports, a second code path in the same binary). The
//                import's identity rests on Ghidra's import resolution
//                (meta.json); the slot's on-disk bytes are an unbound import
//                entry 0x00700BC6 (data.json), not a code address.

#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_UnfreezeLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::unfreeze::engine {

inline constexpr std::uint32_t kUnfreezeAddress = kTimeInfoUnfreeze;  // 0x0047D0F0
inline constexpr std::uint32_t kGetTickCountIatSlot = 0x00A280D0;

// The offsets the body uses (read: MOV AL,[ESI] 0x0047D0F3, [ESI+0x10]
// 0x0047D107/0x0047D10D, [ESI+0x14] 0x0047D10A/0x0047D110). TimeInfo.h asserts
// +0x0C and +0x14 and the size; the two it does not assert are checked here.
inline constexpr std::size_t kFreezeDepthOffset = 0x00;
inline constexpr std::size_t kLastMsOffset = 0x10;
inline constexpr std::size_t kStartTickOffset = 0x14;
static_assert(sizeof(void*) == 4, "the layout mirrors a 32-bit binary");
static_assert(offsetof(TimeInfo, freezeDepth) == kFreezeDepthOffset, "the counter byte at +0x00");
static_assert(sizeof(TimeInfo::freezeDepth) == 1, "the counter is one byte (MOV AL,byte ptr [ESI], 0x0047D0F3)");
static_assert(offsetof(TimeInfo, clockMs) == kLastMsOffset, "clockMs at +0x10");
static_assert(sizeof(TimeInfo::clockMs) == 4, "clockMs is a dword (SUB ECX,dword ptr [ESI+0x10])");
static_assert(offsetof(TimeInfo, startTick) == kStartTickOffset, "startTick at +0x14");
static_assert(sizeof(TimeInfo::startTick) == 4, "startTick is a dword (SUB EAX,dword ptr [ESI+0x14])");

// 0x0047D0F0 itself: thiscall, ECX = this (MOV ESI,ECX at 0x0047D0F1), no stack
// argument, plain RET at 0x0047D114 (read). The EAX it leaves is not a defined
// result (see the logic header), so the declared return is void.
using UnfreezeFn = void(__thiscall*)(TimeInfo* self);

// GetTickCount as called through the IAT slot: stdcall, no arguments,
// DWORD in EAX (Win32 import; the call site pushes nothing, read 0x0047D0FF).
using GetTickCountFn = std::uint32_t(__stdcall*)();

// The body on the engine's layout. readTick is the seam for GetTickCount; in
// the game it would be the function the IAT slot holds once bound.
// Writes only +0x00 (F2, F3) and +0x10/+0x14 (F3); +0x04, +0x08 and +0x0C
// are not touched (read: the only stores are 0x0047D0FB, 0x0047D10D,
// 0x0047D110).
template <class TickSource>
void Unfreeze(TimeInfo& info, TickSource&& readTick) {
	const ClockFields before{info.freezeDepth, info.clockMs, info.startTick};
	const UnfreezeFlow flow = ClassifyUnfreeze(before.depth);
	if (flow == UnfreezeFlow::CounterZero) {
		return;  // F1: no store at all (JZ at 0x0047D0F7 skips 0x0047D0FB)
	}
	const ClockFields after = unfreeze::Unfreeze(before, readTick);
	info.freezeDepth = after.depth;
	if (flow == UnfreezeFlow::Outermost) {
		info.clockMs = after.clockMs;
		info.startTick = after.startTick;
	}
}

} // namespace obng::engine::time::unfreeze::engine
