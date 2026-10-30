#pragma once

// The decisions of 0x007C9140 (proposed UpdateLightPositionConstants), as
// pure functions over plain values, and the body's loop replayed over two
// injected ports. Evidence: re/functions/0x007C9140.md and the export
// re/export/perf-levers/0x007C9140/asm.s; every address below is an
// instruction address in that asm.s (read) unless marked otherwise.
//
// What the body does (read): for each of three slots (loop 0x007C9160..
// 0x007C921C), when the record at [0x00B45040 + 4i] has a non-zero byte at
// +0x08, it copies the light position at 0x00B46528 + 0x10i to the stack,
// transforms it with D3DXVec3TransformCoord (0x009A1B0A, the caller's matrix,
// CALL at 0x007C9195), takes w from 0x00B465A8 + 0x10i and stores the four
// floats at 0x00B44FD8 + 0x10i; when arg4 is 0 it first scales xyz by arg2
// (arg3 in 0x177..0x179) or divides w by arg2 (any other arg3).
//
// Floats are carried as their bit patterns (FloatBits) because the original
// moves them through the x87 (FLD m32 / FSTP m32, FMUL, FDIV) and the stored
// bits are the contract: the x87 namespace below models those instructions
// exactly as the game runs them - precision control 24 bits, round to
// nearest, every exception masked (measured in the game, knowledge/
// frame-clock.md: _control87 0x000A001F). Where that differs from a plain
// C++ float operation:
//   - every arithmetic result is rounded twice: to a 24-bit significand with
//     the x87's wide exponent (precision control), then to float by FSTP m32.
//     For results in the float normal range the two roundings equal one
//     float rounding; in the float subnormal range they can differ by one
//     unit (measured: the emulator and this host's CPU agree, the test's
//     "x87 PC24" sections);
//   - FLD m32 of a signaling NaN quiets it (measured on the host CPU, an AMD
//     Ryzen 7 7800X3D, by the test's hardware replay; Unicorn 2.1.4 does not,
//     a limit of the emulator gate);
//   - two NaN operands give the one with the larger significand, a tie the
//     positive one; a signaling NaN memory operand against a NaN in ST0 gives
//     ST0 (measured on the host CPU; the emulator agrees wherever no
//     signaling NaN went through an FLD first - after one it holds the SNaN
//     in the register and the QNaN wins, emulator cases 23 and 24).
//
// Names are proposals until the perf-levers slice's refutation round.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace obng::engine::render::update_light_position_constants {

using FloatBits = std::uint32_t;

// ---- The loop and its gates ---------------------------------------------------

// B6: EDI runs 0x00B45040, +4, ... while below 0x00B4504C (ADD EDI,4 /
// CMP EDI,0x00B4504C / JL at 0x007C9210..0x007C921C): three slots (derived:
// (0x00B4504C - 0x00B45040) / 4).
inline constexpr int kSlotCount = 3;

// B2: CMP byte ptr [EAX+0x8],0x0 / JZ 0x007C9210 at 0x007C9162/0x007C9166.
// Any non-zero byte enables the slot; only that byte is read.
constexpr bool SlotEnabled(std::uint8_t flagByte) {
	return flagByte != 0;
}

// B4/B5: ADD EAX,0xFFFFFE89 / CMP EAX,2 / JA at 0x007C91AC..0x007C91B4, an
// unsigned compare on arg3 - 0x177: ids 0x177, 0x178, 0x179 scale xyz;
// every other id, negative ones included, divides w.
inline constexpr std::uint32_t kScaledPassFirst = 0x177;
inline constexpr std::uint32_t kScaledPassSpan = 2;

constexpr bool PassScalesPosition(std::uint32_t passId) {
	return passId - kScaledPassFirst <= kScaledPassSpan;
}

// The three outcomes after the transform of an enabled slot.
enum class SlotAdjust : std::uint8_t {
	Keep,           // B3: arg4 != 0 (TEST EBX,EBX / JNZ 0x007C91E8 at 0x007C919A/0x007C91A6)
	ScalePosition,  // B4: arg4 == 0, arg3 in 0x177..0x179 (0x007C91B6..0x007C91DA)
	DivideW,        // B5: arg4 == 0, any other arg3 (0x007C91DC..0x007C91E4)
};

// arg4 is tested as a full dword (TEST EBX,EBX on the value loaded by the
// plain MOV at 0x007C9144), so 0x100 counts as non-zero. The original tests
// arg4 and arg3 again for every enabled slot; both are loop-invariant, so
// one decision per call is the same outcome (derived from 0x007C9144 and
// 0x007C91A8 reading the unchanged argument slots - EBX is not written in
// the loop, and the slot [ESP+0x34] is not written at all).
constexpr SlotAdjust ClassifyAdjust(std::uint32_t passId, std::uint32_t skipAdjust) {
	if (skipAdjust != 0) {
		return SlotAdjust::Keep;
	}
	return PassScalesPosition(passId) ? SlotAdjust::ScalePosition : SlotAdjust::DivideW;
}

