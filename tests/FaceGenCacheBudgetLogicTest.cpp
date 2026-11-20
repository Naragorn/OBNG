// Every flow of the FaceGen cache's limits (src/engine/facegen/FaceGenCacheBudgetLogic.h).

#include "Check.h"
#include "engine/facegen/FaceGenCacheBudgetLogic.h"

using namespace obng::engine::facegen::cache_budget;

int main() {
	Section("The INI's value in megabytes");
	Check(!BudgetOf(0).routed && !BudgetOf(-5).routed, "0 or less: the game's own setting");
	Check(BudgetOf(256).routed && BudgetOf(256).bytes == 256u * kMb, "a number is that many megabytes");
	Check(BudgetOf(1).bytes == kMb, "one megabyte is taken as written");
	Check(BudgetOf(kMaxMb).bytes == static_cast<std::uint32_t>(kMaxMb) * kMb && BudgetOf(kMaxMb + 1).bytes == static_cast<std::uint32_t>(kMaxMb) * kMb
	          && BudgetOf(100000).bytes == static_cast<std::uint32_t>(kMaxMb) * kMb,
	      "more than the most is the most (no overflow of 32 bits)");

	Section("The limit the cache gets");
	const std::uint32_t game = 64u * kMb;
	Check(LimitOf(BudgetOf(0), game) == game, "not routed: the game's");
	Check(LimitOf(BudgetOf(256), game) == 256u * kMb, "routed and larger than the game's: OBNG's");
	Check(LimitOf(BudgetOf(32), game) == game, "routed but smaller than the game's: the game's stays (a player's larger Oblivion.ini value is kept)");
	Check(LimitOf(BudgetOf(64), game) == game, "equal: unchanged");
	Check(LimitOf(BudgetOf(256), 0) == 256u * kMb, "a game setting of 0 is raised too");

	Section("Megabytes for the log");
	Check(MbOf(0) == 0 && MbOf(kMb - 1) == 0 && MbOf(kMb) == 1 && MbOf(67108864u) == 64, "whole megabytes, rounded down");
	return Failures();
}
