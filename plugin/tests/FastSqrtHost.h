#pragma once

// Running square-root code on the host's own x87, for FastSqrtLogicTest and
// FastSqrtOracleTest: the argument is loaded into ST0 under a chosen control
// word with the exception flags cleared, the code is called as _CIsqrt is
// (the argument in ST0, the result in ST0), and what it left is captured -
// the 80-bit result, the control word, the status word before the result is
// popped, and EDX (which the original preserves). GNU inline assembly; the
// plugin's tests are built 32-bit x86 (plugin/tests/CMakeLists.txt).

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fast_sqrt_host {

constexpr std::uint16_t kDefaultControl = 0x037F;  // FNINIT's word, put back after every call

struct Outcome {
	unsigned char result[10];  // ST0 after the call, as FSTP tbyte stores it
	std::uint16_t controlWord;  // after the call
	std::uint16_t statusWord;   // after the call, before the result is popped
	std::uint32_t edx;          // after the call (0x5A5A5A5A on entry)

	std::uint64_t Significand() const {
		std::uint64_t s;
		std::memcpy(&s, result, 8);
		return s;
	}
	std::uint16_t SignExponent() const {
		std::uint16_t e;
		std::memcpy(&e, result + 8, 2);
		return e;
	}
	bool SameResult(const Outcome& other) const { return std::memcmp(result, other.result, 10) == 0; }
	// The exception flags (IE DE ZE OE UE PE) and the stack top - 7 with the
	// one result on an otherwise empty stack (a push decrements TOP from 0).
	std::uint16_t Flags() const { return static_cast<std::uint16_t>(statusWord & 0x003F); }
	std::uint16_t Top() const { return static_cast<std::uint16_t>((statusWord >> 11) & 7); }
	bool OneValueOnStack() const { return Top() == 7; }
	std::uint16_t ConditionCodes() const { return static_cast<std::uint16_t>(statusWord & 0x4700); }
};

// Calls `code` with the double `x` in ST0 under `controlWord`.
inline Outcome Call(const void* code, double x, std::uint16_t controlWord) {
	Outcome out{};
	asm volatile(
		"fnclex\n\t"
		"fldcw %[cw]\n\t"
		"fldl %[x]\n\t"
		"mov $0x5A5A5A5A, %%edx\n\t"
		"call *%[code]\n\t"
		"mov %%edx, %[edx]\n\t"
		"fnstsw %[sw]\n\t"
		"fnstcw %[cwo]\n\t"
		"fstpt %[res]\n\t"
		"fnclex\n\t"
		"fldcw %[def]"
		: [edx] "=m"(out.edx), [sw] "=m"(out.statusWord), [cwo] "=m"(out.controlWord), [res] "=m"(out.result)
		: [cw] "m"(controlWord), [x] "m"(x), [code] "m"(code), [def] "m"(kDefaultControl)
		: "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
	return out;
}

// The same with an 80-bit argument (FLD tbyte), for values no double holds.
inline Outcome CallExtended(const void* code, const unsigned char (&x)[10], std::uint16_t controlWord) {
	Outcome out{};
	asm volatile(
		"fnclex\n\t"
		"fldcw %[cw]\n\t"
		"fldt %[x]\n\t"
		"mov $0x5A5A5A5A, %%edx\n\t"
		"call *%[code]\n\t"
		"mov %%edx, %[edx]\n\t"
		"fnstsw %[sw]\n\t"
		"fnstcw %[cwo]\n\t"
		"fstpt %[res]\n\t"
		"fnclex\n\t"
		"fldcw %[def]"
		: [edx] "=m"(out.edx), [sw] "=m"(out.statusWord), [cwo] "=m"(out.controlWord), [res] "=m"(out.result)
		: [cw] "m"(controlWord), [x] "m"(x), [code] "m"(code), [def] "m"(kDefaultControl)
		: "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
	return out;
}

// A page of executable memory for generated code; null when refused.
inline unsigned char* ExecutablePage(std::size_t size) {
	void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? nullptr : static_cast<unsigned char*>(p);
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

	// A double's bit pattern, biased to the edges: zeros, denormals, few-bit
	// significands (exact roots and ties), exponents around the float range
	// and far beyond it, the whole double range, both signs, inf and NaN.
	std::uint64_t DoubleBits() {
		std::uint64_t bits = Next();
		const std::uint64_t sign = Below(8) == 0 ? (1ull << 63) : 0;  // one in eight negative
		switch (Below(12)) {
		case 0:
			bits = sign;  // a zero
			break;
		case 1:
			bits = sign | (bits & 0x000FFFFFFFFFFFFFull);  // a denormal
			break;
		case 2:
			bits = sign | (static_cast<std::uint64_t>(0x3F0 + Below(0x20)) << 52) | (bits & 0x1F);  // few bits: exact roots, ties
			break;
		case 3:
			bits = sign | (static_cast<std::uint64_t>(0x3F0 + Below(0x20)) << 52) | ((bits & 0xFFF) << 40);  // 12-bit significands
			break;
		case 4:
			bits = sign | (static_cast<std::uint64_t>(0x380 + Below(0x100)) << 52) | (bits & 0x000FFFFFFFFFFFFFull);  // the float range
			break;
		case 5:
			bits = sign | (static_cast<std::uint64_t>(0x001 + Below(0x080)) << 52) | (bits & 0x000FFFFFFFFFFFFFull);  // tiny
			break;
		case 6:
			bits = sign | (static_cast<std::uint64_t>(0x780 + Below(0x07F)) << 52) | (bits & 0x000FFFFFFFFFFFFFull);  // huge
			break;
		case 7:
			bits = sign | 0x7FF0000000000000ull;  // inf
			break;
		case 8:
			bits = sign | 0x7FF0000000000000ull | (bits & 0x000FFFFFFFFFFFFFull) | (Below(2) ? 0x0008000000000000ull : 0x1ull);  // NaN, quiet or signalling
			break;
		case 9:
			bits = sign | (static_cast<std::uint64_t>(0x3FD + Below(0x14)) << 52) | (bits & 0x000FFFFFFFFFFFFFull);  // game-like, 2^-2..2^18
			break;
		default:
			bits = (bits & 0x7FFFFFFFFFFFFFFFull) | sign;  // anywhere
			break;
		}
		return bits;
	}
};

inline double FromBits(std::uint64_t bits) {
	double d;
	std::memcpy(&d, &bits, 8);
	return d;
}

inline void PrintOutcome(const char* label, std::uint64_t inputBits, std::uint16_t controlWord, const Outcome& o) {
	std::printf("  %s: x=%016llx cw=%04X -> st0=%04X:%016llx cw=%04X sw=%04X edx=%08X\n", label,
	            static_cast<unsigned long long>(inputBits), controlWord, o.SignExponent(),
	            static_cast<unsigned long long>(o.Significand()), o.controlWord, o.statusWord, o.edx);
}

}  // namespace fast_sqrt_host
