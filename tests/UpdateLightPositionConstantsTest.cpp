// Tests of the rebuild of 0x007C9140 (proposed UpdateLightPositionConstants):
// three gated light positions transformed by the caller's matrix and stored,
// with an optional scale, as vec4s at 0x00B44FD8.
//
// The mapped control flow (re/functions/0x007C9140.md):
//   B1  loop entry:      JMP 0x007C9160 with slot 0              0x007C9156
//   B2  slot gate:       byte [slotRecord[i]+8] == 0 -> skip     0x007C9162/0x007C9166
//   B3  adjust gate:     arg4 != 0 -> store xyz and w as they are 0x007C919A/0x007C91A6
//   B4  id in range:     arg4 == 0, arg3 - 0x177 <= 2 (unsigned) -> xyz *= arg2
//                                                                0x007C91AC..0x007C91DA
//   B5  id out of range: arg4 == 0, any other arg3 -> w /= arg2   0x007C91B4 taken
//   B6  loop back:       EDI < 0x00B4504C -> next slot           0x007C9216/0x007C921C
// Each section names the flows it drives.
//
// Three kinds of evidence, kept apart:
//   - "read": expectations taken from the instructions (the flow table);
//   - "measured, emulator": the original's bytes run in tools/difftest/
//     emulate.py on tools/difftest/cases/0x007C9140.json (27 cases, run
//     2026-10-30, fpcw 0x007F; every case returned to its sentinel with no
//     error and stack_delta +16, which is the RET 0x10 popping the four
//     arguments - emulate.py measures from the first argument). The result
//     tools/difftest/expected/0x007C9140.json is transcribed literally below
//     (EAX, the stub's trace, the inputs the stub logged, every write into the
//     constant block in order, the order of transform calls and block writes,
//     the block afterwards). Cases 26 and 27 point slot records into the
//     block, so a flag is read after the earlier slots' stores;
//   - "measured, hardware": HardwareReplay below runs the original's own x87
//     instruction sequences (same opcodes, same operand forms, fpcw 0x007F)
//     on the CPU running the test. On 2026-10-30 that was an AMD Ryzen 7
//     7800X3D, the machine the game runs on.
//
// Emulator limit (measured): Unicorn 2.1.4 does not quiet a signaling NaN on
// FLD m32, the CPU does (its #IA is masked and FLD delivers the quiet NaN).
// So the cases that feed signaling NaNs through the body's FLD/FSTP copies
// record the emulator's value, not the game's; the rebuild follows the CPU.
// kHardwareGaps lists every such word literally, emulator and hardware value,
// and the test checks both: the emulator transcript holds the first, the
// rebuild and the hardware replay give the second. Every other word of every
// case must equal the transcript. A float subnormal result (the double
// rounding of precision control 24) is a case where emulator and hardware
// agree, and they do.
//
// What the cases cannot see: D3DXVec3TransformCoord is a raw-code stub that
// copies its input to a log and hands out chosen outputs (the case note), so
// what D3DX computes is outside this test (the rebuild calls it through a
// port); pOut and pV are stack addresses in the emulator and have no
// counterpart in the rebuild, so they are checked as "a stack address, ECX =
// pV, EDX = pOut" on the transcript only; EAX at RET is not a result (void)
// and is transcribed but not compared with the rebuild.

#include "Check.h"
#include "engine/render/UpdateLightPositionConstants.h"
#include "engine/render/UpdateLightPositionConstantsLogic.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace logic = obng::engine::render::update_light_position_constants;
namespace engine_ns = obng::engine::render::update_light_position_constants::engine;
namespace x87 = logic::x87;
using logic::FloatBits;
using logic::SlotAdjust;
using logic::Vec3Bits;
using logic::Vec4Bits;
using x87::Operand;

