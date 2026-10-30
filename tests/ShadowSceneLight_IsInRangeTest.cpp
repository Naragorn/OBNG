// Tests of the rebuild of 0x007D2EA0 (proposed ShadowSceneLight::IsInRange,
// name unverified): is one scene object within reach of one shadow light?
//
// The mapped control flow (re/functions/0x007D2EA0.md):
//   B1  0x007D2EAD  source light null                         -> false, nothing called
//   B2  0x007D2EC7  source GetRTTI == 0x00B3FCFC (directional) -> true, arg1 not read
//   B3  0x007D2EFF  this+0x14C != 0                           -> plane path, else distance path
//   B4  0x007D2F1F  bit i of this+0x1B0 clear                 -> plane i skipped
//   B5  0x007D2F2E  WhichSide == 2                            -> result false, loop goes on
//   B6  0x007D2F3A  WhichSide == 1                            -> bit i cleared; any other value: nothing
//   B7  0x007D2F4D  i < 6                                     -> next plane
//   B8  0x007D2FD6  range > |light - centre| - radius         -> true; equal or NaN -> false
// Each section names the flows it drives.
//
// Two kinds of evidence, kept apart:
//   - "read": expectations taken from the instructions (the flow table);
//   - "measured, emulator": the original's bytes run in tools/difftest/
//     emulate.py on tools/difftest/cases/0x007D2EA0.json (31 cases, run
//     2026-10-30, fpcw 0x007F; every case returned to its sentinel with no
//     error and stack_delta +8, which is the RET 0x8 popping the two
//     arguments - emulate.py measures from the first argument). The result
//     tools/difftest/expected/0x007D2EA0.json is transcribed literally in
//     kEmulatorCases: EAX, the traced calls (GetRTTI's count, WhichSide's
//     plane addresses as offsets from this, the square root's dx, dy, dz
//     from its stack), every mask write in order, the mask afterwards, and the
//     s and q the square-root stub logged. The rebuild runs the same inputs
//     and must produce the same.
//
// What the cases cannot see: the stack copy of the bound is a stack address
// (ECX of every WhichSide call, 0x7F00FEE4) and stack writes are not
// recorded, so the copy's content is checked only through the cases that run
// the original WhichSide bytes (C14..C16) against the WhichSide rebuild. EAX
// above AL is left over (the RTTI pointer, arg1, WhichSide's return, the
// FNSTSW word) and both callers test AL only (0x007C61C7, 0x007D5A10, read):
// it is transcribed, only AL is compared. In 0x007D2EA0.json the square root
// is a stub that runs FSQRT (ST0 in, ST0 out, like 0x00982C30) under the
// case's control word, 0x007F. The CRT's 0x00982C30 runs FSQRT at the
// caller's precision and round to nearest whatever the caller's rounding
// control (read 2026-10-30 in the reference executable, see
// ShadowSceneLight_IsInRangeLogic.h), so at 0x007F the stub and the CRT agree
// (derived), and under the other modes they do not: the cases of
// 0x007D2EA0-rounding.json therefore run the CRT's own bytes (taken from the
// reference executable by emulate.py's "image" ranges) through a logging
// wrapper. The rebuild calls a port; this test gives it FSQRT at 24 bits,
// round to nearest - the CRT's behaviour - in every mode.
//
// Rounding control (the cases of 0x007D2EA0-rounding.json, kRoundingCases):
// every distance case of 0x007D2EA0.json but D6, the garbage-sized bound of
// the in-game mismatch, four distance cases chosen for the modes (ties,
// signs, an exact zero difference, a rounded sum of squares) and one
// plane-path case with WhichSide's own bytes, each at fpcw 0x007F, 0x047F,
// 0x087F and 0x0C7F - 80 runs. D6 is left out there: its NaN sum takes the
// CRT's error path 0x00991AB7, which calls deeper into the CRT than the
// image ranges cover.

#include "Check.h"
#include "engine/render/ShadowSceneLight_IsInRange.h"
#include "engine/render/ShadowSceneLight_IsInRangeLogic.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace logic = obng::engine::render::shadow_scene_light_is_in_range;
namespace engine_ns = obng::engine::render::shadow_scene_light_is_in_range::engine;
namespace ws = obng::engine::render::which_side;
using logic::PlaneOutcome;

