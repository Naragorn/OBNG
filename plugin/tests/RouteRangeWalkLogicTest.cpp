// Every flow of the range walk's route (plugin/src/RouteRangeWalkLogic.h,
// plugin/src/RouteSettings.h): which mode the entry 0x007D59E0 takes
// (nothing, the shadow, the route; the shadow wins when both are asked), the
// routed walk over a fake game against the original walking the same scene
// unpatched (the AddGeometry calls performed in order with the value +0x1B0
// holds at each, +0x1B0 afterwards, AL), the calls left to the original (gate
// off, null object, no source, an inner call), a re-entrant call from inside
// AddGeometry, the gate switched off mid-walk and between the entry's read and
// the walk's, and the summary line.

#include "Check.h"
#include "RangeWalkFakeGame.h"
#include "RouteRangeWalkLogic.h"
#include "RouteSettings.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace obng::plugin::range_walk_test;
using obng::plugin::RangeWalkMode;
using obng::plugin::RouteSettings;
using obng::plugin::SelectRangeWalkMode;

// The unpatched game: the original only, its recursion straight into itself.
struct PlainGame {
	FakeMemory m;
	Builder b{m};
	std::vector<rw::AddGeometryEvent> performed;  // what 0x007D5790 received, with +0x1B0 at the call
	std::function<void(std::uint32_t light)> onAdd;

	std::uint32_t Walk(std::uint32_t light, std::uint32_t object) {
		return OriginalWalk(
			m, light, object, [this](std::uint32_t l, std::uint32_t o) { Walk(l, o); },
			[this](std::uint32_t l, std::uint32_t, std::uint32_t g) {
				performed.push_back(rw::AddGeometryEvent{g, m.Raw32(l + 0x1B0)});
				if (onAdd) {
					onAdd(l);
				}
			});
	}
};

// The routed game: the entry 0x007D59E0 leads to RouteWalk; the original
// (reached for the calls the route leaves to it) recurses through the entry,
// as the patched game does; AddGeometry is the game's own, recorded here with
// the live +0x1B0 it sees.
struct RoutedGame {
	FakeMemory m;
	Builder b{m};
	rw::ThreadWalkState state;
	walk::BatchedWalkScratch scratch;
	std::vector<rw::RouteReport> reports;
	std::vector<rw::AddGeometryEvent> performed;
	std::vector<std::uint32_t> performedLights;
	std::uint64_t originalCalls = 0;
	std::function<void(std::uint32_t light)> onAdd;

	std::uint32_t Mask(std::uint32_t light) const { return m.Raw32(light + 0x1B0); }

	// The entry, with the control word the detour's FNSTCW reads: the one the
	// fake game runs under.
	std::uint32_t Entry(std::uint32_t light, std::uint32_t object) {
		return rw::RouteWalk(
			m, state, scratch, m.controlWord, light, object, [this](std::uint32_t l, std::uint32_t o) { return Original(l, o); },
			[this](std::uint32_t l, std::uint32_t g) { AddGeometry(l, g); }, StubSquareRoot, ws::engine::MxcsrPort{},
			[this](const rw::RouteReport& r) { reports.push_back(r); });
	}

	void AddGeometry(std::uint32_t light, std::uint32_t geometry) {
		performed.push_back(rw::AddGeometryEvent{geometry, Mask(light)});
		performedLights.push_back(light);
		if (onAdd) {
			onAdd(light);
		}
	}

	std::uint32_t Original(std::uint32_t light, std::uint32_t object) {
		++originalCalls;
		return OriginalWalk(
			m, light, object, [this](std::uint32_t l, std::uint32_t o) { Entry(l, o); },
			[this](std::uint32_t l, std::uint32_t, std::uint32_t g) { AddGeometry(l, g); });
	}

	std::vector<rw::AddGeometryEvent> PerformedFor(std::uint32_t light) const {
		std::vector<rw::AddGeometryEvent> out;
		for (std::size_t i = 0; i < performed.size(); ++i) {
			if (performedLights[i] == light) {
				out.push_back(performed[i]);
			}
		}
		return out;
	}
};

bool WritesOnlyTheMask(const FakeMemory& m, std::uint32_t light) {
	for (const auto& w : m.writes) {
		if (w.first != light + 0x1B0) {
			return false;
		}
	}
	return true;
}

// ---- 1. The mode of the entry -----------------------------------------------------------

