// Every flow of the task end budget's INI value (src/engine/io/TaskEndBudgetLogic.h).

#include "Check.h"
#include "engine/io/TaskEndBudgetLogic.h"

using namespace obng::engine::io::task_end_budget;

int main() {
	Section("The game's own setting");
	Check(!BudgetOf(0).routed, "0: the game's setting stays");
	Check(!BudgetOf(-3).routed, "a negative value: the game's setting stays");

	Section("A budget of OBNG's");
	Check(BudgetOf(1).routed && BudgetOf(1).ms == 1, "1 ms");
	Check(BudgetOf(5).routed && BudgetOf(5).ms == 5, "5 ms");
	Check(BudgetOf(50).ms == 50, "the largest");
	Check(BudgetOf(51).routed && BudgetOf(51).ms == kMaxMs && BudgetOf(100000).ms == kMaxMs, "more than that: the largest");

	Section("Without a key in OBNG.ini");
	Check(BudgetOf(kShippedMs).routed && BudgetOf(kShippedMs).ms == 1, "OBNG's own budget: 1 ms, routed");
	return Failures();
}