namespace {

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// The square root of the emulator cases: FSQRT at precision control 24,
// round to nearest - the stub of 0x007D2EA0.json at fpcw 0x007F, and the
// CRT's 0x00982C30 under every caller mode (it loads round to nearest for
// its FSQRT; read). One rounding of the exact root to 24 bits: the double
// root (correctly rounded) rounded again to 24 bits is that, because the
// root of a float is never within half a double ulp of a 24-bit midpoint
// without being exact (derived: s - m^2 would be a non-zero multiple of
// ulp(m^2) smaller than it).
double StubSquareRoot(float s) {
	return ws::RoundToPc24(std::sqrt(static_cast<double>(s)), ws::X87Rounding::Nearest);
}

constexpr ws::X87Rounding kNearest = ws::X87Rounding::Nearest;
constexpr ws::X87Rounding kDown = ws::X87Rounding::Down;
constexpr ws::X87Rounding kUp = ws::X87Rounding::Up;
constexpr ws::X87Rounding kZero = ws::X87Rounding::Zero;

// ---- Flows of the decisions -----------------------------------------------------

void Decisions() {
	Section("B2 IsDirectional: the address compare with 0x00B3FCFC (read 0x007D2EC2)");
	Check(logic::kNiRttiNiDirectionalLight == 0x00B3FCFCu, "NiRTTI_NiDirectionalLight 0x00B3FCFC (xOBSE NiRTTI_1_2_416.inl:145)");
	Check(logic::IsDirectional(0x00B3FCFCu), "0x00B3FCFC: directional");
	Check(!logic::IsDirectional(0x00B3FD80u), "0x00B3FD80 (NiRTTI_NiPointLight): not directional");
	Check(!logic::IsDirectional(0x00000000u), "a null RTTI: not directional");

	Section("B3 UsesPlanePath: any non-zero dword at +0x14C (read 0x007D2ED3/0x007D2EFF)");
	Check(!logic::UsesPlanePath(0), "0: distance path");
	Check(logic::UsesPlanePath(1), "1: plane path (never dereferenced)");
	Check(logic::UsesPlanePath(0x80000000u), "0x80000000: plane path");

	Section("B4 IsPlaneActive: TEST mask, 1 << i (read 0x007D2F10..0x007D2F1F)");
	Check(logic::PlaneBit(0) == 0x01u && logic::PlaneBit(5) == 0x20u, "bits 0x01 .. 0x20");
	Check(logic::IsPlaneActive(0x3F, 0) && logic::IsPlaneActive(0x3F, 5), "0x3F: planes 0 and 5 active");
	Check(!logic::IsPlaneActive(0x2A, 0) && logic::IsPlaneActive(0x2A, 1), "0x2A: plane 0 off, plane 1 on");
	Check(!logic::IsPlaneActive(0xFFFFFFC0u, 5), "bits 6..31 do not make plane 5 active");

	Section("B5/B6 ClassifySide: full-dword compares with 2, then 1 (read 0x007D2F2B..0x007D2F3A)");
	Check(logic::ClassifySide(2) == PlaneOutcome::OutOfRange, "2: out of range");
	Check(logic::ClassifySide(1) == PlaneOutcome::Retire, "1: retire the plane");
	Check(logic::ClassifySide(0) == PlaneOutcome::Keep, "0: keep");
	Check(logic::ClassifySide(3) == PlaneOutcome::Keep, "3: keep");
	Check(logic::ClassifySide(-1) == PlaneOutcome::Keep, "-1: keep");
	Check(logic::ClassifySide(0x102) == PlaneOutcome::Keep, "0x102 (low byte 2): keep");
	Check(logic::ClassifySide(0x101) == PlaneOutcome::Keep, "0x101 (low byte 1): keep");
	Check(logic::ClassifySide(static_cast<std::int32_t>(0x80000001u)) == PlaneOutcome::Keep, "0x80000001: keep");
}

// ---- The plane loop over ports ----------------------------------------------------

struct LoopRun {
	bool inRange;
	std::uint32_t mask;
	std::vector<unsigned> tested;
	std::vector<std::uint32_t> writes;
	int maskReads;
};

LoopRun RunLoop(std::uint32_t mask, const std::vector<std::int32_t>& sides) {
	LoopRun run{true, mask, {}, {}, 0};
	run.inRange = logic::TestPlanes(
		[&] {
			++run.maskReads;
			return run.mask;
		},
		[&](std::uint32_t value) {
			run.writes.push_back(value);
			run.mask = value;
		},
		[&](unsigned plane) {
			const std::size_t call = run.tested.size();
			run.tested.push_back(plane);
			return call < sides.size() ? sides[call] : 0;
		});
	return run;
}

void PlaneLoopFlows() {
	Section("B4 B7: mask 0 - six mask reads, no plane tested, true (read)");
	const LoopRun none = RunLoop(0, {});
	Check(none.inRange && none.tested.empty() && none.writes.empty(), "true, nothing tested, nothing written");
	Check(none.maskReads == 6, "the mask is read once per plane (C5)");

	Section("B6 B7: every plane 1 - bits cleared one by one, ascending");
	const LoopRun ones = RunLoop(0x3F, {1, 1, 1, 1, 1, 1});
	Check(ones.inRange, "true");
	Check(ones.tested == std::vector<unsigned>{0, 1, 2, 3, 4, 5}, "planes 0..5 in order");
	Check(ones.writes == std::vector<std::uint32_t>{0x3E, 0x3C, 0x38, 0x30, 0x20, 0x00}, "writes 0x3E 0x3C 0x38 0x30 0x20 0x00");
	Check(ones.maskReads == 12, "each retiring plane reads the mask twice (TEST, then AND)");

	Section("B5: every plane 2 - false, no write");
	const LoopRun twos = RunLoop(0x3F, {2, 2, 2, 2, 2, 2});
	Check(!twos.inRange && twos.tested.size() == 6 && twos.writes.empty(), "false, six tests, the mask kept");

	Section("B5 then B6: no early exit - a 2 on plane 0 still lets later 1s retire their planes (C6)");
	const LoopRun mixed = RunLoop(0x3F, {2, 1, 0, 1, 2, 1});
	Check(!mixed.inRange, "false");
	Check(mixed.tested.size() == 6, "all six planes tested");
	Check(mixed.writes == std::vector<std::uint32_t>{0x3D, 0x35, 0x15}, "bits 1, 3, 5 cleared: 0x3D 0x35 0x15");

	Section("B4: a mask with gaps tests only its set bits");
	const LoopRun gaps = RunLoop(0x2A, {1, 2, 1});
	Check(gaps.tested == std::vector<unsigned>{1, 3, 5}, "planes 1, 3, 5");
	Check(!gaps.inRange && gaps.mask == 0x08u, "false, mask 0x08");

	Section("B7: bits above 5 are neither tested nor cleared");
	const LoopRun high = RunLoop(0xFFFFFFFFu, {1, 1, 1, 1, 1, 1});
	Check(high.inRange && high.mask == 0xFFFFFFC0u, "true, mask 0xFFFFFFC0");

	Section("B6 not taken: other values change nothing");
	const LoopRun others = RunLoop(0x3F, {3, -1, 0x102, 0x101, static_cast<std::int32_t>(0x80000001u), 0});
	Check(others.inRange && others.mask == 0x3Fu && others.writes.empty(), "true, mask 0x3F, no write");

	Section("C5: the mask is re-read for every plane - a change made by a port is seen by the next plane");
	LoopRun reread{true, 0x3F, {}, {}, 0};
	reread.inRange = logic::TestPlanes([&] { return reread.mask; },
		[&](std::uint32_t value) { reread.mask = value; },
		[&](unsigned plane) {
			reread.tested.push_back(plane);
			if (plane == 0) {
				reread.mask = 0x01;  // as if the plane test had cleared planes 1..5
			}
			return 0;
		});
	Check(reread.tested == std::vector<unsigned>{0}, "planes 1..5 are skipped after the change");
}

// ---- The distance path's arithmetic ------------------------------------------------

void DistanceArithmetic() {
	Section("DifferencePc24 at nearest: FSUB then FSTP float (read 0x007D2F5D..0x007D2F83)");
	Check(logic::DifferencePc24(4.0f, 1.5f, kNearest) == 2.5f, "4 - 1.5 = 2.5");
	Check(logic::DifferencePc24(5.0f, -2.25f, kNearest) == 7.25f, "5 - (-2.25) = 7.25");
	Check(Bits(logic::DifferencePc24(FromBits(0x4B800000u), -1.0f, kNearest)) == 0x4B800000u, "2^24 - (-1): the tie rounds to even, 2^24");
	Check(std::isnan(logic::DifferencePc24(kNaN, 0.0f, kNearest)), "NaN - 0: NaN");

	Section("SumOfSquaresPc24 at nearest: (dy*dy + dx*dx) + dz*dz, each step at 24 bits (read 0x007D2F87..0x007D2FA3)");
	Check(logic::SumOfSquaresPc24(3.0f, 4.0f, 0.0f, kNearest) == 25.0f, "3, 4, 0: 25");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x3F800800u), FromBits(0x39800000u), 0.0f, kNearest)) == 0x3F801000u,
		"dx 1+2^-12, dy 2^-12: the product rounds to 24 bits first, s = 0x3F801000 (64 bits would store 0x3F801001)");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x39800000u), 1.0f, FromBits(0x39800000u), kNearest)) == 0x3F800000u,
		"dx dz 2^-12, dy 1: (1 + 2^-24) rounds to 1, then + 2^-24 again: 1.0");
	Check(Bits(logic::SumOfSquaresPc24(1.0f, FromBits(0x39800000u), FromBits(0x39800000u), kNearest)) == 0x3F800000u,
		"dx 1, dy dz 2^-12: (2^-24 + 1) rounds to 1 first: 1.0, not 0x3F800001");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x1C2F16ABu), 0.0f, 0.0f, kNearest)) == 0x000000F0u,
		"dx 0x1C2F16AB: rounded to 24 bits, then to a subnormal float: 0xF0");
	const float dx = FromBits(0x1C2F16ABu);
	Check(Bits(dx * dx) == 0x000000EFu, "a plain float product rounds once to 0xEF - the rebuild must not use it");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x5F800000u), 0.0f, 0.0f, kNearest)) == 0x7F800000u,
		"dx 2^64: 2^128 in the register, +inf after the store");
	Check(std::isnan(logic::SumOfSquaresPc24(kNaN, 4.0f, 0.0f, kNearest)), "a NaN difference gives a NaN sum");

	Section("Distance at nearest: the stores in order, the root through the port (read 0x007D2FA7..0x007D2FC1)");
	std::vector<std::uint32_t> rootInputs;
	const logic::DistanceTerms terms = logic::Distance(logic::PointValues{4.0f, 5.0f, 6.0f},
		logic::BoundValues{{1.5f, -2.25f, 3.0f}, 0.5f}, kNearest, [&](float s) {
			rootInputs.push_back(Bits(s));
			return StubSquareRoot(s);
		});
	Check(terms.dx == 2.5f && terms.dy == 7.25f && terms.dz == 3.0f, "dx 2.5, dy 7.25, dz 3");
	Check(terms.sumOfSquares == 67.8125f, "s = 67.8125");
	Check(rootInputs == std::vector<std::uint32_t>{0x4287A000u}, "the root port called once, with s");
	Check(Bits(terms.distance) == 0x4103C1E2u, "q = 0x4103C1E2 (measured, emulator D14)");
	Check(terms.gap == FromBits(0x4103C1E2u) - 0.5f, "d = q - 0.5");
	const logic::DistanceTerms rounded = logic::Distance(logic::PointValues{FromBits(0x4B7FFFFFu), 0.0f, 0.0f},
		logic::BoundValues{{0.0f, 0.0f, 0.0f}, -0.5f}, kNearest, StubSquareRoot);
	Check(Bits(rounded.distance) == 0x4B7FFFFFu, "q = 2^24 - 1");
	Check(Bits(rounded.gap) == 0x4B800000u, "q - (-0.5) = 2^24 - 0.5, a tie: rounds to even 2^24");


	Section("DifferencePc24 per mode: the FSUB's 24-bit rounding and the store under the caller's mode (emulator M1, M2, M3)");
	const float two24 = FromBits(0x4B800000u);  // 2^24: 2^24 + 1 is a tie between 2^24 and 2^24 + 2
	Check(Bits(logic::DifferencePc24(two24, -1.0f, kNearest)) == 0x4B800000u && Bits(logic::DifferencePc24(two24, -1.0f, kZero)) == 0x4B800000u &&
			Bits(logic::DifferencePc24(two24, -1.0f, kDown)) == 0x4B800000u,
		"2^24 - (-1): nearest (to even), truncate and down give 2^24");
	Check(Bits(logic::DifferencePc24(two24, -1.0f, kUp)) == 0x4B800001u, "up gives 2^24 + 2");
	Check(Bits(logic::DifferencePc24(-two24, 1.0f, kDown)) == 0xCB800001u && Bits(logic::DifferencePc24(-two24, 1.0f, kUp)) == 0xCB800000u &&
			Bits(logic::DifferencePc24(-two24, 1.0f, kZero)) == 0xCB800000u,
		"-2^24 - 1: down gives -(2^24 + 2), up and truncate -2^24");
	Check(Bits(logic::DifferencePc24(5.0f, 5.0f, kDown)) == 0x80000000u, "x - x under down: -0 (IEEE 754 6.3)");
	Check(Bits(logic::DifferencePc24(5.0f, 5.0f, kNearest)) == 0u && Bits(logic::DifferencePc24(5.0f, 5.0f, kUp)) == 0u &&
			Bits(logic::DifferencePc24(5.0f, 5.0f, kZero)) == 0u,
		"x - x otherwise: +0");

	Section("SumOfSquaresPc24 per mode: the overflow at the store, the subnormal store, the rounded products (emulator G1, D10, D12, M4)");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x5F800000u), 0.0f, 0.0f, kZero)) == 0x7F7FFFFFu &&
			Bits(logic::SumOfSquaresPc24(FromBits(0x5F800000u), 0.0f, 0.0f, kDown)) == 0x7F7FFFFFu,
		"dx 2^64: 2^128 in the register stores as FLT_MAX under truncate and down");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x5F800000u), 0.0f, 0.0f, kUp)) == 0x7F800000u, "and as +inf under up");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x1C2F16ABu), 0.0f, 0.0f, kZero)) == 0x000000EFu &&
			Bits(logic::SumOfSquaresPc24(FromBits(0x1C2F16ABu), 0.0f, 0.0f, kDown)) == 0x000000EFu &&
			Bits(logic::SumOfSquaresPc24(FromBits(0x1C2F16ABu), 0.0f, 0.0f, kUp)) == 0x000000F0u,
		"dx 0x1C2F16AB: the subnormal store gives 0xEF under truncate and down, 0xF0 under up and nearest");
	Check(Bits(logic::SumOfSquaresPc24(FromBits(0x2B800000u), FromBits(0x3F800001u), 0.0f, kUp)) == 0x3F800004u &&
			Bits(logic::SumOfSquaresPc24(FromBits(0x2B800000u), FromBits(0x3F800001u), 0.0f, kNearest)) == 0x3F800002u,
		"dy 1 + 2^-23, dx 2^-40: (1 + 2^-22 + 2^-46) + 2^-80 is 1 + 2^-21 under up, 1 + 2^-22 at nearest");
	{
		const float dx = FromBits(0x7D935C0Eu);  // G1 under truncate
		Check(Bits(logic::SumOfSquaresPc24(dx, FromBits(0x7D935C13u), FromBits(0x7EBFCBA9u), kZero)) == 0x7F7FFFFFu,
			"G1's differences: the sum of squares saturates to FLT_MAX under truncate (the in-game mismatch)");
		Check(Bits(logic::SumOfSquaresPc24(FromBits(0x7D935C0Fu), FromBits(0x7D935C13u), FromBits(0x7EBFCBA9u), kNearest)) == 0x7F800000u,
			"and overflows to +inf at nearest");
	}

	Section("Distance per mode: the same inputs answer differently (emulator D11, G1)");
	{
		const logic::PointValues light{FromBits(0x4B7FFFFFu), 0.0f, 0.0f};
		const logic::BoundValues bound{{0.0f, 0.0f, 0.0f}, -0.5f};
		Check(Bits(logic::Distance(light, bound, kNearest, StubSquareRoot).gap) == 0x4B800000u, "D11 nearest: q + 0.5 ties to 2^24");
		Check(Bits(logic::Distance(light, bound, kZero, StubSquareRoot).gap) == 0x4B7FFFFFu, "D11 truncate: 2^24 - 1");
		Check(Bits(logic::Distance(light, bound, kUp, StubSquareRoot).gap) == 0x4B800000u, "D11 up: 2^24");
		Check(Bits(logic::Distance(light, bound, kDown, StubSquareRoot).gap) == 0x4B7FFFFFu, "D11 down: 2^24 - 1");
		const logic::PointValues g1Light{FromBits(0xC3F5B6A8u), FromBits(0x4486928Fu), FromBits(0x4401EB54u)};
		const logic::BoundValues g1Bound{{FromBits(0xFD935C0Fu), FromBits(0xFD935C13u), FromBits(0xFEBFCBA9u)}, FromBits(0x5FDFFFF8u)};
		const float range = FromBits(0x43800000u);
		Check(!logic::RangeExceeds(range, logic::Distance(g1Light, g1Bound, kNearest, StubSquareRoot).gap), "G1 nearest: gap +inf, out of range");
		Check(logic::RangeExceeds(range, logic::Distance(g1Light, g1Bound, kZero, StubSquareRoot).gap),
			"G1 truncate: q 0x5F7FFFFF - r 0x5FDFFFF8 < 256, in range (the original's 108 geometries)");
	}

	Section("B8 RangeExceeds: range > d, false on equality and NaN (derived from FCOMPP / TEST AH,0x41, C11)");
	Check(logic::RangeExceeds(10.0f, 4.0f), "10 > 4: true");
	Check(!logic::RangeExceeds(4.0f, 4.0f), "4 == 4: false");
	Check(logic::RangeExceeds(FromBits(0x40800001u), 4.0f), "one ulp above: true");
	Check(!logic::RangeExceeds(FromBits(0x407FFFFFu), 4.0f), "one ulp below: false");
	Check(!logic::RangeExceeds(kNaN, 4.0f), "range NaN: false");
	Check(!logic::RangeExceeds(10.0f, kNaN), "d NaN: false");
	Check(!logic::RangeExceeds(kInf, kInf), "+inf == +inf: false");
	Check(logic::RangeExceeds(0.0f, -kInf), "0 > -inf: true");
	Check(logic::RangeExceeds(-0.0f, -1.0f) && !logic::RangeExceeds(-0.0f, 0.0f), "-0 > -1, -0 == +0");
}

