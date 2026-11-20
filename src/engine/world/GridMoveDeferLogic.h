#pragma once
// [Route] GridMoveDefer: the grid's per-frame move with the player at the next update step's start - the decisions
// and the byte work over plain values (the plugin's GridMoveDefer.cpp calls).
//
// Read 2026-11-20 in the update step 0x0040D800: after the player's update (0x0040DC61 - 0x0040DD53, where OBVR's
// camera hook waits for the headset's poses) the step runs one block, 0x0040DD6E - 0x0040DE3E:
//   0040DD6E  call IsMenuMode 0x00578F60; in a menu, or with player +0x7F8 set, on to 0x0040DE3F
//   0040DD98  call 0x00444FB0             TES->move(player + 0x2C, 1): the grid's move with the player
//   0040DD9D  TES +0x51 / +0x52 / +0x34, the player's cell 0x004D6670 against TES's 0x0043F270; when they differ
//   0040DE3A  call 0x0066EAF0             the player moved into the grid's cell (RET 0x20)
//   0040DE3F  the physics step's gate
// The block reads no stack slot of the step, needs EBX = 0 (the step's zero register, set at 0x0040D81C and never
// written after) and uses EAX, ECX, EDX, ESI, EDI as scratch; every way out of it goes to 0x0040DE3F and no jump
// from outside goes into it (read: the step's disassembly, the six jumps of the block). Its pushes and its callees'
// RETs balance (0x00444FB0 RET 8, 0x004F1A80 RET 4, 0x0066EAF0 RET 0x20).
//
// Measured 2026-11-20 (tools/re/crossing-slack.py, 40 crossings): a frame's slack is the 8.9 ms of OBVR's pose wait,
// for work before the wait only. At a crossing the move's 4.5 - 7.6 ms come after it, the frame is late and the next
// one waits a whole period: 36 of 40 crossings miss one.
//
// The route runs the game's own block at the start of the next update step (StepHooks.h, before the task pump and
// the wait) as a copy with its calls' distances fixed and a RET behind it, and puts a jump over the block at its
// place. Seen as a ring of steps nothing changes its neighbours' order but one cut: the rest of a step (the physics,
// the render) now runs before the move of that step's position instead of after it, so the grid follows the player
// one frame later. Class B: the same code with the same inputs' sources, another place in the frame.
// 64-bit clean: addresses as 32-bit handles, the copy's distances in 32-bit arithmetic.
#include <cstddef>
#include <cstdint>

namespace obng::engine::world::grid_move_defer {

enum class Mode : std::uint8_t { Off, On };

inline Mode ModeOf(int value) {
	return value == 1 ? Mode::On : Mode::Off;
}

constexpr std::uint32_t kBlockStart = 0x0040DD6E;  // call IsMenuMode, the block's gate (read)
constexpr std::uint32_t kBlockEnd = 0x0040DE3F;    // the physics step's gate: every way out of the block (read)
constexpr std::uint32_t kBlockSize = kBlockEnd - kBlockStart;
constexpr std::uint32_t kBlockChecksum = 0x8094B1AD;  // FNV-1a of the block in Oblivion.exe 1.2.0.416 (computed from the file)
// The block's near calls (E8 rel32), as offsets: 0x00578F60, 0x00444FB0, 0x0043F270, 0x004D6670, 0x0043F270,
// 0x004F1A80, 0x0066EAF0. Its two indirect calls (FF D0, the player's slot 0x174) need no fixing.
constexpr std::uint32_t kCallOffsets[] = {0x00, 0x2A, 0x4C, 0x59, 0x8F, 0x96, 0xCC};
constexpr std::size_t kCallCount = sizeof(kCallOffsets) / sizeof(kCallOffsets[0]);
static_assert(kBlockSize == 0xD1, "the block is 209 bytes");

// FNV-1a, 32 bit.
inline std::uint32_t Checksum(const std::uint8_t* bytes, std::size_t size) {
	std::uint32_t sum = 0x811C9DC5u;
	for (std::size_t i = 0; i < size; ++i) {
		sum = (sum ^ bytes[i]) * 0x01000193u;
	}
	return sum;
}

enum class Copy : std::uint8_t { Ok, NoRoom, NotACall };

// The block at `from` copied to run at `to`: each listed near call's distance fixed so it reaches the same target,
// every other byte as it is (the block's jumps stay inside it or go to its end), and a RET at its end. `out` needs
// size + 1 bytes. Nothing is written unless everything checks.
inline Copy Relocate(const std::uint8_t* block, std::size_t size, std::uint32_t from, std::uint32_t to, const std::uint32_t* callOffsets,
                     std::size_t calls, std::uint8_t* out, std::size_t capacity) {
	if (block == nullptr || out == nullptr || capacity < size + 1) {
		return Copy::NoRoom;
	}
	for (std::size_t i = 0; i < calls; ++i) {
		if (callOffsets[i] + 5u > size || block[callOffsets[i]] != 0xE8) {
			return Copy::NotACall;
		}
	}
	for (std::size_t i = 0; i < size; ++i) {
		out[i] = block[i];
	}
	for (std::size_t i = 0; i < calls; ++i) {
		const std::uint8_t* at = block + callOffsets[i] + 1;
		const std::uint32_t rel = static_cast<std::uint32_t>(at[0]) | static_cast<std::uint32_t>(at[1]) << 8 | static_cast<std::uint32_t>(at[2]) << 16 |
		                          static_cast<std::uint32_t>(at[3]) << 24;
		const std::uint32_t moved = rel + from - to;  // the same target from the copy's place
		for (unsigned b = 0; b < 4; ++b) {
			out[callOffsets[i] + 1 + b] = static_cast<std::uint8_t>(moved >> (8 * b));
		}
	}
	out[size] = 0xC3;
	return Copy::Ok;
}

// The five bytes at `site` that jump to `target`.
inline void SkipJump(std::uint32_t site, std::uint32_t target, std::uint8_t out[5]) {
	const std::uint32_t rel = target - (site + 5u);
	out[0] = 0xE9;
	for (unsigned b = 0; b < 4; ++b) {
		out[1 + b] = static_cast<std::uint8_t>(rel >> (8 * b));
	}
}

// What the runs at the step's start cost (for the log's summary).
struct Runs {
	std::uint64_t calls = 0;
	std::uint64_t longCalls = 0;
	double totalMs = 0.0;
	double longestMs = 0.0;
};

inline void Count(Runs& runs, double ms, double longMs) {
	++runs.calls;
	runs.longCalls += ms >= longMs ? 1u : 0u;
	runs.totalMs += ms;
	runs.longestMs = ms > runs.longestMs ? ms : runs.longestMs;
}

}  // namespace obng::engine::world::grid_move_defer
