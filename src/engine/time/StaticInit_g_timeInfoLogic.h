#pragma once

// The decision logic of 0x009DC2A0 (proposed StaticInit_g_timeInfo) as pure
// functions over plain values. Evidence: re/functions/0x009DC2A0.md and the
// export re/export/time-info/0x009DC2A0/asm.s. No engine binding exists yet:
// the game is not routed through RunStaticInit, only the tests drive it.
//
// The body is straight-line (read, 0x009DC2A0..0x009DC2CD): one call to
// KERNEL32 GetTickCount through the import slot 0x00A280D0 (0x009DC2A0), then
// the TimeInfo constructor's stores (0x0047D150, inlined - derived, C6) on the
// global at 0x00B33E90:
//   +0x00 byte  0      MOV byte ptr [0x00B33E90],CL    0x009DC2B6 (CL = 0, 0x009DC2AE)
//   +0x04 float +0.0   FLDZ 0x009DC2A6, FST            0x009DC2A8
//   +0x08 float +0.0   FST                             0x009DC2B0
//   +0x0C float +0.0   FSTP                            0x009DC2BC
//   +0x10 dword 0      MOV dword ptr [0x00B33EA0],ECX  0x009DC2C2
//   +0x14 dword tick   MOV [0x00B33EA4],EAX            0x009DC2C8
// +0x01..+0x03 are not written (read); the rebuild keeps them as they were.
//
// x87 precision does not reach the result: FLDZ is an exact +0.0 and its m32
// store is 0x00000000 under every precision-control and rounding mode
// (derived, x87 semantics; the note's C9). The rebuild writes the float
// literal 0.0f, which is the same bit pattern.
//
// Names are proposals until the time-info slice's refutation round.

#include "engine/time/TimeInfo.h"

#include <cstdint>

namespace obng::engine::time::static_init_g_time_info {

// The TimeInfo the body leaves, given what the 0x18 bytes held before and
// GetTickCount's result. Only the six stores above change it.
inline TimeInfo ConstructedTimeInfo(TimeInfo before, std::uint32_t tick) {
	TimeInfo after = before;  // carries pad[0..2] (+0x01..+0x03) through unwritten
	after.freezeDepth = 0;         // 0x009DC2B6
	after.fixedStepMs = 0.0f;     // 0x009DC2A8
	after.fixedStepCarryMs = 0.0f;  // 0x009DC2B0
	after.frameSeconds = 0.0f;  // 0x009DC2BC
	after.clockMs = 0;         // 0x009DC2C2
	after.startTick = tick;   // 0x009DC2C8
	return after;
}

// The body's sequence over a port:
//   clock.GetTickCount() -> std::uint32_t - CALL [0x00A280D0] at 0x009DC2A0,
//                                           before every store (read)
// then the stores into target. No branch, no failure path: EAX is read only by
// the store at 0x009DC2C8 (read).
template <class Clock>
void RunStaticInit(Clock& clock, TimeInfo& target) {
	const std::uint32_t tick = clock.GetTickCount();
	target = ConstructedTimeInfo(target, tick);
}

} // namespace obng::engine::time::static_init_g_time_info