namespace {

// ---- Addresses of the emulator cases (tools/difftest/cases/0x007C9140.json) ----
// The globals have one source each (this body), so they are not header
// constants; here they only name the emulator's addresses.
constexpr std::uint32_t kConstantBlock = 0x00B44FD8u;
constexpr std::uint32_t kTransformCallReturn = 0x007C919Au;  // the instruction after CALL 0x009A1B0A
constexpr std::uint32_t kEmulatorStackLow = 0x7F000000u;
constexpr std::uint32_t kEmulatorStackHigh = 0x7F010000u;
constexpr FloatBits kBlockSentinel = 0xCDCD0000u;  // + word index, the block before each case

constexpr FloatBits Bits(float value) {
	return std::bit_cast<FloatBits>(value);
}

// ---- The hardware replay: the original's x87 sequences on this CPU ------------

constexpr std::uint16_t kGameControlWord = 0x007F;  // 24-bit precision, nearest, all masked

// Memory the sequences address by offset from one register.
struct HardwareFrame {
	FloatBits x;      // +0x00
	FloatBits y;      // +0x04
	FloatBits z;      // +0x08
	FloatBits w;      // +0x0C
	FloatBits scale;  // +0x10
	FloatBits outX;   // +0x14
	FloatBits outY;   // +0x18
	FloatBits outZ;   // +0x1C
	FloatBits outW;   // +0x20
	FloatBits wTemp;  // +0x24, the reused arg4 slot [ESP+0x38]
	std::uint16_t controlWord;  // +0x28
	std::uint16_t savedWord;    // +0x2A
};
static_assert(offsetof(HardwareFrame, controlWord) == 0x28, "offsets used by the asm below");

// FLD m32 / FSTP m32: 0x007C916C/0x007C9173 (and the copies of every float
// the body does not compute).
FloatBits HardwareCopy(FloatBits value) {
	HardwareFrame f{};
	f.x = value;
	f.controlWord = kGameControlWord;
	__asm__ __volatile__(
		"fnstcw 0x2A(%0)\n\t"
		"fldcw 0x28(%0)\n\t"
		"flds 0x00(%0)\n\t"
		"fstps 0x14(%0)\n\t"
		"fldcw 0x2A(%0)\n\t"
		:
		: "r"(&f)
		: "memory", "st");
	return f.outX;
}

// The tail of an enabled slot from 0x007C919C to 0x007C920A, opcode for
// opcode, for one SlotAdjust; x, y, z are the floats D3DX wrote to the stack.
Vec4Bits HardwareReplay(const Vec3Bits& transformed, FloatBits lightW, FloatBits scale, SlotAdjust adjust) {
	HardwareFrame f{};
	f.x = transformed.x;
	f.y = transformed.y;
	f.z = transformed.z;
	f.w = lightW;
	f.scale = scale;
	f.controlWord = kGameControlWord;
	switch (adjust) {
	case SlotAdjust::ScalePosition:
		__asm__ __volatile__(
			"fnstcw 0x2A(%0)\n\t"
			"fldcw 0x28(%0)\n\t"
			"flds 0x0C(%0)\n\t"         // 0x007C919C FLD [lightW]
			"fstps 0x24(%0)\n\t"        // 0x007C91A2 FSTP [ESP+0x38]
			"flds 0x00(%0)\n\t"         // 0x007C91B6 FLD [ESP+0x10]
			"flds 0x10(%0)\n\t"         // 0x007C91BA FLD [ESP+0x30]
			"fld %%st(0)\n\t"           // 0x007C91BE FLD ST0
			"fmulp %%st, %%st(2)\n\t"   // 0x007C91C0 FMULP ST2
			"fxch %%st(1)\n\t"          // 0x007C91C2 FXCH
			"fstps 0x00(%0)\n\t"        // 0x007C91C4 FSTP [ESP+0x10]
			"flds 0x04(%0)\n\t"         // 0x007C91C8 FLD [ESP+0x14]
			"fmul %%st(1), %%st\n\t"    // 0x007C91CC FMUL ST1
			"fstps 0x04(%0)\n\t"        // 0x007C91CE FSTP [ESP+0x14]
			"fmuls 0x08(%0)\n\t"        // 0x007C91D2 FMUL [ESP+0x18]
			"fstps 0x08(%0)\n\t"        // 0x007C91D6 FSTP [ESP+0x18]
			"flds 0x00(%0)\n\t"         // 0x007C91E8.. the four copies to the block
			"fstps 0x14(%0)\n\t"
			"flds 0x04(%0)\n\t"
			"fstps 0x18(%0)\n\t"
			"flds 0x08(%0)\n\t"
			"fstps 0x1C(%0)\n\t"
			"flds 0x24(%0)\n\t"
			"fstps 0x20(%0)\n\t"
			"fldcw 0x2A(%0)\n\t"
			:
			: "r"(&f)
			: "memory", "st", "st(1)", "st(2)");
		break;
	case SlotAdjust::DivideW:
		__asm__ __volatile__(
			"fnstcw 0x2A(%0)\n\t"
			"fldcw 0x28(%0)\n\t"
			"flds 0x0C(%0)\n\t"   // 0x007C919C
			"fstps 0x24(%0)\n\t"  // 0x007C91A2
			"flds 0x24(%0)\n\t"   // 0x007C91DC FLD [ESP+0x38]
			"fdivs 0x10(%0)\n\t"  // 0x007C91E0 FDIV [ESP+0x30]
			"fstps 0x24(%0)\n\t"  // 0x007C91E4 FSTP [ESP+0x38]
			"flds 0x00(%0)\n\t"   // 0x007C91E8..
			"fstps 0x14(%0)\n\t"
			"flds 0x04(%0)\n\t"
			"fstps 0x18(%0)\n\t"
			"flds 0x08(%0)\n\t"
			"fstps 0x1C(%0)\n\t"
			"flds 0x24(%0)\n\t"
			"fstps 0x20(%0)\n\t"
			"fldcw 0x2A(%0)\n\t"
			:
			: "r"(&f)
			: "memory", "st");
		break;
	case SlotAdjust::Keep:
		__asm__ __volatile__(
			"fnstcw 0x2A(%0)\n\t"
			"fldcw 0x28(%0)\n\t"
			"flds 0x0C(%0)\n\t"   // 0x007C919C
			"fstps 0x24(%0)\n\t"  // 0x007C91A2
			"flds 0x00(%0)\n\t"   // 0x007C91E8..
			"fstps 0x14(%0)\n\t"
			"flds 0x04(%0)\n\t"
			"fstps 0x18(%0)\n\t"
			"flds 0x08(%0)\n\t"
			"fstps 0x1C(%0)\n\t"
			"flds 0x24(%0)\n\t"
			"fstps 0x20(%0)\n\t"
			"fldcw 0x2A(%0)\n\t"
			:
			: "r"(&f)
			: "memory", "st");
		break;
	}
	return Vec4Bits{f.outX, f.outY, f.outZ, f.outW};
}

void PrintVec4(const char* label, const Vec4Bits& v) {
	std::printf("      %s %08X %08X %08X %08X\n", label, v.x, v.y, v.z, v.w);
}

// ---- Flows of the decisions -----------------------------------------------------

void SlotGate() {
	Section("B2 slot gate: only byte +0x08, any non-zero value enables (read 0x007C9162/0x007C9166)");
	Check(!logic::SlotEnabled(0x00), "0x00: disabled, the slot is skipped");
	Check(logic::SlotEnabled(0x01), "0x01: enabled");
	Check(logic::SlotEnabled(0x80), "0x80: enabled (CMP byte with 0, not a sign test)");
	Check(logic::SlotEnabled(0xFF), "0xFF: enabled");
}

void AdjustGate() {
	Section("B3/B4/B5: ClassifyAdjust over arg4 and arg3 (read 0x007C919A..0x007C91B4)");
	Check(logic::ClassifyAdjust(0x177, 1) == SlotAdjust::Keep, "B3: arg4 1, arg3 in range: kept");
	Check(logic::ClassifyAdjust(0x10, 1) == SlotAdjust::Keep, "B3: arg4 1, arg3 out of range: kept");
	Check(logic::ClassifyAdjust(0x178, 0x100) == SlotAdjust::Keep, "B3: arg4 0x100 (low byte 0) is non-zero");
	Check(logic::ClassifyAdjust(0x179, 0x80000000u) == SlotAdjust::Keep, "B3: arg4 0x80000000 is non-zero");
	Check(logic::ClassifyAdjust(0x177, 0) == SlotAdjust::ScalePosition, "B4: arg3 0x177, lower edge");
	Check(logic::ClassifyAdjust(0x178, 0) == SlotAdjust::ScalePosition, "B4: arg3 0x178");
	Check(logic::ClassifyAdjust(0x179, 0) == SlotAdjust::ScalePosition, "B4: arg3 0x179, upper edge");
	Check(logic::ClassifyAdjust(0x176, 0) == SlotAdjust::DivideW, "B5: arg3 0x176, one below");
	Check(logic::ClassifyAdjust(0x17A, 0) == SlotAdjust::DivideW, "B5: arg3 0x17A, one above");
	Check(logic::ClassifyAdjust(0, 0) == SlotAdjust::DivideW, "B5: arg3 0 (0 - 0x177 wraps above 2)");
	Check(logic::ClassifyAdjust(0xFFFFFFFFu, 0) == SlotAdjust::DivideW, "B5: arg3 -1, unsigned compare (JA)");
	Check(logic::PassScalesPosition(0x177) && !logic::PassScalesPosition(0x17A), "PassScalesPosition: the range 0x177..0x179");
}

// ---- Flows of the x87 model -------------------------------------------------------------
// Expected patterns: "measured" ones were seen in the emulator and on the
// CPU (the replay section checks every one of them on the CPU again).

void X87Rounding() {
	Section("x87 RoundShift: nearest, ties to even, sticky above the tie");
	Check(x87::RoundShift(0b1011, 2, false) == 0b11, "0b10.11 -> 0b11 (above half)");
	Check(x87::RoundShift(0b1001, 2, false) == 0b10, "0b10.01 -> 0b10 (below half)");
	Check(x87::RoundShift(0b1010, 2, false) == 0b10, "0b10.10 tie -> even 0b10");
	Check(x87::RoundShift(0b1110, 2, false) == 0b100, "0b11.10 tie -> even 0b100");
	Check(x87::RoundShift(0b1010, 2, true) == 0b11, "0b10.10 with sticky is above the tie -> 0b11");
	Check(x87::RoundShift(0x123, 0, true) == 0x123, "no bits dropped: unchanged");
	Check(x87::RoundShift(0xFFFFFFu, 64, false) == 0, "64 bits dropped: zero");

	Section("x87 Load (FLD m32 / FSTP m32): exact, a signaling NaN comes out quiet (measured, CPU)");
	Check(x87::Load(Bits(1.5f)) == Bits(1.5f), "a number is unchanged");
	Check(x87::Load(0x80000000u) == 0x80000000u, "-0 is unchanged");
	Check(x87::Load(0x00000001u) == 0x00000001u, "the smallest subnormal is unchanged");
	Check(x87::Load(0x7FC00000u) == 0x7FC00000u, "a quiet NaN is unchanged");
	Check(x87::Load(0x7F800001u) == 0x7FC00001u, "SNaN 0x7F800001 -> 0x7FC00001");
	Check(x87::Load(0xFF800002u) == 0xFFC00002u, "SNaN 0xFF800002 -> 0xFFC00002, sign kept");
	Check(x87::Load(0x7F800000u) == 0x7F800000u, "+inf is unchanged");

	Section("x87 Multiply: one rounding in the normal range, two in the subnormal range");
	Check(x87::Multiply(Bits(2.0f), Bits(10.0f), Operand::Register) == Bits(20.0f), "2 * 10 = 20, exact");
	Check(x87::Multiply(Bits(3.0f), Bits(1.1f), Operand::Register) == 0x40533334u,
		"3 * 1.1f = 0x40533334, the float rounding (measured, emulator case 4)");
	Check(x87::Multiply(0x1F408697u, 0x1CA5E5B5u, Operand::Register) == 0x00007CC4u,
		"subnormal: 0x1F408697 * 0x1CA5E5B5 = 0x7CC4 at PC 24 (one rounding would give 0x7CC3; measured)");
	Check(x87::Multiply(0x1F408697u, 0x1CF184B3u, Operand::Register) == 0x0000B5A2u,
		"subnormal: rounding down where one rounding goes up (0xB5A3; measured)");
	Check(x87::Multiply(0x7F000000u, 0x7F000000u, Operand::Register) == 0x7F800000u, "overflow: +inf (measured)");
	Check(x87::Multiply(0x7F000000u, 0xFF000000u, Operand::Register) == 0xFF800000u, "overflow negative: -inf");
	Check(x87::Multiply(0x00000001u, Bits(0.25f), Operand::Register) == 0x00000000u, "underflow past half the smallest subnormal: +0");
	Check(x87::Multiply(0x00000001u, 0x3F400000u, Operand::Register) == 0x00000001u, "0.75 of the smallest subnormal: rounds up to it");
	Check(x87::Multiply(0x00000001u, Bits(0.5f), Operand::Register) == 0x00000000u, "exactly half the smallest subnormal: tie to even, +0");
	Check(x87::Multiply(0x007FFFFFu, 0x3F800001u, Operand::Register) == 0x00800000u, "largest subnormal * (1 + ulp): carries into the smallest normal");
	Check(x87::Multiply(Bits(-1.0f), 0x00000000u, Operand::Register) == 0x80000000u, "-1 * +0 = -0");
	Check(x87::Multiply(0x7F800000u, 0x00000000u, Operand::Register) == x87::kIndefinite, "inf * 0: the indefinite NaN 0xFFC00000 (measured)");
	Check(x87::Multiply(0x80000000u, 0xFF800000u, Operand::Memory) == x87::kIndefinite, "-0 * -inf: indefinite");
	Check(x87::Multiply(0xFF800000u, Bits(2.0f), Operand::Register) == 0xFF800000u, "-inf * 2 = -inf");

	Section("x87 Divide (FDIV m32): the same double rounding, zero divide and invalid masked");
	Check(x87::Divide(Bits(100.0f), Bits(4.0f)) == Bits(25.0f), "100 / 4 = 25, exact");
	Check(x87::Divide(Bits(100.0f), Bits(3.0f)) == 0x42055555u, "100 / 3 = 0x42055555, the float rounding (measured, emulator case 7)");
	Check(x87::Divide(Bits(1.0f), Bits(3.0f)) == 0x3EAAAAABu, "1 / 3 = 0x3EAAAAAB, the sticky bit decides");
	Check(x87::Divide(0x0537D3EBu, 0x4619B735u) == 0x00099132u,
		"subnormal: 0x0537D3EB / 0x4619B735 = 0x99132 at PC 24 (one rounding: 0x99131; measured)");
	Check(x87::Divide(0x05BAA4D2u, 0x4619B735u) == 0x00136D6Cu,
		"subnormal: rounding down where one rounding goes up (0x136D6D; measured)");
	Check(x87::Divide(Bits(1.0f), 0x00000000u) == 0x7F800000u, "1 / +0 = +inf (measured)");
	Check(x87::Divide(Bits(-2.0f), 0x00000000u) == 0xFF800000u, "-2 / +0 = -inf (measured)");
	Check(x87::Divide(Bits(1.0f), 0x80000000u) == 0xFF800000u, "1 / -0 = -inf");
	Check(x87::Divide(0x00000000u, 0x00000000u) == x87::kIndefinite, "0 / 0: indefinite (measured)");
	Check(x87::Divide(0x7F800000u, 0xFF800000u) == x87::kIndefinite, "inf / -inf: indefinite (measured)");
	Check(x87::Divide(Bits(5.0f), 0x7F800000u) == 0x00000000u, "5 / inf = +0 (measured)");
	Check(x87::Divide(0x80000000u, Bits(2.0f)) == 0x80000000u, "-0 / 2 = -0");
	Check(x87::Divide(0xFF800000u, Bits(2.0f)) == 0xFF800000u, "-inf / 2 = -inf");
	Check(x87::Divide(0x7F000000u, 0x00000001u) == 0x7F800000u, "huge / smallest subnormal: overflow, +inf");

	Section("x87 NaN operands (measured, CPU): larger significand, tie positive, SNaN memory operand loses to ST0");
	Check(x87::Multiply(0x7FC00001u, 0xFFC00002u, Operand::Register) == 0xFFC00002u, "registers: the larger significand");
	Check(x87::Multiply(0xFFC00002u, 0x7FC00001u, Operand::Register) == 0xFFC00002u, "registers: the same in the other order");
	Check(x87::Multiply(0xFFC00001u, 0x7FC00001u, Operand::Register) == 0x7FC00001u, "registers: a tie gives the positive one");
	Check(x87::Multiply(0x7FC00001u, 0x7F800010u, Operand::Register) == 0x7FC00010u,
		"registers: an SNaN loaded by FLD is quiet, then larger significand");
	Check(x87::Multiply(0x7FC00001u, 0x7F800010u, Operand::Memory) == 0x7FC00001u,
		"memory: an SNaN operand against a NaN in ST0 gives ST0 (measured)");
	Check(x87::Multiply(Bits(2.0f), 0x7F800010u, Operand::Memory) == 0x7FC00010u, "memory: an SNaN against a number comes out quiet");
	Check(x87::Multiply(0x7FC00001u, 0xFFC00100u, Operand::Memory) == 0xFFC00100u, "memory: two quiet NaNs, larger significand");
	Check(x87::Multiply(Bits(1.0f), 0x7FC00000u, Operand::Register) == 0x7FC00000u, "one NaN propagates");
	Check(x87::Multiply(0x7F800001u, 0x00000000u, Operand::Register) == 0x7FC00001u, "SNaN * 0: the quieted SNaN, not the indefinite");
	Check(x87::Divide(0x7FC00001u, 0x7F800010u) == 0x7FC00001u, "w QNaN / SNaN scale: w (measured, emulator case 25)");
	Check(x87::Divide(Bits(2.0f), 0x7F800010u) == 0x7FC00010u, "2 / SNaN scale: the quieted scale (measured, emulator case 25)");
	Check(x87::Divide(0x7F800010u, 0x7FC00008u) == 0x7FC00010u, "SNaN w (quiet after FLD) / QNaN scale: larger significand (measured, CPU)");
}

void ComposeFlows() {
	const Vec3Bits t{Bits(10.0f), Bits(11.0f), Bits(12.0f)};
	Section("ComposeSlotConstant B3: xyz and w stored as they are (read 0x007C91A6 -> 0x007C91E8)");
	Check(logic::ComposeSlotConstant(t, Bits(100.0f), Bits(2.0f), SlotAdjust::Keep) ==
			Vec4Bits{Bits(10.0f), Bits(11.0f), Bits(12.0f), Bits(100.0f)},
		"(10, 11, 12, 100), the scale unused");
	Section("ComposeSlotConstant B4: xyz times the scale, w kept (read 0x007C91B6..0x007C91D6)");
	Check(logic::ComposeSlotConstant(t, Bits(100.0f), Bits(2.0f), SlotAdjust::ScalePosition) ==
			Vec4Bits{Bits(20.0f), Bits(22.0f), Bits(24.0f), Bits(100.0f)},
		"(20, 22, 24, 100)");
	Section("ComposeSlotConstant B5: w over the scale, xyz kept (read 0x007C91DC..0x007C91E4)");
	Check(logic::ComposeSlotConstant(t, Bits(100.0f), Bits(4.0f), SlotAdjust::DivideW) ==
			Vec4Bits{Bits(10.0f), Bits(11.0f), Bits(12.0f), Bits(25.0f)},
		"(10, 11, 12, 25)");
	Section("ComposeSlotConstant: the copies quiet signaling NaNs in every flow");
	const Vec3Bits signaling{0x7F800001u, 0xFF800002u, 0x7F800003u};
	Check(logic::ComposeSlotConstant(signaling, 0x7FA00000u, Bits(2.0f), SlotAdjust::Keep) ==
			Vec4Bits{0x7FC00001u, 0xFFC00002u, 0x7FC00003u, 0x7FE00000u},
		"B3: all four come out quiet");
	Check(logic::TransformInput(Vec3Bits{0x7F800001u, Bits(1.0f), 0x80000000u}) ==
			Vec3Bits{0x7FC00001u, Bits(1.0f), 0x80000000u},
		"TransformInput: the FLD/FSTP copy to pV quiets an SNaN, keeps -0");
}

// ---- The loop over ports ------------------------------------------------------------------

// Every port call in one ordered list: 'F' a flag read, 'T' a transform,
// 'S' a store, each followed by its slot ('T' by the slot of the flag read
// before it), so the interleaving itself is checked.
struct PortLog {
	std::string events;
	std::vector<Vec3Bits> transformInputs;
	std::vector<std::uint32_t> matrices;
	std::vector<int> storedSlots;
	std::vector<Vec4Bits> stored;
};

void LogEvent(PortLog& log, char kind, int slot) {
	log.events += kind;
	log.events += static_cast<char>('0' + slot);
}

PortLog RunLoop(const logic::Inputs& in, std::array<std::uint8_t, 3> flags, const std::vector<Vec3Bits>& outputs,
	std::uint32_t matrix) {
	PortLog log;
	int flagSlot = -1;
	logic::UpdateLightPositionConstants(
		in, matrix,
		[&](int slot) {
			flagSlot = slot;
			LogEvent(log, 'F', slot);
			return flags[static_cast<std::size_t>(slot)];
		},
		[&](const Vec3Bits& input, std::uint32_t m) {
			const std::size_t call = log.transformInputs.size();
			LogEvent(log, 'T', flagSlot);
			log.transformInputs.push_back(input);
			log.matrices.push_back(m);
			return call < outputs.size() ? outputs[call] : Vec3Bits{};
		},
		[&](int slot, const Vec4Bits& value) {
			LogEvent(log, 'S', slot);
			log.storedSlots.push_back(slot);
			log.stored.push_back(value);
		});
	return log;
}

logic::Inputs LoopInputs(std::uint32_t passId, std::uint32_t skip) {
	return logic::Inputs{
		{Vec3Bits{Bits(1.0f), Bits(2.0f), Bits(3.0f)}, Vec3Bits{Bits(4.0f), Bits(5.0f), Bits(6.0f)},
			Vec3Bits{Bits(7.0f), Bits(8.0f), Bits(9.0f)}},
		{Bits(100.0f), Bits(200.0f), Bits(300.0f)},
		Bits(2.0f),
		passId,
		skip,
	};
}

void LoopFlows() {
	const std::vector<Vec3Bits> outputs{{Bits(10.0f), Bits(11.0f), Bits(12.0f)}, {Bits(13.0f), Bits(14.0f), Bits(15.0f)},
		{Bits(16.0f), Bits(17.0f), Bits(18.0f)}};

	Section("B1 B6: three slots, in order, each flag then its transform then its store (read 0x007C9160, 0x007C9195, "
			"0x007C91EC..0x007C920A, JL 0x007C9160 at 0x007C921C)");
	PortLog all = RunLoop(LoopInputs(0x177, 1), {1, 1, 1}, outputs, 0x10001000u);
	Check(all.events == "F0T0S0F1T1S1F2T2S2", "the ports interleave per slot: F0 T0 S0 F1 T1 S1 F2 T2 S2");
	Check(all.transformInputs.size() == 3 && all.stored.size() == 3, "three transforms, three stores");
	Check(all.storedSlots == std::vector<int>{0, 1, 2}, "slots 0, 1, 2 in order");
	Check(all.transformInputs[1] == Vec3Bits{Bits(4.0f), Bits(5.0f), Bits(6.0f)}, "slot 1 transforms the position of slot 1");
	Check(all.matrices == std::vector<std::uint32_t>{0x10001000u, 0x10001000u, 0x10001000u}, "every call gets the caller's matrix");
	Check(all.stored[2] == Vec4Bits{Bits(16.0f), Bits(17.0f), Bits(18.0f), Bits(300.0f)}, "B3: slot 2 stores its transform and its own w");

	Section("B2 B6: a disabled slot calls neither transform nor store, the others keep their slot index");
	PortLog middle = RunLoop(LoopInputs(0x177, 0), {0, 1, 0}, outputs, 0x10001000u);
	Check(middle.events == "F0F1T1S1F2", "every flag is read, only slot 1 transforms and stores");
	Check(middle.transformInputs.size() == 1 && middle.storedSlots == std::vector<int>{1}, "only slot 1 runs");
	Check(middle.transformInputs[0] == Vec3Bits{Bits(4.0f), Bits(5.0f), Bits(6.0f)}, "with the position of slot 1");
	Check(middle.stored[0] == Vec4Bits{Bits(20.0f), Bits(22.0f), Bits(24.0f), Bits(200.0f)}, "B4: the first transform's output times 2, w of slot 1");
	PortLog outer = RunLoop(LoopInputs(0x10, 0), {1, 0, 1}, outputs, 0x10001000u);
	Check(outer.events == "F0T0S0F1F2T2S2", "slot 0 stores before slot 1's flag is read");
	Check(outer.storedSlots == std::vector<int>{0, 2}, "slots 0 and 2 run, 1 is skipped");
	Check(outer.stored[1] == Vec4Bits{Bits(13.0f), Bits(14.0f), Bits(15.0f), Bits(150.0f)}, "B5: slot 2 gets the second output and 300 / 2");

	Section("B2: no slot enabled - three flag reads, no transform, no store");
	PortLog none = RunLoop(LoopInputs(0x177, 0), {0, 0, 0}, outputs, 0x10001000u);
	Check(none.events == "F0F1F2", "only the flags are read");
	Check(none.transformInputs.empty() && none.stored.empty(), "nothing called");
}

// A slot record that points into the block: its flag is the low byte of the
// block word +0x08 lands on (little-endian), read after the earlier slots'
// stores (read: 0x007C9160/0x007C9162 run again after 0x007C91EC..
// 0x007C920A; measured: emulator cases 26 and 27).
struct AliasedRun {
	std::array<FloatBits, 12> block;
	std::vector<int> storedSlots;
};

AliasedRun RunAliased(std::array<int, 3> flagWord, FloatBits word0, const std::vector<Vec3Bits>& outputs) {
	AliasedRun run{};
	for (int k = 0; k < 12; ++k) {
		run.block[static_cast<std::size_t>(k)] = kBlockSentinel + static_cast<FloatBits>(k);
	}
	run.block[0] = word0;
	std::size_t call = 0;
	logic::UpdateLightPositionConstants(
		LoopInputs(0x177, 1), 0x10001000u,
		[&](int slot) -> std::uint8_t {
			const int word = flagWord[static_cast<std::size_t>(slot)];
			return word < 0 ? std::uint8_t{1} : static_cast<std::uint8_t>(run.block[static_cast<std::size_t>(word)] & 0xFFu);
		},
		[&](const Vec3Bits&, std::uint32_t) { return call < outputs.size() ? outputs[call++] : Vec3Bits{}; },
		[&](int slot, const Vec4Bits& value) {
			run.storedSlots.push_back(slot);
			for (int k = 0; k < 4; ++k) {
				run.block[static_cast<std::size_t>(slot * 4 + k)] = k == 0 ? value.x : k == 1 ? value.y : k == 2 ? value.z : value.w;
			}
		});
	return run;
}

void AliasedSlotRecordFlows() {
	const std::vector<Vec3Bits> outputs{{0x41200000u, Bits(11.0f), Bits(12.0f)}, {0x41500000u, Bits(14.0f), Bits(15.0f)},
		{Bits(16.0f), Bits(17.0f), Bits(18.0f)}};
	Section("B2 aliasing: slot 1's flag is block word 0's low byte, set at entry, cleared by slot 0's store");
	const AliasedRun cleared = RunAliased({-1, 0, -1}, 0xCDCD0001u, outputs);
	Check(cleared.storedSlots == std::vector<int>{0, 2}, "slot 1 reads 0x00 after slot 0 stored x = 0x41200000: skipped");
	Check(cleared.block[4] == kBlockSentinel + 4 && cleared.block[7] == kBlockSentinel + 7, "words 4..7 keep the sentinel");

	Section("B2 aliasing: a flag clear at entry is set by an earlier store, and the next one cleared");
	std::vector<Vec3Bits> setting = outputs;
	setting[0].x = 0x41200001u;
	const AliasedRun set = RunAliased({-1, 0, 4}, 0xCDCD0000u, setting);
	Check(set.storedSlots == std::vector<int>{0, 1}, "slot 1 reads 0x01 from slot 0's x; slot 2 reads 0x00 from slot 1's x");
	Check(set.block[4] == 0x41500000u && set.block[8] == kBlockSentinel + 8, "slot 1 stored, slot 2 kept the sentinel");
}

void Layouts() {
	Section("Engine shape: addresses and views (read; static_asserts in the header)");
	Check(engine_ns::kUpdateLightPositionConstantsAddress == 0x007C9140u, "the function at 0x007C9140");
	Check(logic::kSlotCount == 3, "three slots: (0x00B4504C - 0x00B45040) / 4");
	Check(sizeof(engine_ns::LightConstantView) == 0x10 && engine_ns::kConstantBlockBytes == 0x30,
		"the block: three vec4s, 0x00B44FD8..0x00B45007");
	Check(offsetof(engine_ns::ShaderConstantDescView, enabled) == 0x08, "the slot flag at +0x08");
}

// ---- Differential gate, layer 1: the original in the emulator --------------------------

struct TracedCall {
	std::uint32_t from;  // the return address of the CALL
	std::uint32_t ecx;
	std::uint32_t edx;
	std::uint32_t args[3];  // pOut, pV, pM
};

struct BlockWrite {
	std::uint32_t address;
	FloatBits value;
};

constexpr std::uint32_t kRecord0 = 0x10000000u;
constexpr std::uint32_t kRecord1 = 0x10000100u;
constexpr std::uint32_t kRecord2 = 0x10000200u;
constexpr std::uint32_t kFlagOffset = 0x08u;
constexpr std::uint32_t kConstantBlockEnd = kConstantBlock + 0x30u;

constexpr std::array<FloatBits, 12> SentinelBlock() {
	std::array<FloatBits, 12> block{};
	for (std::size_t k = 0; k < block.size(); ++k) {
		block[k] = kBlockSentinel + static_cast<FloatBits>(k);
	}
	return block;
}

struct EmulatorCase {
	const char* name;
	// Inputs (the case file).
	std::uint8_t flags[3];        // byte +0x08 of each record outside the block (records below)
	Vec3Bits positions[3];        // 0x00B46528 + 0x10i
	FloatBits lightW[3];          // 0x00B465A8 + 0x10i
	Vec3Bits stubOutputs[3];      // what the stub hands out, per call
	std::uint32_t matrix;         // arg1
	FloatBits scale;              // arg2
	std::uint32_t passId;         // arg3
	std::uint32_t skipAdjust;     // arg4
	// The original's result (tools/difftest/expected/0x007C9140.json).
	std::uint32_t eax;
	std::vector<TracedCall> calls;
	std::vector<Vec3Bits> transformInputs;  // the stub's log at 0x10002000
	std::vector<BlockWrite> blockWrites;    // every write into 0x00B44FD8..0x00B45007, in order
	std::array<FloatBits, 12> block;        // 0x00B44FD8..0x00B45007 afterwards
	// The order of the transcript's writes outside the stack: 'T' the stub's
	// last write (its call counter 0x10002FF0, at 0x009A1B2B), one per call,
	// 'W' a write into the block.
	const char* events;
	// The slot records [0x00B45040 + 4i] and the block before the call (the
	// case file); a record whose +0x08 lies in the block reads its flag there.
	std::array<std::uint32_t, 3> records = {kRecord0, kRecord1, kRecord2};
	std::array<FloatBits, 12> blockBefore = SentinelBlock();
};

// clang-format off
const EmulatorCase kEmulatorCases[] = {
	{
		"B1 B2 B3 B6: all slots on, arg4 1 keeps xyz and w (arg3 0x177 ignored)",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000001u,
		0x7F00FED4u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x43960000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x42C80000u, 0x41500000u, 0x41600000u, 0x41700000u, 0x43480000u, 0x41800000u, 0x41880000u, 0x41900000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B3: arg4 0x00000100 (low byte 0) is non-zero, the full dword is tested",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000178u, 0x00000100u,
		0x7F00FED4u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x43960000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x42C80000u, 0x41500000u, 0x41600000u, 0x41700000u, 0x43480000u, 0x41800000u, 0x41880000u, 0x41900000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4: arg3 0x177, scale 2.0 multiplies xyz, w kept",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000000u,
		0x00000000u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41A00000u}, {0x00B44FDCu, 0x41B00000u}, {0x00B44FE0u, 0x41C00000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x41D00000u}, {0x00B44FECu, 0x41E00000u}, {0x00B44FF0u, 0x41F00000u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x42000000u}, {0x00B44FFCu, 0x42080000u}, {0x00B45000u, 0x42100000u}, {0x00B45004u, 0x43960000u}},
		{0x41A00000u, 0x41B00000u, 0x41C00000u, 0x42C80000u, 0x41D00000u, 0x41E00000u, 0x41F00000u, 0x43480000u, 0x42000000u, 0x42080000u, 0x42100000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4: arg3 0x178, scale 3.0, inexact products (1.1f, 2.2f, 3.3f)",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x3F8CCCCDu, 0x400CCCCDu, 0x40533333u}, {0x3DCCCCCDu, 0x3F333333u, 0x3A83126Fu}, {0xC0B00000u, 0x40C80000u, 0xC0F80000u}},
		0x10001000u, 0x40400000u, 0x00000178u, 0x00000000u,
		0x00000001u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x40533334u}, {0x00B44FDCu, 0x40D33334u}, {0x00B44FE0u, 0x411E6666u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x3E99999Au}, {0x00B44FECu, 0x40066666u}, {0x00B44FF0u, 0x3B449BA6u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0xC1840000u}, {0x00B44FFCu, 0x41960000u}, {0x00B45000u, 0xC1BA0000u}, {0x00B45004u, 0x43960000u}},
		{0x40533334u, 0x40D33334u, 0x411E6666u, 0x42C80000u, 0x3E99999Au, 0x40066666u, 0x3B449BA6u, 0x43480000u, 0xC1840000u, 0x41960000u, 0xC1BA0000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4: arg3 0x179 (upper edge), scale 0.5",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x3F000000u, 0x00000179u, 0x00000000u,
		0x00000002u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x40A00000u}, {0x00B44FDCu, 0x40B00000u}, {0x00B44FE0u, 0x40C00000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x40D00000u}, {0x00B44FECu, 0x40E00000u}, {0x00B44FF0u, 0x40F00000u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x41000000u}, {0x00B44FFCu, 0x41080000u}, {0x00B45000u, 0x41100000u}, {0x00B45004u, 0x43960000u}},
		{0x40A00000u, 0x40B00000u, 0x40C00000u, 0x42C80000u, 0x40D00000u, 0x40E00000u, 0x40F00000u, 0x43480000u, 0x41000000u, 0x41080000u, 0x41100000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5: arg3 0x176 (below the range), scale 4.0 divides w, xyz kept",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40800000u, 0x00000176u, 0x00000000u,
		0xFFFFFFFFu,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x41C80000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x42480000u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x42960000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x41C80000u, 0x41500000u, 0x41600000u, 0x41700000u, 0x42480000u, 0x41800000u, 0x41880000u, 0x41900000u, 0x42960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5: arg3 0x17A (above the range), scale 3.0, inexact quotients",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x3F800000u, 0xC0000000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40400000u, 0x0000017Au, 0x00000000u,
		0x00000003u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x42055555u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x3EAAAAABu}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0xBF2AAAABu}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x42055555u, 0x41500000u, 0x41600000u, 0x41700000u, 0x3EAAAAABu, 0x41800000u, 0x41880000u, 0x41900000u, 0xBF2AAAABu},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5: arg3 0xFFFFFFFF (-1, unsigned compare), scale 0.75",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x3F400000u, 0xFFFFFFFFu, 0x00000000u,
		0xFFFFFE88u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x43055555u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x43855555u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x43C80000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x43055555u, 0x41500000u, 0x41600000u, 0x41700000u, 0x43855555u, 0x41800000u, 0x41880000u, 0x41900000u, 0x43C80000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5: arg3 0, scale 8.0",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x41000000u, 0x00000000u, 0x00000000u,
		0xFFFFFE89u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x41480000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x41C80000u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x42160000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x41480000u, 0x41500000u, 0x41600000u, 0x41700000u, 0x41C80000u, 0x41800000u, 0x41880000u, 0x41900000u, 0x42160000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B2 B6: slot 0 off, slot 1 on, slot 2 off (arg4 1)",
		{0x00, 0x01, 0x00},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000001u,
		0x10000200u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x40800000u, 0x40A00000u, 0x40C00000u}},
		{{0x00B44FE8u, 0x41200000u}, {0x00B44FECu, 0x41300000u}, {0x00B44FF0u, 0x41400000u}, {0x00B44FF4u, 0x43480000u}},
		{0xCDCD0000u, 0xCDCD0001u, 0xCDCD0002u, 0xCDCD0003u, 0x41200000u, 0x41300000u, 0x41400000u, 0x43480000u, 0xCDCD0008u, 0xCDCD0009u, 0xCDCD000Au, 0xCDCD000Bu},
		"TWWWW",
	},
	{
		"B2 B6: slot 0 on, slot 1 off, slot 2 on, B4",
		{0x01, 0x00, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000000u,
		0x00000000u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41A00000u}, {0x00B44FDCu, 0x41B00000u}, {0x00B44FE0u, 0x41C00000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FF8u, 0x41D00000u}, {0x00B44FFCu, 0x41E00000u}, {0x00B45000u, 0x41F00000u}, {0x00B45004u, 0x43960000u}},
		{0x41A00000u, 0x41B00000u, 0x41C00000u, 0x42C80000u, 0xCDCD0004u, 0xCDCD0005u, 0xCDCD0006u, 0xCDCD0007u, 0x41D00000u, 0x41E00000u, 0x41F00000u, 0x43960000u},
		"TWWWWTWWWW",
	},
	{
		"B2: all slots off, no call and no store",
		{0x00, 0x00, 0x00},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000000u,
		0x10000200u,
		{},
		{},
		{},
		{0xCDCD0000u, 0xCDCD0001u, 0xCDCD0002u, 0xCDCD0003u, 0xCDCD0004u, 0xCDCD0005u, 0xCDCD0006u, 0xCDCD0007u, 0xCDCD0008u, 0xCDCD0009u, 0xCDCD000Au, 0xCDCD000Bu},
		"",
	},
	{
		"B2: flag byte 0x80 is on, 0xFF neighbours of a 0 flag are not",
		{0x80, 0x00, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000010u, 0x00000000u,
		0xFFFFFE99u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x42480000u}, {0x00B44FF8u, 0x41500000u}, {0x00B44FFCu, 0x41600000u}, {0x00B45000u, 0x41700000u}, {0x00B45004u, 0x43160000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x42480000u, 0xCDCD0004u, 0xCDCD0005u, 0xCDCD0006u, 0xCDCD0007u, 0x41500000u, 0x41600000u, 0x41700000u, 0x43160000u},
		"TWWWWTWWWW",
	},
	{
		"B4 x87 PC24: subnormal products round twice (24 bits, then FSTP m32)",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x1CA5E5B5u, 0x1C2AEC2Fu, 0x1CF184B3u}, {0x1D5883E4u, 0x3F800000u, 0x00000001u}, {0x1CF0DA54u, 0x80000000u, 0x007FFFFFu}},
		0x10001000u, 0x1F408697u, 0x00000177u, 0x00000000u,
		0x00000000u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x00007CC4u}, {0x00B44FDCu, 0x00004046u}, {0x00B44FE0u, 0x0000B5A2u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x000145AAu}, {0x00B44FECu, 0x1F408697u}, {0x00B44FF0u, 0x00000000u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x0000B522u}, {0x00B44FFCu, 0x80000000u}, {0x00B45000u, 0x00000000u}, {0x00B45004u, 0x43960000u}},
		{0x00007CC4u, 0x00004046u, 0x0000B5A2u, 0x42C80000u, 0x000145AAu, 0x1F408697u, 0x00000000u, 0x43480000u, 0x0000B522u, 0x80000000u, 0x00000000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5 x87 PC24: subnormal quotients round twice (24 bits, then FSTP m32)",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x0537D3EBu, 0x05518137u, 0x05BAA4D2u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x4619B735u, 0x00000200u, 0x00000000u,
		0x00000089u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x00099132u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x000AE74Cu}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x00136D6Cu}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x00099132u, 0x41500000u, 0x41600000u, 0x41700000u, 0x000AE74Cu, 0x41800000u, 0x41880000u, 0x41900000u, 0x00136D6Cu},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5 scale 0: w/0 is +inf, 0/0 the indefinite NaN, -2/0 -inf",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x3F800000u, 0x00000000u, 0xC0000000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x00000000u, 0x00000200u, 0x00000000u,
		0x00000089u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x7F800000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0xFFC00000u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0xFF800000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x7F800000u, 0x41500000u, 0x41600000u, 0x41700000u, 0xFFC00000u, 0x41800000u, 0x41880000u, 0x41900000u, 0xFF800000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5 scale +inf and -0: w/inf is 0, inf/inf indefinite",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x40A00000u, 0x7F800000u, 0xFF800000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x7F800000u, 0x00000200u, 0x00000000u,
		0x00000089u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x00000000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0xFFC00000u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0xFFC00000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x00000000u, 0x41500000u, 0x41600000u, 0x41700000u, 0xFFC00000u, 0x41800000u, 0x41880000u, 0x41900000u, 0xFFC00000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4 scale 0: inf*0 indefinite, SNaN*0 quiet NaN, 1*0 zero, w SNaN through FLD/FSTP",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x7F800005u, 0xFFBFFFFFu, 0x80000000u},
		{{0x7F800000u, 0x7F800001u, 0x3F800000u}, {0xBF800000u, 0xFFC00123u, 0x80000001u}, {0x007FFFFFu, 0xFF800000u, 0x40000000u}},
		0x10001000u, 0x00000000u, 0x00000178u, 0x00000000u,
		0x00000001u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0xFFC00000u}, {0x00B44FDCu, 0x7FC00001u}, {0x00B44FE0u, 0x00000000u}, {0x00B44FE4u, 0x7F800005u}, {0x00B44FE8u, 0x80000000u}, {0x00B44FECu, 0xFFC00123u}, {0x00B44FF0u, 0x80000000u}, {0x00B44FF4u, 0xFFBFFFFFu}, {0x00B44FF8u, 0x00000000u}, {0x00B44FFCu, 0xFFC00000u}, {0x00B45000u, 0x00000000u}, {0x00B45004u, 0x80000000u}},
		{0xFFC00000u, 0x7FC00001u, 0x00000000u, 0x7F800005u, 0x80000000u, 0xFFC00123u, 0x80000000u, 0xFFBFFFFFu, 0x00000000u, 0xFFC00000u, 0x00000000u, 0x80000000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4 overflow: products past the float range store as inf",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x7F000000u, 0xFF000000u, 0x3F800000u}, {0x7F7FFFFFu, 0x3F000000u, 0x00000000u}, {0x3FC00000u, 0xBFC00000u, 0x7F7FFFFFu}},
		0x10001000u, 0x7F000000u, 0x00000177u, 0x00000000u,
		0x00000000u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x7F800000u}, {0x00B44FDCu, 0xFF800000u}, {0x00B44FE0u, 0x7F000000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x7F800000u}, {0x00B44FECu, 0x7E800000u}, {0x00B44FF0u, 0x00000000u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x7F400000u}, {0x00B44FFCu, 0xFF400000u}, {0x00B45000u, 0x7F800000u}, {0x00B45004u, 0x43960000u}},
		{0x7F800000u, 0xFF800000u, 0x7F000000u, 0x42C80000u, 0x7F800000u, 0x7E800000u, 0x00000000u, 0x43480000u, 0x7F400000u, 0xFF400000u, 0x7F800000u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B3: SNaN positions, transform outputs and w through FLD/FSTP; -0 and subnormals copied",
		{0x01, 0x01, 0x01},
		{{0x7F800001u, 0xFF800002u, 0x80000000u}, {0x00000001u, 0x807FFFFFu, 0x7FC00000u}, {0x7FBFFFFFu, 0x3F800000u, 0x80000000u}},
		{0x7FA00000u, 0x00000003u, 0xFFC00000u},
		{{0x7F800003u, 0x80000000u, 0x00000002u}, {0xFF812345u, 0x7FFFFFFFu, 0x7F800000u}, {0x00400000u, 0x80400000u, 0xC0400000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000001u,
		0x7F00FED4u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x7F800001u, 0xFF800002u, 0x80000000u}, {0x00000001u, 0x807FFFFFu, 0x7FC00000u}, {0x7FBFFFFFu, 0x3F800000u, 0x80000000u}},
		{{0x00B44FD8u, 0x7F800003u}, {0x00B44FDCu, 0x80000000u}, {0x00B44FE0u, 0x00000002u}, {0x00B44FE4u, 0x7FA00000u}, {0x00B44FE8u, 0xFF812345u}, {0x00B44FECu, 0x7FFFFFFFu}, {0x00B44FF0u, 0x7F800000u}, {0x00B44FF4u, 0x00000003u}, {0x00B44FF8u, 0x00400000u}, {0x00B44FFCu, 0x80400000u}, {0x00B45000u, 0xC0400000u}, {0x00B45004u, 0xFFC00000u}},
		{0x7F800003u, 0x80000000u, 0x00000002u, 0x7FA00000u, 0xFF812345u, 0x7FFFFFFFu, 0x7F800000u, 0x00000003u, 0x00400000u, 0x80400000u, 0xC0400000u, 0xFFC00000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5 two NaNs: the larger significand wins, a tie gives the positive one",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x7FC00001u, 0x7FC00003u, 0x7FC00002u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0xFFC00002u, 0x00000300u, 0x00000000u,
		0x00000189u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0xFFC00002u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x7FC00003u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x7FC00002u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0xFFC00002u, 0x41500000u, 0x41600000u, 0x41700000u, 0x7FC00003u, 0x41800000u, 0x41880000u, 0x41900000u, 0x7FC00002u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4 two NaNs: x tie, y SNaN quieted then tie, z larger significand",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0xFFC00001u, 0xFF800001u, 0x7FC00010u}, {0x7F800001u, 0x3F800000u, 0xFFFFFFFFu}, {0x3F800000u, 0x7FC00000u, 0xFFC00000u}},
		0x10001000u, 0x7FC00001u, 0x00000177u, 0x00000000u,
		0x00000000u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x7FC00001u}, {0x00B44FDCu, 0x7FC00001u}, {0x00B44FE0u, 0x7FC00010u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x7FC00001u}, {0x00B44FECu, 0x7FC00001u}, {0x00B44FF0u, 0xFFFFFFFFu}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x7FC00001u}, {0x00B44FFCu, 0x7FC00001u}, {0x00B45000u, 0x7FC00001u}, {0x00B45004u, 0x43960000u}},
		{0x7FC00001u, 0x7FC00001u, 0x7FC00010u, 0x42C80000u, 0x7FC00001u, 0x7FC00001u, 0xFFFFFFFFu, 0x43480000u, 0x7FC00001u, 0x7FC00001u, 0x7FC00001u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B4 SNaN transform outputs against a QNaN scale: the NaN choice after FLD",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x7F800010u, 0xFF800020u, 0x7F800030u}, {0x7F800001u, 0xFF800001u, 0x3F800000u}, {0x40000000u, 0x7FC00000u, 0xFF800002u}},
		0x10001000u, 0x7FC00001u, 0x00000179u, 0x00000000u,
		0x00000002u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x7FC00001u}, {0x00B44FDCu, 0x7FC00001u}, {0x00B44FE0u, 0x7FC00001u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x7FC00001u}, {0x00B44FECu, 0x7FC00001u}, {0x00B44FF0u, 0x7FC00001u}, {0x00B44FF4u, 0x43480000u}, {0x00B44FF8u, 0x7FC00001u}, {0x00B44FFCu, 0x7FC00001u}, {0x00B45000u, 0x7FC00001u}, {0x00B45004u, 0x43960000u}},
		{0x7FC00001u, 0x7FC00001u, 0x7FC00001u, 0x42C80000u, 0x7FC00001u, 0x7FC00001u, 0x7FC00001u, 0x43480000u, 0x7FC00001u, 0x7FC00001u, 0x7FC00001u, 0x43960000u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5 SNaN w against a QNaN scale: the NaN choice after FLD",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x7F800010u, 0x7F800001u, 0x3F800000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x7FC00008u, 0x00000176u, 0x00000000u,
		0xFFFFFFFFu,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x7FC00008u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x7FC00008u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0x7FC00008u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x7FC00008u, 0x41500000u, 0x41600000u, 0x41700000u, 0x7FC00008u, 0x41800000u, 0x41880000u, 0x41900000u, 0x7FC00008u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B5 SNaN scale (the FDIV memory operand): a QNaN w wins, a number w gives the quieted scale",
		{0x01, 0x01, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x7FC00001u, 0x40000000u, 0xFFC00100u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x7F800010u, 0x00000176u, 0x00000000u,
		0xFFFFFFFFu,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x7FC00001u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x7FC00010u}, {0x00B44FF8u, 0x41800000u}, {0x00B44FFCu, 0x41880000u}, {0x00B45000u, 0x41900000u}, {0x00B45004u, 0xFFC00100u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x7FC00001u, 0x41500000u, 0x41600000u, 0x41700000u, 0x7FC00010u, 0x41800000u, 0x41880000u, 0x41900000u, 0xFFC00100u},
		"TWWWWTWWWWTWWWW",
	},
	{
		"B2 aliasing: slot 1's record is 0x00B44FD0, its flag the low byte of block word 0 (0x01 at entry, word 0 = 0xCDCD0001); slot 0 stores x = 0x41200000 first, so the flag read for slot 1 is 0x00 and slot 1 is skipped",
		{0x01, 0x00, 0x01},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200000u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000001u,
		0x7F00FED4u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{{0x00B44FD8u, 0x41200000u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FF8u, 0x41500000u}, {0x00B44FFCu, 0x41600000u}, {0x00B45000u, 0x41700000u}, {0x00B45004u, 0x43960000u}},
		{0x41200000u, 0x41300000u, 0x41400000u, 0x42C80000u, 0xCDCD0004u, 0xCDCD0005u, 0xCDCD0006u, 0xCDCD0007u, 0x41500000u, 0x41600000u, 0x41700000u, 0x43960000u},
		"TWWWWTWWWW",
		{0x10000000u, 0x00B44FD0u, 0x10000200u},
		{0xCDCD0001u, 0xCDCD0001u, 0xCDCD0002u, 0xCDCD0003u, 0xCDCD0004u, 0xCDCD0005u, 0xCDCD0006u, 0xCDCD0007u, 0xCDCD0008u, 0xCDCD0009u, 0xCDCD000Au, 0xCDCD000Bu},
	},
	{
		"B2 aliasing both ways: slot 1's flag is the low byte of block word 0 (0x00 at entry), slot 2's the low byte of word 4 (0x04 at entry); slot 0 stores x = 0x41200001, so slot 1 runs, and slot 1 stores x = 0x41500000, so slot 2 is skipped",
		{0x01, 0x00, 0x00},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, {0x40E00000u, 0x41000000u, 0x41100000u}},
		{0x42C80000u, 0x43480000u, 0x43960000u},
		{{0x41200001u, 0x41300000u, 0x41400000u}, {0x41500000u, 0x41600000u, 0x41700000u}, {0x41800000u, 0x41880000u, 0x41900000u}},
		0x10001000u, 0x40000000u, 0x00000177u, 0x00000001u,
		0x00B44FE0u,
		{{0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}, {0x007C919Au, 0x7F00FEE0u, 0x7F00FED4u, {0x7F00FED4u, 0x7F00FEE0u, 0x10001000u}}},
		{{0x3F800000u, 0x40000000u, 0x40400000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}},
		{{0x00B44FD8u, 0x41200001u}, {0x00B44FDCu, 0x41300000u}, {0x00B44FE0u, 0x41400000u}, {0x00B44FE4u, 0x42C80000u}, {0x00B44FE8u, 0x41500000u}, {0x00B44FECu, 0x41600000u}, {0x00B44FF0u, 0x41700000u}, {0x00B44FF4u, 0x43480000u}},
		{0x41200001u, 0x41300000u, 0x41400000u, 0x42C80000u, 0x41500000u, 0x41600000u, 0x41700000u, 0x43480000u, 0xCDCD0008u, 0xCDCD0009u, 0xCDCD000Au, 0xCDCD000Bu},
		"TWWWWTWWWW",
		{0x10000000u, 0x00B44FD0u, 0x00B44FE0u},
		{0xCDCD0000u, 0xCDCD0001u, 0xCDCD0002u, 0xCDCD0003u, 0xCDCD0004u, 0xCDCD0005u, 0xCDCD0006u, 0xCDCD0007u, 0xCDCD0008u, 0xCDCD0009u, 0xCDCD000Au, 0xCDCD000Bu},
	},
};
// clang-format on

