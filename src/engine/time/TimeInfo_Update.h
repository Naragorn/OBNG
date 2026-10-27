#pragma once

// The engine-facing shape of 0x0047D170 (proposed TimeInfo::Update): the
// layout it touches, the data it reads and its calling convention. The
// decisions and the store order live in TimeInfo_UpdateLogic.h.
//
// Evidence: re/functions/0x0047D170.md (revision 3),
// re/export/time-info/0x0047D170/ (asm.s, data.json). Names are proposals
// until the time-info slice's refutation round.
//
// The layout is the shared TimeInfo (TimeInfo.h); this header only asserts
// the offsets and widths this body relies on and adds nothing to the struct.
// TimeInfo.h's names differ from the mapping's (freezeDepth / fixedStepMs /
// fixedStepCarryMs against freezeDepth / fixedStepMs / fixedStepCarryMs); the
// rename is a layout request, not done here.

#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_UpdateLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::update::engine {

// +0x00: CMP byte ptr [ECX],0x0 0x0047D18C (read).
static_assert(offsetof(TimeInfo, freezeDepth) == 0x00, "the freeze counter at +0x00");
static_assert(sizeof(TimeInfo::freezeDepth) == 1, "the freeze counter is one byte (CMP byte ptr 0x0047D18C)");
// +0x04: FCOMP float [ECX+0x4] 0x0047D195, FADD 0x0047D1A2, FLD 0x0047D1EB (read).
static_assert(offsetof(TimeInfo, fixedStepMs) == 0x04, "the fixed step at +0x04");
static_assert(sizeof(TimeInfo::fixedStepMs) == 4, "the fixed step is a float");
// +0x08: FLD float [ECX+0x8] 0x0047D19F, FSTP 0x0047D1E8 (read).
static_assert(offsetof(TimeInfo, fixedStepCarryMs) == 0x08, "the carry at +0x08");
static_assert(sizeof(TimeInfo::fixedStepCarryMs) == 4, "the carry is a float");
// +0x0C: FSTP float [ECX+0xC] 0x0047D241 and 0x0047D24D, FLD 0x0047D244 (read).
static_assert(offsetof(TimeInfo, frameSeconds) == 0x0C, "frameSeconds at +0x0C");
static_assert(sizeof(TimeInfo::frameSeconds) == 4, "frameSeconds is a float");
// +0x10: MOV EDX,[ECX+0x10] 0x0047D1E1, SUB ESI,[ECX+0x10] 0x0047D1F3, MOV [ECX+0x10],EDX 0x0047D23E (read).
static_assert(offsetof(TimeInfo, clockMs) == 0x10, "clockMs at +0x10");
static_assert(sizeof(TimeInfo::clockMs) == 4, "clockMs is a dword");
// +0x14: SUB EDX,[ECX+0x14] 0x0047D176 (read).
static_assert(offsetof(TimeInfo, startTick) == 0x14, "startTick at +0x14");
static_assert(sizeof(TimeInfo::startTick) == 4, "startTick is a dword");
static_assert(sizeof(TimeInfo) == 0x18, "TimeInfo is 0x18 bytes (PUSH 0x18 0x005AD2E7)");
static_assert(sizeof(void*) == 4, "a 32-bit build");

// Constants the body reads that TimeInfo.h does not carry. Each address is
// read here and in a second code path of the same binary, 0x0055D4C0
// (FADD float [0x00A2FC78] 0x0055D62F, FMUL double [0x00A30E40] 0x0055D635,
// re/export/time-info/0x0055D4C0/asm.s); the values are in
// TimeInfo_UpdateLogic.h, from data.json.
inline constexpr std::uint32_t kTwoPow32fAddress = 0x00A2FC78;              // float 4294967296.0f
inline constexpr std::uint32_t kMillisecondsToSecondsAddress = 0x00A30E40;  // double (double)0.001f
// The others are TimeInfo.h's: kFrameCeilingAddress 0x00A3D14C,
// kTimeMultiplierAddress 0x00B06704, kClockMsAddress 0x00B39DB0,
// kPreviousClockMsAddress 0x00B39DB4, and the function's kTimeInfoUpdate.

// 0x0047D170 itself: `this` in ECX (first use SUB EDX,[ECX+0x14] 0x0047D176;
// callers load ECX at 0x0040D8A6, 0x005AD325, 0x005AD9C0), one stack
// argument, the GetTickCount value pushed by every caller (MOV EDX,[ESP+4]
// 0x0047D170), callee cleans (RET 0x4 0x0047D253) - thiscall (read). EAX at
// exit is a leftover (status word or n, measured); void. EAX and EDX are
// clobbered, ESI is preserved (PUSH/POP 0x0047D1F0/0x0047D237), the x87
// control word is restored (FLDCW 0x0047D1CD) and the x87 stack is balanced
// (read; derived from the instruction sequence).
using UpdateFn = void(__thiscall*)(TimeInfo* self, std::uint32_t tick);

} // namespace obng::engine::time::update::engine
