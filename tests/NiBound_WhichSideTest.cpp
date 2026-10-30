// Tests of the rebuild of 0x0047DA70 (proposed NiBound::WhichSide, name
// unverified): a bounding sphere (this, ECX) against one plane (the stack
// argument), returning 2 (wholly on the negative side), 1 (wholly on the
// positive side) or 0 (straddling, or a NaN reached a comparison).
//
// The mapped control flow (re/functions/0x0047DA70.md):
//   B1  0x0047DAA7  FCOMP (-r against d) / TEST AH,0x1 / JNZ   not taken: d <= -r, ordered -> 2
//   B2  0x0047DABD  FCOMPP (r against d) / TEST AH,0x41 / JP   not taken: d >= r, ordered  -> 1
//                                                              taken: r > d or unordered   -> 0
// Each section names the branch outcomes it drives.
//
// Precision. The game's x87 runs at 24-bit precision control, round to
// nearest (measured, knowledge/frame-clock.md). The rebuild models that
// reading exactly where the instructions round (NiBound_WhichSideLogic.h):
// every FMUL/FADDP/FSUB rounds the significand to 24 bits and keeps the
// register's exponent range, each FSTP float rounds to the float format.
// Section "Differential" holds the original's results from the emulator
// (measured, tools/difftest/expected/0x0047DA70.json, Unicorn's x87); the
// sections before it are derived from the instructions and say from which.

#include "Check.h"
#include "engine/render/NiBound_WhichSide.h"
#include "engine/render/NiBound_WhichSideLogic.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>

namespace ws = obng::engine::render::which_side;
using ws::PlaneSide;