void TestModeSelection() {
	Section("Which mode the entry 0x007D59E0 takes");
	Check(SelectRangeWalkMode(false, false) == RangeWalkMode::Off, "neither asked: the entry stays the game's");
	Check(SelectRangeWalkMode(true, false) == RangeWalkMode::Shadow, "the shadow alone: shadow");
	Check(SelectRangeWalkMode(false, true) == RangeWalkMode::Route, "the route alone: route");
	Check(SelectRangeWalkMode(true, true) == RangeWalkMode::Shadow,
		"both asked: the shadow wins (one entry, and a route without a clean shadow run makes nothing safer)");
	Check(obng::plugin::RouteSkippedForShadow(true, true) && !obng::plugin::RouteSkippedForShadow(false, true) &&
			!obng::plugin::RouteSkippedForShadow(true, false) && !obng::plugin::RouteSkippedForShadow(false, false),
		"the route counts as skipped only when it was asked and the shadow took the entry");

	RouteSettings r;
	Check(!r.rangeWalk && r.Asked() == 0, "the route is off by default, nothing asked");
	r.rangeWalk = true;
	Check(r.Asked() == 1, "the range-walk route is one detour (the walk's entry; AddGeometry stays unpatched)");
}

void TestRouteEax() {
	Section("What the route returns in EAX");
	Check(rw::RouteEax(true) == 1u, "a walk that ran: AL 1 (MOV AL,1 at the three walked exits)");
	Check(rw::RouteEax(false) == 0u, "the gate off at the walk's root read: AL 0 (XOR AL,AL)");
}

// ---- 2. The scene port that performs ----------------------------------------------------

void TestPerformingScene() {
	Section("The scene port performs AddGeometry with the mask written first");
	FakeMemory m;
	Builder b{m};
	const std::uint32_t light = b.Light(0, 0, 0x3F);
	std::vector<std::pair<std::uint32_t, std::uint32_t>> calls;  // (geometry, +0x1B0 at the call)
	auto perform = [&](std::uint32_t l, std::uint32_t g) { calls.emplace_back(g, m.Raw32(l + 0x1B0)); };
	rw::PerformingScene<FakeMemory, decltype(perform)> scene{{m, nullptr}, light, &perform};
	static_assert(walk::engine::RangeWalkScene<rw::PerformingScene<FakeMemory, decltype(perform)>>,
		"PerformingScene satisfies the scene concept");
	Check(!scene.maskWritten && scene.adds == 0, "nothing written, nothing performed before the first call");
	scene.addGeometry(0x77, 0x3B);
	scene.addGeometry(0x88, 0x2A);
	Check(calls.size() == 2 && calls[0] == std::make_pair(0x77u, 0x3Bu) && calls[1] == std::make_pair(0x88u, 0x2Au),
		"each AddGeometry sees its own mask in +0x1B0, in order");
	Check(scene.maskWritten && scene.adds == 2 && m.writes.size() == 2 && WritesOnlyTheMask(m, light),
		"two writes, both to +0x1B0, two calls counted");
}

// ---- 3. The routed walk against the original --------------------------------------------

struct Pair {
	PlainGame plain;
	RoutedGame routed;
	SceneShape p{}, r{};
};

void Build(Pair& pair, bool planes) {
	pair.p = BuildScene(pair.plain.b, planes);
	pair.r = BuildScene(pair.routed.b, planes);
}

