// Every flow of the range-walk shadow's logic (plugin/src/ShadowRangeWalkLogic.h):
// the comparison of two AddGeometry sequences and of the final mask and AL,
// the entry decision (outermost call compared, inner recursive calls passed
// straight through, the three skips), the AddGeometry recording rule, the
// depth scope, the mapped memory reads of the light and the scene, and the
// detour's whole flow over a fake game whose "original" walks the scene the
// way 0x007D59E0 is read to (re/functions/0x007D59E0.md), recursing through
// the detour and calling AddGeometry through the recording hook, as the
// patched game does.
//
// The fake game (memory, builder, scene, the original as read) is shared
// with the route's tests: RangeWalkFakeGame.h.

#include "Check.h"
#include "RangeWalkFakeGame.h"
#include "ShadowRangeWalkLogic.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rw = obng::plugin::range_walk;
namespace walk = obng::engine::render::shadow_light_range_walk;
namespace ws = obng::engine::render::which_side;

namespace {

using namespace obng::plugin::range_walk_test;

// ---- 1. The comparison --------------------------------------------------------------

rw::WalkOutcome Outcome(std::vector<rw::AddGeometryEvent> adds, std::uint32_t mask, bool result) {
	rw::WalkOutcome o;
	o.adds = std::move(adds);
	o.maskAfter = mask;
	o.result = result;
	return o;
}

void TestCompare() {
	Section("Two walks compared: AddGeometry sequence, final mask, AL");
	const rw::WalkOutcome a = Outcome({{0x10, 0x3F}, {0x20, 0x3E}, {0x30, 0x3F}}, 0x3F, true);

	rw::Comparison c = rw::Compare(a, a);
	Check(!c.Mismatch() && c.firstDifference == rw::kNoDifference && c.originalCount == 3 && c.rebuildCount == 3 &&
			!c.maskDiffers && !c.resultDiffers,
		"equal sequences, masks and AL: no mismatch, counts 3/3");

	const rw::WalkOutcome empty = Outcome({}, 0x3F, true);
	c = rw::Compare(empty, empty);
	Check(!c.Mismatch() && c.originalCount == 0 && c.rebuildCount == 0, "two empty sequences are equal");

	rw::WalkOutcome b = a;
	b.adds[1].geometry = 0x21;
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.firstDifference == 1 && !c.maskDiffers && !c.resultDiffers,
		"a different geometry at index 1: first difference 1");

	b = a;
	b.adds[2].mask = 0x3B;
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.firstDifference == 2, "the same geometry with a different live mask at index 2: first difference 2");

	b = a;
	b.adds[0] = {0x20, 0x3E};
	b.adds[1] = {0x10, 0x3F};
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.firstDifference == 0, "the same set in another order: first difference 0 (the order is compared)");

	b = a;
	b.adds.pop_back();
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.firstDifference == 2 && c.originalCount == 3 && c.rebuildCount == 2,
		"the rebuild one short: first difference at the shorter length, counts 3/2");

	b = a;
	b.adds.push_back({0x40, 0x3F});
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.firstDifference == 3 && c.originalCount == 3 && c.rebuildCount == 4,
		"the rebuild one long: first difference 3, counts 3/4");

	c = rw::Compare(empty, a);
	Check(c.Mismatch() && c.firstDifference == 0 && c.originalCount == 0 && c.rebuildCount == 3,
		"the original empty, the rebuild not: first difference 0");

	b = a;
	b.maskAfter = 0x3E;
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.maskDiffers && c.firstDifference == rw::kNoDifference && !c.resultDiffers,
		"only the final mask differs: a mismatch through maskDiffers alone");

	b = a;
	b.result = false;
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.resultDiffers && !c.maskDiffers && c.firstDifference == rw::kNoDifference,
		"only AL differs: a mismatch through resultDiffers alone");

	b = a;
	b.adds[0].mask = 0;
	b.maskAfter = 0;
	b.result = false;
	c = rw::Compare(a, b);
	Check(c.Mismatch() && c.firstDifference == 0 && c.maskDiffers && c.resultDiffers, "all three at once: all three reported");
}

