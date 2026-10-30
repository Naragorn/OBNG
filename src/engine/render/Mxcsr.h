#pragma once

// The SSE control and status register as a port: what the native reading of
// the x87 contract (NiBound_WhichSideLogic.h, "The native reading") sets and
// reads around its arithmetic. The intrinsics are STMXCSR / LDMXCSR, no CRT
// call (clang's and MSVC's xmmintrin.h; read in clang's: _mm_getcsr,
// _mm_setcsr). The same register exists on a 64-bit host, so nothing here
// depends on the pointer size.
//
// A test may pass any type with the same two members instead (a fake that
// records the writes and answers the flags it is told to).

#include <xmmintrin.h>

#include <cstdint>

namespace obng::engine::render::which_side::engine {

struct MxcsrPort {
	static std::uint32_t Read() { return static_cast<std::uint32_t>(_mm_getcsr()); }
	static void Write(std::uint32_t value) { _mm_setcsr(static_cast<unsigned int>(value)); }
};

} // namespace obng::engine::render::which_side::engine