// The words where the emulator's FLD m32 keeps a signaling NaN that the CPU
// quiets (measured: the hardware replay gives the hardware value, the
// emulator transcript the other). where: 'I' a transform input word (call * 3
// + component), 'B' a block word (slot * 4 + component); block writes follow
// the block words.
struct HardwareGap {
	int caseIndex;
	char where;
	int word;
	FloatBits emulator;
	FloatBits hardware;
};

const HardwareGap kHardwareGaps[] = {
	// Case 18 (B4, scale 0): w is an SNaN, copied by FLD/FSTP (0x007C919C/0x007C91A2).
	{17, 'B', 3, 0x7F800005u, 0x7FC00005u},
	{17, 'B', 7, 0xFFBFFFFFu, 0xFFFFFFFFu},
	// Case 20 (B3): SNaN positions copied to pV (0x007C916C..0x007C9191) ...
	{19, 'I', 0, 0x7F800001u, 0x7FC00001u},
	{19, 'I', 1, 0xFF800002u, 0xFFC00002u},
	{19, 'I', 6, 0x7FBFFFFFu, 0x7FFFFFFFu},
	// ... and SNaN transform outputs and w copied to the block (0x007C91E8..0x007C920A).
	{19, 'B', 0, 0x7F800003u, 0x7FC00003u},
	{19, 'B', 3, 0x7FA00000u, 0x7FE00000u},
	{19, 'B', 4, 0xFF812345u, 0xFFC12345u},
	// Case 23 (B4): SNaN x and y loaded by FLD, then multiplied by the QNaN
	// scale in registers (0x007C91C0, 0x007C91CC): on the CPU both are quiet
	// and the larger significand wins; in the emulator the register SNaN loses
	// to the QNaN.
	{22, 'B', 0, 0x7FC00001u, 0x7FC00010u},
	{22, 'B', 1, 0x7FC00001u, 0xFFC00020u},
	// Case 24 (B5): SNaN w loaded by FLD (0x007C91DC), divided by the QNaN
	// scale: the same rule.
	{23, 'B', 3, 0x7FC00008u, 0x7FC00010u},
};