// ---- The engine-facing rebuild over views --------------------------------------------

struct RttiStub {
	std::uint32_t result;
	int calls;
	const engine_ns::NiLightView* self;
};
RttiStub g_rtti{};

const void* __thiscall FakeGetRtti(const engine_ns::NiLightView* self) {
	++g_rtti.calls;
	g_rtti.self = self;
	return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_rtti.result));
}

const engine_ns::NiObjectVtableView kFakeVtable{nullptr, &FakeGetRtti};

// Port calls in order: 'P' + plane index for WhichSide, 'R' for the root.
struct PortTrace {
	std::string events;
	std::vector<std::uint32_t> planeOffsets;  // plane address - this
	std::vector<std::uint32_t> rootInputs;
	std::vector<std::uint32_t> rootOutputs;  // as stored, float bits
	std::vector<engine_ns::NiBound> bounds;
	std::vector<std::uint32_t> masksSeen;  // this+0x1B0 when each plane test starts
};

struct Scene {
	engine_ns::ShadowSceneLightView light{};
	engine_ns::NiAVObjectView object{};
	engine_ns::NiLightView source{};
};

Scene MakeScene(std::uint32_t planeSource, std::uint32_t mask) {
	Scene scene;
	scene.light.planeSource = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(planeSource));
	scene.light.activePlanes = mask;
	scene.object.worldBound = engine_ns::NiBound{{1.5f, -2.25f, 3.0f}, 0.5f};
	scene.source.vtable = &kFakeVtable;
	scene.source.worldTranslate = engine_ns::NiPoint3{4.0f, 5.0f, 6.0f};
	scene.source.range = 100.0f;
	return scene;
}

