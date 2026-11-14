#include "SceneOrderCheck.h"

#include <windows.h>

#include "engine/scene/SceneOrderCheckLogic.h"

namespace obng::plugin {
namespace {

namespace su = engine::scene::scene_update;
namespace oc = engine::scene::order_check;

oc::Mode g_mode = oc::Mode::kOff;
oc::Counts g_counts;  // under the order table's lock: OrderFor holds it around the check
LogFn g_log = nullptr;
double g_ticksPerMs = 0.0;

std::uint64_t Now() {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return static_cast<std::uint64_t>(t.QuadPart);
}

}  // namespace

void InstallSceneOrderCheck(const char* iniPath, LogFn log) {
	g_mode = oc::ModeFromIni(static_cast<int>(GetPrivateProfileIntA("Probe", "SceneOrderCheck", 0, iniPath)));
	if (g_mode == oc::Mode::kOff) {
		return;
	}
	g_log = log;
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_ticksPerMs = static_cast<double>(f.QuadPart) / 1000.0;
	log("Probe: scene order check %s ([Probe] SceneOrderCheck): the flat skeleton update's check of its order against the tree, timed%s",
	    g_mode == oc::Mode::kTwice ? "twice" : "timed", g_mode == oc::Mode::kTwice ? ", and run again at once to see how much of it is fetching the tree" : "");
}

bool SceneOrderChecked(const su::Order& order, const su::engine::Tree& tree) {
	if (g_mode == oc::Mode::kOff) {
		return su::Matches(order, tree);
	}
	const std::uint64_t t0 = Now();
	const bool first = su::Matches(order, tree);
	const std::uint64_t t1 = Now();
	bool second = first;
	std::uint64_t t2 = t1;
	if (g_mode == oc::Mode::kTwice) {
		second = su::Matches(order, tree);
		t2 = Now();
	}
	g_counts.Add(static_cast<std::uint32_t>(order.entries.size()), t1 - t0, t2 - t1, first == second);
	return first;
}

void LogSceneOrderCheckSummary() {
	if (g_mode == oc::Mode::kOff || g_log == nullptr) {
		return;
	}
	char line[400];
	oc::Format(g_counts, g_mode, g_ticksPerMs, line, sizeof(line));
	g_log("%s", line);
	g_counts = oc::Counts{};
}

}  // namespace obng::plugin