namespace {

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void ReturnValues() {
	Section("Return values: MOV EAX,0x2 0x0047DAAB, MOV EAX,0x1 0x0047DABF, XOR EAX,EAX 0x0047DAC7 (read)");
	Check(static_cast<std::int32_t>(PlaneSide::Negative) == 2, "Negative = 2");
	Check(static_cast<std::int32_t>(PlaneSide::Positive) == 1, "Positive = 1");
	Check(static_cast<std::int32_t>(PlaneSide::Straddle) == 0, "Straddle = 0");
}

void BranchB1() {
	Section("B1 0x0047DAA7: not taken (-> 2) exactly when d <= -r and both are ordered (derived: C0 = 1 for -r < d or unordered)");
	Check(ws::IsWhollyNegative(-3.0f, 1.0f), "d < -r: not taken");
	Check(ws::IsWhollyNegative(-1.0f, 1.0f), "d == -r: not taken (equality clears C0)");
	Check(ws::IsWhollyNegative(0.0f, 0.0f), "r = +0, d = +0: -r = -0 == +0, not taken");
	Check(ws::IsWhollyNegative(-0.0f, 0.0f), "r = +0, d = -0: not taken");
	Check(!ws::IsWhollyNegative(0.5f, 1.0f), "-r < d: taken");
	Check(!ws::IsWhollyNegative(kNaN, 1.0f), "d NaN: unordered sets C0, taken");
	Check(!ws::IsWhollyNegative(0.5f, kNaN), "r NaN: unordered, taken");
	Check(ws::IsWhollyNegative(0.5f, -1.0f), "r < 0: d = 0.5 <= -r = 1, not taken");
	Check(!ws::IsWhollyNegative(5.0f, kInf), "r = +inf: -inf < d, taken");
	Check(ws::IsWhollyNegative(-kInf, 1.0f), "d = -inf: not taken");
}

void BranchB2() {
	Section("B2 0x0047DABD: not taken (-> 1) exactly when d >= r and both are ordered (derived: parity of C0|C3)");
	Check(ws::IsWhollyPositive(3.0f, 1.0f), "d > r: C0 = 1, C3 = 0, odd parity, not taken");
	Check(ws::IsWhollyPositive(1.0f, 1.0f), "d == r: C0 = 0, C3 = 1, odd parity, not taken");
	Check(!ws::IsWhollyPositive(0.5f, 1.0f), "r > d: C0 = C3 = 0, even parity, taken");
	Check(!ws::IsWhollyPositive(kNaN, 1.0f), "d NaN: C0 = C3 = 1, even parity, taken");
	Check(!ws::IsWhollyPositive(1.0f, kNaN), "r NaN: taken");
	Check(ws::IsWhollyPositive(kInf, 1.0f), "d = +inf: not taken");
	Check(!ws::IsWhollyPositive(5.0f, kInf), "r = +inf: taken");
}

void Classification() {
	Section("Classify: the flows of the mapping (flows 1..9), B1 first, then B2");
	Check(ws::Classify(-3.0f, 1.0f) == PlaneSide::Negative, "flow 1: d < -r -> 2 (B1 not taken)");
	Check(ws::Classify(-1.0f, 1.0f) == PlaneSide::Negative, "flow 2: d == -r -> 2 (B1 not taken)");
	Check(ws::Classify(0.0f, 0.0f) == PlaneSide::Negative, "flow 2: r = 0, d = +0 -> 2 (the negative test runs first)");
	Check(ws::Classify(-0.0f, 0.0f) == PlaneSide::Negative, "flow 2: r = 0, d = -0 -> 2");
	Check(ws::Classify(0.5f, 1.0f) == PlaneSide::Straddle, "flow 3: -r < d < r -> 0 (B1 taken, B2 taken)");
	Check(ws::Classify(1.0f, 1.0f) == PlaneSide::Positive, "flow 4: d == r -> 1 (B1 taken, B2 not taken)");
	Check(ws::Classify(3.0f, 1.0f) == PlaneSide::Positive, "flow 5: d > r -> 1 (B1 taken, B2 not taken)");
	Check(ws::Classify(kNaN, 1.0f) == PlaneSide::Straddle, "flow 6: d NaN -> 0 (B1 taken, B2 taken)");
	Check(ws::Classify(0.5f, kNaN) == PlaneSide::Straddle, "flow 7: r NaN -> 0");
	Check(ws::Classify(0.5f, -1.0f) == PlaneSide::Negative, "flow 8: r < 0, d <= -r -> 2");
	Check(ws::Classify(2.0f, -1.0f) == PlaneSide::Positive, "flow 8: r < 0, d > -r -> 1 (never 0 for an ordered d)");
	Check(ws::Classify(-0.5f, -1.0f) == PlaneSide::Negative, "flow 8: r < 0, d between r and -r -> 2 (both tests hold, 2 wins)");
	Check(ws::Classify(kNaN, -1.0f) == PlaneSide::Straddle, "flow 8: r < 0, d NaN -> 0");
	Check(ws::Classify(5.0f, kInf) == PlaneSide::Straddle, "flow 9: r = +inf, finite d -> 0");
	Check(ws::Classify(kInf, 1.0f) == PlaneSide::Positive, "flow 9: d = +inf -> 1");
	Check(ws::Classify(-kInf, 1.0f) == PlaneSide::Negative, "flow 9: d = -inf -> 2");
	Check(ws::Classify(kInf, kInf) == PlaneSide::Positive, "d = r = +inf: B1 taken (-inf < inf), B2 not taken (equal) -> 1");
}

void Pc24Rounding() {
	Section("x87 at precision control 24: the significand rounds to 24 bits, the exponent keeps the register's range (SDM 8.1.5.2, read; knowledge/bone-palettes.md)");
	// 1 + 2^-24 is the tie between 1 and 1 + 2^-23: to even.
	Check(ws::RoundToPc24(1.0 + std::ldexp(1.0, -24)) == 1.0, "1 + 2^-24 (tie) -> 1 (even)");
	Check(ws::RoundToPc24(1.0 + 3.0 * std::ldexp(1.0, -24)) == 1.0 + std::ldexp(1.0, -22), "1 + 3*2^-24 (tie) -> 1 + 2^-22 (even)");
	Check(ws::RoundToPc24(1.0 + std::ldexp(1.0, -24) + std::ldexp(1.0, -50)) == 1.0 + std::ldexp(1.0, -23), "just above the tie -> up");
	Check(ws::RoundToPc24(-(1.0 + std::ldexp(1.0, -24) + std::ldexp(1.0, -50))) == -(1.0 + std::ldexp(1.0, -23)), "the sign is kept (round to nearest is symmetric)");
	Check(ws::RoundToPc24(2.0 - std::ldexp(1.0, -24)) == 2.0, "2 - 2^-24 (tie below 2) -> 2: the carry moves into the exponent");
	Check(ws::RoundToPc24(std::ldexp(1.0, 200)) == std::ldexp(1.0, 200), "2^200 stays finite: no float overflow in the register");
	Check(ws::RoundToPc24(std::ldexp(1.0 + std::ldexp(1.0, -22) + std::ldexp(1.0, -46), -128)) == std::ldexp(1.0 + std::ldexp(1.0, -22), -128),
	      "(1+2^-23)^2 * 2^-128 -> (1+2^-22) * 2^-128: rounded as a normal value below FLT_MIN");
	Check(ws::RoundToPc24(0.0) == 0.0 && !std::signbit(ws::RoundToPc24(0.0)), "+0 stays +0");
	Check(std::signbit(ws::RoundToPc24(-0.0)), "-0 stays -0");
	Check(std::isinf(ws::RoundToPc24(std::numeric_limits<double>::infinity())), "+inf passes");
	Check(std::isnan(ws::RoundToPc24(std::numeric_limits<double>::quiet_NaN())), "NaN passes");

	Section("FSTP float at 0x0047DA88 / 0x0047DA93: the second rounding, to the float format");
	Check(Bits(ws::StoreSingle(std::ldexp(1.0, 200))) == 0x7F800000u, "2^200 -> +inf at the store (overflow masked: fpcw 0x007F)");
	Check(Bits(ws::StoreSingle(-std::ldexp(1.0, 200))) == 0xFF800000u, "-2^200 -> -inf at the store");
	Check(Bits(ws::StoreSingle(std::ldexp(1.0 + std::ldexp(1.0, -22), -128))) == 0x00200000u,
	      "(1+2^-22) * 2^-128 is a denormal tie -> 2^-128 (even), bits 0x00200000");
	Check(Bits(ws::StoreSingle(1.0)) == 0x3F800000u, "1 stays 1");

	Section("The double rounding of the original (derived from the two roundings above)");
	const double product = ws::MulPc24(FromBits(0x1F800001u), FromBits(0x1F800001u));
	Check(Bits(ws::StoreSingle(product)) == 0x00200000u,
	      "(1+2^-23)*2^-64 squared: 24-bit register value, then the store -> 2^-128 (a single rounding would give 0x00200001)");
	Check(Bits(FromBits(0x1F800001u) * FromBits(0x1F800001u)) == 0x00200001u, "control: float arithmetic rounds once -> 0x00200001");
}

void Distance() {
	Section("SignedDistance: ((n.y*c.y) + (n.x*c.x)) + n.z*c.z, store, minus the constant, store (C1, C4; read 0x0047DA74..0x0047DA93)");
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{0.0f, 0.0f, 1.0f, 2.0f}, ws::SphereValues{0.0f, 0.0f, 5.0f, 1.0f});
		Check(Bits(d.dotStored) == Bits(5.0f), "first store 0x0047DA88: the dot product 5");
		Check(Bits(d.distanceStored) == Bits(3.0f), "second store 0x0047DA93: 5 - 2 = 3");
	}
	{
		// C2: pseudo.c's (z + x) + y gives 0 here.
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{-1e4f, 1e4f, 1.0f, 0.5f}, ws::SphereValues{1e4f, 1e4f, 1.0f, 0.25f});
		Check(Bits(d.dotStored) == Bits(1.0f), "order: (1e8 + -1e8) + 1 = 1, not pseudo.c's (1 + -1e8) + 1e8 = 0");
		Check(Bits(d.distanceStored) == Bits(0.5f), "minus 0.5 -> 0.5");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{-0x1p100f, 0x1p100f, 1.0f, 0.0f}, ws::SphereValues{0x1p100f, 0x1p100f, 1.0f, 0.5f});
		Check(Bits(d.dotStored) == Bits(1.0f), "2^200 - 2^200 cancels in the register before the store: 1, not NaN");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{0.0f, 0x1p100f, 0.0f, 0.0f}, ws::SphereValues{0.0f, 0x1p100f, 0.0f, 1.0f});
		Check(Bits(d.dotStored) == 0x7F800000u, "2^200 becomes +inf only at the store 0x0047DA88");
		Check(Bits(d.distanceStored) == 0x7F800000u, "inf - 0 = inf");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{0.0f, 0x1p100f, 0.0f, kInf}, ws::SphereValues{0.0f, 0x1p100f, 0.0f, 1.0f});
		Check(std::isnan(d.distanceStored), "stored +inf minus +inf: NaN");
	}
	{
		const ws::DistanceStores d = ws::SignedDistance(ws::PlaneValues{1.0f, 0.0f, 0.0f, FromBits(0x33000001u)}, ws::SphereValues{1.0f, 0.0f, 0.0f, 0.0f});
		Check(Bits(d.distanceStored) == 0x3F7FFFFFu, "1 - (2^-25 + 2^-48) rounds to 1 - 2^-24 at the FSUB 0x0047DA90");
	}
}

