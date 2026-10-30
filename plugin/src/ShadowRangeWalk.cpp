// The range-walk shadow: OBNG's first subsystem replacement checked in the
// game (docs/subsystems/shadow-light-range-walk.md, "How the in-game gate
// will compare the two"). Two entry detours:
//
//   0x007D59E0 ShadowSceneLight::CollectGeometryInRange (proposed name) -
//     the outermost call per thread reads the light, runs the original with
//     its AddGeometry calls recorded, then runs BatchedWalk over the same
//     scene through ports that read the game's objects and call the engine's
//     own helpers but record AddGeometry, and compares. The walk's own
//     recursive calls (0x007D5A74, 0x007D5ABD) come back through this entry
//     and pass straight through.
//   0x007D5790 ShadowSceneLight::AddGeometry (proposed name) - records the
//     geometry and the light's +0x1B0 while a compared walk of that light
//     runs on this thread, then calls the original. Its other caller
//     0x007C6100 is not recorded.
//
// The game always continues with the original's result. The flow and every
// decision are in ShadowRangeWalkLogic.h; the 32-bit engine shape (the
// volatile reads, the thiscall calls, the square root, the TLS slot) is in
// RangeWalkGame.h, shared with the route (RouteRangeWalk.cpp).

#include "ShadowRangeWalk.h"

#include <windows.h>

#include <cstdio>

#include "RangeWalkGame.h"
#include "ShadowRangeWalkLogic.h"
#include "ShadowRecorder.h"

namespace obng::plugin {
namespace {

namespace rw = range_walk;
namespace game = range_walk_game;
namespace walk_engine = engine::render::shadow_light_range_walk::engine;

game::WalkFn g_walk = nullptr;
game::AddGeometryFn g_addGeometry = nullptr;
bool g_on = false;
LogFn g_log = nullptr;

SRWLOCK g_statsLock = SRWLOCK_INIT;
rw::WalkStats g_stats;

// Per thread: the depth, the recording and the buffers.
struct ThreadSlot {
	rw::ThreadWalkState state;
	rw::WalkBuffers buffers;
};
game::ThreadSlots<ThreadSlot> g_slots;

const char* ModeName(engine::render::shadow_light_range_walk::SourceMode mode) {
	switch (mode) {
	case engine::render::shadow_light_range_walk::SourceMode::NoSource: return "no source";
	case engine::render::shadow_light_range_walk::SourceMode::Directional: return "directional";
	case engine::render::shadow_light_range_walk::SourceMode::Planes: return "planes";
	case engine::render::shadow_light_range_walk::SourceMode::Distance: return "distance";
	}
	return "?";
}

void Report(const rw::WalkReport& report) {
	if (report.action != rw::EntryAction::Compare) {
		RecordSkipped(Shadowed::RangeWalk, rw::SkipReason(report.action));
		return;
	}
	AcquireSRWLockExclusive(&g_statsLock);
	g_stats.Add(report);
	ReleaseSRWLockExclusive(&g_statsLock);
	const bool mismatch = report.comparison.Mismatch();
	char detail[400] = {};
	if (mismatch) {
		const rw::WalkBuffers& buffers = g_slots.Get().buffers;  // this thread's: Report runs inside its walk
		rw::DescribeComparison(report.comparison, buffers.original, buffers.rebuild, detail, sizeof(detail));
	}
	char inputs[160];
	std::snprintf(inputs, sizeof(inputs), "light %08X root %08X source %s mask %08X thread %lu", report.light,
	              report.object, ModeName(report.mode), report.maskBefore, GetCurrentThreadId());
	RecordCall(Shadowed::RangeWalk, mismatch, detail, inputs);
	SummaryIfDue(GetTickCount(), nullptr, false);
}

UInt32 __fastcall ShadowAddGeometry(UInt32 light, void* /*edx*/, UInt32 geometry) {
	// A thread without a slot has never run a walk, so nothing to record.
	if (ThreadSlot* slot = g_slots.Existing()) {
		rw::OnAddGeometry(slot->state, light, geometry,
		                  [light] { return game::Read<UInt32>(light + walk_engine::kActivePlanesOffset); });
	}
	return g_addGeometry(light, nullptr, geometry);
}

UInt32 __fastcall ShadowWalk(UInt32 light, void* /*edx*/, UInt32 object) {
	ThreadSlot& slot = g_slots.Get();
	game::GameMemory memory;
	return rw::ShadowWalk(memory, slot.state, slot.buffers, light, object,
	                      [](std::uint32_t l, std::uint32_t o) { return static_cast<std::uint32_t>(g_walk(l, nullptr, o)); },
	                      &game::GameSquareRoot, &Report);
}

}  // namespace

UInt32 InstallShadowRangeWalk(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.rangeWalk) {
		return 0;
	}
	s_done = true;
	g_log = log;
	if (!g_slots.Allocate()) {
		log("Shadow %s: no TLS slot; not shadowed", ShadowName(Shadowed::RangeWalk));
		return 0;
	}
	// AddGeometry first: without its recording every compared walk would
	// show the original as empty.
	if (!InstallEntryDetour("ShadowSceneLight::AddGeometry (recording)", game::kAddGeometry, game::kAddGeometryBytes,
	                        sizeof(game::kAddGeometryBytes), reinterpret_cast<const void*>(&ShadowAddGeometry),
	                        reinterpret_cast<void**>(&g_addGeometry), log)) {
		log("Shadow %s: not shadowed - its AddGeometry recording could not be put", ShadowName(Shadowed::RangeWalk));
		return 0;
	}
	if (!InstallEntryDetour(ShadowName(Shadowed::RangeWalk), game::kWalk, game::kWalkBytes, sizeof(game::kWalkBytes),
	                        reinterpret_cast<const void*>(&ShadowWalk), reinterpret_cast<void**>(&g_walk), log)) {
		return 1;
	}
	g_on = true;
	return 2;
}

void LogRangeWalkSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	AcquireSRWLockExclusive(&g_statsLock);
	const rw::WalkStats stats = g_stats;
	g_stats.Reset();
	ReleaseSRWLockExclusive(&g_statsLock);
	char line[300];
	rw::FormatWalkStats(stats, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
