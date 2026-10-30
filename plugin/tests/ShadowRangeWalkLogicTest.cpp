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
#include <functional>
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
	const walk::LightValues v = rw::CaptureLight(m, light, ws::X87Rounding::Zero);
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
	Check(v.rounding == ws::X87Rounding::Zero, "the rounding control the caller passes (the entry's control word)");

	FakeMemory n;
	Builder c{n};
	const std::uint32_t bare = c.Light(0, 0, 0x3F);
	const walk::LightValues w = rw::CaptureLight(n, bare, ws::X87Rounding::Down);
	Check(!w.hasSource && w.sourceRtti == 0 && w.sourceRange == 0.0f && n.calls == 0 && w.rounding == ws::X87Rounding::Down,
		"no source: no virtual call, the source fields stay zero, the rounding control kept");
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
	// Side effects of the outermost original on its own inputs, before and after it walks.
	std::function<void(FakeGame&)> atOriginalStart;
	std::function<void(FakeGame&)> atOriginalEnd;
	obng::plugin::RangeWalkOrder order = obng::plugin::RangeWalkOrder::OriginalFirst;
	// >= 0: the detour is handed this word instead of the one the game runs
	// under (a broken FNSTCW, to see the comparison catch it).
	int handedControlWord = -1;

	std::uint32_t Mask(std::uint32_t light) { return m.Raw32(light + 0x1B0); }

	// The entry 0x007D59E0, patched: every call goes to the detour first, with
	// the control word its FNSTCW reads.
	std::uint32_t Entry(std::uint32_t light, std::uint32_t object) {
		++entries;
		const auto controlWord = handedControlWord >= 0 ? static_cast<std::uint16_t>(handedControlWord) : m.controlWord;
		return rw::ShadowWalk(m, state, buffers, order, controlWord, light, object,
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
		if (outermost && atOriginalStart) {
			atOriginalStart(*this);
		}
		const std::uint32_t eax = OriginalWalk(
			m, light, object, [this](std::uint32_t l, std::uint32_t o) { Entry(l, o); },
			[this](std::uint32_t l, std::uint32_t o, std::uint32_t g) {
				if (o != dropGeometry) {
					AddGeometry(l, g);
				}
			});
		if (outermost && atOriginalEnd) {
			atOriginalEnd(*this);
		}
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
		const std::uint32_t eax = rw::ShadowWalk(game.m, game.state, game.buffers, obng::plugin::RangeWalkOrder::OriginalFirst, game.m.controlWord, s.light, 0,
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
		const std::uint32_t eax = rw::ShadowWalk(game.m, game.state, game.buffers, obng::plugin::RangeWalkOrder::OriginalFirst, game.m.controlWord, s.light, s.root,
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


// ---- 5. The order switch and the trace of the rebuild's reads ---------------------------

using obng::plugin::RangeWalkOrder;

void TestParseOrder() {
	Section("[Shadow] RangeWalkOrder parsed");
	bool known = false;
	Check(obng::plugin::ParseRangeWalkOrder("original-first", &known) == RangeWalkOrder::OriginalFirst && known,
		"original-first");
	Check(obng::plugin::ParseRangeWalkOrder("rebuild-first", &known) == RangeWalkOrder::RebuildFirst && known, "rebuild-first");
	Check(obng::plugin::ParseRangeWalkOrder("Rebuild-First", &known) == RangeWalkOrder::RebuildFirst && known,
		"case does not matter (an INI typed by hand)");
	Check(obng::plugin::ParseRangeWalkOrder("rebuild", &known) == RangeWalkOrder::OriginalFirst && !known,
		"anything else: original-first, and said to be unknown");
	Check(obng::plugin::ParseRangeWalkOrder("", &known) == RangeWalkOrder::OriginalFirst && !known, "empty: the same");
	Check(obng::plugin::ParseRangeWalkOrder("rebuild-first-x", &known) == RangeWalkOrder::OriginalFirst && !known,
		"a longer word is not a match");
	Check(std::string(obng::plugin::RangeWalkOrderName(RangeWalkOrder::OriginalFirst)) == "original-first" &&
			std::string(obng::plugin::RangeWalkOrderName(RangeWalkOrder::RebuildFirst)) == "rebuild-first",
		"the names round-trip");
}

void TestTrace() {
	Section("The trace of the rebuild's scene reads and its first difference");
	FakeMemory m;
	Builder b{m};
	const SceneShape s = obng::plugin::range_walk_test::BuildScene(b, true);
	std::vector<rw::AddGeometryEvent> adds;
	rw::MemoryScene<FakeMemory> inner{m, &adds};
	std::vector<rw::TraceEvent> trace;
	rw::TracingScene<rw::MemoryScene<FakeMemory>> scene{inner, &trace};
	static_assert(walk::engine::RangeWalkScene<rw::TracingScene<rw::MemoryScene<FakeMemory>>>, "the tracing scene is a scene");
	Check(scene.flags(s.culled) == 0x01 && trace.size() == 1 && trace[0].kind == rw::TraceKind::Flags &&
			trace[0].object == s.culled && trace[0].values[0] == 0x01,
		"a read is forwarded and logged with its result");
	const ws::SphereValues bound = scene.bound(s.g1);
	Check(bound.centerX == 10.0f && trace.back().kind == rw::TraceKind::Bound &&
			trace.back().values[0] == std::bit_cast<std::uint32_t>(10.0f) &&
			trace.back().values[3] == std::bit_cast<std::uint32_t>(5.0f),
		"a bound is logged as its four words, bit for bit");
	scene.addGeometry(s.g1, 0x3E);
	Check(adds.size() == 1 && trace.back().kind == rw::TraceKind::AddGeometry, "AddGeometry is forwarded (recorded) and logged");

	const std::vector<rw::TraceEvent> a = {{rw::TraceKind::Flags, 1, {0, 0, 0, 0}}, {rw::TraceKind::ChildCount, 2, {3, 0, 0, 0}}};
	std::vector<rw::TraceEvent> c = a;
	Check(!rw::FirstTraceDifference(a, c).found, "equal traces: no difference");
	c[1].values[0] = 1;
	rw::TraceDifference d = rw::FirstTraceDifference(a, c);
	Check(d.found && d.index == 1, "a different value at read 1: index 1");
	c = a;
	c.pop_back();
	d = rw::FirstTraceDifference(a, c);
	Check(d.found && d.index == 1, "one trace a prefix of the other: the shorter length");
	char text[300];
	c = a;
	c[1].values[0] = 1;
	rw::DescribeTraceDifference(rw::FirstTraceDifference(a, c), a, c, text, sizeof(text));
	Check(std::string(text).find("inputs first differ at read 1: fresh child-count 00000002 = 00000003 00000000 00000000 "
								 "00000000, after the original child-count 00000002 = 00000001") != std::string::npos,
		"the text names the read, the object and both values");
	c.pop_back();
	rw::DescribeTraceDifference(rw::FirstTraceDifference(a, c), a, c, text, sizeof(text));
	Check(std::string(text).find("after the original (ended)") != std::string::npos, "an ended trace is named as such");
	rw::DescribeTraceDifference(rw::TraceDifference{}, a, a, text, sizeof(text));
	Check(std::string(text).empty(), "no difference: no text");
}

void TestOrders() {
	Section("Both orders over the fake game");
	for (const RangeWalkOrder order : {RangeWalkOrder::OriginalFirst, RangeWalkOrder::RebuildFirst}) {
		for (const bool planes : {true, false}) {
			FakeGame game;
			game.order = order;
			const SceneShape s = BuildScene(game, planes);
			const std::uint32_t eax = game.Entry(s.light, s.root);
			const rw::Comparison& c = game.reports[0].comparison;
			Check(!c.Mismatch() && c.originalCount == 3 && game.performedAdds.size() == 3 && (eax & 0xFF) == 1 &&
					game.reports[0].order == order && !game.reports[0].trace.found,
				order == RangeWalkOrder::RebuildFirst ? "rebuild-first, an original without side effects: equal, its EAX returned"
													  : "original-first, the same: equal (unchanged behaviour)");
			Check(game.state.depth == 0 && game.state.recordingLight == 0, "nothing left recording");
		}
	}

	struct Mutation {
		const char* what;
		std::function<void(FakeGame&, const SceneShape&)> apply;
		rw::TraceKind kind;
	};
	const Mutation mutations[] = {
		{"the flags byte of g2 (culled)", [](FakeGame& g, const SceneShape& s) { g.m.bytes[s.g2 + 0x18] = 0x01; },
			rw::TraceKind::Flags},
		{"the bound of g1 (radius 0.0: the child filter skips it)",
			[](FakeGame& g, const SceneShape& s) { g.m.PutF(s.g1 + 0x2C, 0.0f); },
			rw::TraceKind::Bound},
		{"a node's child count (3 -> 1)",
			[](FakeGame& g, const SceneShape& s) {
				const std::uint32_t data = g.m.Raw32(s.root + 0xB0);
				const std::uint32_t a = g.m.Raw32(data);
				g.m.Put16(a + 0xB6, 1);
			},
			rw::TraceKind::ChildCount},
	};
	for (const Mutation& mutation : mutations) {
		FakeGame fresh;
		fresh.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(fresh, true);
		fresh.atOriginalStart = [&](FakeGame& g) { mutation.apply(g, s); };
		fresh.Entry(s.light, s.root);
		const rw::WalkReport& r = fresh.reports[0];
		Check(r.comparison.Mismatch(), (std::string("rebuild-first, the original changes ") + mutation.what +
										   " before walking: a mismatch").c_str());
		const rw::TraceEvent& before = fresh.buffers.traceFresh[r.trace.index];
		Check(r.trace.found && before.kind == mutation.kind && fresh.buffers.traceAfter[r.trace.index].kind == mutation.kind &&
				!(before == fresh.buffers.traceAfter[r.trace.index]),
			"the trace names the read that changed, with the value before and after the original");
		char text[600];
		rw::DescribeComparison(r.comparison, fresh.buffers.original, fresh.buffers.rebuild, text, sizeof(text));
		const std::size_t at = std::strlen(text);
		rw::DescribeTraceDifference(r.trace, fresh.buffers.traceFresh, fresh.buffers.traceAfter, text + at, sizeof(text) - at);
		Check(std::string(text).find("inputs first differ at read") != std::string::npos, "and the mismatch text carries it");

		FakeGame after;
		const SceneShape t = BuildScene(after, true);
		after.atOriginalStart = [&](FakeGame& g) { mutation.apply(g, t); };
		after.Entry(t.light, t.root);
		Check(!after.reports[0].comparison.Mismatch() && !after.reports[0].trace.found,
			"original-first, the same change: equal - the rebuild reads what the original left");
	}

	{
		FakeGame game;
		const SceneShape s = BuildScene(game, false);
		game.atOriginalEnd = [&](FakeGame& g) { g.m.PutF(g.m.Raw32(s.light + 0x100) + 0xF8, 0.0f); };
		game.Entry(s.light, s.root);
		Check(!game.reports[0].comparison.Mismatch() && game.reports[0].comparison.rebuildCount == 3,
			"original-first: the light is read BEFORE the original - a range the original zeroes on its way out is not seen");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, false);
		game.atOriginalStart = [&](FakeGame& g) { g.m.PutF(g.m.Raw32(s.light + 0x100) + 0xF8, 0.0f); };
		game.Entry(s.light, s.root);
		Check(game.reports[0].comparison.Mismatch() && game.reports[0].comparison.originalCount == 0 &&
				game.reports[0].comparison.rebuildCount == 3,
			"original-first: a range the original zeroes before walking shows as a mismatch (the light is not re-read)");
	}
	{
		FakeGame game;
		game.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(game, false);
		game.atOriginalStart = [&](FakeGame& g) { g.m.PutF(g.m.Raw32(s.light + 0x100) + 0xF8, 0.0f); };
		game.Entry(s.light, s.root);
		const rw::WalkReport& r = game.reports[0];
		Check(r.comparison.Mismatch() && r.lightChanged && !r.trace.found,
			"rebuild-first, the original changes the light's range: a mismatch, reported as the light's values changing "
			"(the scene reads equal)");
	}
	{
		FakeGame game;
		game.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(game, true);
		game.atOriginalStart = [&](FakeGame& g) { g.m.bytes[s.g2 + 0x18] = 0x01; };
		game.Entry(s.light, s.root);
		Check(game.reports[0].trace.found && !game.reports[0].lightChanged, "a scene change leaves the light's values unchanged");
	}
	{
		FakeGame game;
		game.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(game, true);
		game.dropGeometry = s.g2;
		game.Entry(s.light, s.root);
		Check(game.reports[0].comparison.Mismatch() && !game.reports[0].trace.found,
			"rebuild-first, an original that differs without changing its inputs: a mismatch, the two rebuild traces equal");
	}
}

void TestX87State() {
	Section("The x87 state at the walk's entry, decoded from an FXSAVE image");
	std::uint8_t area[512] = {};
	area[0] = 0x7F;  // FCW 0x007F: precision 24-bit, rounding nearest, all masked
	area[1] = 0x00;
	area[2] = 0x00;  // FSW 0x3800: TOP = 7
	area[3] = 0x38;
	area[4] = 0x80;  // abridged tags: physical register 7 in use
	area[24] = 0x80;  // MXCSR 0x00001F80
	area[25] = 0x1F;
	const rw::X87State x = rw::DecodeFxsave(area);
	Check(x.taken && x.control == 0x007F && x.status == 0x3800 && x.tags == 0x80 && x.mxcsr == 0x1F80,
		"FCW at offset 0, FSW at 2, the abridged tag byte at 4, MXCSR at 24");
	Check(rw::X87Top(0x3800) == 7 && rw::X87Top(0x0000) == 0 && rw::X87Top(0xC7FF) == 0 && rw::X87Top(0x0800) == 1,
		"TOP is bits 11..13 of the status word");
	Check(rw::X87InUse(0x00) == 0 && rw::X87InUse(0xFF) == 8 && rw::X87InUse(0x81) == 2, "registers in use: the tag bits set");
	Check(!rw::X87PushOverflows(0x3800, 0x80), "TOP 7, only R7 in use: a push goes to R6, free");
	Check(rw::X87PushOverflows(0x3800, 0x40), "TOP 7 with R6 in use: a push would overflow");
	Check(rw::X87PushOverflows(0x0000, 0x80), "TOP 0: a push goes to R7 (wraps), in use: overflow");
	Check(rw::X87PushOverflows(0x0000, 0xFF), "all eight in use (an MMX state without EMMS): overflow");
	Check(std::string(rw::X87PrecisionName(0x007F)) == "24-bit" && std::string(rw::X87PrecisionName(0x027F)) == "53-bit" &&
			std::string(rw::X87PrecisionName(0x037F)) == "64-bit" && std::string(rw::X87PrecisionName(0x017F)) == "reserved",
		"precision control, bits 8..9 of the control word");
	char text[300];
	rw::FormatX87State(x, text, sizeof(text));
	Check(std::string(text) ==
			" x87 at entry: TOP=7, tags=0x80 (1 of 8 in use, a push would not overflow), control 0x007F (precision 24-bit), "
			"status 0x3800, MXCSR 0x00001F80;",
		"the log text");
	rw::FormatX87State(rw::X87State{}, text, sizeof(text));
	Check(std::string(text) == " x87 at entry: not taken;", "not taken: said so");
	char tiny[10];
	rw::FormatX87State(x, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
}

void TestRootDistance() {
	Section("The root's distance terms on a rebuild-first mismatch");
	{
		FakeGame game;
		game.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(game, false);
		game.atOriginalStart = [&](FakeGame& g) { g.m.PutF(g.m.Raw32(s.light + 0x100) + 0xF8, 0.0f); };
		game.Entry(s.light, s.root);
		const rw::RootDistance& d = game.reports[0].root;
		Check(d.valid && d.range == 100.0f && d.bound.radius == 60.0f && d.terms.sumOfSquares == 0.0f && d.terms.gap == -60.0f &&
				d.inRange,
			"distance path: the root's bound, the range the rebuild used, the terms, its decision");
		char text[400];
		rw::FormatRootDistance(d, text, sizeof(text));
		Check(std::string(text).find(" root distance: range 100 (42C80000) gap -60 (C2700000) in range 1;") == 0,
			"the text leads with range, gap and decision, floats with their bits");
		Check(std::string(text).find("s 0 (00000000) q 0 (00000000)") != std::string::npos, "and the square root's in and out");
	}
	{
		FakeGame game;
		game.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(game, true);
		game.dropGeometry = s.g2;
		game.Entry(s.light, s.root);
		Check(game.reports[0].comparison.Mismatch() && !game.reports[0].root.valid, "planes path: no distance terms");
		char text[100];
		rw::FormatRootDistance(game.reports[0].root, text, sizeof(text));
		Check(std::string(text).empty(), "and no text");
	}
	{
		FakeGame game;
		const SceneShape s = BuildScene(game, false);
		game.atOriginalStart = [&](FakeGame& g) { g.m.PutF(g.m.Raw32(s.light + 0x100) + 0xF8, 0.0f); };
		game.Entry(s.light, s.root);
		Check(game.reports[0].comparison.Mismatch() && !game.reports[0].root.valid,
			"original-first: the terms are only computed for the rebuild-first diagnostic");
	}
	{
		FakeGame game;
		game.order = RangeWalkOrder::RebuildFirst;
		const SceneShape s = BuildScene(game, false);
		game.Entry(s.light, s.root);
		Check(!game.reports[0].comparison.Mismatch() && !game.reports[0].root.valid, "no mismatch: no terms");
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
	r.controlWord = 0x0C7F;
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
			"14 AddGeometry calls compared, at most 9 in one walk, 1 mismatching walks; by rounding control at entry "
			"nearest 1, down 0, up 0, truncate 1",
		"the line names the walks by source path, the adds and the rounding controls the walks ran under");
	Check(stats.byRounding[0] == 1 && stats.byRounding[3] == 1, "0x0000 counts as nearest, 0x0C7F as truncate; a skip is not counted");
	stats.Reset();
	Check(stats.Walks() == 0 && stats.adds == 0 && stats.maxAdds == 0, "reset clears it");
	char tiny[6];
	rw::FormatWalkStats(stats, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny), "a small buffer is never overrun");
}

void TestControlWord() {
	Section("The control word at the entry decides the rebuild's rounding (the in-game mismatch of run 20261030-131826)");
	for (const RangeWalkOrder order : {RangeWalkOrder::OriginalFirst, RangeWalkOrder::RebuildFirst}) {
		const bool rebuildFirst = order == RangeWalkOrder::RebuildFirst;
		{
			FakeGame game;
			game.order = order;
			game.m.controlWord = 0x0C7F;
			const GarbageScene g = BuildGarbageScene(game.b);
			game.Entry(g.light, g.root);
			Check(game.reports.size() == 1 && !game.reports[0].comparison.Mismatch() && game.reports[0].comparison.originalCount == 1 &&
					game.reports[0].controlWord == 0x0C7F,
				rebuildFirst ? "rebuild-first, truncate: the root in, one geometry on both sides, no mismatch, fpcw 0C7F reported"
				             : "original-first, truncate: the root in, one geometry on both sides, no mismatch, fpcw 0C7F reported");
		}
		{
			FakeGame game;
			game.order = order;
			const GarbageScene g = BuildGarbageScene(game.b);
			game.Entry(g.light, g.root);
			Check(game.reports.size() == 1 && !game.reports[0].comparison.Mismatch() && game.reports[0].comparison.originalCount == 0,
				rebuildFirst ? "rebuild-first, nearest: the root out on both sides, no mismatch"
				             : "original-first, nearest: the root out on both sides, no mismatch");
		}
		{
			FakeGame game;
			game.order = order;
			game.m.controlWord = 0x0C7F;
			game.handedControlWord = 0x007F;  // the rebuild told nearest while the game walks at truncate
			const GarbageScene g = BuildGarbageScene(game.b);
			game.Entry(g.light, g.root);
			const rw::WalkReport& r = game.reports[0];
			Check(r.comparison.Mismatch() && r.comparison.originalCount == 1 && r.comparison.rebuildCount == 0,
				rebuildFirst ? "rebuild-first, the wrong word: the mismatch of the game run (adds 1/0)"
				             : "original-first, the wrong word: the mismatch of the game run (adds 1/0)");
			if (rebuildFirst) {
				Check(r.root.valid && std::bit_cast<std::uint32_t>(r.root.terms.sumOfSquares) == 0x7F800000u && !r.root.inRange,
					"  the root's terms under the handed word: s +inf, out of range");
			}
		}
	}
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
	TestParseOrder();
	TestTrace();
	TestOrders();
	TestX87State();
	TestRootDistance();
	TestControlWord();
	return Failures();
}
