#pragma once

// The engine-facing shape of 0x0047D120 (proposed TimeInfo::Freeze): the
// layout it touches and its calling convention. The decisions and the access
// order live in TimeInfo_FreezeLogic.h.
//
// Evidence: re/functions/0x0047D120.md, re/export/time-info/0x0047D120/asm.s.
// Names are proposals until the time-info slice's refutation round.
//
// The layout is the shared TimeInfo (TimeInfo.h); this header only asserts
// the offsets and widths this body relies on and adds nothing to the struct.
//
// Constants: the function's address is TimeInfo.h's kTimeInfoFreeze. The IAT
// slot 0x00A280D0 (GetTickCount) is read here (0x0047D130), in 0x0047D0F0
// (0x0047D0FF) and in 0x005AD980 (0x005AD9B9), but the callee's identity
// rests on Ghidra's import resolution only, and a binding calls GetTickCount
// itself, not through the slot; it stays a comment.

#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_FreezeLogic.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::freeze::engine {

// +0x00: MOV AL,byte ptr [ESI] 0x0047D123, MOV byte ptr [ESI],CL 0x0047D12C (read).
static_assert(offsetof(TimeInfo, freezeDepth) == 0x00, "the freeze counter at +0x00");
static_assert(sizeof(TimeInfo::freezeDepth) == 1, "the freeze counter is one byte (8-bit ADD at 0x0047D127)");
// +0x10: SUB EDX,[ESI+0x10] 0x0047D138, MOV [ESI+0x10],EDX 0x0047D13E (read).
static_assert(offsetof(TimeInfo, clockMs) == 0x10, "clockMs at +0x10");
static_assert(sizeof(TimeInfo::clockMs) == 4, "clockMs is a dword");
// +0x14: SUB EAX,[ESI+0x14] 0x0047D13B, MOV [ESI+0x14],EAX 0x0047D141 (read).
static_assert(offsetof(TimeInfo, startTick) == 0x14, "startTick at +0x14");
static_assert(sizeof(TimeInfo::startTick) == 4, "startTick is a dword");
static_assert(sizeof(TimeInfo) == 0x18, "TimeInfo is 0x18 bytes");
static_assert(sizeof(void*) == 4, "a 32-bit build");

// 0x0047D120 itself: this in ECX (MOV ESI,ECX 0x0047D121; both callers load
// ECX = 0x00B33E90 before the call, 0x00494A13 and 0x004E15CF), no stack
// arguments, RET without immediate (0x0047D145) - thiscall with no arguments
// (read). EAX at exit is the old counter or now - old +0x14 depending on B1;
// no known caller reads it (0x004E15DB overwrites it, 0x004949E0 does not
// read it in 0x00494A1D..0x00494A2B, read), so void. ESI is preserved, EAX,
// ECX and EDX are clobbered, as thiscall allows (read: PUSH/POP ESI).
using FreezeFn = void(__thiscall*)(TimeInfo* self);

// GetTickCount through the IAT slot 0x00A280D0: no arguments, stdcall, the
// tick in EAX (read: CALL dword ptr [0x00A280D0] at 0x0047D130, EAX used at
// 0x0047D136).
using GetTickCountFn = std::uint32_t(__stdcall*)();

} // namespace obng::engine::time::freeze::engine
