// Every flow of the grid's move at the next step's start (src/engine/world/GridMoveDeferLogic.h).

#include <cstring>

#include "Check.h"
#include "engine/world/GridMoveDeferLogic.h"

using namespace obng::engine::world::grid_move_defer;

namespace {

std::uint32_t Rel(const std::uint8_t* at) {
	std::uint32_t rel = 0;
	std::memcpy(&rel, at + 1, 4);
	return rel;
}

}  // namespace

int main() {
	Section("The INI's value");
	Check(ModeOf(0) == Mode::Off && ModeOf(1) == Mode::On && ModeOf(2) == Mode::Off && ModeOf(-1) == Mode::Off, "0 off, 1 on, anything else off");

	Section("The block's place");
	Check(kBlockSize == 0xD1 && kBlockStart + kBlockSize == kBlockEnd, "209 bytes from the gate's call to the physics step's");
	Check(kCallCount == 7 && kCallOffsets[0] == 0 && kCallOffsets[1] == 0x2A && kCallOffsets[6] == 0xCC, "seven near calls, the gate's first, the move's second");
	for (std::size_t i = 0; i < kCallCount; ++i) {
		Check(kCallOffsets[i] + 5 <= kBlockSize && (i == 0 || kCallOffsets[i] >= kCallOffsets[i - 1] + 5), "a call lies inside the block and behind the one before it");
	}

	Section("The checksum");
	const std::uint8_t abc[] = {'a', 'b', 'c'};
	Check(Checksum(nullptr, 0) == 0x811C9DC5u, "nothing: the FNV offset basis");
	Check(Checksum(abc, 3) == 0x1A47E90Bu, "FNV-1a of \"abc\"");
	const std::uint8_t abd[] = {'a', 'b', 'd'};
	Check(Checksum(abd, 3) != Checksum(abc, 3), "one byte changed: another sum");

	Section("The block copied for a run elsewhere");
	// A block of 13 bytes at 0x1000: call +0x100 (to 0x1105), nop, je +5 (to the end), call -0x20 (to 0x0FEC), nop.
	const std::uint8_t block[] = {0xE8, 0x00, 0x01, 0x00, 0x00, 0x90, 0x74, 0x05, 0xE8, 0xDF, 0xFF, 0xFF, 0xFF};
	const std::uint32_t calls[] = {0, 8};
	std::uint8_t out[32];
	std::memset(out, 0xCC, sizeof(out));
	Check(Relocate(block, sizeof(block), 0x1000u, 0x5000u, calls, 2, out, sizeof(out)) == Copy::Ok, "copied");
	Check(0x5000u + 5 + Rel(out) == 0x1105u, "the first call still reaches its target");
	Check(0x5000u + 8 + 5 + Rel(out + 8) == 0x1000u + 8 + 5 + 0xFFFFFFDFu, "the second call (backwards) still reaches its target");
	Check(out[5] == 0x90 && out[6] == 0x74 && out[7] == 0x05, "everything that is not a call's operand is the block's own byte: a jump to the block's end ...");
	Check(out[sizeof(block)] == 0xC3 && out[sizeof(block) + 1] == 0xCC, "... lands on the RET written behind it, and nothing further is written");

	Section("A copy below the block (the distance wraps)");
	Check(Relocate(block, sizeof(block), 0x70001000u, 0x00005000u, calls, 2, out, sizeof(out)) == Copy::Ok, "copied");
	Check(0x00005000u + 5 + Rel(out) == 0x70001105u, "the call reaches its target with 32-bit wrap");

	Section("Refusals");
	Check(Relocate(block, sizeof(block), 0x1000u, 0x5000u, calls, 2, out, sizeof(block)) == Copy::NoRoom, "no room for the RET: refused");
	const std::uint32_t wrong[] = {0, 5};
	std::memset(out, 0xCC, sizeof(out));
	Check(Relocate(block, sizeof(block), 0x1000u, 0x5000u, wrong, 2, out, sizeof(out)) == Copy::NotACall, "a listed place that holds no near call: refused");
	Check(out[0] == 0xCC, "... and nothing was written");
	const std::uint32_t outside[] = {10};
	Check(Relocate(block, sizeof(block), 0x1000u, 0x5000u, outside, 1, out, sizeof(out)) == Copy::NotACall, "a call that would end behind the block: refused");
	Check(Relocate(nullptr, sizeof(block), 0x1000u, 0x5000u, calls, 2, out, sizeof(out)) == Copy::NoRoom
	          && Relocate(block, sizeof(block), 0x1000u, 0x5000u, calls, 2, nullptr, sizeof(out)) == Copy::NoRoom,
	      "no block or no place: refused");

	Section("The jump that skips the block at its place");
	std::uint8_t jump[5];
	SkipJump(kBlockStart, kBlockEnd, jump);
	Check(jump[0] == 0xE9 && Rel(jump) == kBlockSize - 5, "a near jump over the block's rest: E9 CC 00 00 00");
	SkipJump(0x2000u, 0x1000u, jump);
	Check(0x2000u + 5 + Rel(jump) == 0x1000u, "backwards too");

	Section("The run's numbers");
	Runs runs;
	Count(runs, 0.02, 3.0);
	Count(runs, 5.5, 3.0);
	Count(runs, 3.0, 3.0);
	Check(runs.calls == 3 && runs.longCalls == 2, "a run of the long limit or more counts as long");
	Check(runs.totalMs > 8.51 && runs.totalMs < 8.53 && runs.longestMs == 5.5, "the sum and the longest are kept");
	return Failures();
}