bool RunEngine(Scene& scene, const engine_ns::NiAVObjectView* object, const engine_ns::NiLightView* source,
	std::uint32_t rtti, const std::vector<std::int32_t>& sides, bool realWhichSide, PortTrace& trace, ws::X87Rounding mode) {
	g_rtti = RttiStub{rtti, 0, nullptr};
	std::size_t call = 0;
	return engine_ns::IsInRange(
		scene.light, object, source, mode,
		[&](const engine_ns::NiBound& bound, const engine_ns::NiPlane& plane) -> std::int32_t {
			const auto offset = static_cast<std::uint32_t>(reinterpret_cast<const std::uint8_t*>(&plane) -
				reinterpret_cast<const std::uint8_t*>(&scene.light));
			trace.events += 'P';
			trace.events += static_cast<char>('0' + (offset - 0x150) / 0x10);
			trace.planeOffsets.push_back(offset);
			trace.bounds.push_back(bound);
			trace.masksSeen.push_back(scene.light.activePlanes);
			if (realWhichSide) {
				return engine_ns::WhichSidePort{mode}(bound, plane);
			}
			const std::size_t index = call < sides.size() ? call : sides.size() - 1;  // the stub's last value repeats
			++call;
			return sides.empty() ? 0 : sides[index];
		},
		[&](float s) {
			trace.events += 'R';
			trace.rootInputs.push_back(Bits(s));
			const double root = StubSquareRoot(s);
			trace.rootOutputs.push_back(Bits(static_cast<float>(root)));
			return root;
		});
}

void EngineFlows() {
	Section("B1: a null source returns false before the virtual call, the object is not read");
	{
		Scene scene = MakeScene(0x12345678u, 0x3F);
		PortTrace trace;
		const bool result = RunEngine(scene, nullptr, nullptr, 0x00B3FD80u, {}, false, trace, kNearest);
		Check(!result, "false");
		Check(g_rtti.calls == 0 && trace.events.empty(), "no GetRTTI, no WhichSide, no root");
		Check(scene.light.activePlanes == 0x3Fu, "mask kept");
	}

	Section("B2: directional - true after one GetRTTI on the source, the object (null here) not read");
	for (const std::uint32_t planeSource : {0x12345678u, 0u}) {
		Scene scene = MakeScene(planeSource, 0x3F);
		PortTrace trace;
		const bool result = RunEngine(scene, nullptr, &scene.source, 0x00B3FCFCu, {}, false, trace, kNearest);
		Check(result, planeSource != 0 ? "plane source set: true" : "plane source 0: true");
		Check(g_rtti.calls == 1 && g_rtti.self == &scene.source, "one GetRTTI with this = the source light");
		Check(trace.events.empty() && scene.light.activePlanes == 0x3Fu, "no plane test, no root, mask kept");
	}

	Section("B3 B4..B7: plane path on the views - planes at +0x150 + 16i, the bound copy, the mask in place");
	{
		Scene scene = MakeScene(0x12345678u, 0x2A);
		PortTrace trace;
		const bool result = RunEngine(scene, &scene.object, &scene.source, 0x00B3FD80u, {1, 2, 1}, false, trace, kNearest);
		Check(!result, "false (plane 3 answered 2)");
		Check(trace.events == "P1P3P5", "planes 1, 3, 5, no root");
		Check(trace.planeOffsets == std::vector<std::uint32_t>{0x160, 0x180, 0x1A0}, "plane addresses this+0x160, +0x180, +0x1A0");
		Check(scene.light.activePlanes == 0x08u, "mask 0x08 written into the view");
		const engine_ns::NiBound& bound = trace.bounds.front();
		Check(bound.center.x == 1.5f && bound.center.y == -2.25f && bound.center.z == 3.0f && bound.radius == 0.5f,
			"WhichSide gets the object's bound +0x20..+0x2C");
		Check(&trace.bounds.front() != &scene.object.worldBound, "as a copy (C3)");
	}

	Section("B3 B8: distance path on the views - one root, the mask untouched");
	{
		Scene scene = MakeScene(0, 0x3F);
		scene.source.range = 7.735f;
		PortTrace trace;
		Check(RunEngine(scene, &scene.object, &scene.source, 0x00B3FD80u, {}, false, trace, kNearest), "range 7.735 > d: true");
		Check(trace.events == "R" && trace.rootInputs == std::vector<std::uint32_t>{0x4287A000u}, "one root of s = 67.8125");
		Check(scene.light.activePlanes == 0x3Fu, "mask untouched");
		scene.source.range = 7.734f;
		PortTrace second;
		Check(!RunEngine(scene, &scene.object, &scene.source, 0x00B3FD80u, {}, false, second, kNearest), "range 7.734 < d: false");
	}
}

void Layouts() {
	Section("Engine shape: addresses and views (read; static_asserts in the header)");
	Check(engine_ns::kIsInRangeAddress == 0x007D2EA0u, "the function at 0x007D2EA0");
	Check(offsetof(engine_ns::ShadowSceneLightView, planeSource) == 0x14C, "plane source at +0x14C");
	Check(offsetof(engine_ns::ShadowSceneLightView, planes) == 0x150, "planes at +0x150");
	Check(offsetof(engine_ns::ShadowSceneLightView, activePlanes) == 0x1B0, "mask at +0x1B0");
	Check(offsetof(engine_ns::NiAVObjectView, worldBound) == 0x20, "object bound at +0x20");
	Check(offsetof(engine_ns::NiLightView, worldTranslate) == 0x88 && offsetof(engine_ns::NiLightView, range) == 0xF8,
		"light position at +0x88, range at +0xF8");
	Check(offsetof(engine_ns::NiObjectVtableView, getRtti) == 0x04, "GetRTTI at vtable +0x04");
}

// ---- Differential gate, layer 1: the original in the emulator ------------------------