void TestRoutedEqualsOriginal() {
	Section("The routed walk performs what the original performs (planes, distance, directional, a narrowed mask)");
	struct Case {
		const char* name;
		bool planes;
		bool directional;
		std::uint32_t entryMask;
		walk::SourceMode mode;
		std::size_t adds;
	};
	const Case cases[] = {
		{"planes", true, false, 0x3F, walk::SourceMode::Planes, 3},
		{"distance", false, false, 0x3F, walk::SourceMode::Distance, 3},
		{"directional", true, true, 0x3F, walk::SourceMode::Directional, 4},
		{"planes, entry mask 0x2A", true, false, 0x2A, walk::SourceMode::Planes, 3},
	};
	for (const Case& c : cases) {
		Pair pair;
		Build(pair, c.planes);
		for (FakeMemory* m : {&pair.plain.m, &pair.routed.m}) {
			if (c.directional) {
				m->rtti[m->Raw32(pair.p.light + 0x100)] = kDirectionalRtti;
			}
			m->Put32(pair.p.light + 0x1B0, c.entryMask);
		}
		const std::uint32_t plainEax = pair.plain.Walk(pair.p.light, pair.p.root);
		const std::uint32_t routedEax = pair.routed.Entry(pair.r.light, pair.r.root);
		const std::string n = c.name;
		Check(pair.p.light == pair.r.light && pair.p.root == pair.r.root, (n + ": both games built the same addresses").c_str());
		Check(pair.routed.performed == pair.plain.performed && pair.routed.performed.size() == c.adds,
			(n + ": the same AddGeometry calls, in order, each with the original's live +0x1B0").c_str());
		Check(pair.routed.Mask(pair.r.light) == pair.plain.m.Raw32(pair.p.light + 0x1B0) &&
				pair.routed.Mask(pair.r.light) == c.entryMask,
			(n + ": +0x1B0 afterwards is the entry value, as the original leaves it").c_str());
		Check((plainEax & 0xFFu) == 1 && routedEax == 1, (n + ": AL 1 on both").c_str());
		Check(pair.routed.originalCalls == 0, (n + ": the original never ran").c_str());
		Check(pair.routed.reports.size() == 1 && pair.routed.reports[0].action == rw::EntryAction::Compare &&
				pair.routed.reports[0].mode == c.mode && pair.routed.reports[0].adds == c.adds &&
				pair.routed.reports[0].result,
			(n + ": one report: routed, the source mode, the calls performed, AL").c_str());
		bool writeBeforeEach = pair.routed.m.writes.size() == c.adds + 1;
		for (std::size_t i = 0; writeBeforeEach && i < c.adds; ++i) {
			writeBeforeEach = pair.routed.m.writes[i].second == pair.routed.performed[i].mask;
		}
		Check(writeBeforeEach && WritesOnlyTheMask(pair.routed.m, pair.r.light) &&
				pair.routed.m.writes.back().second == c.entryMask,
			(n + ": one +0x1B0 write before each call and one restore at the end, nothing else written").c_str());
		Check(pair.routed.state.depth == 0, (n + ": the depth is 0 again").c_str());
	}

	Pair pair;
	Build(pair, true);
	pair.routed.Entry(pair.r.light, pair.r.root);
	pair.routed.performed.clear();
	pair.routed.performedLights.clear();
	pair.plain.Walk(pair.p.light, pair.p.root);
	pair.routed.Entry(pair.r.light, pair.r.root);
	Check(pair.routed.performed == pair.plain.performed && pair.routed.reports.size() == 2,
		"a second walk reuses the scratch and performs the same again");

	Pair narrowedPlanes;
	Build(narrowedPlanes, true);
	narrowedPlanes.plain.Walk(narrowedPlanes.p.light, narrowedPlanes.p.root);
	bool narrowed = false;
	for (const rw::AddGeometryEvent& e : narrowedPlanes.plain.performed) {
		narrowed = narrowed || e.mask != 0x3F;
	}
	Check(narrowed, "the plane case narrows the mask at some call (so the mask written is really checked)");
}

void TestRouteNothingInRange() {
	Section("A routed walk that performs nothing writes nothing");
	RoutedGame game;
	const SceneShape s = BuildScene(game.b, false);
	game.m.PutF(game.m.Raw32(s.light + 0x100) + 0xF8, 1.0f);  // the source's range: nothing reaches the root
	game.m.PutF(s.root + 0x20, 1000.0f);
	const std::uint32_t eax = game.Entry(s.light, s.root);
	Check(eax == 1 && game.performed.empty() && game.m.writes.empty() && game.Mask(s.light) == 0x3F,
		"the root out of range: AL 1, no AddGeometry, +0x1B0 never written");
	Check(game.reports.size() == 1 && game.reports[0].action == rw::EntryAction::Compare && game.reports[0].adds == 0,
		"reported as routed with 0 calls");

	RoutedGame single;
	const SceneShape t = BuildScene(single.b, true);
	single.Entry(t.light, t.g1);
	Check(single.performed.size() == 1 && single.performed[0].geometry == t.g1 && single.m.writes.size() == 2 &&
			single.Mask(t.light) == 0x3F,
		"a geometry as the root: one call, its mask written before it, +0x1B0 restored after");
}

// ---- 4. The calls left to the original ----------------------------------------------------

