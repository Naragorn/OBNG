#pragma once

// The decisions of 0x0047D0F0 (proposed TimeInfo::Unfreeze), the second half of
// the clock freeze bracket, as pure functions over plain values. The
// engine-facing shape (the TimeInfo layout, the calling convention, the tick
// source) is in TimeInfo_Unfreeze.h.
//
// Evidence: re/functions/0x0047D0F0.md, re/export/time-info/0x0047D0F0/asm.s.
// pseudo.c agrees with the instructions (same condition order, same offsets,
// same now - field transform; read). Names are proposals until the time-info
// slice's refutation round.
//
// The body (read, 0x0047D0F0..0x0047D114):
//   MOV AL,[ESI] / TEST AL,AL / JZ exit         0x0047D0F3..0x0047D0F7  F1
//   SUB AL,1 / MOV [ESI],AL / JNZ exit          0x0047D0F9..0x0047D0FD  F2
//   CALL [GetTickCount]                         0x0047D0FF              F3
//   ECX = now - [ESI+0x10]; EAX = now - [ESI+0x14]; store both
//                                               0x0047D105..0x0047D110
// All arithmetic is integer: an 8-bit counter and two 32-bit subtractions that
// wrap modulo 2^32 with no carry check (read). No float, no x87 instruction, so
// nothing here depends on the x87 precision control.

#include <cstdint>

namespace obng::engine::time::unfreeze {

// The three values the body reads and writes (read: the only memory operands on
// ESI are +0x00, +0x10, +0x14).
struct ClockFields {
	std::uint8_t depth;      // +0x00, the freeze nesting counter (TimeInfo::freezeDepth in TimeInfo.h)
	std::uint32_t clockMs;    // +0x10; while freezeDepth it holds t_freeze - clockMs (derived, 0x0047D120)
	std::uint32_t startTick; // +0x14; while freezeDepth it holds t_freeze - startTick (derived, 0x0047D120)
};

enum class UnfreezeFlow {
	CounterZero,  // F1: JZ at 0x0047D0F7 taken - nothing written, no tick (an unbalanced call is absorbed)
	StillNested,  // F2: the decrement leaves a nonzero byte - only the byte is stored
	Outermost,    // F3: the decrement reaches 0 - the byte is stored, the clock read, both fields rewritten
};

// Which flow a counter value takes (read, 0x0047D0F5..0x0047D0FD).
constexpr UnfreezeFlow ClassifyUnfreeze(std::uint8_t depth) {
	if (depth == 0) {
		return UnfreezeFlow::CounterZero;
	}
	if (depth == 1) {
		return UnfreezeFlow::Outermost;
	}
	return UnfreezeFlow::StillNested;
}

// F3's transform with the tick already read: the counter becomes 0 and each
// field becomes now - field, modulo 2^32 (read, 0x0047D0FB with AL = 0,
// 0x0047D107/0x0047D10A/0x0047D10D/0x0047D110). Unsigned arithmetic gives the
// wrap of F3w without a clamp, as the SUB does.
constexpr ClockFields ApplyOutermostUnfreeze(ClockFields fields, std::uint32_t now) {
	return ClockFields{
		0,
		static_cast<std::uint32_t>(now - fields.clockMs),
		static_cast<std::uint32_t>(now - fields.startTick),
	};
}

// The whole body over plain values. readTick stands for GetTickCount (CALL
// [0x00A280D0] at 0x0047D0FF) and is called once on F3 and never on F1/F2.
// The original's EAX on exit (AL = counter on F1/F2, the new +0x14 on F3, read)
// is not a defined result; whether a caller reads it is unverified, so nothing
// is returned for it.
template <class TickSource>
ClockFields Unfreeze(ClockFields fields, TickSource&& readTick) {
	switch (ClassifyUnfreeze(fields.depth)) {
	case UnfreezeFlow::CounterZero:
		return fields;
	case UnfreezeFlow::StillNested:
		fields.depth = static_cast<std::uint8_t>(fields.depth - 1);
		return fields;
	case UnfreezeFlow::Outermost:
		break;
	}
	const std::uint32_t now = readTick();
	return ApplyOutermostUnfreeze(fields, now);
}

} // namespace obng::engine::time::unfreeze