struct EmulatorCase {
	const char* name;
	// Inputs (tools/difftest/cases/0x007D2EA0.json).
	bool hasSource;               // arg2 = 0x10002000 or 0
	bool hasObject;               // arg1 = 0x10001000 or 0 (unmapped)
	std::uint32_t rtti;           // what GetRTTI (vtable +4) returns
	std::uint32_t planeSource;    // this+0x14C
	std::uint32_t mask;           // this+0x1B0
	std::uint32_t planes[6][4];   // this+0x150..+0x1AF
	std::uint32_t bound[4];       // object+0x20..+0x2C
	std::uint32_t lightPos[3];    // source+0x88..+0x90
	std::uint32_t range;          // source+0xF8
	bool realWhichSide;           // 0x0047DA70 ran the original bytes
	std::vector<std::uint32_t> sides;  // else: the per-call stub's returns, the last repeats
	// The original's result (tools/difftest/expected/0x007D2EA0.json).
	std::uint32_t eax;
	int rttiCalls;                // calls of the GetRTTI stub, each from 0x007D2EC2 with ECX = the source
	std::vector<std::uint32_t> planeOffsets;  // WhichSide's stack argument - this, each from 0x007D2F2B
	std::vector<std::uint32_t> maskWrites;    // every write to this+0x1B0, each at 0x007D2F3E
	std::uint32_t maskAfter;      // readback of this+0x1B0
	bool rootCalled;              // the square-root stub, from 0x007D2FB0
	std::uint32_t differences[3]; // dx, dy, dz: the stub's stack args[2..4] (ESP0-0x1C..-0x14)
	std::uint32_t loggedS;        // the stub's FST dword [0x10005000]: s
	std::uint32_t loggedQ;        // the stub's FST dword [0x10005004]: q as a float
};

// clang-format off
const EmulatorCase kEmulatorCases[] = {
	{
		"C1 B1: null source light -> false, no virtual call, no write (EAX upper bytes kept)",
		false, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000000u},
		0xA5A5A500u, 0, {}, {}, 0x0000003Fu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C2 B2: directional, plane source set, object 0 (unmapped) -> true, arg1 not read",
		true, false, 0x00B3FCFCu, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000000u},
		0x00B3FC01u, 1, {}, {}, 0x0000003Fu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C3 B2: directional, plane source 0, object 0 (unmapped) -> true",
		true, false, 0x00B3FCFCu, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000000u},
		0x00B3FC01u, 1, {}, {}, 0x0000003Fu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C4 B3 B4: plane path, mask 0 -> true, no WhichSide call, no write",
		true, true, 0x00B3FD80u, 0x12345678u, 0x00000000u,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000000u},
		0x10001001u, 1, {}, {}, 0x00000000u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C5 B6 B7: plane path, mask 0x3F, every plane 1 -> true, six writes clear bits 0..5",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Eu, 0x0000003Cu, 0x00000038u, 0x00000030u, 0x00000020u, 0x00000000u}, 0x00000000u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C6 B5: every plane 2 -> false, no write",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000002u, 0x00000002u, 0x00000002u, 0x00000002u, 0x00000002u, 0x00000002u},
		0x00000000u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {}, 0x0000003Fu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C7 B5 B6 not taken: every plane 0 -> true, no write",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {}, 0x0000003Fu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C8 B5 then B6: sides 2 1 0 1 2 1 -> false, no early exit, bits 1 3 5 cleared",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000002u, 0x00000001u, 0x00000000u, 0x00000001u, 0x00000002u, 0x00000001u},
		0x00000000u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Du, 0x00000035u, 0x00000015u}, 0x00000015u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C9 B4: mask 0x2A (gaps), sides 1 2 1 for planes 1 3 5 -> false, bits 1 and 5 cleared",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000002Au,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000001u, 0x00000002u, 0x00000001u},
		0x00000000u, 1, {0x160, 0x180, 0x1A0}, {0x00000028u, 0x00000008u}, 0x00000008u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C10 B7: mask 0xFFFFFFFF, every plane 1 -> bits 6..31 untouched, 0xFFFFFFC0",
		true, true, 0x00B3FD80u, 0x12345678u, 0xFFFFFFFFu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0xFFFFFFFEu, 0xFFFFFFFCu, 0xFFFFFFF8u, 0xFFFFFFF0u, 0xFFFFFFE0u, 0xFFFFFFC0u}, 0xFFFFFFC0u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C11 B5 B6: full-dword compares, sides 3 -1 0x102 0x101 0x80000001 0 change nothing -> true",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000003u, 0xFFFFFFFFu, 0x00000102u, 0x00000101u, 0x80000001u, 0x00000000u},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {}, 0x0000003Fu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C12 B3: plane source 0x00000001 (not a valid pointer) selects the plane path, never dereferenced",
		true, true, 0x00B3FD80u, 0x00000001u, 0x00000001u,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000002u},
		0x00000000u, 1, {0x150}, {}, 0x00000001u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C13 B5 B6: one 2 on plane 5 after five 1s -> false, five bits cleared",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		false, {0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000001u, 0x00000002u},
		0x00000000u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Eu, 0x0000003Cu, 0x00000038u, 0x00000030u, 0x00000020u}, 0x00000020u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C14 end to end with the original WhichSide bytes: sides 1 0 2 1 1 2 -> false, bits 0 3 4 cleared",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0xC0A00000u}, {0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x3F800000u, 0x40A00000u}, {0xBF800000u, 0x00000000u, 0x00000000u, 0xC0000000u}, {0x00000000u, 0xBF800000u, 0x00000000u, 0xBF800000u}, {0x00000000u, 0x00000000u, 0xBF800000u, 0x3F800000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		true, {},
		0x00000000u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Eu, 0x00000036u, 0x00000026u}, 0x00000026u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C15 end to end with the original WhichSide bytes: offset bound, sides 1 1 0 0 1 1 -> true",
		true, true, 0x00B3FD80u, 0x12345678u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x3F800000u, 0x00000000u, 0xC0400000u}, {0x00000000u, 0x00000000u, 0x3F800000u, 0x40400000u}, {0x00000000u, 0x00000000u, 0x3F800000u, 0x40300000u}, {0x3F19999Au, 0x3F4CCCCDu, 0x00000000u, 0xC1200000u}, {0x00000000u, 0x00000000u, 0xBF800000u, 0xC0600000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		true, {},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Eu, 0x0000003Cu, 0x0000002Cu, 0x0000000Cu}, 0x0000000Cu,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"C16 end to end with the original WhichSide bytes: mask 0x15 tests planes 0 2 4 only",
		true, true, 0x00B3FD80u, 0x12345678u, 0x00000015u,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0xC0A00000u}, {0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0x3F800000u, 0x40A00000u}, {0xBF800000u, 0x00000000u, 0x00000000u, 0xC0000000u}, {0x00000000u, 0xBF800000u, 0x00000000u, 0xBF800000u}, {0x00000000u, 0x00000000u, 0xBF800000u, 0x3F800000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x42C80000u,
		true, {},
		0x00000000u, 1, {0x150, 0x170, 0x190}, {0x00000014u, 0x00000004u}, 0x00000004u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u,
	},
	{
		"D1 B3 B8: distance path, |(3,4,0)| = 5, r 1, range 10 -> true",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41200000u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u,
	},
	{
		"D2 B8: range 4 == d -> false (equality)",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800000u,
		false, {0x00000000u},
		0x10004000u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u,
	},
	{
		"D3 B8: range one ulp above d -> true",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800001u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u,
	},
	{
		"D4 B8: range one ulp below d -> false",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x407FFFFFu,
		false, {0x00000000u},
		0x10000100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u,
	},
	{
		"D5 B8: range QNaN -> false (unordered)",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x7FC00000u,
		false, {0x00000000u},
		0x10004500u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u,
	},
	{
		"D6 B8: light x QNaN -> s NaN -> false",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x7FC00000u, 0x40800000u, 0x00000000u}, 0x41200000u,
		false, {0x00000000u},
		0x10004500u, 1, {}, {}, 0x0000003Fu,
		true, {0x7FC00000u, 0x40800000u, 0x00000000u}, 0x7FC00000u, 0x7FC00000u,
	},
	{
		"D7 PC24: dx 1+2^-12, dy 2^-12 -> s 0x3F801000 (PC64 would store 0x3F801001)",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x40000000u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x3F801000u, 0x3F800800u,
	},
	{
		"D8 order: dx dz 2^-12, dy 1 -> s 1.0 ((dx2 + dz2) first would give 0x3F800001)",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u,
	},
	{
		"D9 order: dy dz 2^-12, dx 1 -> s 1.0 ((dy2 + dz2) first would give 0x3F800001)",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u,
	},
	{
		"D10 PC24 double rounding: dx 0x1C2F16AB -> s 0x000000F0 (one rounding: 0xEF)",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x3F800000u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x000000F0u, 0x1C2F456Fu,
	},
	{
		"D11 FSUB rounding: q 2^24-1, r -0.5 -> d rounds to 2^24 == range -> false",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0xBF000000u}, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x4B800000u,
		false, {0x00000000u},
		0x10004000u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x577FFFFEu, 0x4B7FFFFFu,
	},
	{
		"D12 overflow: dx 2^64 -> s +inf, d +inf, range +inf -> false",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u,
		false, {0x00000000u},
		0x10004000u, 1, {}, {}, 0x0000003Fu,
		true, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u, 0x7F800000u,
	},
	{
		"D13 radius +inf -> d -inf, range 0 -> true",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x00000000u,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u,
	},
	{
		"D14 differences: centre (1.5, -2.25, 3), light (4, 5, 6) -> s 67.8125, r 0.5, range 7.735 -> true",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F7851Fu,
		false, {0x00000000u},
		0x10000001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u,
	},
	{
		"D15 differences: same, range 7.734 -> false",
		true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F77CEEu,
		false, {0x00000000u},
		0x10000100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u,
	},
};
// clang-format on

