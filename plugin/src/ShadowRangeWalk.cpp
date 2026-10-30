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
#include <cstring>

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
RangeWalkOrder g_order = RangeWalkOrder::OriginalFirst;

SRWLOCK g_statsLock = SRWLOCK_INIT;
rw::WalkStats g_stats;

// Per thread: the depth, the recording and the buffers.
struct ThreadSlot {
	rw::ThreadWalkState state;
	rw::WalkBuffers buffers;
	rw::X87State entryX87;  // rebuild-first: the x87 state at the outermost walk's entry
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
	char detail[1200] = {};
	if (mismatch) {
		const rw::WalkBuffers& buffers = g_slots.Get().buffers;  // this thread's: Report runs inside its walk
		rw::DescribeComparison(report.comparison, buffers.original, buffers.rebuild, detail, sizeof(detail));
		if (report.order == RangeWalkOrder::RebuildFirst) {
			std::size_t at = std::strlen(detail);
			if (report.trace.found) {
				rw::DescribeTraceDifference(report.trace, buffers.traceFresh, buffers.traceAfter, detail + at,
				                            sizeof(detail) - at);
			} else {
				std::snprintf(detail + at, sizeof(detail) - at, " the rebuild's reads equal before and after the original;");
			}
			at = std::strlen(detail);
			std::snprintf(detail + at, sizeof(detail) - at, " light values %s by the original;",
			              report.lightChanged ? "CHANGED" : "unchanged");
			at = std::strlen(detail);
			rw::FormatX87State(g_slots.Get().entryX87, detail + at, sizeof(detail) - at);
			at = std::strlen(detail);
			rw::FormatRootDistance(report.root, detail + at, sizeof(detail) - at);
		}
	}
	char inputs[220];
	std::snprintf(inputs, sizeof(inputs), "light %08X root %08X source %s mask %08X fpcw %04X thread %lu order %s",
	              report.light, report.object, ModeName(report.mode), report.maskBefore,
	              static_cast<unsigned>(report.controlWord), GetCurrentThreadId(), RangeWalkOrderName(report.order));
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
	// The control word first, before anything here could touch it (this
	// function's own code is SSE; the TLS lookup is a kernel32 call): the
	// original walks under its rounding control, so the rebuild does too.
	const std::uint16_t controlWord = game::CaptureControlWord();
	// rebuild-first: the whole x87 state, for the mismatch line.
	game::FxsaveArea entry;
	const bool diagnose = g_order == RangeWalkOrder::RebuildFirst;
	if (diagnose) {
		game::CaptureX87(entry.Aligned());
	}
	ThreadSlot& slot = g_slots.Get();
	if (diagnose && slot.state.depth == 0) {
		slot.entryX87 = rw::DecodeFxsave(static_cast<const std::uint8_t*>(entry.Aligned()));
	}
	game::GameMemory memory;
	return rw::ShadowWalk(memory, slot.state, slot.buffers, g_order, controlWord, light, object,
	                      [](std::uint32_t l, std::uint32_t o) { return static_cast<std::uint32_t>(g_walk(l, nullptr, o)); },
	                      game::GameSquareRootPort{}, &Report);
}

}  // namespace

UInt32 InstallShadowRangeWalk(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.rangeWalk) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_order = settings.rangeWalkOrder;
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
	log("Shadow %s: order %s", ShadowName(Shadowed::RangeWalk), RangeWalkOrderName(g_order));
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
