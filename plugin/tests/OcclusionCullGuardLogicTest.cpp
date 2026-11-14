// The occlusion culler's guards (plugin/src/OcclusionCullGuardLogic.h and the
// identity and far LOD parts of OcclusionCullLogic.h's Decide and Collect):
// which object an address holds, what hangs under the far LOD's root, the
// LOD rules, the guards' summary line and the window after a cell change.

#include "Check.h"
#include "OcclusionCullGuardLogic.h"

#include <cstring>
#include <map>
#include <string>

namespace cull = obng::plugin::occlusion_cull;
namespace guard = obng::plugin::occlusion_cull::guard;
using cull::Batch;
using cull::Camera;
using cull::Cameras;
using cull::Decided;
using cull::Decision;
using cull::LodRule;
using cull::Read;
using cull::Ring;
using cull::Settings;
using cull::Stats;
using cull::Table;
using cull::TestKind;

namespace {

Cameras StillCameras() {
	Cameras cameras;
	Camera still;
	still.direction[1] = 1.0f;
	for (std::uint32_t f = 0; f < cull::kCameraRing; ++f) {
		cameras.Set(f, still);
	}
	return cameras;
}

bool IsLod(std::uint32_t object) {
	return object == 0x4000 || object == 0x6000;
}

int g_lodAsked = 0;
bool CountedLod(std::uint32_t object) {
	++g_lodAsked;
	return IsLod(object);
}

void TestIdentity() {
	Section("The object at an address");
	const std::uint32_t a = guard::Identity(0x00B10000, 0x80001000, 0x80002000);
	Check(a != 0 && a == guard::Identity(0x00B10000, 0x80001000, 0x80002000), "the same name, parent and data: the same identity");
	Check(a != guard::Identity(0x00B10040, 0x80001000, 0x80002000), "another name: another");
	Check(a != guard::Identity(0x00B10000, 0x80001100, 0x80002000), "another parent: another");
	Check(a != guard::Identity(0x00B10000, 0x80001000, 0x80002100), "other data: another");
	Check(a != guard::Identity(0x80001000, 0x00B10000, 0x80002000), "the fields are not interchangeable");
	Check(guard::Identity(0, 0, 0) != 0, "never 0, which means not said");
	Check(a == guard::Identity(0x00B10000, 0x80001000, 0x80002000, 0) && a != guard::Identity(0x00B10000, 0x80001000, 0x80002000, guard::Place(5000.0f, 0.0f)),
	      "another place: another");

	Section("The place of a bound's centre");
	Check(guard::SquareOf(0.0f) == 0 && guard::SquareOf(1023.9f) == 0 && guard::SquareOf(1024.0f) == 1 && guard::SquareOf(-0.1f) == -1 &&
	          guard::SquareOf(-1024.0f) == -1 && guard::SquareOf(-1024.1f) == -2,
	      "squares of 1024 units, the floor on both sides of zero");
	Check(guard::Place(100.0f, 200.0f) == guard::Place(900.0f, 1000.0f), "inside one square: the same place");
	Check(guard::Place(100.0f, 200.0f) != guard::Place(1100.0f, 200.0f) && guard::Place(100.0f, 200.0f) != guard::Place(100.0f, 1200.0f) &&
	          guard::Place(100.0f, 200.0f) != guard::Place(-100.0f, 200.0f),
	      "the next square in x, in y, across zero: another");
	Check(guard::Place(-190146.0f, -37089.0f) != guard::Place(-190146.0f + 4096.0f, -37089.0f) && guard::Place(1500.0f, 2500.0f) != guard::Place(2500.0f, 1500.0f),
	      "the next cell is another place; x and y are not interchangeable");

	Section("A verdict made for another object is dropped before it is acted on");
	Settings settings;
	settings.maxAge = 2;
	const Cameras cameras = StillCameras();
	Stats stats;
	Table table;
	const std::uint32_t first = guard::Identity(1, 2, 3);
	const std::uint32_t second = guard::Identity(1, 2, 4);
	Decided d = cull::Decide(table, 0x1000, 1, TestKind::kTest, settings, cameras, &stats, first);
	Check(d.decision == Decision::kDraw && d.verdict->identity == first && stats.guard.identityResets == 0, "a new slot takes the identity, nothing dropped");
	Check(table.Record(0x1000, 1, true, first), "its test's result is taken");
	Check(cull::Decide(table, 0x1000, 2, TestKind::kTest, settings, cameras, &stats, first).decision == Decision::kSkip, "the same object, hidden a frame ago: left out");
	Check(table.Record(0x1000, 2, true, first), "hidden again");
	d = cull::Decide(table, 0x1000, 3, TestKind::kTest, settings, cameras, &stats, second);
	Check(d.decision == Decision::kDraw && d.verdict->hiddenFrame == 0 && d.verdict->testedFrame == 0 && d.verdict->identity == second,
	      "another object at the address: drawn, the verdict cleared");
	Check(stats.guard.identityResets == 1 && stats.guard.identityResetsHidden == 1, "counted, and that it would have been left out");
	Check(!table.Record(0x1000, 2, true, first) && d.verdict->hiddenFrame == 0, "the gone object's result read afterwards is dropped");
	Check(cull::Decide(table, 0x1000, 4, TestKind::kTest, settings, cameras, &stats, second).decision == Decision::kDraw, "so the new object stays drawn");
	Check(table.Record(0x1000, 4, true, second), "its own result is taken");
	Check(cull::Decide(table, 0x1000, 5, TestKind::kTest, settings, cameras, &stats, second).decision == Decision::kSkip, "and acted on");

	Section("A verdict too old to act on is dropped without the second count; no identity said changes nothing");
	d = cull::Decide(table, 0x1000, 20, TestKind::kTest, settings, cameras, &stats, first);
	Check(stats.guard.identityResets == 2 && stats.guard.identityResetsHidden == 1 && d.decision == Decision::kDraw, "old hidden verdict: dropped, not counted as a wrong skip");
	Table plain;
	cull::Decide(plain, 0x2000, 1, TestKind::kTest, settings, cameras, &stats);
	plain.Record(0x2000, 1, true);
	Check(cull::Decide(plain, 0x2000, 2, TestKind::kTest, settings, cameras, &stats).decision == Decision::kSkip && stats.guard.identityResets == 2,
	      "identity 0 on both sides: as before");

	Section("The same frame again: the frame's decision, the identity not asked twice");
	Table once;
	Stats s2;
	g_lodAsked = 0;
	cull::Decide(once, 0x4000, 1, TestKind::kTest, settings, cameras, &s2, first, &CountedLod);
	cull::Decide(once, 0x4000, 1, TestKind::kTest, settings, cameras, &s2, second, &CountedLod);
	cull::Decide(once, 0x4000, 2, TestKind::kTest, settings, cameras, &s2, first, &CountedLod);
	Check(g_lodAsked == 1 && s2.guard.identityResets == 0, "the LOD question once per identity; a repeat in the frame does not look at the identity");
}

void TestCollect() {
	Section("Reading results: a gone object's is dropped and counted, a far LOD piece's seen skip is counted");
	Ring ring;
	Table table;
	Stats stats;
	bool lost = false;
	Batch& b = ring.Begin(1, &lost);
	table.Claim(0x1000, 1)->identity = 7;
	table.Claim(0x2000, 1)->identity = 8;
	table.Claim(0x3000, 1)->identity = 9;
	b.tests[0].geometry = 0x1000;
	b.tests[0].identity = 6;  // made for the object that was there before
	b.tests[1].geometry = 0x2000;
	b.tests[1].identity = 8;
	b.tests[1].skipped = true;
	b.tests[1].lod = true;
	b.tests[2].geometry = 0x3000;
	b.tests[2].identity = 9;
	b.tests[2].skipped = true;
	b.count = 3;
	b.pending = true;
	cull::Collect(&ring, &table, &stats, [&](const Batch&, std::uint32_t test, std::uint32_t* pixels) {
		*pixels = test == 0 ? 0 : 12;
		return Read::kDone;
	});
	Check(stats.guard.staleResults == 1 && table.Find(0x1000)->hiddenFrame == 0 && stats.resultsHidden == 0, "the stale hidden result does not reach the verdict or the counts");
	Check(stats.skipsUnconfirmed == 2 && stats.guard.lodUnconfirmed == 1, "two skips seen by their test, one of them far LOD");
	Check(!b.pending && b.cursor == 3, "the batch is read to its end");
}

void TestLod() {
	Section("What hangs under the far LOD's root");
	std::map<std::uint32_t, std::uint32_t> parent = {{10, 11}, {11, 12}, {12, 13}, {13, 99}, {20, 21}, {21, 0}, {30, 31}, {31, 32}, {32, 33}, {33, 34}, {34, 35}, {35, 36}, {36, 99}};
	auto parentOf = [&](std::uint32_t o) { return parent.count(o) != 0 ? parent[o] : 0u; };
	Check(guard::UnderRoot(10, 99, parentOf), "four parents up: under it");
	Check(guard::UnderRoot(13, 99, parentOf), "its child");
	Check(!guard::UnderRoot(99, 99, parentOf), "the root itself is not under itself");
	Check(!guard::UnderRoot(20, 99, parentOf), "another tree, ending in null");
	Check(!guard::UnderRoot(30, 99, parentOf), "deeper than the levels walked: not found");
	Check(guard::UnderRoot(30, 99, parentOf, 7), "found with more levels");
	Check(!guard::UnderRoot(0, 99, parentOf) && !guard::UnderRoot(10, 0, parentOf), "no object, no root: no");

	Section("[Route] OcclusionCullLod");
	Check(guard::LodRuleFromIni(0, LodRule::kMaySkip) == LodRule::kNeverSkip && guard::LodRuleFromIni(1, LodRule::kNeverSkip) == LodRule::kMaySkip &&
	          guard::LodRuleFromIni(2, LodRule::kNeverSkip) == LodRule::kCount,
	      "0, 1 and 2");
	Check(guard::LodRuleFromIni(3, LodRule::kNeverSkip) == LodRule::kNeverSkip && guard::LodRuleFromIni(-1, LodRule::kCount) == LodRule::kCount, "anything else: the default");
	Check(!guard::LodTested(LodRule::kNeverSkip) && guard::LodTested(LodRule::kMaySkip) && guard::LodTested(LodRule::kCount), "only a piece that may be acted on or counted is tested");
	Check(std::string(guard::LodRuleName(LodRule::kNeverSkip)) == "never left out" && std::string(guard::LodRuleName(LodRule::kMaySkip)) == "left out like the rest" &&
	          std::string(guard::LodRuleName(LodRule::kCount)) == "drawn and counted",
	      "the rules' names");

	Section("The rules in the decision");
	const Cameras cameras = StillCameras();
	for (const LodRule rule : {LodRule::kNeverSkip, LodRule::kMaySkip, LodRule::kCount}) {
		Settings settings;
		settings.maxAge = 2;
		settings.lodRule = rule;
		Stats stats;
		Table table;
		cull::Decide(table, 0x4000, 1, TestKind::kTest, settings, cameras, &stats, 5, &IsLod);
		cull::Decide(table, 0x5000, 1, TestKind::kTest, settings, cameras, &stats, 6, &IsLod);
		table.Record(0x4000, 1, true, 5);
		table.Record(0x5000, 1, true, 6);
		const Decision lod = cull::Decide(table, 0x4000, 2, TestKind::kTest, settings, cameras, &stats, 5, &IsLod).decision;
		const Decision other = cull::Decide(table, 0x5000, 2, TestKind::kTest, settings, cameras, &stats, 6, &IsLod).decision;
		Check(other == Decision::kSkip, "what is not far LOD is left out under every rule");
		Check(stats.guard.lodAsked == 2, "the far LOD piece counted once a frame");
		if (rule == LodRule::kMaySkip) {
			Check(lod == Decision::kSkip && stats.guard.lodSkipped == 1 && stats.guard.lodKept == 0, "rule 1: left out and counted");
		} else {
			Check(lod == Decision::kDraw && stats.guard.lodSkipped == 0 && stats.guard.lodKept == 1, "rules 0 and 2: drawn against the hidden verdict, counted");
			Check(table.Find(0x4000)->decision == Decision::kDraw, "the frame's kept decision is the draw");
		}
		Check(cull::Decide(table, 0x6000, 2, TestKind::kBothOutside, settings, cameras, &stats, 9, &IsLod).decision == Decision::kSkipFrustum,
		      "a far LOD piece outside both frustums is not drawn under any rule");
	}
}

void TestFormatGuards() {
	Section("The guards' summary line");
	Stats s;
	s.eyePasses = 4;
	s.guard.identityResets = 12;
	s.guard.identityResetsHidden = 3;
	s.guard.staleResults = 2;
	s.guard.lodAsked = 90;
	s.guard.lodSkipped = 7;
	s.guard.lodKept = 5;
	s.guard.lodUnconfirmed = 1;
	char line[600];
	guard::FormatGuards(s, LodRule::kCount, line, sizeof(line));
	Check(std::string(line) ==
	          "Route: occlusion cull guards - 12 verdicts dropped for another object at the address (3 of them hidden within the age: the new object "
	          "would have been left out), 2 results for a gone object dropped; far LOD (drawn and counted): 22.5 asked a pass, 7 left out, 5 drawn "
	          "against a hidden verdict, 1 left out and seen by their own pass's test",
	      "every count, the far LOD per pass");
	Stats none;
	guard::FormatGuards(none, LodRule::kNeverSkip, line, sizeof(line));
	Check(std::strstr(line, "far LOD (never left out): 0.0 asked a pass") != nullptr, "no pass: no division by zero");
	char small[20];
	Check(guard::FormatGuards(s, LodRule::kMaySkip, small, sizeof(small)) > static_cast<int>(sizeof(small)) && small[sizeof(small) - 1] == 0,
	      "a buffer too small is not overrun");
}

void TestCellWatch() {
	Section("The totals that only grow");
	Stats s;
	s.eyePasses = 3;
	s.kinds[0].draws = 100;
	s.kinds[1].draws = 20;
	s.kinds[0].skipped = 9;
	s.kinds[1].skippedFrustum = 1;
	s.skipsUnconfirmed = 4;
	s.guard.lodSkipped = 2;
	s.guard.lodKept = 6;
	s.guard.lodUnconfirmed = 1;
	s.guard.identityResets = 8;
	s.guard.identityResetsHidden = 5;
	guard::Totals t = guard::TotalsOf(s);
	Check(t.passes == 3 && t.draws == 120 && t.skipped == 10 && t.unconfirmed == 4 && t.lodSkipped == 2 && t.lodKept == 6 && t.lodUnconfirmed == 1 &&
	          t.identityResets == 8 && t.identityResetsHidden == 5,
	      "taken from the window's counts");
	guard::Totals sum = t;
	sum += t;
	const guard::Totals d = sum.Since(t);
	Check(sum.draws == 240 && sum.identityResetsHidden == 10 && d.passes == 3 && d.draws == 120 && d.skipped == 10 && d.unconfirmed == 4 && d.lodSkipped == 2 &&
	          d.lodKept == 6 && d.lodUnconfirmed == 1 && d.identityResets == 8 && d.identityResetsHidden == 5,
	      "added and taken apart again");

	Section("The window after a cell change");
	guard::CellWatch watch;
	guard::CellWatch::Window w;
	guard::Totals now;
	Check(!watch.Pass(0, 1, now, &w), "no cell yet (a load): nothing");
	Check(!watch.Pass(0xA0, 2, now, &w), "the first cell seen opens nothing");
	Check(!watch.Pass(0xA0, 3, now, &w), "the same cell: nothing");
	now.draws = 1000;
	now.skipped = 100;
	Check(!watch.Pass(0xB0, 10, now, &w), "a change opens a window");
	now.draws = 1500;
	Check(!watch.Pass(0, 11, now, &w), "a null cell inside it changes nothing");
	Check(!watch.Pass(0xC0, 20, now, &w), "a further change inside it is counted, the window runs on");
	now.draws = 9000;
	now.skipped = 400;
	now.unconfirmed = 30;
	now.lodSkipped = 6;
	now.passes = 32;
	Check(!watch.Pass(0xC0, 10 + guard::kWindowPasses - 1, now, &w), "one pass before its end: still open");
	Check(watch.Pass(0xC0, 10 + guard::kWindowPasses, now, &w), "at its end: closed once");
	Check(w.frame == 10 && w.changes == 2 && w.counts.draws == 8000 && w.counts.skipped == 300 && w.counts.unconfirmed == 30 && w.counts.lodSkipped == 6 &&
	          w.counts.passes == 32,
	      "with what happened since the change");
	Check(!watch.Pass(0xC0, 10 + guard::kWindowPasses + 1, now, &w), "not again");
	now.draws = 9100;
	Check(!watch.Pass(0xD0, 100, now, &w), "the next change opens the next window");
	now.draws = 9900;
	Check(watch.Pass(0xE0, 100 + guard::kWindowPasses, now, &w) && w.frame == 100 && w.changes == 1 && w.counts.draws == 800,
	      "a change in the pass that closes a window closes it first");
	Check(!watch.Pass(0xE0, 101 + guard::kWindowPasses, now, &w), "and has opened the next one");
	now.draws = 10000;
	Check(watch.Pass(0xE0, 100 + 2 * guard::kWindowPasses, now, &w) && w.frame == 100 + guard::kWindowPasses && w.changes == 1 && w.counts.draws == 100,
	      "which closes in its turn");

	Section("The window's line");
	guard::CellWatch::Window line;
	line.frame = 4711;
	line.changes = 2;
	line.counts.passes = 32;
	line.counts.draws = 64000;
	line.counts.skipped = 5000;
	line.counts.unconfirmed = 123;
	line.counts.lodSkipped = 40;
	line.counts.lodUnconfirmed = 8;
	line.counts.lodKept = 0;
	line.counts.identityResets = 900;
	line.counts.identityResetsHidden = 77;
	char text[600];
	guard::FormatCellWindow(line, text, sizeof(text));
	Check(std::string(text) ==
	          "Route: occlusion cull after a cell change - pass 4711, 2 change(s), the 32 passes after it: 64000 draws, 5000 left out, 123 of them seen by "
	          "their own pass's test; far LOD 40 left out (8 seen), 0 drawn against a hidden verdict; 900 verdicts dropped for another object at the "
	          "address (77 hidden within the age)",
	      "every number");
}

}  // namespace

int main() {
	TestIdentity();
	TestCollect();
	TestLod();
	TestFormatGuards();
	TestCellWatch();
	return Failures();
}