engine_ns::NiPlane PlaneFromBits(const std::uint32_t (&p)[4]) {
	return engine_ns::NiPlane{{FromBits(p[0]), FromBits(p[1]), FromBits(p[2])}, FromBits(p[3])};
}

// One emulator case: the rebuild over the case's views under `mode` against
// what the original did.
void CheckEmulatorCase(const EmulatorCase& c, ws::X87Rounding mode) {
	Scene scene;
	scene.light.planeSource = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(c.planeSource));
	scene.light.activePlanes = c.mask;
	for (int i = 0; i < 6; ++i) {
		scene.light.planes[i] = PlaneFromBits(c.planes[i]);
	}
	scene.object.worldBound =
		engine_ns::NiBound{{FromBits(c.bound[0]), FromBits(c.bound[1]), FromBits(c.bound[2])}, FromBits(c.bound[3])};
	scene.source.vtable = &kFakeVtable;
	scene.source.worldTranslate = engine_ns::NiPoint3{FromBits(c.lightPos[0]), FromBits(c.lightPos[1]), FromBits(c.lightPos[2])};
	scene.source.range = FromBits(c.range);

	std::vector<std::int32_t> sides;
	for (const std::uint32_t side : c.sides) {
		sides.push_back(static_cast<std::int32_t>(side));
	}
	PortTrace trace;
	const bool result = RunEngine(scene, c.hasObject ? &scene.object : nullptr, c.hasSource ? &scene.source : nullptr,
		c.rtti, sides, c.realWhichSide, trace, mode);

	Check(result == ((c.eax & 0xFFu) != 0) && (c.eax & 0xFEu) == 0, "AL: the original's bool");
	Check(g_rtti.calls == c.rttiCalls && (c.rttiCalls == 0 || g_rtti.self == &scene.source), "GetRTTI calls and their this");
	Check(trace.planeOffsets == c.planeOffsets, "WhichSide's plane addresses, in order");
	Check(scene.light.activePlanes == c.maskAfter, "the mask afterwards");
	Check(trace.rootInputs.size() == (c.rootCalled ? 1u : 0u), "the square root called iff the original called it");
	if (c.rootCalled) {
		const logic::DistanceTerms terms = logic::Distance(
			logic::PointValues{scene.source.worldTranslate.x, scene.source.worldTranslate.y, scene.source.worldTranslate.z},
			logic::BoundValues{{scene.object.worldBound.center.x, scene.object.worldBound.center.y,
								   scene.object.worldBound.center.z},
				scene.object.worldBound.radius},
			mode, StubSquareRoot);
		Check(Bits(terms.dx) == c.differences[0] && Bits(terms.dy) == c.differences[1] && Bits(terms.dz) == c.differences[2],
			"dx, dy, dz as the original stored them");
		Check(trace.rootInputs[0] == c.loggedS && Bits(terms.sumOfSquares) == c.loggedS, "s bit for bit");
		Check(trace.rootOutputs[0] == c.loggedQ && Bits(terms.distance) == c.loggedQ, "q bit for bit");
	}
	// Mask writes: the rebuild writes the view in place; the plane port saw
	// the mask before every test. Each write clears a set bit (so it
	// changes the value) and is followed by another test or the return
	// (derived from 0x007D2F3E..0x007D2F4F), so the changes between the
	// values seen, and the value afterwards, are the writes in order.
	std::vector<std::uint32_t> writes;
	std::uint32_t previous = c.mask;
	std::vector<std::uint32_t> seen = trace.masksSeen;
	seen.push_back(scene.light.activePlanes);
	for (const std::uint32_t value : seen) {
		if (value != previous) {
			writes.push_back(value);
			previous = value;
		}
	}
	Check(writes == c.maskWrites, "every mask write, in order");
}

void Differential() {
	Section("Differential, layer 1 (measured, emulator): the rebuild against the original's 31 results at fpcw 0x007F");
	int index = 0;
	for (const EmulatorCase& c : kEmulatorCases) {
		++index;
		std::printf("    %s\n", c.name);
		CheckEmulatorCase(c, kNearest);
	}
	Check(index == 31, "all 31 cases ran");
}

// The original under the four rounding controls, transcribed from
// tools/difftest/expected/0x007D2EA0-rounding.json (emulator run 2026-10-30
// on re/export/draw-setup/0x007D2EA0 with the reference executable's CRT
// square root and WhichSide, cases tools/difftest/cases/
// 0x007D2EA0-rounding.json from generators/0x007D2EA0-rounding.py; measured
// in Unicorn's x87). Every case returned to its sentinel with stack_delta +8
// and no emulator error; the only writes outside the stack are the wrapper's
// two logs and the mask writes at 0x007D2F3E; GetRTTI was called from
// 0x007D2EC2 with ECX = the source, WhichSide from 0x007D2F2B, the square
// root from 0x007D2FB0 (asserted when the rows were transcribed).
struct RoundingCase {
	std::uint16_t fpcw;
	EmulatorCase c;
};

