#pragma once

// The values 0x0047D150 (proposed TimeInfo::TimeInfo) stores, as plain
// values, and Apply, which writes exactly the fields the original writes.
// Evidence: re/functions/0x0047D150.md and the export
// re/export/time-info/0x0047D150/asm.s. The name is a proposal until the
// time-info slice's refutation round (open question 1 of the function file:
// constructor or init method).
//
// The body has no decision (read: one block 0x0047D150..0x0047D16C, no
// conditional instruction, data.json has no jump table). Its single flow F1
// stores, in this order (read):
//   0x0047D156  FST  float [ECX+0x04]   +0.0 (ST0 from FLDZ at 0x0047D150)
//   0x0047D159  MOV  byte  [ECX+0x00]   0
//   0x0047D15C  FST  float [ECX+0x08]   +0.0
//   0x0047D15F  MOV  dword [ECX+0x10]   0 (imm32)
//   0x0047D166  FSTP float [ECX+0x0C]   +0.0
//   0x0047D169  MOV  dword [ECX+0x14]   the stack argument ([ESP+4], 0x0047D152)
// and nothing at +0x01..+0x03 (derived from the full instruction list).
//
// Where pseudo.c and the instructions disagree, the instructions are
// followed (AGENTS.md): pseudo.c types the stores to +0x04, +0x08 and +0x0C
// as undefined4 integer zeros; the instructions are float stores of +0.0
// (read). The bit pattern is the same, 0x00000000.
//
// x87 precision: no result here depends on the precision or rounding control;
// +0.0 is exact in every format, so FST/FSTP float store 0x00000000 under all
// of them (derived). The tests run the 53-bit and the 64-bit reading.
//
// Store order: Apply writes in the original's order, but nothing a single
// thread can see depends on it, and the compiler may reorder plain stores.
// The order is not a modelled behaviour (derived: no load and no call between
// the stores, read).

#include "engine/time/TimeInfo.h"

#include <cstdint>

namespace obng::engine::time::timeinfo_ctor {

// The six stored values. The floats are +0.0f, bit pattern 0x00000000; the
// tests compare them as bit patterns so a -0.0f would fail.
struct InitialValues {
	std::uint8_t freezeDepth;       // +0x00  0x0047D159 (read)
	float fixedStepMs;             // +0x04  0x0047D156 (read)
	float fixedStepCarryMs;        // +0x08  0x0047D15C (read)
	float frameSeconds;        // +0x0C  0x0047D166 (read)
	std::uint32_t clockMs;      // +0x10  0x0047D15F (read)
	std::uint32_t startTick;   // +0x14  0x0047D169 (read)
};

// F1: what 0x0047D150 stores for a start tick. The tick is stored unchanged:
// a plain 32-bit MOV, no sign, range or wrap handling (read, 0x0047D169).
constexpr InitialValues InitialValuesFor(std::uint32_t startTick) {
	return InitialValues{0, 0.0f, 0.0f, 0.0f, 0, startTick};
}

// Writes the six fields, in the original's order, and leaves pad[0..2]
// (+0x01..+0x03) as they were, as the original does (claim 7 of the function
// file, derived).
constexpr void Apply(TimeInfo& self, const InitialValues& values) {
	self.fixedStepMs = values.fixedStepMs;
	self.freezeDepth = values.freezeDepth;
	self.fixedStepCarryMs = values.fixedStepCarryMs;
	self.clockMs = values.clockMs;
	self.frameSeconds = values.frameSeconds;
	self.startTick = values.startTick;
}

} // namespace obng::engine::time::timeinfo_ctor
