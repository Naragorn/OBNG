#include "TaskEndBudget.h"

#include <windows.h>

#include <cstring>

#include "Memory.h"
#include "engine/io/TaskEndBudgetLogic.h"

namespace obng::plugin {

namespace {

namespace teb = obng::engine::io::task_end_budget;

constexpr UInt32 kSite = 0x004335D4;                             // mov eax, ds:[0x00B048E4] in the pump 0x00433590 (read)
constexpr UInt8 kSiteBytes[] = {0xA1, 0xE4, 0x48, 0xB0, 0x00};   // the setting iPostProcessMilliseconds:BackgroundLoad's value

UInt32 g_budgetMs = 0;  // what the pump reads instead

}  // namespace

void InstallTaskEndBudget(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const teb::Budget budget = teb::BudgetOf(static_cast<int>(GetPrivateProfileIntA("Route", "TaskEndBudget", 0, iniPath)));
	if (!budget.routed) {
		return;
	}
	if (!Verify(kSite, kSiteBytes, sizeof(kSiteBytes))) {
		log("Route: task end budget - the pump's read at %08X is not the game's own; the game's setting stays", kSite);
		return;
	}
	g_budgetMs = budget.ms;
	UInt8 patch[sizeof(kSiteBytes)] = {0xA1};
	const UInt32 address = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&g_budgetMs));
	std::memcpy(patch + 1, &address, 4);
	if (!SafeWrite(kSite, patch, sizeof(patch))) {
		log("Route: task end budget - the read at %08X could not be written; the game's setting stays", kSite);
		return;
	}
	log("Route: task end budget - the pump ends finished background tasks for %u ms a frame (the game's iPostProcessMilliseconds is %u)", g_budgetMs,
	    *reinterpret_cast<const UInt32*>(0x00B048E4));
}

}  // namespace obng::plugin
