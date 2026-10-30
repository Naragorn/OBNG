// The native reading of 0x0047DA70's arithmetic (NiBound_WhichSideLogic.h,
// "The native reading") against the host's own x87 running the original's
// instruction sequence under the four rounding controls at precision control
// 24 - the hardware the game runs on, not a model of it - and the flows of
// the MXCSR scope over a fake port.
//
// What runs on the x87 (GNU inline assembly; the tests are built 32-bit x86,
// tests/CMakeLists.txt): FLDCW; FLD n.y, FMUL c.y, FLD n.x, FMUL c.x, FADDP,
// FLD n.z, FMUL c.z, FADDP, FSTP float, FLD float, FSUB k, FSTP float -
// 0x0047DA74..0x0047DA97 as read - then FLDCW back to FNINIT's 0x037F. The
// control words 0x007F, 0x047F, 0x087F and 0x0C7F mask every exception, as
// every word the game was measured with.
//
// Three claims are measured here, per rounding control:
//   1. the exact path (SignedDistance) equals the x87 on every input;
//   2. the native path with the fallback equals the x87 on every input;
//   3. soundness of the flags: wherever the raw native result differs from
//      the x87's, a fallback flag was raised (the converse need not hold:
//      a raised flag with an equal result only costs the recomputation).
// The inputs come from a fixed-seed generator biased to the edges: denormal
// and huge floats, few-bit significands (ties), zeros, and plain values.

#include "Check.h"
#include "engine/render/Mxcsr.h"
#include "engine/render/NiBound_WhichSideLogic.h"

#include <xmmintrin.h>

#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>

namespace ws = obng::engine::render::which_side;
using ws::engine::MxcsrPort;

namespace {

constexpr std::uint16_t kDefaultControl = 0x037F;

// The original's sequence on the host x87 under `control`.
float X87SignedDistance(ws::PlaneValues plane, ws::SphereValues sphere, std::uint16_t control) {
	float out;
	float dot;
	asm volatile(
		"fldcw %[cw]\n\t"
		"flds %[ny]\n\t"
		"fmuls %[cy]\n\t"
		"flds %[nx]\n\t"
		"fmuls %[cx]\n\t"
		"faddp\n\t"
		"flds %[nz]\n\t"
		"fmuls %[cz]\n\t"
		"faddp\n\t"
		"fstps %[dot]\n\t"
		"flds %[dot]\n\t"
		"fsubs %[k]\n\t"
		"fstps %[out]\n\t"
		"fldcw %[def]"
		: [out] "=m"(out), [dot] "=m"(dot)
		: [cw] "m"(control), [def] "m"(kDefaultControl), [nx] "m"(plane.normalX), [ny] "m"(plane.normalY),
		  [nz] "m"(plane.normalZ), [k] "m"(plane.constant), [cx] "m"(sphere.centerX), [cy] "m"(sphere.centerY),
		  [cz] "m"(sphere.centerZ));
	return out;
}

// xorshift64*, fixed seed: the same inputs on every run.
struct Random {
	std::uint64_t state = 0x9E3779B97F4A7C15ull;
	std::uint64_t Next() {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return state * 0x2545F4914F6CDD1Dull;
	}
	unsigned Below(unsigned n) { return static_cast<unsigned>(Next() % n); }

