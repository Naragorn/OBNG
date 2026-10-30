// The rounding helpers of NiBound_WhichSideLogic.h against the host's own x87,
// under the four rounding controls at precision control 24 - the hardware the
// game runs on, not a model of it.
//
// What runs on the x87 (GNU inline assembly; the tests are built 32-bit x86,
// tests/CMakeLists.txt):
//   FLD dword / FMUL dword / FSTP qword     -> MulPc24 (a float times a float)
//   FLD qword / FADD qword / FSTP qword     -> AddPc24 (two 24-bit register values)
//   FLD qword / FMUL 1.0 / FSTP qword       -> RoundToPc24 (any double, rounded to 24 bits)
//   FLD qword / FSTP dword                  -> StoreSingle (any double to float)
// each after FLDCW with 0x007F, 0x047F, 0x087F or 0x0C7F (all exceptions
// masked, as in every control word the game was measured with), and the
// control word put back to FNINIT's 0x037F after it. The 64-bit double store
// is exact for every result here (a 24-bit significand inside double's
// range). The inputs come from a fixed-seed generator, biased to the edges:
// denormal and huge floats, few-bit significands (ties), exact cancellation,
// signed zeros, sums with far-apart exponents (the sticky bit beyond double).
//
// The rebuild's own code is SSE (FLT_EVAL_METHOD 0, asserted in the header),
// so the x87 control word set here never reaches it.

#include "Check.h"
#include "engine/render/NiBound_WhichSideLogic.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ws = obng::engine::render::which_side;

namespace {

constexpr std::uint16_t kDefaultControl = 0x037F;

struct X87 {
	std::uint16_t control;

	void Restore() const { asm volatile("fldcw %0" ::"m"(kDefaultControl)); }
	double Mul(float a, float b) const {
		double r;
		asm volatile("fldcw %3\n\tflds %1\n\tfmuls %2\n\tfstpl %0" : "=m"(r) : "m"(a), "m"(b), "m"(control));
		Restore();
		return r;
	}
	double Add(double a, double b) const {
		double r;
		asm volatile("fldcw %3\n\tfldl %1\n\tfaddl %2\n\tfstpl %0" : "=m"(r) : "m"(a), "m"(b), "m"(control));
		Restore();
		return r;
	}
	double Round(double a) const {
		const double one = 1.0;
		double r;
		asm volatile("fldcw %3\n\tfldl %1\n\tfmull %2\n\tfstpl %0" : "=m"(r) : "m"(a), "m"(one), "m"(control));
		Restore();
		return r;
	}
	float Store(double a) const {
		float r;
		asm volatile("fldcw %2\n\tfldl %1\n\tfstps %0" : "=m"(r) : "m"(a), "m"(control));
		Restore();
		return r;
	}
};

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
		switch (Below(8)) {
		case 0:
			bits &= 0x807FFFFFu;  // denormal or zero
			break;
		case 1:
			bits = (bits & 0x807FFFFFu) | ((0x60u + Below(40)) << 23);  // around 2^-30
			break;
		case 2:
			bits = (bits & 0x807FFFFFu) | ((0xF0u + Below(14)) << 23);  // huge
			break;
		case 3:
			bits = (bits & 0x80000007u) | ((0x60u + Below(60)) << 23);  // few significant bits: ties
			break;
		default:
			break;
		}
		const float value = std::bit_cast<float>(bits);
		return std::isfinite(value) ? value : 1.0f;
	}
	// A register value: a float, or a product of two floats rounded to 24 bits
	// (the register's exponent range, 2^-298 .. 2^256).
	double Register() {
		const float a = Float();
		if (Below(3) == 0) {
			return a;
		}
		return ws::MulPc24(a, Float(), ws::X87Rounding::Nearest);
	}
	// Any normal double between 2^-300 and 2^300.
	double Wide() {
		const std::uint64_t exponent = 1023 - 300 + Below(600);
		return std::bit_cast<double>((Next() & 0x800FFFFFFFFFFFFFull) | (exponent << 52));
	}
};

bool Same(double a, double b) {
	return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool Same(float a, float b) {
	return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

void Compare(std::uint16_t control, int inputs) {
	const X87 x87{control};
	const ws::X87Rounding mode = ws::RoundingFromControlWord(control);
	Random random;
	int mul = 0, add = 0, round = 0, store = 0, zeros = 0;
	for (int i = 0; i < inputs; ++i) {
		const float a = random.Float();
		const float b = random.Float();
		mul += Same(x87.Mul(a, b), ws::MulPc24(a, b, mode)) ? 0 : 1;

		double x = random.Register();
		double y = random.Register();
		switch (random.Below(8)) {
		case 0:
			y = -x;  // exact cancellation
			break;
		case 1:
			x = random.Below(2) != 0 ? 0.0 : -0.0;
			y = random.Below(2) != 0 ? 0.0 : -0.0;
			break;
		default:
			break;
		}
		const double sum = x87.Add(x, y);
		zeros += sum == 0.0 ? 1 : 0;
		add += Same(sum, ws::AddPc24(x, y, mode)) ? 0 : 1;

		const double wide = random.Wide();
		round += Same(x87.Round(wide), ws::RoundToPc24(wide, mode)) ? 0 : 1;
		store += Same(x87.Store(wide), ws::StoreSingle(wide, mode)) ? 0 : 1;
		const double held = random.Register();
		store += Same(x87.Store(held), ws::StoreSingle(held, mode)) ? 0 : 1;
	}
	char line[200];
	std::snprintf(line, sizeof(line), "fpcw %04X: %d inputs - FMUL %d, FADD %d (%d exact zeros among them), FMUL by 1 %d, FSTP dword %d mismatches",
		static_cast<unsigned>(control), inputs, mul, add, zeros, round, store);
	Check(mul == 0 && add == 0 && round == 0 && store == 0 && zeros > inputs / 10, line);
}

} // namespace

int main() {
	Section("The host x87 at precision control 24 against the rebuild's helpers, per rounding control (measured on this machine)");
	for (const std::uint16_t control : {std::uint16_t{0x007F}, std::uint16_t{0x047F}, std::uint16_t{0x087F}, std::uint16_t{0x0C7F}}) {
		Compare(control, 200000);
	}
	return Failures();
}
