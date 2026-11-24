#pragma once
// Whether two byte ranges are equal - memcmp(a, b, n) == 0, sixteen bytes a step (SSE2, which every build here has).
//
// Why not memcmp: the DLL's memcmp is the static C runtime's 32-bit one. In the tutorial's fight it was the second
// function of OBNG.dll inside the scene pass, 0.18 ms a frame for about 245 KB compared (the volatile pack's
// source check, VolatilePackLogic.h SameSource: a head's positions and normals against the kept ones; measured
// 2026-11-24, run obng-profile-prison-fight-hold/20261124-130308) - about 1.4 GB/s.
//
// Reads [a, a + n) and [b, b + n) and nothing beyond; no alignment asked for. Only equality, not memcmp's order.
//
// 64-bit clean: pointers and a size, no engine types.

#include <cstddef>
#include <emmintrin.h>

namespace obng::engine::memory {

namespace equal_bytes {

inline __m128i Same16(const unsigned char* a, const unsigned char* b) {
	return _mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(a)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(b)));
}

inline bool All(__m128i same) {
	return _mm_movemask_epi8(same) == 0xFFFF;
}

}  // namespace equal_bytes

inline bool EqualBytes(const unsigned char* a, const unsigned char* b, std::size_t n) {
	using namespace equal_bytes;
	if (n < 16) {
		for (std::size_t i = 0; i < n; ++i) {
			if (a[i] != b[i]) {
				return false;
			}
		}
		return true;
	}
	std::size_t i = 0;
	for (; i + 64 <= n; i += 64) {
		const __m128i same = _mm_and_si128(_mm_and_si128(Same16(a + i, b + i), Same16(a + i + 16, b + i + 16)),
		                                   _mm_and_si128(Same16(a + i + 32, b + i + 32), Same16(a + i + 48, b + i + 48)));
		if (!All(same)) {
			return false;
		}
	}
	for (; i + 16 <= n; i += 16) {
		if (!All(Same16(a + i, b + i))) {
			return false;
		}
	}
	// The tail: the range's last sixteen bytes, overlapping what was compared.
	return i == n || All(Same16(a + n - 16, b + n - 16));
}

}  // namespace obng::engine::memory