void TestLeftToOriginal() {
	Section("The calls the route leaves to the original");
	{
		RoutedGame game;
		const SceneShape s = BuildScene(game.b, true);
		game.m.Put32(kGate, 0);
		const std::uint32_t eax = game.Entry(s.light, s.root);
		Check(eax == kGateOffEax && game.originalCalls == 1, "gate 0: the original runs and its EAX is returned whole");
		Check(game.reports.size() == 1 && game.reports[0].action == rw::EntryAction::SkipGateOff && game.m.writes.empty(),
			"reported as left (gate off), nothing written by the route");
	}
	{
		RoutedGame game;
		const SceneShape s = BuildScene(game.b, true);
		game.m.Put32(s.light + 0x100, 0);
		const std::uint32_t eax = game.Entry(s.light, s.root);
		Check(eax == kWalkedEax && game.originalCalls == 1 && game.reports.size() == 1 &&
				game.reports[0].action == rw::EntryAction::SkipNoSource && game.performed.empty(),
			"a light without a source: the original runs (every object out of range), reported as left");
	}
	{
		RoutedGame game;
		const SceneShape s = BuildScene(game.b, true);
		std::uint64_t originalRuns = 0;
		const std::uint32_t eax = rw::RouteWalk(
			game.m, game.state, game.scratch, game.m.controlWord, s.light, 0,
			[&](std::uint32_t, std::uint32_t) {
				++originalRuns;
				return 7u;
			},
			[&](std::uint32_t, std::uint32_t) {}, StubSquareRoot, ws::engine::MxcsrPort{},
			[&](const rw::RouteReport& r) { game.reports.push_back(r); });
		Check(eax == 7 && originalRuns == 1 && game.reports.size() == 1 &&
				game.reports[0].action == rw::EntryAction::SkipNullObject,
			"a null object: the original runs (whatever it does with it), reported as left");
	}
	{
		RoutedGame game;
		const SceneShape s = BuildScene(game.b, true);
		game.state.depth = 1;  // a walk call already running on this thread
		const std::uint64_t readsBefore = game.m.reads;
		std::uint64_t originalRuns = 0;
		const std::uint32_t eax = rw::RouteWalk(
			game.m, game.state, game.scratch, game.m.controlWord, s.light, s.root,
			[&](std::uint32_t, std::uint32_t) {
				++originalRuns;
				return 0x55u;
			},
			[&](std::uint32_t, std::uint32_t) {}, StubSquareRoot, ws::engine::MxcsrPort{},
			[&](const rw::RouteReport& r) { game.reports.push_back(r); });
		Check(eax == 0x55 && originalRuns == 1 && game.reports.empty() && game.m.reads == readsBefore && game.m.writes.empty(),
			"an inner call: the original runs, nothing is read, written or reported");
		Check(game.state.depth == 1, "the depth is back where the inner call found it");
	}
}

void TestReentrant() {
	Section("A walk entered again from inside AddGeometry on the same thread");
	Pair pair;
	Build(pair, true);
	const SceneShape other = BuildScene(pair.routed.b, false);  // a second light and scene, walked by the original
	bool entered = false;
	std::uint32_t innerEax = 0;
	pair.routed.onAdd = [&](std::uint32_t light) {
		if (!entered && light == pair.r.light) {
			entered = true;
			innerEax = pair.routed.Entry(other.light, other.root);
		}
	};
	pair.plain.Walk(pair.p.light, pair.p.root);
	const std::uint32_t eax = pair.routed.Entry(pair.r.light, pair.r.root);
	Check(entered && pair.routed.originalCalls > 0 && (innerEax & 0xFFu) == 1,
		"the inner call went to the original (depth 1), which walked its own scene");
	Check(pair.routed.PerformedFor(other.light).size() == 3 && pair.routed.Mask(other.light) == 0x3F,
		"the original's calls for the other light were performed, its +0x1B0 restored by the original");
	Check(pair.routed.PerformedFor(pair.r.light) == pair.plain.performed && eax == 1 &&
			pair.routed.Mask(pair.r.light) == 0x3F,
		"the routed walk went on unchanged: the same calls and masks as the original, +0x1B0 restored, AL 1");
	Check(pair.routed.reports.size() == 1 && pair.routed.state.depth == 0,
		"only the outer call is reported; the depth is 0 again");
}