void EngineShape() {
	Section("Engine-facing shape: NiBound / NiPlane (layouts asserted in NiBound_WhichSide.h)");
	const ws::engine::NiBound sphere{{0.0f, 0.0f, 5.0f}, 1.0f};
	const ws::engine::NiPlane plane{{0.0f, 0.0f, 1.0f}, 2.0f};
	Check(ws::engine::WhichSide(sphere, plane) == 1, "centre z 5, constant 2, radius 1: d = 3 >= 1 -> 1");
	const ws::engine::NiPlane flipped{{0.0f, 0.0f, -1.0f}, 2.0f};
	Check(ws::engine::WhichSide(sphere, flipped) == 2, "normal flipped: d = -7 <= -1 -> 2");
	Check(ws::engine::kWhichSideAddress == 0x0047DA70u, "the original's address");
}

// The original's results, transcribed from tools/difftest/expected/0x0047DA70.json
// (emulator run 2026-10-30 on re/export/perf-levers/0x0047DA70, cases
// tools/difftest/cases/0x0047DA70.json; measured in Unicorn's x87, not in the
// game). Every case returned to its sentinel with stack_delta +4 (the RET 0x4
// pops the one argument: balanced for this callee-cleans function), no
// emulator error, ECX = 0x00B00000 and EDX = 0x5A5A5A5A unchanged, no write
// outside the emulator's stack (the two FSTP to [ESP+4] are stack writes the
// emulator does not record), no call, and the 32 input bytes read back
// unchanged.
struct EmulatorCase {
	const char* name;
	std::uint32_t sphere[4];  // centre x, y, z, radius at ECX+0x00..+0x0C
	std::uint32_t plane[4];   // normal x, y, z, constant at [ESP+4]+0x00..+0x0C
	std::uint16_t fpcw;
	std::uint32_t eax;        // the original's EAX at return
	int writes;               // recorded writes outside the stack
	int calls;                // traced calls
};