void TestDescribe() {
	Section("The mismatch text");
	const rw::WalkOutcome a = Outcome({{0x10, 0x3F}, {0x20, 0x3E}}, 0x3F, true);
	rw::WalkOutcome b = a;
	b.adds[1] = {0x21, 0x3C};
	b.maskAfter = 0x3E;
	b.result = false;
	char text[400];
	rw::DescribeComparison(rw::Compare(a, b), a, b, text, sizeof(text));
	const std::string s = text;
	Check(s.find("adds 2/2") != std::string::npos, "the counts");
	Check(s.find("first difference at 1: original 00000020 mask 0000003E, rebuild 00000021 mask 0000003C") !=
			std::string::npos,
		"the first differing pair with both masks");
	Check(s.find("mask after original 0000003F rebuild 0000003E") != std::string::npos, "the final masks");
	Check(s.find("AL original 1 rebuild 0") != std::string::npos, "AL of both");

	b = a;
	b.adds.pop_back();
	rw::DescribeComparison(rw::Compare(a, b), a, b, text, sizeof(text));
	Check(std::string(text).find("first difference at 1: original 00000020 mask 0000003E, rebuild (ended)") !=
			std::string::npos,
		"a sequence that ended is named as such");

	rw::DescribeComparison(rw::Compare(b, a), b, a, text, sizeof(text));
	Check(std::string(text).find("original (ended), rebuild 00000020 mask 0000003E") != std::string::npos,
		"the original ending first, the same");

	rw::DescribeComparison(rw::Compare(a, a), a, a, text, sizeof(text));
	Check(std::string(text) == " adds 2/2;", "no difference: only the counts");

	char tiny[8];
	rw::DescribeComparison(rw::Compare(a, b), a, b, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
}

// ---- 2. The entry decision and the recording rule -----------------------------------

void TestDecideEntry() {
	Section("The entry decision");
	Check(rw::DecideEntry(0, 3, 0x1000, 0x2000) == rw::EntryAction::Compare, "outermost, gate on, object and source: compare");
	Check(rw::DecideEntry(1, 3, 0x1000, 0x2000) == rw::EntryAction::PassThrough, "depth 1 (the walk's own recursion): pass through");
	Check(rw::DecideEntry(7, 0, 0, 0) == rw::EntryAction::PassThrough,
		"an inner call passes through whatever else holds (nothing is read for it)");
	Check(rw::DecideEntry(0, 0, 0x1000, 0x2000) == rw::EntryAction::SkipGateOff, "gate 0: skipped (the original returns at once)");
	Check(rw::DecideEntry(0, 0, 0, 0) == rw::EntryAction::SkipGateOff, "gate 0 is decided before anything else");
	Check(rw::DecideEntry(0, 1, 0, 0x2000) == rw::EntryAction::SkipNullObject, "a null object: skipped");
	Check(rw::DecideEntry(0, 1, 0, 0) == rw::EntryAction::SkipNullObject, "a null object is decided before the source");
	Check(rw::DecideEntry(0, 1, 0x1000, 0) == rw::EntryAction::SkipNoSource, "a light without a source (+0x100 null): skipped");
	Check(rw::DecideEntry(0, 0xFFFFFFFF, 0x1000, 0x2000) == rw::EntryAction::Compare, "any non-zero gate counts as on");

	Check(rw::SkipReason(rw::EntryAction::SkipGateOff) != nullptr && rw::SkipReason(rw::EntryAction::SkipNullObject) != nullptr &&
			rw::SkipReason(rw::EntryAction::SkipNoSource) != nullptr,
		"every skip has a reason for the log");
	Check(rw::SkipReason(rw::EntryAction::Compare) == nullptr && rw::SkipReason(rw::EntryAction::PassThrough) == nullptr,
		"compare and pass-through are not skips");
}

void TestDepthAndRecording() {
	Section("The depth scope and the AddGeometry recording rule");
	rw::ThreadWalkState state;
	{
		const rw::DepthScope outer(state);
		Check(outer.Before() == 0 && state.depth == 1, "the first scope sees depth 0 and raises it to 1");
		{
			const rw::DepthScope inner(state);
			Check(inner.Before() == 1 && state.depth == 2, "a nested scope sees 1");
		}
		Check(state.depth == 1, "leaving the nested scope restores 1");
	}
	Check(state.depth == 0, "leaving the outer scope restores 0");

	std::vector<rw::AddGeometryEvent> sink;
	Check(!rw::RecordsAddGeometry(state, 0x5000), "no walk being compared: nothing recorded (0x007C6100's calls)");
	state.recordingLight = 0x5000;
	state.recording = &sink;
	Check(rw::RecordsAddGeometry(state, 0x5000), "the compared walk's light: recorded");
	Check(!rw::RecordsAddGeometry(state, 0x6000), "another light during the walk: not recorded");
	rw::OnAddGeometry(state, 0x5000, 0x77, [] { return 0x3Bu; });
	rw::OnAddGeometry(state, 0x6000, 0x88, [] { return 0x3Fu; });
	Check(sink.size() == 1 && sink[0].geometry == 0x77 && sink[0].mask == 0x3B, "OnAddGeometry records geometry and live mask once");
	bool maskRead = false;
	state.recordingLight = 0;
	rw::OnAddGeometry(state, 0x5000, 0x99, [&] {
		maskRead = true;
		return 0u;
	});
	Check(sink.size() == 1 && !maskRead, "not recording: the mask is not even read");
}

// ---- 3. The mapped reads ----------------------------------------------------------------

void TestCaptureLight() {
	Section("The light's values read at the mapped offsets");
	FakeMemory m;
	Builder b{m};
	const std::uint32_t source = b.Source(kPointLightRtti, 1.0f, 2.0f, 3.0f, 400.0f);
	const std::uint32_t light = b.Light(source, 0x1234, 0x2D);
	for (unsigned i = 0; i < 6; ++i) {
		b.Plane(light, i, static_cast<float>(i), 10.0f + i, 20.0f + i, 30.0f + i);
	}
	const walk::LightValues v = rw::CaptureLight(m, light);
	bool planes = true;
	for (unsigned i = 0; i < 6; ++i) {
		planes = planes && v.planes[i].normalX == static_cast<float>(i) && v.planes[i].normalY == 10.0f + i &&
			v.planes[i].normalZ == 20.0f + i && v.planes[i].constant == 30.0f + i;
	}
	Check(v.hasSource && v.sourceRtti == kPointLightRtti && v.planeSource == 0x1234 && v.activePlanes == 0x2D,
		"source, its RTTI through slot +0x04, +0x14C, +0x1B0");
	Check(planes, "six planes at +0x150 + 16*i (normal x, y, z, constant)");
	Check(v.sourcePosition.x == 1.0f && v.sourcePosition.y == 2.0f && v.sourcePosition.z == 3.0f && v.sourceRange == 400.0f,
		"the source's position +0x88 and range +0xF8");
	Check(m.callLog.size() == 1 && m.callLog[0] == std::make_pair(0x04u, source), "one GetRTTI call, on the source");

	FakeMemory n;
	Builder c{n};
	const std::uint32_t bare = c.Light(0, 0, 0x3F);
	const walk::LightValues w = rw::CaptureLight(n, bare);
	Check(!w.hasSource && w.sourceRtti == 0 && w.sourceRange == 0.0f && n.calls == 0,
		"no source: no virtual call, the source fields stay zero");
}

void TestMemoryScene() {
	Section("The scene ports read at the mapped offsets and call the mapped helpers");
	FakeMemory m;
	Builder b{m};
	const std::uint32_t node = b.Object(Kind::Node, 1, 2, 3, 4, 0x21);
	const std::uint32_t g1 = b.Object(Kind::Geometry, 0, 0, 0, 1);
	const std::uint32_t g2 = b.Object(Kind::Geometry, 0, 0, 0, 0);
	b.Children(node, {g1, 0, g2});
	const std::uint32_t sw = b.Object(Kind::Switch, 0, 0, 0, 1);
	b.Children(sw, {g1, g2});
	m.Put32(sw + 0xE0, 1);
	m.Put32(kGate, 4);
	m.geometry[g1] = 0xABCD0000;

	std::vector<rw::AddGeometryEvent> adds;
	rw::MemoryScene<FakeMemory> scene{m, &adds};
	static_assert(walk::engine::RangeWalkScene<rw::MemoryScene<FakeMemory>>, "MemoryScene satisfies the scene concept");
	Check(scene.gate() == 4, "the gate dword 0x00B42F48");
	Check(scene.asNode(node) == node && scene.asNode(g1) == 0, "asNode through slot +0x08");
	Check(scene.nodeRtti(sw) == kSwitchRtti && scene.nodeRtti(node) == kNodeRtti, "nodeRtti through slot +0x04");
	Check(scene.asGeometry(g1) == 0xABCD0000 && scene.asGeometry(node) == 0, "asGeometry through slot +0x10");
	Check(scene.childCount(node) == 3 && scene.child(node, 0) == g1 && scene.child(node, 1) == 0 && scene.child(node, 2) == g2,
		"the u16 count +0xB6 and the slots of the array +0xB0");
	Check(scene.flags(node) == 0x21, "flags, the byte +0x18");
	const ws::SphereValues s = scene.bound(node);
	Check(s.centerX == 1 && s.centerY == 2 && s.centerZ == 3 && s.radius == 4, "the world bound +0x20..+0x2C");
	m.callLog.clear();
	Check(scene.activeChild(sw) == g2 && m.callLog.size() == 1 && m.callLog[0] == std::make_pair(0x007C59E0u, sw),
		"activeChild calls 0x007C59E0 on the switch");
	m.callLog.clear();
	Check(scene.hasNonZeroBoundRadius(g1) && !scene.hasNonZeroBoundRadius(g2) && m.callLog.size() == 2 &&
			m.callLog[0] == std::make_pair(0x0049FFC0u, g1),
		"hasNonZeroBoundRadius calls 0x0049FFC0 and tests AL only");
	scene.addGeometry(g1, 0x3E);
	Check(adds.size() == 1 && adds[0].geometry == g1 && adds[0].mask == 0x3E, "addGeometry records, nothing is called");
}

// ---- 4. The detour's flow over a fake game ------------------------------------------------

// A fake patched game: the walk's entry leads to the detour (ShadowWalk), the
// detour's callOriginal is FakeOriginal, which walks as 0x007D59E0 is read,
// recursing through the entry (the detour again) and calling AddGeometry
// through the recording hook.
struct FakeGame {
	FakeMemory m;
	Builder b{m};
	rw::ThreadWalkState state;
	rw::WalkBuffers buffers;
	std::vector<rw::WalkReport> reports;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> performedAdds;  // what the "original AddGeometry" received
	std::uint64_t originalCalls = 0;
	std::uint64_t entries = 0;
	// Faults to inject into the original, to see the comparison catch them.
	std::uint32_t dropGeometry = 0;   // never handed to AddGeometry
	bool changeMaskOnExit = false;    // the outermost call leaves +0x1B0 other than it found it
	bool returnZero = false;          // AL 0 at the outermost call

	std::uint32_t Mask(std::uint32_t light) { return m.Raw32(light + 0x1B0); }

	// The entry 0x007D59E0, patched: every call goes to the detour first.
	std::uint32_t Entry(std::uint32_t light, std::uint32_t object) {
		++entries;
		return rw::ShadowWalk(m, state, buffers, light, object,
			[this](std::uint32_t l, std::uint32_t o) { return FakeOriginal(l, o); }, StubSquareRoot,
			[this](const rw::WalkReport& r) { reports.push_back(r); });
	}

	// 0x007D5790, patched: the recording hook, then the original.
	void AddGeometry(std::uint32_t light, std::uint32_t geometry) {
		rw::OnAddGeometry(state, light, geometry, [&] { return Mask(light); });
		performedAdds.emplace_back(light, geometry);
	}

	std::uint32_t FakeOriginal(std::uint32_t light, std::uint32_t object) {
		++originalCalls;
		const bool outermost = state.depth == 1;  // the detour's scope raised it to 1 for the outermost call
		const std::uint32_t saved = Mask(light);
		const std::uint32_t eax = OriginalWalk(
			m, light, object, [this](std::uint32_t l, std::uint32_t o) { Entry(l, o); },
			[this](std::uint32_t l, std::uint32_t o, std::uint32_t g) {
				if (o != dropGeometry) {
					AddGeometry(l, g);
				}
			});
		if (eax == kGateOffEax) {
			return eax;
		}
		if (outermost && changeMaskOnExit) {
			m.Put32(light + 0x1B0, saved ^ 0x01u);
		}
		return outermost && returnZero ? 0x100 : eax;
	}
};

SceneShape BuildScene(FakeGame& game, bool planes) {
	return obng::plugin::range_walk_test::BuildScene(game.b, planes);
}

void TestFlowEqual() {
	Section("The detour's flow: original and rebuild agree (planes and distance)");
	for (const bool planes : {true, false}) {
		FakeGame game;
		const SceneShape s = BuildScene(game, planes);
		const std::uint32_t eax = game.Entry(s.light, s.root);
		Check((eax & 0xFF) == 1, planes ? "planes: the original's EAX is what the detour returns"
										 : "distance: the original's EAX is what the detour returns");
		Check(game.reports.size() == 1 && game.reports[0].action == rw::EntryAction::Compare,
			"one report for the outermost call, none for the inner recursive calls");
		Check(game.entries > 1 && game.originalCalls == game.entries, "every inner call went through the entry and on to the original");
		const rw::Comparison& c = game.reports[0].comparison;
		Check(!c.Mismatch() && c.originalCount == 3 && c.rebuildCount == 3, "three geometries, both sides equal");
		Check(game.buffers.original.adds.size() == 3 && game.buffers.original.adds[0].geometry == s.g1 &&
				game.buffers.original.adds[1].geometry == s.g2 && game.buffers.original.adds[2].geometry == s.g3,
			"the original's AddGeometry calls in pre-order: g1, g2, the switch's active g3");
		Check(game.performedAdds.size() == 3, "the original's AddGeometry calls were performed (the game continues with them)");
		bool narrowed = false;
		for (const rw::AddGeometryEvent& e : game.buffers.original.adds) {
			narrowed = narrowed || e.mask != 0x3F;
		}
		Check(narrowed == planes, planes ? "planes: some live masks were narrowed (the masks are really compared)"
										 : "distance: the mask is never narrowed");

		Check(game.state.depth == 0 && game.state.recordingLight == 0 && game.state.recording == nullptr,
			"after the walk: depth 0, nothing recording");
		Check(game.reports[0].mode == (planes ? walk::SourceMode::Planes : walk::SourceMode::Distance) &&
				game.reports[0].light == s.light && game.reports[0].object == s.root && game.reports[0].maskBefore == 0x3F,
			"the report names the light, the root, the source mode and the entry mask");
	}

	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.m.rtti[game.m.Raw32(s.light + 0x100)] = kDirectionalRtti;
		game.Entry(s.light, s.root);
		const rw::Comparison& c = game.reports[0].comparison;
		Check(game.reports[0].mode == walk::SourceMode::Directional && !c.Mismatch() && c.originalCount == 4,
			"a directional source: every object in range, the far geometry too (4 adds), both sides equal");
	}

	FakeGame game;
	const SceneShape s = BuildScene(game, true);
	game.m.Put32(s.light + 0x1B0, 0x2A);
	game.Entry(s.light, s.root);
	Check(!game.reports[0].comparison.Mismatch() && game.reports[0].maskBefore == 0x2A,
		"a narrowed entry mask (0x2A): still equal, the mask carried into the AddGeometry events");
	bool liveMasks = true;
	for (const rw::AddGeometryEvent& e : game.buffers.original.adds) {
		liveMasks = liveMasks && (e.mask & ~0x2Au) == 0;
	}
	Check(liveMasks, "the recorded live masks never hold a bit the entry mask lacked");

	game.reports.clear();
	game.Entry(s.light, s.root);
	Check(game.reports.size() == 1 && !game.reports[0].comparison.Mismatch() && game.buffers.original.adds.size() == 3,
		"a second walk reuses the buffers: cleared, not appended");
}