// clang-format off
const RoundingCase kRoundingCases[] = {
	{0x007F, {"G1 garbage scene-root bound (run 20261030-131826)", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xFD935C0Fu, 0xFD935C13u, 0xFEBFCBA9u, 0x5FDFFFF8u}, {0xC3F5B6A8u, 0x4486928Fu, 0x4401EB54u}, 0x43800000u,
		false, {0x00000000u},
		0x00000100u, 1, {}, {}, 0x0000003Fu,
		true, {0x7D935C0Fu, 0x7D935C13u, 0x7EBFCBA9u}, 0x7F800000u, 0x7F800000u}},
	{0x047F, {"G1 garbage scene-root bound (run 20261030-131826)", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xFD935C0Fu, 0xFD935C13u, 0xFEBFCBA9u, 0x5FDFFFF8u}, {0xC3F5B6A8u, 0x4486928Fu, 0x4401EB54u}, 0x43800000u,
		false, {0x00000000u},
		0x47EF0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x7D935C0Eu, 0x7D935C13u, 0x7EBFCBA9u}, 0x7F7FFFFFu, 0x5F7FFFFFu}},
	{0x087F, {"G1 garbage scene-root bound (run 20261030-131826)", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xFD935C0Fu, 0xFD935C13u, 0xFEBFCBA9u, 0x5FDFFFF8u}, {0xC3F5B6A8u, 0x4486928Fu, 0x4401EB54u}, 0x43800000u,
		false, {0x00000000u},
		0x00000100u, 1, {}, {}, 0x0000003Fu,
		true, {0x7D935C0Fu, 0x7D935C14u, 0x7EBFCBAAu}, 0x7F800000u, 0x7F800000u}},
	{0x0C7F, {"G1 garbage scene-root bound (run 20261030-131826)", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xFD935C0Fu, 0xFD935C13u, 0xFEBFCBA9u, 0x5FDFFFF8u}, {0xC3F5B6A8u, 0x4486928Fu, 0x4401EB54u}, 0x43800000u,
		false, {0x00000000u},
		0x47EF0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x7D935C0Eu, 0x7D935C13u, 0x7EBFCBA9u}, 0x7F7FFFFFu, 0x5F7FFFFFu}},
	{0x007F, {"D1 B3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41200000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"D1 B3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41200000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x80000000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"D1 B3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41200000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"D1 B3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41200000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"D2 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800000u,
		false, {0x00000000u},
		0x40394000u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"D2 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800000u,
		false, {0x00000000u},
		0x40394000u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x80000000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"D2 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800000u,
		false, {0x00000000u},
		0x40394000u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"D2 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800000u,
		false, {0x00000000u},
		0x40394000u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"D3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800001u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"D3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800001u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x80000000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"D3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800001u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"D3 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x40800001u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"D4 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x407FFFFFu,
		false, {0x00000000u},
		0x40390100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"D4 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x407FFFFFu,
		false, {0x00000000u},
		0x40390100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x80000000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"D4 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x407FFFFFu,
		false, {0x00000000u},
		0x40390100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"D4 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x407FFFFFu,
		false, {0x00000000u},
		0x40390100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"D5 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x7FC00000u,
		false, {0x00000000u},
		0x40394500u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"D5 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x7FC00000u,
		false, {0x00000000u},
		0x40394500u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x80000000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"D5 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x7FC00000u,
		false, {0x00000000u},
		0x40394500u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"D5 B8", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x7FC00000u,
		false, {0x00000000u},
		0x40394500u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"D7 PC24", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x3F801000u, 0x3F800800u}},
	{0x047F, {"D7 PC24", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800800u, 0x39800000u, 0x80000000u}, 0x3F801000u, 0x3F800800u}},
	{0x087F, {"D7 PC24", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x3F801002u, 0x3F800801u}},
	{0x0C7F, {"D7 PC24", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800800u, 0x39800000u, 0x00000000u}, 0x3F801000u, 0x3F800800u}},
	{0x007F, {"D8 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u}},
	{0x047F, {"D8 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u}},
	{0x087F, {"D8 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x3F800002u, 0x3F800001u}},
	{0x0C7F, {"D8 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x39800000u, 0x3F800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u}},
	{0x007F, {"D9 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u}},
	{0x047F, {"D9 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u}},
	{0x087F, {"D9 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x3F800002u, 0x3F800001u}},
	{0x0C7F, {"D9 order", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x40000000u,
		false, {0x00000000u},
		0x3FF00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x3F800000u, 0x39800000u, 0x39800000u}, 0x3F800000u, 0x3F800000u}},
	{0x007F, {"D10 PC24 double rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x3F800000u,
		false, {0x00000000u},
		0x371E0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x000000F0u, 0x1C2F456Fu}},
	{0x047F, {"D10 PC24 double rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x3F800000u,
		false, {0x00000000u},
		0x371D0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x1C2F16ABu, 0x80000000u, 0x80000000u}, 0x000000EFu, 0x1C2EE7DBu}},
	{0x087F, {"D10 PC24 double rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x3F800000u,
		false, {0x00000000u},
		0x371E0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x000000F0u, 0x1C2F456Fu}},
	{0x0C7F, {"D10 PC24 double rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x3F800000u,
		false, {0x00000000u},
		0x371D0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x1C2F16ABu, 0x00000000u, 0x00000000u}, 0x000000EFu, 0x1C2EE7DBu}},
	{0x007F, {"D11 FSUB rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0xBF000000u}, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x4B800000u,
		false, {0x00000000u},
		0x42EF4000u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x577FFFFEu, 0x4B7FFFFFu}},
	{0x047F, {"D11 FSUB rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0xBF000000u}, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x4B800000u,
		false, {0x00000000u},
		0x42EF0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B7FFFFFu, 0x80000000u, 0x80000000u}, 0x577FFFFEu, 0x4B7FFFFFu}},
	{0x087F, {"D11 FSUB rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0xBF000000u}, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x4B800000u,
		false, {0x00000000u},
		0x42EF4000u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x577FFFFFu, 0x4B7FFFFFu}},
	{0x0C7F, {"D11 FSUB rounding", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0xBF000000u}, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x4B800000u,
		false, {0x00000000u},
		0x42EF0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B7FFFFFu, 0x00000000u, 0x00000000u}, 0x577FFFFEu, 0x4B7FFFFFu}},
	{0x007F, {"D12 overflow", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u,
		false, {0x00000000u},
		0x00004000u, 1, {}, {}, 0x0000003Fu,
		true, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u, 0x7F800000u}},
	{0x047F, {"D12 overflow", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u,
		false, {0x00000000u},
		0x47EF0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x5F800000u, 0x80000000u, 0x80000000u}, 0x7F7FFFFFu, 0x5F7FFFFFu}},
	{0x087F, {"D12 overflow", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u,
		false, {0x00000000u},
		0x00004000u, 1, {}, {}, 0x0000003Fu,
		true, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u, 0x7F800000u}},
	{0x0C7F, {"D12 overflow", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F800000u,
		false, {0x00000000u},
		0x47EF0001u, 1, {}, {}, 0x0000003Fu,
		true, {0x5F800000u, 0x00000000u, 0x00000000u}, 0x7F7FFFFFu, 0x5F7FFFFFu}},
	{0x007F, {"D13 radius +inf", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x00000000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"D13 radius +inf", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x00000000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x80000000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"D13 radius +inf", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x00000000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"D13 radius +inf", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x7F800000u}, {0x40400000u, 0x40800000u, 0x00000000u}, 0x00000000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40400000u, 0x40800000u, 0x00000000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"D14 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F7851Fu,
		false, {0x00000000u},
		0x40500001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x047F, {"D14 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F7851Fu,
		false, {0x00000000u},
		0x40500001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x087F, {"D14 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F7851Fu,
		false, {0x00000000u},
		0x40500001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x0C7F, {"D14 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F7851Fu,
		false, {0x00000000u},
		0x40500001u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x007F, {"D15 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F77CEEu,
		false, {0x00000000u},
		0x40500100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x047F, {"D15 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F77CEEu,
		false, {0x00000000u},
		0x40500100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x087F, {"D15 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F77CEEu,
		false, {0x00000000u},
		0x40500100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x0C7F, {"D15 differences", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3FC00000u, 0xC0100000u, 0x40400000u, 0x3F000000u}, {0x40800000u, 0x40A00000u, 0x40C00000u}, 0x40F77CEEu,
		false, {0x00000000u},
		0x40500100u, 1, {}, {}, 0x0000003Fu,
		true, {0x40200000u, 0x40E80000u, 0x40400000u}, 0x4287A000u, 0x4103C1E2u}},
	{0x007F, {"M1 the difference ties", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xBF800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x4B800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B800000u, 0x00000000u, 0x00000000u}, 0x57800000u, 0x4B800000u}},
	{0x047F, {"M1 the difference ties", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xBF800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x4B800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B800000u, 0x80000000u, 0x80000000u}, 0x57800000u, 0x4B800000u}},
	{0x087F, {"M1 the difference ties", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xBF800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x4B800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F04000u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B800001u, 0x00000000u, 0x00000000u}, 0x57800003u, 0x4B800001u}},
	{0x0C7F, {"M1 the difference ties", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0xBF800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x4B800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F00001u, 1, {}, {}, 0x0000003Fu,
		true, {0x4B800000u, 0x00000000u, 0x00000000u}, 0x57800000u, 0x4B800000u}},
	{0x007F, {"M2 M1 negative", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F00001u, 1, {}, {}, 0x0000003Fu,
		true, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x57800000u, 0x4B800000u}},
	{0x047F, {"M2 M1 negative", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F04000u, 1, {}, {}, 0x0000003Fu,
		true, {0xCB800001u, 0x80000000u, 0x80000000u}, 0x57800002u, 0x4B800001u}},
	{0x087F, {"M2 M1 negative", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F00001u, 1, {}, {}, 0x0000003Fu,
		true, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x57800000u, 0x4B800000u}},
	{0x0C7F, {"M2 M1 negative", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x4B800001u,
		false, {0x00000000u},
		0x42F00001u, 1, {}, {}, 0x0000003Fu,
		true, {0xCB800000u, 0x00000000u, 0x00000000u}, 0x57800000u, 0x4B800000u}},
	{0x007F, {"M3 an exact zero difference", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x40A00000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40A00000u, 0x40400000u, 0x40800000u}, 0x40C00000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x00000000u, 0x40400000u, 0x40800000u}, 0x41C80000u, 0x40A00000u}},
	{0x047F, {"M3 an exact zero difference", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x40A00000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40A00000u, 0x40400000u, 0x40800000u}, 0x40C00000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x80000000u, 0x40400000u, 0x40800000u}, 0x41C80000u, 0x40A00000u}},
	{0x087F, {"M3 an exact zero difference", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x40A00000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40A00000u, 0x40400000u, 0x40800000u}, 0x40C00000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x00000000u, 0x40400000u, 0x40800000u}, 0x41C80000u, 0x40A00000u}},
	{0x0C7F, {"M3 an exact zero difference", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x40A00000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40A00000u, 0x40400000u, 0x40800000u}, 0x40C00000u,
		false, {0x00000000u},
		0x40390001u, 1, {}, {}, 0x0000003Fu,
		true, {0x00000000u, 0x40400000u, 0x40800000u}, 0x41C80000u, 0x40A00000u}},
	{0x007F, {"M4 the sum of squares rounds", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800001u,
		false, {0x00000000u},
		0x3FF04000u, 1, {}, {}, 0x0000003Fu,
		true, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800002u, 0x3F800001u}},
	{0x047F, {"M4 the sum of squares rounds", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800001u,
		false, {0x00000000u},
		0x3FF04000u, 1, {}, {}, 0x0000003Fu,
		true, {0x2B800000u, 0x3F800001u, 0x80000000u}, 0x3F800002u, 0x3F800001u}},
	{0x087F, {"M4 the sum of squares rounds", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800001u,
		false, {0x00000000u},
		0x3FF00100u, 1, {}, {}, 0x0000003Fu,
		true, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800004u, 0x3F800002u}},
	{0x0C7F, {"M4 the sum of squares rounds", true, true, 0x00B3FD80u, 0x00000000u, 0x0000003Fu,
		{{0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x40000000u, 0x00000000u, 0x00000000u, 0x3F800000u}, {0x40400000u, 0x00000000u, 0x00000000u, 0x40000000u}, {0x40800000u, 0x00000000u, 0x00000000u, 0x40400000u}, {0x40A00000u, 0x00000000u, 0x00000000u, 0x40800000u}, {0x40C00000u, 0x00000000u, 0x00000000u, 0x40A00000u}},
		{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800001u,
		false, {0x00000000u},
		0x3FF04000u, 1, {}, {}, 0x0000003Fu,
		true, {0x2B800000u, 0x3F800001u, 0x00000000u}, 0x3F800002u, 0x3F800001u}},
	{0x007F, {"P1 plane path, WhichSide's own bytes", true, true, 0x00B3FD80u, 0x00000001u, 0x0000003Fu,
		{{0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}},
		{0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x00000000u, 0x00000000u, 0x00000000u}, 0x42C80000u,
		true, {},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Bu, 0x00000033u, 0x00000023u, 0x00000003u}, 0x00000003u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u}},
	{0x047F, {"P1 plane path, WhichSide's own bytes", true, true, 0x00B3FD80u, 0x00000001u, 0x0000003Fu,
		{{0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}},
		{0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x00000000u, 0x00000000u, 0x00000000u}, 0x42C80000u,
		true, {},
		0x00000000u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Bu, 0x00000033u, 0x00000023u, 0x00000003u}, 0x00000003u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u}},
	{0x087F, {"P1 plane path, WhichSide's own bytes", true, true, 0x00B3FD80u, 0x00000001u, 0x0000003Fu,
		{{0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}},
		{0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x00000000u, 0x00000000u, 0x00000000u}, 0x42C80000u,
		true, {},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Eu, 0x0000003Au, 0x00000032u, 0x00000022u, 0x00000002u}, 0x00000002u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u}},
	{0x0C7F, {"P1 plane path, WhichSide's own bytes", true, true, 0x00B3FD80u, 0x00000001u, 0x0000003Fu,
		{{0x33800000u, 0x3F800000u, 0x30800000u, 0x3F800000u}, {0xB3800000u, 0xBF800000u, 0xB0800000u, 0xBF800000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u}},
		{0x3F800000u, 0x3F800000u, 0x30800000u, 0x33800000u}, {0x00000000u, 0x00000000u, 0x00000000u}, 0x42C80000u,
		true, {},
		0x00000001u, 1, {0x150, 0x160, 0x170, 0x180, 0x190, 0x1A0}, {0x0000003Bu, 0x00000033u, 0x00000023u, 0x00000003u}, 0x00000003u,
		false, {0x00000000u, 0x00000000u, 0x00000000u}, 0x00000000u, 0x00000000u}},
};
// clang-format on