constexpr EmulatorCase kCases[] = {
	{"E1 d -3 < -r -1", {0xC0400000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E2 d -1 == -r", {0xBF800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E3 r +0, d +0", {0, 0, 0, 0}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E4 straddle d 0.5, r 1", {0x3F000000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E5 d == r 1", {0x3F800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E6 d 3 > r 1", {0x40400000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E7 d NaN", {0x7FC00000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E8 r NaN", {0x3F000000u, 0, 0, 0x7FC00000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E9 r -1, d 0.5", {0x3F000000u, 0, 0, 0xBF800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E10 r -1, d 2", {0x40000000u, 0, 0, 0xBF800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E11 r -1, d NaN", {0x7FC00000u, 0, 0, 0xBF800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E12 r +inf, d 5", {0x40A00000u, 0, 0, 0x7F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 0, 0, 0},
	{"E13 d +inf", {0x7F800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 1, 0, 0},
	{"E14 d -inf", {0xFF800000u, 0, 0, 0x3F800000u}, {0x3F800000u, 0, 0, 0}, 0x007F, 2, 0, 0},
	{"E15 constant 4.5, centre x 5, r 0.25", {0x40A00000u, 0, 0, 0x3E800000u}, {0x3F800000u, 0, 0, 0x40900000u}, 0x007F, 1, 0, 0},
	{"E16 summation order", {0x461C4000u, 0x461C4000u, 0x3F800000u, 0x3E800000u}, {0xC61C4000u, 0x461C4000u, 0x3F800000u, 0x3F000000u}, 0x007F, 1, 0, 0},
	{"E17 PC 24 tie at the first FADDP", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x007F, 0, 0, 0},
	{"E19 extended exponent in the register", {0x71800000u, 0x71800000u, 0x3F800000u, 0x3F000000u}, {0xF1800000u, 0x71800000u, 0x3F800000u, 0}, 0x007F, 1, 0, 0},
	{"E20 +inf at the store", {0, 0x71800000u, 0, 0x3F800000u}, {0, 0x71800000u, 0, 0}, 0x007F, 1, 0, 0},
	{"E21 -inf at the store", {0, 0x71800000u, 0, 0x3F800000u}, {0, 0xF1800000u, 0, 0}, 0x007F, 2, 0, 0},
	{"E22 +inf minus constant +inf", {0, 0x71800000u, 0, 0x3F800000u}, {0, 0x71800000u, 0, 0x7F800000u}, 0x007F, 0, 0, 0},
	{"E23 double rounding to a denormal at the store", {0x1F800001u, 0, 0, 0x00000001u}, {0x1F800001u, 0, 0, 0x00200000u}, 0x007F, 0, 0, 0},
	{"E24 the FSUB rounds below a tie", {0x3F800000u, 0, 0, 0x3F7FFFFFu}, {0x3F800000u, 0, 0, 0x33000001u}, 0x007F, 1, 0, 0},
};

// E18: E17's inputs at fpcw 0x037F (64-bit precision). The original returned
// 1 there (1 + 2^-24 + 2^-60 stays exact in the register, the store rounds it
// up). The game runs at 24 bits (measured, knowledge/frame-clock.md) and the
// rebuild models only that, so it gives E17's 0 for these inputs: the
// difference is the documented precision dependence (C11), and E17/E18
// together show that the emulator honours the precision control.
constexpr EmulatorCase kPc64Case =
	{"E18 E17 at PC 64", {0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, 0x037F, 1, 0, 0};

std::int32_t RunRebuild(const EmulatorCase& c) {
	const ws::engine::NiBound sphere{{FromBits(c.sphere[0]), FromBits(c.sphere[1]), FromBits(c.sphere[2])}, FromBits(c.sphere[3])};
	const ws::engine::NiPlane plane{{FromBits(c.plane[0]), FromBits(c.plane[1]), FromBits(c.plane[2])}, FromBits(c.plane[3])};
	return ws::engine::WhichSide(sphere, plane);
}

void Differential() {
	Section("Differential (emulator gate, layer 1): the rebuild against the original's EAX, case by case (measured)");
	char line[160];
	for (const EmulatorCase& c : kCases) {
		const std::int32_t rebuilt = RunRebuild(c);
		std::snprintf(line, sizeof(line), "%s: original EAX %u, rebuild %d", c.name, static_cast<unsigned>(c.eax), static_cast<int>(rebuilt));
		Check(c.fpcw == 0x007F && static_cast<std::uint32_t>(rebuilt) == c.eax, line);
		std::snprintf(line, sizeof(line), "%s: no write outside the stack, no call (the rebuild has no ports)", c.name);
		Check(c.writes == 0 && c.calls == 0, line);
	}
	Check(std::size(kCases) == 23, "23 cases at fpcw 0x007F, E18 apart");

	Section("E18: the original at PC 64 differs from the PC 24 reading the rebuild models");
	Check(kPc64Case.eax == 1, "the original at fpcw 0x037F returned 1");
	Check(RunRebuild(kPc64Case) == 0, "the rebuild (PC 24) returns E17's 0 for the same inputs");
}

} // namespace

int main() {
	ReturnValues();
	BranchB1();
	BranchB2();
	Classification();
	Pc24Rounding();
	Distance();
	EngineShape();
	Differential();
	return Failures();
}