void TestFlowMismatch() {
	Section("The detour's flow: the comparison catches a different original");
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.dropGeometry = s.g2;
		game.Entry(s.light, s.root);
		const rw::Comparison& c = game.reports[0].comparison;
		Check(c.Mismatch() && c.firstDifference == 1 && c.originalCount == 2 && c.rebuildCount == 3,
			"the original skips g2: first difference 1, counts 2/3");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.changeMaskOnExit = true;
		game.Entry(s.light, s.root);
		const rw::Comparison& c = game.reports[0].comparison;
		Check(c.maskDiffers && c.firstDifference == rw::kNoDifference,
			"the original leaves +0x1B0 changed on exit: only the final mask differs");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, false);
		game.returnZero = true;
		const std::uint32_t eax = game.Entry(s.light, s.root);
		Check(game.reports[0].comparison.resultDiffers && eax == 0x100, "the original returns AL 0: AL differs, its EAX still returned");
	}
}

void TestFlowSkips() {
	Section("The detour's flow: skips and pass-through");
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.m.Put32(kGate, 0);
		const std::uint32_t eax = game.Entry(s.light, s.root);
		Check(eax == 0xAB00 && game.originalCalls == 1, "gate 0: the original runs and its EAX is returned");
		Check(game.reports.size() == 1 && game.reports[0].action == rw::EntryAction::SkipGateOff,
			"reported as skipped, gate off");
		Check(game.buffers.original.adds.empty() && game.state.recordingLight == 0, "nothing recorded");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.m.Put32(s.light + 0x100, 0);
		game.Entry(s.light, s.root);
		Check(game.reports.size() == 1 && game.reports[0].action == rw::EntryAction::SkipNoSource && game.originalCalls == 1,
			"a light without a source: the original runs, the call is reported as skipped");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		std::uint64_t originalRuns = 0;
		const std::uint32_t eax = rw::ShadowWalk(game.m, game.state, game.buffers, s.light, 0,
			[&](std::uint32_t, std::uint32_t) {
				++originalRuns;
				return 7u;
			},
			StubSquareRoot, [&](const rw::WalkReport& r) { game.reports.push_back(r); });
		Check(eax == 7 && originalRuns == 1 && game.reports.size() == 1 &&
				game.reports[0].action == rw::EntryAction::SkipNullObject,
			"a null object: the original runs, reported as skipped");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.state.depth = 1;  // as if the walk were already running on this thread
		const std::uint64_t readsBefore = game.m.reads;
		std::uint64_t originalRuns = 0;
		const std::uint32_t eax = rw::ShadowWalk(game.m, game.state, game.buffers, s.light, s.root,
			[&](std::uint32_t, std::uint32_t) {
				++originalRuns;
				return 0x55u;
			},
			StubSquareRoot, [&](const rw::WalkReport& r) { game.reports.push_back(r); });
		Check(eax == 0x55 && originalRuns == 1 && game.reports.empty() && game.m.reads == readsBefore,
			"an inner call: the original runs, nothing is read, nothing reported");
		Check(game.state.depth == 1, "the depth is back where the inner call found it");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, true);
		game.AddGeometry(s.light, s.g1);  // 0x007C6100's call: no walk on this thread
		Check(game.buffers.original.adds.empty() && game.performedAdds.size() == 1,
			"an AddGeometry outside a compared walk is performed and not recorded");
	}
}


