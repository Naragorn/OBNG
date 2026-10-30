// The range walk's route: one entry detour on
// ShadowSceneLight::CollectGeometryInRange 0x007D59E0 (proposed name). The
// outermost call per thread with the gate on, an object and a source runs
// BatchedWalk over the game's scene through the same ports the shadow's
// in-game gate ran over (RangeWalkGame.h), performing AddGeometry 0x007D5790
// directly - it is not patched under the route, so its address is the
// original - with +0x1B0 written to the original's value before each call
// and restored afterwards. Every other call runs the original through the
// trampoline. The flow and every decision are in RouteRangeWalkLogic.h.
//
// The route does not drive the summary: its counters are written when
// another detour or the summary timer ([Log] SummaryTimer) makes one due, so
// a routed and an unrouted measurement run carry the same summary cost.

#include "RouteRangeWalk.h"

#include <windows.h>

#include <cstdio>

#include "RangeWalkGame.h"
#include "RouteRangeWalkLogic.h"
#include "ShadowRecorder.h"

namespace obng::plugin {
namespace {

namespace rw = range_walk;
namespace game = range_walk_game;
namespace walk = engine::render::shadow_light_range_walk;

game::WalkFn g_walk = nullptr;  // the original, through the trampoline
const auto g_addGeometry = reinterpret_cast<game::AddGeometryFn>(game::kAddGeometry);
bool g_on = false;
LogFn g_log = nullptr;

SRWLOCK g_statsLock = SRWLOCK_INIT;
rw::RouteStats g_stats;

// Per thread: the depth and BatchedWalk's reusable stack.
struct ThreadSlot {
	rw::ThreadWalkState state;
	walk::BatchedWalkScratch scratch;
};
game::ThreadSlots<ThreadSlot> g_slots;

void Report(const rw::RouteReport& report) {
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.Add(report);
	ReleaseSRWLockExclusive(&g_statsLock);
}

UInt32 __fastcall RouteWalk(UInt32 light, void* /*edx*/, UInt32 object) {
	ThreadSlot& slot = g_slots.Get();
	game::GameMemory memory;
	return rw::RouteWalk(
		memory, slot.state, slot.scratch, light, object,
		[](std::uint32_t l, std::uint32_t o) { return static_cast<std::uint32_t>(g_walk(l, nullptr, o)); },
		[](std::uint32_t l, std::uint32_t g) { g_addGeometry(l, nullptr, g); }, &game::GameSquareRoot, &Report);
}

}  // namespace

UInt32 InstallRouteRangeWalk(const RouteSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.rangeWalk) {
		return 0;
	}
	s_done = true;
	g_log = log;
	constexpr const char* kName = "ShadowSceneLight::CollectGeometryInRange";
	if (!g_slots.Allocate()) {
		log("Route %s: no TLS slot; not routed", kName);
		return 0;
	}
	if (!InstallEntryDetour(kName, game::kWalk, game::kWalkBytes, sizeof(game::kWalkBytes),
	                        reinterpret_cast<const void*>(&RouteWalk), reinterpret_cast<void**>(&g_walk), log,
	                        DetourRole::Route)) {
		return 0;
	}
	g_on = true;
	return 1;
}

void LogRouteSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	AcquireSRWLockExclusive(&g_statsLock);
	const rw::RouteStats stats = g_stats;
	g_stats.Reset();
	ReleaseSRWLockExclusive(&g_statsLock);
	char line[400];
	rw::FormatRouteStats(stats, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