const HardwareGap* FindGap(int caseIndex, char where, int word) {
	for (const HardwareGap& gap : kHardwareGaps) {
		if (gap.caseIndex == caseIndex && gap.where == where && gap.word == word) {
			return &gap;
		}
	}
	return nullptr;
}

FloatBits Component(const Vec3Bits& v, int k) {
	return k == 0 ? v.x : k == 1 ? v.y : v.z;
}

FloatBits Component(const Vec4Bits& v, int k) {
	return k == 0 ? v.x : k == 1 ? v.y : k == 2 ? v.z : v.w;
}

// Compares one word: equal to the transcript, or a listed gap where the
// transcript holds the emulator's value and the rebuild the hardware's.
bool WordMatches(int caseIndex, char where, int word, FloatBits rebuild, FloatBits transcript, int& gapsUsed) {
	const HardwareGap* gap = FindGap(caseIndex, where, word);
	if (gap == nullptr) {
		if (rebuild != transcript) {
			std::printf("      case %d %c%d: rebuild %08X, emulator %08X (not a listed gap)\n", caseIndex + 1, where, word,
				rebuild, transcript);
		}
		return rebuild == transcript;
	}
	++gapsUsed;
	const bool ok = transcript == gap->emulator && rebuild == gap->hardware && rebuild != transcript;
	if (!ok) {
		std::printf("      case %d %c%d: rebuild %08X, emulator %08X, gap lists %08X / %08X\n", caseIndex + 1, where, word,
			rebuild, transcript, gap->emulator, gap->hardware);
	}
	return ok;
}