// ---- The x87 as the game runs it ------------------------------------------------

namespace x87 {

inline constexpr FloatBits kSignBit = 0x80000000u;
inline constexpr FloatBits kExponentMask = 0x7F800000u;
inline constexpr FloatBits kMantissaMask = 0x007FFFFFu;
inline constexpr FloatBits kQuietBit = 0x00400000u;
inline constexpr FloatBits kInfinity = 0x7F800000u;
inline constexpr FloatBits kIndefinite = 0xFFC00000u;  // the masked invalid operation's NaN, stored as float

inline constexpr int kPrecisionBits = 24;     // precision control 00 (game: _PC_24)
inline constexpr int kFloatBias = 150;        // value = significand * 2^(field - 150) for a normal float
inline constexpr int kFloatMaxField = 0xFF;   // exponent field of inf / NaN
inline constexpr int kSubnormalExponent = -149;
inline constexpr int kDivideExtraBits = 26;   // quotient bits beyond the 24 kept: a guard bit and more

constexpr bool IsNaN(FloatBits bits) {
	return (bits & kExponentMask) == kExponentMask && (bits & kMantissaMask) != 0;
}

constexpr bool IsSignalingNaN(FloatBits bits) {
	return IsNaN(bits) && (bits & kQuietBit) == 0;
}

constexpr bool IsInfinity(FloatBits bits) {
	return (bits & ~kSignBit) == kInfinity;
}

constexpr bool IsZero(FloatBits bits) {
	return (bits & ~kSignBit) == 0;
}

// FLD m32: exact for every float, a signaling NaN comes out quiet. A
// following FSTP m32 of the loaded value stores the same bits, so this is
// also the FLD/FSTP copy the body makes of every float it does not compute.
constexpr FloatBits Load(FloatBits bits) {
	return IsNaN(bits) ? (bits | kQuietBit) : bits;
}

// Round n >> drop to nearest, ties to even; sticky says the true value lies
// strictly above n (bits already lost below n).
constexpr std::uint64_t RoundShift(std::uint64_t n, int drop, bool sticky) {
	if (drop <= 0) {
		return n;
	}
	if (drop >= 64) {
		return 0;  // callers pass n < 2^48, far below half a unit at this shift
	}
	const std::uint64_t kept = n >> drop;
	const std::uint64_t rest = n & ((std::uint64_t{1} << drop) - 1);
	const std::uint64_t half = std::uint64_t{1} << (drop - 1);
	const bool up = rest > half || (rest == half && (sticky || (kept & 1) != 0));
	return kept + (up ? 1 : 0);
}

// A finite non-zero value as significand * 2^exponent, the significand
// normalised to [2^23, 2^24) (subnormal floats included).
struct Finite {
	bool negative;
	std::uint32_t significand;
	int exponent;
};

constexpr Finite Unpack(FloatBits bits) {
	const auto field = static_cast<int>((bits & kExponentMask) >> 23);
	std::uint32_t significand = bits & kMantissaMask;
	int exponent = kSubnormalExponent;
	if (field != 0) {
		significand |= 0x00800000u;
		exponent = field - kFloatBias;
	}
	while (significand < 0x00800000u) {
		significand <<= 1;
		--exponent;
	}
	return Finite{(bits & kSignBit) != 0, significand, exponent};
}

// The arithmetic instruction's rounding to 24 bits (wide exponent), then
// FSTP m32's rounding to float: the double rounding of the game's x87.
// value = (n + sticky part) * 2^exponent, n != 0.
constexpr FloatBits RoundAndStore(bool negative, std::uint64_t n, int exponent, bool sticky) {
	const FloatBits sign = negative ? kSignBit : 0u;
	const int drop = std::bit_width(n) - kPrecisionBits;
	std::uint64_t significand = RoundShift(n, drop, sticky);
	exponent += drop;
	if (significand == (std::uint64_t{1} << kPrecisionBits)) {
		significand >>= 1;
		++exponent;
	}
	const int field = exponent + kFloatBias;
	if (field >= kFloatMaxField) {
		return sign | kInfinity;  // overflow masked, round to nearest: inf
	}
	if (field >= 1) {
		return sign | (static_cast<FloatBits>(field) << 23) | (static_cast<FloatBits>(significand) & kMantissaMask);
	}
	// Subnormal float: the second rounding, onto the 2^-149 grid. A carry
	// into bit 23 gives the smallest normal's encoding, which is right.
	return sign | static_cast<FloatBits>(RoundShift(significand, 1 - field, false));
}

// Two NaN operands both held in x87 registers (so both quiet): the larger
// significand, a tie the positive one.
constexpr FloatBits LargerNaN(FloatBits a, FloatBits b) {
	const FloatBits ma = a & kMantissaMask;
	const FloatBits mb = b & kMantissaMask;
	if (ma != mb) {
		return ma > mb ? a : b;
	}
	return (a & kSignBit) == 0 ? a : b;
}

// The NaN result of an operation with ST0 (loaded, so quiet) and another
// operand that is a register (loaded too) or a raw m32 memory operand.
enum class Operand : std::uint8_t {
	Register,
	Memory,
};

constexpr FloatBits NaNResult(FloatBits st0, FloatBits other, Operand kind) {
	const FloatBits a = Load(st0);
	const FloatBits b = kind == Operand::Register ? Load(other) : other;
	if (!IsNaN(a)) {
		return Load(b);
	}
	if (!IsNaN(b)) {
		return a;
	}
	if (IsSignalingNaN(b)) {
		return a;  // a signaling memory operand against a register NaN: the register NaN
	}
	return LargerNaN(a, b);
}

// FMUL: st0 * other, stored by FSTP m32.
constexpr FloatBits Multiply(FloatBits st0, FloatBits other, Operand kind) {
	if (IsNaN(st0) || IsNaN(other)) {
		return NaNResult(st0, other, kind);
	}
	const FloatBits sign = (st0 ^ other) & kSignBit;
	if ((IsInfinity(st0) && IsZero(other)) || (IsZero(st0) && IsInfinity(other))) {
		return kIndefinite;
	}
	if (IsInfinity(st0) || IsInfinity(other)) {
		return sign | kInfinity;
	}
	if (IsZero(st0) || IsZero(other)) {
		return sign;
	}
	const Finite a = Unpack(st0);
	const Finite b = Unpack(other);
	const std::uint64_t product = std::uint64_t{a.significand} * b.significand;  // exact, < 2^48
	return RoundAndStore(sign != 0, product, a.exponent + b.exponent, false);
}

// FDIV m32: st0 / divisor, stored by FSTP m32.
constexpr FloatBits Divide(FloatBits st0, FloatBits divisor) {
	if (IsNaN(st0) || IsNaN(divisor)) {
		return NaNResult(st0, divisor, Operand::Memory);
	}
	const FloatBits sign = (st0 ^ divisor) & kSignBit;
	if ((IsInfinity(st0) && IsInfinity(divisor)) || (IsZero(st0) && IsZero(divisor))) {
		return kIndefinite;
	}
	if (IsInfinity(st0) || IsZero(divisor)) {
		return sign | kInfinity;  // zero divide masked: inf
	}
	if (IsZero(st0) || IsInfinity(divisor)) {
		return sign;
	}
	const Finite a = Unpack(st0);
	const Finite b = Unpack(divisor);
	const std::uint64_t dividend = std::uint64_t{a.significand} << kDivideExtraBits;
	const std::uint64_t quotient = dividend / b.significand;  // 26 or 27 bits
	const bool sticky = dividend % b.significand != 0;
	return RoundAndStore(sign != 0, quotient, a.exponent - kDivideExtraBits - b.exponent, sticky);
}

} // namespace x87