void TestGateOffMidWalk() {
	Section("The gate switched off during a walk");
	Pair pair;
	Build(pair, true);
	pair.plain.onAdd = [&](std::uint32_t) { pair.plain.m.Put32(kGate, 0); };
	pair.routed.onAdd = [&](std::uint32_t) { pair.routed.m.Put32(kGate, 0); };
	const std::uint32_t plainEax = pair.plain.Walk(pair.p.light, pair.p.root);
	const std::uint32_t routedEax = pair.routed.Entry(pair.r.light, pair.r.root);
	Check(pair.plain.performed.size() == 1 && pair.routed.performed == pair.plain.performed,
		"after the first AddGeometry no child is entered on either side (each entry reads the gate)");
	Check((plainEax & 0xFFu) == 1 && routedEax == 1 && pair.routed.Mask(pair.r.light) == 0x3F,
		"AL 1 (the root's gate read was on), +0x1B0 restored");

	RoutedGame game;
	const SceneShape s = BuildScene(game.b, true);
	game.m.gateOnReads = 1;  // the entry's read sees it on, the walk's root read sees 0
	const std::uint32_t eax = game.Entry(s.light, s.root);
	Check(eax == 0 && game.performed.empty() && game.m.writes.empty() && game.originalCalls == 0,
		"off between the entry's read and the walk's: AL 0 as the original's gate exit, nothing performed or written");
	Check(game.reports.size() == 1 && game.reports[0].action == rw::EntryAction::Compare && !game.reports[0].result,
		"reported as routed with AL 0");
}

// ---- 5. The summary line ------------------------------------------------------------------

void TestStats() {
	Section("The route's summary line");
	rw::RouteStats stats;
	rw::RouteReport r{};
	r.action = rw::EntryAction::Compare;
	r.mode = walk::SourceMode::Planes;
	r.adds = 5;
	r.planes = walk::PlaneTestCounters{12, 3, 1};
	stats.Add(r);
	r.mode = walk::SourceMode::Distance;
	r.adds = 9;
	r.controlWord = 0x0C7F;
	r.planes = walk::PlaneTestCounters{};
	stats.Add(r);
	rw::RouteReport left{};
	left.action = rw::EntryAction::SkipGateOff;
	left.adds = 100;
	stats.Add(left);
	left.action = rw::EntryAction::SkipNullObject;
	stats.Add(left);
	left.action = rw::EntryAction::SkipNoSource;
	stats.Add(left);
	stats.Add(left);
	left.action = rw::EntryAction::PassThrough;
	stats.Add(left);
	Check(stats.Routed() == 2 && stats.adds == 14 && stats.Left() == 4,
		"two routed walks, 14 calls (a left call's count is not added), four left, a pass-through not counted");
	char line[400];
	rw::FormatRouteStats(stats, line, sizeof(line));
	Check(std::string(line) ==
			"Route: range walk 2 walks routed, 14 AddGeometry calls; by source planes 1, distance 1, directional 0; "
			"4 left to the original (gate off 1, null object 1, no source 2); by rounding control at entry nearest 1, "
			"down 0, up 0, truncate 1; plane batches native 3, recomputed 1",
		"the line: walks routed, calls performed, the source paths, the calls left by reason, the rounding controls, the native pass's share");
	stats.Reset();
	Check(stats.Routed() == 0 && stats.adds == 0 && stats.Left() == 0, "reset clears it");
	char tiny[6];
	rw::FormatRouteStats(stats, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
}

void TestControlWord() {
	Section("The routed walk rounds under the entry's control word, as the original (the garbage root of run 20261030-131826)");
	for (const std::uint16_t controlWord : {std::uint16_t{0x007F}, std::uint16_t{0x047F}, std::uint16_t{0x087F}, std::uint16_t{0x0C7F}}) {
		PlainGame plain;
		plain.m.controlWord = controlWord;
		const GarbageScene p = BuildGarbageScene(plain.b);
		plain.Walk(p.light, p.root);
		RoutedGame routed;
		routed.m.controlWord = controlWord;
		const GarbageScene r = BuildGarbageScene(routed.b);
		const std::uint32_t eax = routed.Entry(r.light, r.root);
		const bool truncateOrDown = controlWord == 0x0C7F || controlWord == 0x047F;  // s stores as FLT_MAX: the root in
		char line[200];
		std::snprintf(line, sizeof(line), "fpcw %04X: the original performed %zu, the route %zu - the same calls and masks, %s",
			static_cast<unsigned>(controlWord), plain.performed.size(), routed.performed.size(),
			truncateOrDown ? "the root in" : "the root out");
		Check(routed.performed == plain.performed && plain.performed.size() == (truncateOrDown ? 1u : 0u) && eax == 1 &&
				routed.reports.size() == 1 && routed.reports[0].controlWord == controlWord,
			line);
	}
}

}  // namespace

int main() {
	TestModeSelection();
	TestRouteEax();
	TestPerformingScene();
	TestRoutedEqualsOriginal();
	TestRouteNothingInRange();
	TestLeftToOriginal();
	TestReentrant();
	TestGateOffMidWalk();
	TestStats();
	TestControlWord();
	return Failures();
}