void TestStats() {
	Section("The coverage line of the summary");
	rw::WalkStats stats;
	rw::WalkReport r{};
	r.action = rw::EntryAction::Compare;
	r.mode = walk::SourceMode::Planes;
	r.comparison.originalCount = 5;
	stats.Add(r);
	r.mode = walk::SourceMode::Distance;
	r.comparison.originalCount = 9;
	r.comparison.maskDiffers = true;
	stats.Add(r);
	rw::WalkReport skipped{};
	skipped.action = rw::EntryAction::SkipGateOff;
	skipped.comparison.originalCount = 100;
	stats.Add(skipped);
	Check(stats.Walks() == 2 && stats.adds == 14 && stats.maxAdds == 9 && stats.mismatches == 1,
		"two compared walks (a skip is not one), 14 adds, at most 9, one mismatching");
	char line[300];
	rw::FormatWalkStats(stats, line, sizeof(line));
	Check(std::string(line) ==
			"Shadow range walk: 2 walks compared (planes 1, distance 1, directional 0, no source 0), "
			"14 AddGeometry calls compared, at most 9 in one walk, 1 mismatching walks",
		"the line names the walks by source path and the adds");
	stats.Reset();
	Check(stats.Walks() == 0 && stats.adds == 0 && stats.maxAdds == 0, "reset clears it");
	char tiny[6];
	rw::FormatWalkStats(stats, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
}

}  // namespace

int main() {
	TestCompare();
	TestDescribe();
	TestDecideEntry();
	TestDepthAndRecording();
	TestCaptureLight();
	TestMemoryScene();
	TestFlowEqual();
	TestFlowMismatch();
	TestFlowSkips();
	TestStats();
	return Failures();
}