// ---- One slot -------------------------------------------------------------------------

struct Vec3Bits {
	FloatBits x;
	FloatBits y;
	FloatBits z;
	friend constexpr bool operator==(const Vec3Bits&, const Vec3Bits&) = default;
};

struct Vec4Bits {
	FloatBits x;
	FloatBits y;
	FloatBits z;
	FloatBits w;
	friend constexpr bool operator==(const Vec4Bits&, const Vec4Bits&) = default;
};

// What D3DXVec3TransformCoord's pV receives: the three floats copied by
// FLD m32 / FSTP m32 (0x007C916C..0x007C9191).
constexpr Vec3Bits TransformInput(const Vec3Bits& lightPosition) {
	return Vec3Bits{x87::Load(lightPosition.x), x87::Load(lightPosition.y), x87::Load(lightPosition.z)};
}

// The vec4 an enabled slot stores (0x007C919C..0x007C920A). transformed is
// what D3DXVec3TransformCoord wrote to pOut, lightW the float at
// 0x00B465A8 + 0x10i, scale arg2.
constexpr Vec4Bits ComposeSlotConstant(const Vec3Bits& transformed, FloatBits lightW, FloatBits scale, SlotAdjust adjust) {
	using x87::Operand;
	// w: FLD [0x00B465A8+ESI] / FSTP [ESP+0x38] (0x007C919C/0x007C91A2), later
	// FLD / FSTP to the block (0x007C9206/0x007C920A).
	const FloatBits w = x87::Load(lightW);
	switch (adjust) {
	case SlotAdjust::ScalePosition:
		// x: FMULP ST2 with ST0 = arg2 (0x007C91C0), y: FMUL ST1 with ST0 = y
		// (0x007C91CC), z: FMUL m32 with ST0 = arg2 (0x007C91D2); each result
		// stored by FSTP m32 and copied once more by FLD/FSTP (0x007C91E8..).
		return Vec4Bits{
			x87::Multiply(scale, transformed.x, Operand::Register),
			x87::Multiply(transformed.y, scale, Operand::Register),
			x87::Multiply(scale, transformed.z, Operand::Memory),
			w,
		};
	case SlotAdjust::DivideW:
		// FLD [ESP+0x38] / FDIV m32 arg2 / FSTP (0x007C91DC..0x007C91E4).
		return Vec4Bits{x87::Load(transformed.x), x87::Load(transformed.y), x87::Load(transformed.z),
			x87::Divide(w, scale)};
	case SlotAdjust::Keep:
		break;
	}
	return Vec4Bits{x87::Load(transformed.x), x87::Load(transformed.y), x87::Load(transformed.z), w};
}