void DifferentialRounding() {
	Section("Differential under the four rounding controls (measured, emulator): G1, D1..D15 but D6, M1..M4, P1 at four control words");
	int perMode[4] = {0, 0, 0, 0};
	int modesDiffer = 0;
	for (const RoundingCase& r : kRoundingCases) {
		const ws::X87Rounding mode = ws::RoundingFromControlWord(r.fpcw);
		++perMode[static_cast<unsigned>(mode)];
		std::printf("    %s [fpcw %04X]\n", r.c.name, static_cast<unsigned>(r.fpcw));
		Check((r.fpcw & 0xF3FFu) == 0x007Fu, "the case's control word is PC 24 with every exception masked");
		CheckEmulatorCase(r.c, mode);
	}
	// How many of these runs a nearest-only rebuild would get wrong: the
	// original's answer under a directed mode differs from its own answer at
	// nearest for the same inputs (AL, the stored differences, s or q, or the
	// mask).
	for (std::size_t i = 0; i < std::size(kRoundingCases); ++i) {
		const RoundingCase& r = kRoundingCases[i];
		if (r.fpcw == 0x007F) {
			continue;
		}
		const RoundingCase& nearest = kRoundingCases[i - (static_cast<std::size_t>(ws::RoundingFromControlWord(r.fpcw)))];
		const bool same = nearest.fpcw == 0x007F && (nearest.c.eax & 0xFFu) == (r.c.eax & 0xFFu) &&
			nearest.c.maskAfter == r.c.maskAfter && nearest.c.loggedS == r.c.loggedS && nearest.c.loggedQ == r.c.loggedQ &&
			nearest.c.differences[0] == r.c.differences[0] && nearest.c.differences[1] == r.c.differences[1] &&
			nearest.c.differences[2] == r.c.differences[2];
		modesDiffer += same ? 0 : 1;
	}
	Check(std::size(kRoundingCases) == 80, "80 runs: 20 inputs at four control words");
	Check(perMode[0] == 20 && perMode[1] == 20 && perMode[2] == 20 && perMode[3] == 20, "20 at each control word");
	char line[160];
	std::snprintf(line, sizeof(line), "%d of the 60 directed-mode runs differ from the same inputs at nearest, 28 expected (a nearest-only rebuild fails those)", modesDiffer);
	Check(modesDiffer == 28, line);
}

} // namespace

int main() {
	Decisions();
	PlaneLoopFlows();
	DistanceArithmetic();
	EngineFlows();
	Layouts();
	Differential();
	DifferentialRounding();
	return Failures();
}
