#pragma once
// [Route] TaskEndBudget: how long the IO manager's pump 0x00433590 goes on ending finished background tasks on the
// main thread in one frame - the decision over plain values (the plugin's TaskEndBudget.cpp).
//
// Read 2026-11-20: the pump takes its budget in milliseconds from the setting iPostProcessMilliseconds:BackgroundLoad
// (the value at 0x00B048E4, read at 0x004335D4; 5 as shipped since patch 1.2) and, when the manager's +0x38 is not 6,
// from iPostProcessMillisecondsLoadingQueuedPriority (0x00B048EC, 20). It ends one task, looks at the clock and goes
// on while the budget lasts; a task's end is never cut short.
//
// Measured (knowledge/background-loading.md): in VR a frame has about 8 ms of slack before OBVR's pose wait, where
// the pump runs; a background cell task's end evicts a cell (3 ms on average, up to 17), and with 5 ms two or three of
// them land in one frame, 10 - 19 ms. With 1 ms the pump stops after the first costly end: long frames at the
// evictions 11 / 15 -> 6 / 5 a ride.
//
// The route gives the pump a budget of its own instead of the setting's (the setting and Oblivion.ini stay as they
// are). Class B: the same ends in the same order, spread over more frames.
#include <cstdint>

namespace obng::engine::io::task_end_budget {

constexpr std::uint32_t kMaxMs = 50;  // the editor's own budget; more is not a frame's work

struct Budget {
	bool routed = false;       // false: the game's own setting
	std::uint32_t ms = 0;
};

// The INI's value: 0 or less leaves the game's setting; anything else is the budget, at most kMaxMs.
inline Budget BudgetOf(int value) {
	Budget budget;
	if (value <= 0) {
		return budget;
	}
	budget.routed = true;
	budget.ms = static_cast<std::uint32_t>(value) > kMaxMs ? kMaxMs : static_cast<std::uint32_t>(value);
	return budget;
}

}  // namespace obng::engine::io::task_end_budget
