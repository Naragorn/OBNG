#pragma once
// [Route] QuestTargetDefer: the quest targets' rebuild at a cell crossing one frame later - the decisions over plain
// values (the plugin's QuestTargetDefer.cpp calls).
//
// Read 2026-11-20: the grid's move 0x00444FB0 calls 0x00665260 (the player's quest targets, __thiscall(player,
// player), RET 4) at 0x004454E6 after every crossing. With the player's targets marked dirty (+0x117) it rebuilds the
// list through 0x00529A20, which for each target finds a path over the doors from the player to it, 0x0065D8D0 (the
// pathing 0x0068B030 / 0x0068AB20), and keeps its first door at target +0x10 for the compass. Measured (runs
// 20261120-090130, -091623, -091951): 3.1 - 4.6 ms at every crossing, a third to a half of the grid's move.
//
// The route keeps the call and makes it at the next update step's start (StepHooks.h), on the main thread, with the
// same arguments: the compass points at the old door one frame longer, as it does anyway between two crossings. A call
// kept for a player who is not the player any more (a load in between) is dropped. Class B: the same computation one
// frame later.
// 64-bit clean: the player as a 32-bit handle.
#include <cstdint>

namespace obng::engine::world::quest_target_defer {

enum class Mode : std::uint8_t { Off, On };

inline Mode ModeOf(int value) {
	return value == 1 ? Mode::On : Mode::Off;
}

struct Pending {
	bool pending = false;
	std::uint32_t player = 0;
	std::uint32_t argument = 0;
	std::uint32_t merged = 0;  // calls kept while one was already waiting (one run serves them all)
};

inline void Keep(Pending& pending, std::uint32_t player, std::uint32_t argument) {
	if (pending.pending) {
		++pending.merged;
	}
	pending.pending = true;
	pending.player = player;
	pending.argument = argument;
}

enum class Outcome : std::uint8_t { None, Run, Drop };

// At the step's start: run the kept call when the player is still the one it was kept for.
inline Outcome Take(Pending& pending, std::uint32_t currentPlayer) {
	if (!pending.pending) {
		return Outcome::None;
	}
	pending.pending = false;
	return pending.player != 0 && pending.player == currentPlayer ? Outcome::Run : Outcome::Drop;
}

}  // namespace obng::engine::world::quest_target_defer