void EmulatorCases() {
	int gapsUsed = 0;
	int index = 0;
	for (const EmulatorCase& c : kEmulatorCases) {
		char title[200];
		std::snprintf(title, sizeof title, "Emulator case %d: %s", index + 1, c.name);
		Section(title);

		// The transcript itself, as far as it is read (re/functions/0x007C9140.md).
		bool traceShape = true;
		for (const TracedCall& call : c.calls) {
			traceShape = traceShape && call.from == kTransformCallReturn && call.args[2] == c.matrix &&
				call.ecx == call.args[1] && call.edx == call.args[0] && call.args[0] >= kEmulatorStackLow &&
				call.args[0] < kEmulatorStackHigh && call.args[1] >= kEmulatorStackLow && call.args[1] < kEmulatorStackHigh;
		}
		Check(traceShape, "transcript: every call returns to 0x007C919A with pM = arg1, pV = ECX, pOut = EDX on the stack");

		// The rebuild on the same inputs, over the case's memory: the flag port
		// reads a record outside the block from the case, one inside it from
		// the block as the stores so far left it.
		logic::Inputs in{
			{c.positions[0], c.positions[1], c.positions[2]},
			{c.lightW[0], c.lightW[1], c.lightW[2]},
			c.scale,
			c.passId,
			c.skipAdjust,
		};
		std::array<FloatBits, 12> block = c.blockBefore;
		std::vector<Vec3Bits> inputs;
		std::vector<std::uint32_t> matrices;
		std::vector<BlockWrite> writes;
		std::vector<int> storedSlots;
		std::string events;
		bool replayAgrees = true;
		logic::UpdateLightPositionConstants(
			in, c.matrix,
			[&](int slot) -> std::uint8_t {
				const std::uint32_t flagAddress = c.records[static_cast<std::size_t>(slot)] + kFlagOffset;
				if (flagAddress < kConstantBlock || flagAddress >= kConstantBlockEnd) {
					return c.flags[slot];
				}
				const std::uint32_t offset = flagAddress - kConstantBlock;
				return static_cast<std::uint8_t>(block[offset / 4u] >> (8u * (offset % 4u)));
			},
			[&](const Vec3Bits& input, std::uint32_t m) {
				const std::size_t call = inputs.size();
				events += 'T';
				inputs.push_back(input);
				matrices.push_back(m);
				return call < 3 ? c.stubOutputs[call] : Vec3Bits{};
			},
			[&](int slot, const Vec4Bits& value) {
				storedSlots.push_back(slot);
				for (int k = 0; k < 4; ++k) {
					const FloatBits bits = Component(value, k);
					events += 'W';
					block[static_cast<std::size_t>(slot * 4 + k)] = bits;
					writes.push_back(BlockWrite{kConstantBlock + 0x10u * static_cast<std::uint32_t>(slot) + 4u * static_cast<std::uint32_t>(k), bits});
				}
			});

		// The hardware replay of every slot the rebuild stored, with the
		// transform output of its call (measured).
		{
			const SlotAdjust adjust = logic::ClassifyAdjust(c.passId, c.skipAdjust);
			for (std::size_t call = 0; call < storedSlots.size() && call < 3; ++call) {
				const auto slot = static_cast<std::size_t>(storedSlots[call]);
				const Vec3Bits& p = c.positions[slot];
				const Vec3Bits hardwareInput{HardwareCopy(p.x), HardwareCopy(p.y), HardwareCopy(p.z)};
				const Vec4Bits hardware = HardwareReplay(c.stubOutputs[call], c.lightW[slot], c.scale, adjust);
				const Vec4Bits model = logic::ComposeSlotConstant(c.stubOutputs[call], c.lightW[slot], c.scale, adjust);
				if (call < inputs.size() && !(inputs[call] == hardwareInput)) {
					replayAgrees = false;
				}
				if (!(hardware == model)) {
					replayAgrees = false;
					PrintVec4("hardware", hardware);
					PrintVec4("model   ", model);
				}
			}
		}
		Check(replayAgrees, "hardware replay (this CPU, fpcw 0x007F): the model's every stored word and transform input");

		Check(inputs.size() == c.calls.size(), "as many transform calls as the original's trace");
		if (events != c.events) {
			std::printf("      rebuild %s, original %s\n", events.c_str(), c.events);
		}
		Check(events == c.events, "transform calls and block writes interleave as in the original's transcript");
		bool sameMatrix = true;
		for (std::uint32_t m : matrices) {
			sameMatrix = sameMatrix && m == c.matrix;
		}
		Check(sameMatrix, "every transform call gets arg1");

		bool inputsMatch = inputs.size() == c.transformInputs.size();
		for (std::size_t call = 0; call < inputs.size() && call < c.transformInputs.size(); ++call) {
			for (int k = 0; k < 3; ++k) {
				inputsMatch = WordMatches(index, 'I', static_cast<int>(call) * 3 + k, Component(inputs[call], k),
					Component(c.transformInputs[call], k), gapsUsed) && inputsMatch;
			}
		}
		Check(inputsMatch, "the transform inputs equal the stub's log (listed hardware gaps aside)");

		bool writesMatch = writes.size() == c.blockWrites.size();
		for (std::size_t i = 0; i < writes.size() && i < c.blockWrites.size(); ++i) {
			const bool address = writes[i].address == c.blockWrites[i].address;
			const int word = static_cast<int>((writes[i].address - kConstantBlock) / 4u);
			const HardwareGap* gap = FindGap(index, 'B', word);
			const bool value = gap == nullptr ? writes[i].value == c.blockWrites[i].value
											  : c.blockWrites[i].value == gap->emulator && writes[i].value == gap->hardware;
			writesMatch = writesMatch && address && value;
		}
		Check(writesMatch, "the block writes: same addresses, same order, same bits (listed hardware gaps aside)");

		bool blockMatches = true;
		for (int k = 0; k < 12; ++k) {
			blockMatches = WordMatches(index, 'B', k, block[static_cast<std::size_t>(k)], c.block[static_cast<std::size_t>(k)], gapsUsed) &&
				blockMatches;
		}
		Check(blockMatches, "the block afterwards, all twelve words (disabled slots keep the sentinel)");
		++index;
	}

	Section("Emulator gate: every listed hardware gap was met exactly once");
	Check(gapsUsed == static_cast<int>(std::size(kHardwareGaps)), "no listed gap left unused");
}

} // namespace

int main() {
	SlotGate();
	AdjustGate();
	X87Rounding();
	ComposeFlows();
	LoopFlows();
	AliasedSlotRecordFlows();
	Layouts();
	EmulatorCases();
	return Failures();
}
