#pragma once

// The decisions of 0x0047D120 (proposed TimeInfo::Freeze), as pure functions
// over plain values, and RunFreeze, which replays the body's access order
// over an injected tick port so an engine binding and the tests' fakes share
// one sequence. Evidence: re/functions/0x0047D120.md and the export
// re/export/time-info/0x0047D120/asm.s. Names are proposals until the
// time-info slice's refutation round; the address stays next to each.
//
// The body (read, 0x0047D120..0x0047D145, 16 instructions):
//   0x0047D123  MOV AL,[ESI]            old counter (byte at +0x00)
//   0x0047D127  ADD CL,0x1              old + 1, 8 bits, no carry check
//   0x0047D12A  TEST AL,AL              B1 on the OLD value
//   0x0047D12C  MOV [ESI],CL            store, unconditional (flags unchanged)
//   0x0047D12E  JNZ 0x0047D144          nested: return
//   0x0047D130  CALL [0x00A280D0]       GetTickCount
//   0x0047D138  SUB EDX,[ESI+0x10]      now - clockMs
//   0x0047D13B  SUB EAX,[ESI+0x14]      now - startTick
//   0x0047D13E  MOV [ESI+0x10],EDX
//   0x0047D141  MOV [ESI+0x14],EAX
// pseudo.c agrees with the instructions; no disagreement found (read).
//
// No floating point in the body (read: no x87 or SSE instruction), so the
// result does not depend on x87 precision control.
//
// The counter's read-modify-write is a plain MOV/ADD/MOV with no LOCK prefix
// (read), so it is not atomic in the original either; RunFreeze makes no
// thread-safety promise beyond that.
//
// The field names follow TimeInfo.h: +0x00 is `freezeDepth` there, although it is
// a nesting counter (claim 1, read at 0x0047D127); the rename to freezeDepth
// is a layout request, not done here (the shared header is not edited).

#include "engine/time/TimeInfo.h"

#include <cstdint>

namespace obng::engine::time::freeze {

// 0x0047D127: the counter after this call - old + 1 in 8 bits; 0xFF wraps to
// 0x00 (read: ADD CL,0x1, no check before the store at 0x0047D12C).
constexpr std::uint8_t NextFreezeDepth(std::uint8_t oldDepth) {
	return static_cast<std::uint8_t>(oldDepth + 1u);
}

// B1 (0x0047D12A TEST AL,AL / 0x0047D12E JNZ): the outer entry is the one
// whose counter was zero BEFORE the increment (read). Any nonzero old value,
// 0x80..0xFF included, is a nested entry (TEST sets ZF on zero only).
constexpr bool IsOuterEntry(std::uint8_t oldDepth) {
	return oldDepth == 0;
}

// 0x0047D138 / 0x0047D13B: a field's freezeDepth form, now - field, as a plain
// 32-bit SUB; wraps modulo 2^32, no check (read).
constexpr std::uint32_t FrozenForm(std::uint32_t now, std::uint32_t field) {
	return now - field;
}

// The two fields the outer entry rewrites, as plain values.
struct ClockFields {
	std::uint32_t clockMs;     // TimeInfo +0x10
	std::uint32_t startTick;  // TimeInfo +0x14
};

// The outer entry's rewrite: both fields from the same tick (one call at
// 0x0047D130, its EAX feeds both SUBs, read).
constexpr ClockFields Shift(ClockFields fields, std::uint32_t now) {
	return ClockFields{FrozenForm(now, fields.clockMs), FrozenForm(now, fields.startTick)};
}

// The body's sequence. Clock::TickCount() -> std::uint32_t stands for
// GetTickCount through the IAT slot 0x00A280D0 (read: 0x0047D130; the
// callee's identity rests on Ghidra's import resolution, meta.json callees).
// Writes only +0x00 and, on the outer entry, +0x10 then +0x14 (claim 5, read).
template <class Clock>
void RunFreeze(TimeInfo& self, Clock& clock) {
	const std::uint8_t oldDepth = self.freezeDepth;  // 0x0047D123
	self.freezeDepth = NextFreezeDepth(oldDepth);    // 0x0047D127, 0x0047D12C
	if (!IsOuterEntry(oldDepth)) {              // B1 taken: 0x0047D12E -> 0x0047D144
		return;
	}
	const std::uint32_t now = clock.TickCount();  // 0x0047D130
	const ClockFields shifted = Shift(ClockFields{self.clockMs, self.startTick}, now);
	self.clockMs = shifted.clockMs;        // 0x0047D13E
	self.startTick = shifted.startTick;  // 0x0047D141
}

} // namespace obng::engine::time::freeze