	float Float() {
		auto bits = static_cast<std::uint32_t>(Next());
		switch (Below(10)) {
		case 0:
			bits &= 0x807FFFFFu;  // denormal or zero
			break;
		case 1:
			bits = (bits & 0x807FFFFFu) | ((0x40u + Below(40)) << 23);  // around 2^-63: products underflow
			break;
		case 2:
			bits = (bits & 0x807FFFFFu) | ((0xF0u + Below(14)) << 23);  // huge: products overflow
			break;
		case 3:
			bits = (bits & 0x80000007u) | ((0x60u + Below(60)) << 23);  // few significant bits: ties
			break;
		case 4:
			bits = (bits & 0x80000000u) | ((0x70u + Below(30)) << 23) | (bits & 0x7FFFFFu);  // moderate, 2^-15..2^15
			break;
		case 5:
			bits = (bits & 0x80000000u) | ((0x7Du + Below(16)) << 23) | (bits & 0x7FFFFFu);  // game-like, 2^-2..2^14
			break;
		default:
			break;
		}
		const float value = std::bit_cast<float>(bits);
		return std::isfinite(value) ? value : 1.0f;
	}
	ws::PlaneValues Plane() { return ws::PlaneValues{Float(), Float(), Float(), Float()}; }
	ws::SphereValues Sphere() { return ws::SphereValues{Float(), Float(), Float(), Float()}; }
};

std::uint32_t Bits(float f) {
	return std::bit_cast<std::uint32_t>(f);
}

bool Same(float a, float b) {
	return Bits(a) == Bits(b);
}

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

// A port that records the writes and answers the reads it is told to.
struct FakeMxcsr {
	std::vector<std::uint32_t> reads;  // answered in order; the last one repeats
	std::vector<std::uint32_t> writes;
	std::size_t next = 0;
	std::uint32_t Read() {
		const std::uint32_t value = reads[next < reads.size() ? next : reads.size() - 1];
		++next;
		return value;
	}
	void Write(std::uint32_t value) { writes.push_back(value); }
};

using Planes = std::array<ws::PlaneValues, 6>;

// The native path with the fallback, as ClassifyPlanes takes it, on one plane.
float NativeWithFallback(ws::PlaneValues plane, ws::SphereValues sphere, ws::X87Rounding mode, bool* native) {
	const Planes planes{plane, plane, plane, plane, plane, plane};
	MxcsrPort port;
	float out[6] = {};
	*native = ws::NativeDistances(planes, 0x01u, sphere, mode, port, out);
	return *native ? out[0] : ws::SignedDistance(plane, sphere, mode).distanceStored;
}

void Constants() {
	Section("The MXCSR constants against clang's xmmintrin.h macros (read there)");
	Check(ws::kMxcsrExceptionMasks == _MM_MASK_MASK, "the six masks are _MM_MASK_MASK 0x1F80");
	Check(ws::kMxcsrFallbackFlags == (_MM_EXCEPT_INVALID | _MM_EXCEPT_DENORM | _MM_EXCEPT_OVERFLOW | _MM_EXCEPT_UNDERFLOW),
		"the fallback flags are IE, DE, OE and UE");
	Check(ws::kMxcsrDivideByZero == _MM_EXCEPT_DIV_ZERO && ws::kMxcsrInexact == _MM_EXCEPT_INEXACT, "ZE and PE as named");
	Check(ws::NativeControl(ws::X87Rounding::Nearest) == (_MM_MASK_MASK | _MM_ROUND_NEAREST) &&
			ws::NativeControl(ws::X87Rounding::Down) == (_MM_MASK_MASK | _MM_ROUND_DOWN) &&
			ws::NativeControl(ws::X87Rounding::Up) == (_MM_MASK_MASK | _MM_ROUND_UP) &&
			ws::NativeControl(ws::X87Rounding::Zero) == (_MM_MASK_MASK | _MM_ROUND_TOWARD_ZERO),
		"NativeControl: the x87's rounding control in MXCSR's bits 13-14, every exception masked");
	for (const auto mode : {ws::X87Rounding::Nearest, ws::X87Rounding::Down, ws::X87Rounding::Up, ws::X87Rounding::Zero}) {
		const std::uint32_t control = ws::NativeControl(mode);
		Check((control & _MM_EXCEPT_MASK) == 0 && (control & _MM_FLUSH_ZERO_MASK) == 0 && (control & 0x40u) == 0,
			"no flag set, flush-to-zero off, denormals-are-zero off");
	}

	Section("NativeResultExact: the four flags each force the fallback, ZE and PE do not");
	Check(ws::NativeResultExact(0), "no flag: exact");
	Check(!ws::NativeResultExact(ws::kMxcsrInvalid), "IE: fallback");
	Check(!ws::NativeResultExact(ws::kMxcsrDenormal), "DE: fallback");
	Check(!ws::NativeResultExact(ws::kMxcsrOverflow), "OE: fallback");
	Check(!ws::NativeResultExact(ws::kMxcsrUnderflow), "UE: fallback");
	Check(ws::NativeResultExact(ws::kMxcsrInexact), "PE alone: exact (the ordinary case)");
	Check(ws::NativeResultExact(ws::kMxcsrDivideByZero | ws::kMxcsrInexact), "ZE and PE: exact (no division here)");
	Check(ws::NativeResultExact(ws::NativeControl(ws::X87Rounding::Zero) | ws::kMxcsrInexact),
		"the control bits and PE together: exact");
	Check(!ws::NativeResultExact(0x3Fu), "every flag: fallback");
}

void Scope() {
	Section("NativeDistances over a fake port: the register saved, set to NativeControl, read after, put back");
	const Planes planes{ws::PlaneValues{1, 0, 0, 0}, ws::PlaneValues{0, 1, 0, 0}, ws::PlaneValues{0, 0, 1, 0},
		ws::PlaneValues{1, 1, 0, 0}, ws::PlaneValues{0, 1, 1, 0}, ws::PlaneValues{1, 0, 1, 0}};
	const ws::SphereValues sphere{2, 3, 5, 1};
	{
		FakeMxcsr fake{{0x5FBFu, 0x7FA0u}, {}};  // the game's word, then our control with only PE raised
		float out[6] = {9, 9, 9, 9, 9, 9};
		const bool native = ws::NativeDistances(planes, 0x2Au, sphere, ws::X87Rounding::Zero, fake, out);
		Check(native, "PE alone after the arithmetic: the native results are kept");
		Check(fake.writes.size() == 2 && fake.writes[0] == ws::NativeControl(ws::X87Rounding::Zero) && fake.writes[1] == 0x5FBFu,
			"two writes: NativeControl(mode), then the saved word (with its flags) back");
		Check(fake.next == 2, "two reads: before the arithmetic and after it");
		Check(Same(out[1], 3.0f) && Same(out[3], 5.0f) && Same(out[5], 7.0f), "lanes 1, 3, 5 computed");
		Check(Same(out[0], 9.0f) && Same(out[2], 9.0f) && Same(out[4], 9.0f), "the other entries left alone");
	}
	{
		FakeMxcsr fake{{0x1F80u, 0x7F88u}, {}};  // OE raised after
		float out[6] = {};
		const bool native = ws::NativeDistances(planes, 0x3Fu, sphere, ws::X87Rounding::Nearest, fake, out);
		Check(!native, "OE after the arithmetic: the caller recomputes");
		Check(fake.writes.size() == 2 && fake.writes[1] == 0x1F80u, "the saved word is put back on this flow too");
	}
	{
		const std::uint32_t distinctive = 0x5FBFu;  // rounding up, every flag set, as a caller might leave it
		MxcsrPort port;
		const std::uint32_t before = port.Read();
		port.Write(distinctive);
		float out[6] = {};
		const bool native = ws::NativeDistances(planes, 0x3Fu, sphere, ws::X87Rounding::Zero, port, out);
		const std::uint32_t after = port.Read();
		port.Write(before);
		Check(native && Same(out[0], 2.0f) && Same(out[3], 5.0f) && Same(out[4], 8.0f), "the real port: small integers, exact, kept");
		Check(after == distinctive, "the real port: the caller's MXCSR (rounding and flags) is as it was");
	}
}

struct Forced {
	const char* name;
	ws::PlaneValues plane;
	ws::SphereValues sphere;
	bool exact;  // what NativeDistances returns
};

void ForcedCases() {
	Section("Forced cases on the real port: each fallback flag, and the ordinary case, against the host x87");
	constexpr float kInf = std::numeric_limits<float>::infinity();
	const Forced cases[] = {
		{"ordinary values: kept", {0.6f, 0.8f, 0.0f, 12.5f}, {100.25f, -3.0f, 7.0f, 1.0f}, true},
		{"a product overflows (OE): recomputed", {3e38f, 0, 0, 0}, {4.0f, 0, 0, 1}, false},
		{"a sum overflows (OE): recomputed", {1.0f, 1.0f, 0, 0}, {3e38f, 3e38f, 0, 1}, false},
		{"a product underflows inexactly (UE): recomputed", {1e-30f, 0, 0, 0}, {1.5e-10f, 0, 0, 1}, false},
		{"a denormal input (DE): recomputed", {FLT_TRUE_MIN, 0, 0, 0}, {1.0f, 0, 0, 1}, false},
		{"a denormal intermediate used again (DE): recomputed", {1e-23f, 1e-23f, 0, 0}, {1e-23f, 1e-23f, 0, 1}, false},
		{"inf * 0 (IE): recomputed", {kInf, 0, 0, 0}, {0.0f, 0, 0, 1}, false},
		{"inf - inf (IE): recomputed", {kInf, 0, 0, kInf}, {1.0f, 0, 0, 1}, false},
		{"inf * 1 - 1 (no flag, inf in both): kept", {kInf, 0, 0, 1.0f}, {1.0f, 0, 0, 1}, true},
		{"the game's garbage bound of run 20261030-131826 against a unit plane (the dot -1.8e38 stays finite): kept",
			{1.0f, 1.0f, 1.0f, 0.0f}, {FromBits(0xFD935C0Fu), FromBits(0xFD935C13u), FromBits(0xFEBFCBA9u), FromBits(0x5FDFFFF8u)},
			true},
		{"the same bound against a plane of normal 2 (OE): recomputed",
			{2.0f, 2.0f, 2.0f, 0.0f}, {FromBits(0xFD935C0Fu), FromBits(0xFD935C13u), FromBits(0xFEBFCBA9u), FromBits(0x5FDFFFF8u)},
			false},
	};
	const std::uint16_t words[] = {0x007F, 0x047F, 0x087F, 0x0C7F};
	for (const Forced& c : cases) {
		bool allExactAsExpected = true;
		bool allSame = true;
		for (const std::uint16_t word : words) {
			const ws::X87Rounding mode = ws::RoundingFromControlWord(word);
			bool native = false;
			const float got = NativeWithFallback(c.plane, c.sphere, mode, &native);
			allExactAsExpected = allExactAsExpected && native == c.exact;
			allSame = allSame && Same(got, X87SignedDistance(c.plane, c.sphere, word));
		}
		char line[200];
		std::snprintf(line, sizeof(line), "%s - under the four modes", c.name);
		Check(allExactAsExpected, line);
		std::snprintf(line, sizeof(line), "%s - equal to the host x87 under the four modes", c.name);
		Check(allSame, line);
	}
}

void RandomDifferential() {
	Section("The host x87 (0x0047DA74..0x0047DA97) against the exact path, the native path with its fallback, and the flags' soundness");
	constexpr int kInputs = 200000;
	for (const std::uint16_t word : {std::uint16_t{0x007F}, std::uint16_t{0x047F}, std::uint16_t{0x087F}, std::uint16_t{0x0C7F}}) {
		const ws::X87Rounding mode = ws::RoundingFromControlWord(word);
		Random random;
		int exactMismatches = 0, nativeMismatches = 0, unsound = 0, kept = 0, recomputed = 0, rawDiffers = 0, nonFinite = 0;
		for (int i = 0; i < kInputs; ++i) {
			const ws::PlaneValues plane = random.Plane();
			const ws::SphereValues sphere = random.Sphere();
			const float x87 = X87SignedDistance(plane, sphere, word);
			nonFinite += std::isfinite(x87) ? 0 : 1;
			exactMismatches += Same(ws::SignedDistance(plane, sphere, mode).distanceStored, x87) ? 0 : 1;
			const Planes planes{plane, plane, plane, plane, plane, plane};
			MxcsrPort port;
			float raw[6] = {};
			const bool native = ws::NativeDistances(planes, 0x01u, sphere, mode, port, raw);
			const float withFallback = native ? raw[0] : ws::SignedDistance(plane, sphere, mode).distanceStored;
			nativeMismatches += Same(withFallback, x87) ? 0 : 1;
			kept += native ? 1 : 0;
			recomputed += native ? 0 : 1;
			if (!Same(raw[0], x87)) {
				++rawDiffers;
				unsound += native ? 1 : 0;  // a differing raw result that no flag reported
			}
		}
		char line[300];
		std::snprintf(line, sizeof(line),
			"fpcw %04X: %d inputs - exact path %d mismatches; native with fallback %d mismatches (%d kept, %d recomputed; "
			"the raw native result differed %d times, %d of them unreported); %d non-finite results",
			static_cast<unsigned>(word), kInputs, exactMismatches, nativeMismatches, kept, recomputed, rawDiffers, unsound,
			nonFinite);
		Check(exactMismatches == 0 && nativeMismatches == 0 && unsound == 0, line);
		Check(kept > 0 && recomputed > 0 && rawDiffers > 0,
			"both paths taken and raw differences reached (the inputs are biased to the edges, so most are recomputed)");
	}
}

void GameLikeShare() {
	Section("Game-like values (moderate magnitudes): the native path is kept on every input");
	Random random;
	int kept = 0, mismatches = 0;
	constexpr int kInputs = 50000;
	for (int i = 0; i < kInputs; ++i) {
		const auto moderate = [&] {
			const std::uint32_t bits = static_cast<std::uint32_t>(random.Next());
			return FromBits((bits & 0x80000000u) | ((0x7Du + random.Below(16)) << 23) | (bits & 0x7FFFFFu));  // 2^-2 .. 2^14
		};
		const ws::PlaneValues plane{moderate(), moderate(), moderate(), moderate()};
		const ws::SphereValues sphere{moderate(), moderate(), moderate(), moderate()};
		bool native = false;
		const float got = NativeWithFallback(plane, sphere, ws::X87Rounding::Zero, &native);
		kept += native ? 1 : 0;
		mismatches += Same(got, X87SignedDistance(plane, sphere, 0x0C7F)) ? 0 : 1;
	}
	char line[200];
	std::snprintf(line, sizeof(line), "%d inputs between 2^-2 and 2^14: %d kept natively, %d mismatches against the x87", kInputs, kept,
		mismatches);
	Check(kept == kInputs && mismatches == 0, line);
}

} // namespace

int main() {
	Constants();
	Scope();
	ForcedCases();
	RandomDifferential();
	GameLikeShare();
	return Failures();
}
