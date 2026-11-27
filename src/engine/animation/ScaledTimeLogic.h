#pragma once
// NiTimeController's scaled time, 0x007155A0 (slot +0x64 of every time
// controller; a proposal: Gamebryo's ComputeScaledTime) - read 2026-11-27 in
// the instructions 0x007155A0..0x00715816.
//
// The original takes a critical section (0x00B3FCA0) around a one-entry memo
// in statics (0x00B3FC88 the result, 0x00B3FC8C the time, 0x00B3FC90 the low
// key, 0x00B3FC94 the high key, 0x00B27130 the cycle type): a call with the
// last call's low, high, time and cycle type returns the last result. Every
// controller update of the game goes through it - an NPC's skeleton about 200
// times a frame - and several threads updating controllers would wait on it.
// This is the same computation without the lock; the memo is the caller's (one
// a thread, or none).
//
// The memo's key leaves out the reverse flag (bit 4), which is applied after
// the keyed part: a hit returns the last result even when the two controllers
// differ in that flag (read: the hit jumps past 0x007157DB). With a memo this
// is kept; without one such a call gets its own flag's result.
//
// The arithmetic is the x87's at precision control 24 (the game's control
// words, knowledge/frame-clock.md): every product, sum and difference rounds
// to a float, under the caller's rounding. The remainder (the CRT's _CIfmod
// 0x00982BFA, FPREM) is exact and a float.
//
// 64-bit clean: plain values only.
#include <cmath>
#include <cstdint>

namespace obng::engine::animation::scaled_time {

inline constexpr float kUnset = 3.4028235e+38f;  // [0x00A7DEB4]; its negative marks a time not set, itself a low key not set

// The controller's fields the function reads and writes.
struct Controller {
	std::uint8_t flags = 0;  // +0x08: bit 0 a first call's step is 0, bits 1-2 the cycle type, bit 4 reverse
	float frequency = 1.0f;  // +0x0C
	float phase = 0.0f;      // +0x10
	float low = 0.0f;        // +0x14
	float high = 0.0f;       // +0x18
	float start = -kUnset;   // +0x1C (written)
	float last = -kUnset;    // +0x20 (written)
	float scaled = 0.0f;     // +0x24 (written)
};

struct Memo {
	float result = 0.0f;
	float time = 0.0f;
	float low = 0.0f;
	float high = 0.0f;
	std::uint32_t cycle = 0;
};

inline constexpr std::uint32_t kLoop = 0;
inline constexpr std::uint32_t kReverse = 1;

// The keyed part: the time carried into [low, high] by the cycle type, then clamped.
inline float Cycle(float time, float low, float high, std::uint32_t cycle) {
	if (high != -kUnset && low != kUnset) {
		if (cycle == kLoop) {
			const float span = high - low;
			if (span == 0.0f) {
				time = low;
			} else {
				const float offset = time - low;
				time = std::fmod(offset, span) + low;
				if (low > time) {
					time = time + span;
				}
			}
		} else if (cycle == kReverse) {
			const float span = high - low;
			if (span == 0.0f) {
				time = low;
			} else {
				const float twice = span + span;
				float rest = std::fmod(time, twice);  // the time itself, not its offset from low (read 0x007156FA)
				if (rest < 0.0f) {
					rest = rest + twice;
				}
				if (span < rest) {
					rest = twice - rest;
				}
				time = rest + low;
			}
		}
	}
	if (high < time) {
		return high;
	}
	if (low > time) {
		return low;
	}
	return time;
}

// The whole function. `memo` null: no memo.
inline float Compute(Controller& c, float time, Memo* memo) {
	if (c.start == -kUnset) {
		c.start = time;
	}
	float step;
	if (c.last == -kUnset) {
		c.scaled = 0.0f;
		step = (c.flags & 0x01u) != 0 ? 0.0f : time;
	} else {
		step = time - c.last;
	}
	const std::uint32_t cycle = (static_cast<std::uint32_t>(c.flags) >> 1) & 3u;
	const float product = c.frequency * step;
	const float scaled = product + c.scaled;
	c.scaled = scaled;
	const float keyed = scaled + c.phase;
	c.last = time;
	if (memo != nullptr) {
		if (c.high == memo->high && c.low == memo->low && keyed == memo->time && cycle == memo->cycle) {
			return memo->result;
		}
		memo->high = c.high;
		memo->cycle = cycle;
		memo->low = c.low;
		memo->time = keyed;
	}
	float result = Cycle(keyed, c.low, c.high, cycle);
	if ((c.flags & 0x10u) != 0) {
		const float offset = result - c.low;
		result = c.high - offset;
	}
	if (memo != nullptr) {
		memo->result = result;
	}
	return result;
}

}  // namespace obng::engine::animation::scaled_time
