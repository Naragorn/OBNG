// The cost of one object's plane batch (six lanes) on this machine: the exact
// software path (SignedDistance), the native pass with its MXCSR scope as the
// walk runs it (NativeDistances through MxcsrPort), and the native arithmetic
// alone without the register accesses, as the floor. Game-like inputs (2^-2 to
// 2^14, no flag raised), so the native pass is kept on every batch.
//
//   clang++ -m32 -O2 -std=c++20 -I src tools/bench/range-walk-batch-bench.cpp -o build-bench/range-walk-batch-bench
//   build-bench/range-walk-batch-bench
//
// A measurement on the host, not in the game: it says how the three compare,
// the game run says what the walk costs there.

#include "engine/render/Mxcsr.h"
#include "engine/render/NiBound_WhichSideLogic.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ws = obng::engine::render::which_side;

namespace {

struct Random {
	std::uint64_t state = 0x9E3779B97F4A7C15ull;
	std::uint64_t Next() {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return state * 0x2545F4914F6CDD1Dull;
	}
	float Moderate() {
		const auto bits = static_cast<std::uint32_t>(Next());
		return std::bit_cast<float>((bits & 0x80000000u) | ((0x7Du + (bits >> 8) % 16) << 23) | (bits & 0x7FFFFFu));
	}
};

using Planes = std::array<ws::PlaneValues, 6>;

struct Case {
	Planes planes;
	ws::SphereValues sphere;
};

// The arithmetic alone, under whatever MXCSR the process has (nearest).
float NativeOnly(const Planes& planes, ws::SphereValues sphere, float* out) {
	float sum = 0;
	for (unsigned p = 0; p < 6; ++p) {
		out[p] = ws::SignedDistanceNative(planes[p], sphere);
		sum += out[p];
	}
	return sum;
}

template <class Run>
double Measure(const char* name, const std::vector<Case>& cases, int rounds, Run&& run) {
	float sink = 0;
	const auto start = std::chrono::steady_clock::now();
	for (int r = 0; r < rounds; ++r) {
		for (const Case& c : cases) {
			sink += run(c);
		}
	}
	const auto end = std::chrono::steady_clock::now();
	const double ns = std::chrono::duration<double, std::nano>(end - start).count() / (static_cast<double>(rounds) * cases.size());
	std::printf("%-52s %7.1f ns per six-lane batch (sink %g)\n", name, ns, static_cast<double>(sink));
	return ns;
}

} // namespace

int main() {
	Random random;
	std::vector<Case> cases(4096);
	for (Case& c : cases) {
		for (ws::PlaneValues& p : c.planes) {
			p = ws::PlaneValues{random.Moderate(), random.Moderate(), random.Moderate(), random.Moderate()};
		}
		c.sphere = ws::SphereValues{random.Moderate(), random.Moderate(), random.Moderate(), random.Moderate()};
	}
	constexpr int kRounds = 2000;
	const ws::X87Rounding mode = ws::X87Rounding::Zero;
	ws::engine::MxcsrPort port;
	std::uint64_t recomputed = 0;

	const double exact = Measure("exact path (SignedDistance, software rounding)", cases, kRounds, [&](const Case& c) {
		float sum = 0;
		for (unsigned p = 0; p < 6; ++p) {
			sum += ws::SignedDistance(c.planes[p], c.sphere, mode).distanceStored;
		}
		return sum;
	});
	const double native = Measure("native pass with the MXCSR scope (NativeDistances)", cases, kRounds, [&](const Case& c) {
		float out[6];
		if (!ws::NativeDistances(c.planes, 0x3Fu, c.sphere, mode, port, out)) {
			++recomputed;
		}
		return out[0] + out[1] + out[2] + out[3] + out[4] + out[5];
	});
	const double floor = Measure("native arithmetic alone (no register access)", cases, kRounds, [&](const Case& c) {
		float out[6];
		return NativeOnly(c.planes, c.sphere, out);
	});
	std::printf("exact / native %.1f x; the MXCSR scope costs %.1f ns of the native pass's %.1f; %llu batches recomputed\n",
		exact / native, native - floor, native, static_cast<unsigned long long>(recomputed));
	return 0;
}
