#pragma once

// The engine-facing shape of 0x0047D150 (proposed TimeInfo::TimeInfo): the
// layout it writes and its calling convention, plus the rebuild with that
// convention. The values live in TimeInfo_TimeInfoLogic.h.
//
// Evidence: re/functions/0x0047D150.md, re/export/time-info/0x0047D150/asm.s.
// The layout is the shared TimeInfo (TimeInfo.h); this body writes every one
// of its offsets except the padding, and the static initialiser 0x009DC2A0
// writes the same offsets inline on g_timeInfo 0x00B33E90 (a second code path
// in the same binary, claim 10b of the function file, derived).
//
// The function's address has one source so far (the call at 0x005AD30E in
// 0x005AD0D0, read; TimeInfo.h carries it as kTimeInfoInit), so no new
// engine constant is introduced here.
//
// Calling convention (read):
//   - __thiscall: `this` in ECX, the base of every store, never loaded in the
//     body (0x0047D156..0x0047D169);
//   - one 4-byte stack argument, the start tick ([ESP+4] at 0x0047D152), a
//     GetTickCount value at the only call site (derived, 0x005AD2F9/0x005AD309/
//     0x005AD30B in the caller's export);
//   - callee pops it: RET 0x4 at 0x0047D16C;
//   - EAX on return is the tick, not `this` (last EAX write 0x0047D152). The
//     only caller ignores EAX (0x005AD313..0x005AD322, read), so the function
//     file proposes a void signature; the rebuild returns the tick anyway, so
//     that EAX matches the original for any caller (a void declaration of the
//     same function stays call-compatible, derived: EAX is a scratch register
//     in __thiscall).
//   - x87 stack balanced: one FLDZ, one FSTP (derived).
//
// Not modelled: a null `this`. The original faults on the store at 0x0047D156;
// its only caller skips the call when the allocation is null (JZ at
// 0x005AD307, read), and the rebuild has no guard either. No binding exists
// yet (nothing routes the game through kRebuild).

#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_TimeInfoLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::timeinfo_ctor::engine {

// Every offset this body stores to (read, 0x0047D156..0x0047D169). TimeInfo.h
// asserts +0x0C, +0x14 and the size; the rest are asserted here.
static_assert(sizeof(TimeInfo) == 0x18, "0x18 bytes: FormHeap_Allocate(0x18) at 0x005AD2E9, the only caller");
static_assert(offsetof(TimeInfo, freezeDepth) == 0x00, "freezeDepth at +0x00 (byte store 0x0047D159)");
static_assert(offsetof(TimeInfo, pad) == 0x01 && sizeof(TimeInfo::pad) == 3, "+0x01..+0x03 not written");
static_assert(offsetof(TimeInfo, fixedStepMs) == 0x04, "fixedStepMs at +0x04 (FST 0x0047D156)");
static_assert(offsetof(TimeInfo, fixedStepCarryMs) == 0x08, "fixedStepCarryMs at +0x08 (FST 0x0047D15C)");
static_assert(offsetof(TimeInfo, frameSeconds) == 0x0C, "frameSeconds at +0x0C (FSTP 0x0047D166)");
static_assert(offsetof(TimeInfo, clockMs) == 0x10, "clockMs at +0x10 (MOV dword 0x0047D15F)");
static_assert(offsetof(TimeInfo, startTick) == 0x14, "startTick at +0x14 (MOV dword 0x0047D169)");
static_assert(sizeof(float) == 4 && sizeof(void*) == 4, "a 32-bit build");

// 0x0047D150: ECX = this, the tick pushed, callee cleans, EAX = the tick.
using ConstructFn = std::uint32_t(__thiscall*)(TimeInfo* self, std::uint32_t startTick);

// The rebuild of 0x0047D150 with the original's convention.
inline std::uint32_t __thiscall Construct(TimeInfo* self, std::uint32_t startTick) {
	Apply(*self, InitialValuesFor(startTick));
	return startTick;
}

inline constexpr ConstructFn kRebuild = &Construct;

} // namespace obng::engine::time::timeinfo_ctor::engine