// ---- The whole body over ports --------------------------------------------------------

// The inputs the body reads besides the matrix and the slot flags (read:
// claim 7 of the mapping; no other global is referenced, meta.json
// data_refs). Reading them once per call is exact: the only stores of the
// body go to 0x00B44FD8..0x00B45007 (0x007C91EC..0x007C920A) and the stack,
// and the light tables 0x00B46528..0x00B46557 and 0x00B465A8..0x00B465D7
// are fixed globals outside that range (derived from those addresses; that
// D3DXVec3TransformCoord writes only pOut, a stack local, is unverified -
// its body is outside the export). The slot flags are not inputs: a slot
// record may point into the block, so they go through a port.
struct Inputs {
	std::array<Vec3Bits, kSlotCount> lightPositions;    // 0x00B46528 + 0x10i, +0x00..+0x08
	std::array<FloatBits, kSlotCount> lightW;           // 0x00B465A8 + 0x10i
	FloatBits scale;                                    // arg2
	std::uint32_t passId;                               // arg3
	std::uint32_t skipAdjust;                           // arg4
};

// The three ports, called in the original's order, slot by slot: the flag,
// then (enabled) the transform, then the store; the next slot's flag comes
// after this slot's store.
// slotFlag(int slot) -> std::uint8_t stands for MOV EAX,[EDI] / CMP byte ptr
// [EAX+0x8],0 (0x007C9160/0x007C9162): the record pointer at 0x00B45040 +
// 4 * slot and its byte +0x08, both read again for every slot, after the
// earlier slots' stores (read: the loop back JL 0x007C9160 at 0x007C921C
// follows the FSTPs at 0x007C91EC..0x007C920A). A record that points into
// the block (+0x08 inside 0x00B44FD8..0x00B45007) therefore sees what the
// earlier slots stored (measured, emulator cases 26 and 27). The pointers
// themselves (0x00B45040..0x00B4504B) lie outside the block, so no store of
// this body changes them (derived from the two ranges).
// transform(const Vec3Bits& in, Matrix matrix) -> Vec3Bits stands for
// D3DXVec3TransformCoord(&out, &in, matrix) (0x009A1B0A); its result EAX is
// ignored by the original (read: EAX is next written at 0x007C91A8). It is
// called after the earlier slots' stores, so a matrix inside the block
// reads them (derived from the same loop order).
// store(int slot, const Vec4Bits&) stands for the four FSTPs into
// 0x00B44FD8 + 0x10 * slot (0x007C91EC..0x007C920A, x, y, z, w in order).
// A disabled slot calls neither transform nor store (B2).
//
// A null slot record is the slotFlag port's to handle: the original
// dereferences it without a test (0x007C9160/0x007C9162) and faults
// (derived). EAX at RET is not a result (void, re/functions/0x007C9140.md).
template <typename Matrix, typename SlotFlagPort, typename TransformPort, typename StorePort>
void UpdateLightPositionConstants(const Inputs& in, Matrix matrix, SlotFlagPort&& slotFlag, TransformPort&& transform,
	StorePort&& store) {
	const SlotAdjust adjust = ClassifyAdjust(in.passId, in.skipAdjust);
	for (int slot = 0; slot < kSlotCount; ++slot) {
		const auto index = static_cast<std::size_t>(slot);
		if (!SlotEnabled(slotFlag(slot))) {
			continue;
		}
		const Vec3Bits transformed = transform(TransformInput(in.lightPositions[index]), matrix);
		store(slot, ComposeSlotConstant(transformed, in.lightW[index], in.scale, adjust));
	}
}

} // namespace obng::engine::render::update_light_position_constants
