// [Route] OcclusionCullQuick (plugin/src/OcclusionCullQuickLogic.h): a draw
// with a visible verdict for this very object and no test due is decided
// without its rectangles; the rule and its windows, when it applies, that the
// quick decision is the full one's, the check's counts and the summary line.

#include "Check.h"
#include "OcclusionCullQuickLogic.h"

#include <cstring>
#include <memory>
#include <string>

namespace cull = obng::plugin::occlusion_cull;
namespace quick = obng::plugin::occlusion_cull::quick;
using cull::Decided;
using cull::Decision;
using cull::LodRule;
using cull::Stats;
using cull::Table;
using cull::TestKind;
using cull::Verdict;

namespace {

constexpr std::uint32_t kEvery = 4;
constexpr std::uint32_t kIdentity = 77;

// A geometry whose turn for a test is the frame 8 (phase 0), tested at
// `tested`, seen in the frame before `frame`.
Verdict Visible(std::uint32_t geometry, std::uint32_t tested) {
	Verdict v;
	v.geometry = geometry;
	v.identity = kIdentity;
	v.testedFrame = tested;
	v.seenFrame = tested;
	return v;
}

void TestRule() {
	Section("The rule from the INI, and the windows it gives");
	Check(quick::RuleFromIni(0) == quick::Rule::kOff && quick::RuleFromIni(1) == quick::Rule::kOn && quick::RuleFromIni(2) == quick::Rule::kCheck &&
	          quick::RuleFromIni(3) == quick::Rule::kAlternate,
	      "the INI's 0, 1, 2 and 3");
	Check(quick::RuleFromIni(9) == quick::Rule::kOn && quick::RuleFromIni(-1) == quick::Rule::kOn, "any other value: on, as without the key");
	quick::State state;
	state.Set(quick::Rule::kOn);
	Check(state.on && !state.Checks(), "on: quick, no check");
	state.NextWindow();
	Check(state.on, "on: the next window too");
	state.Set(quick::Rule::kOff);
	Check(!state.on && !state.Checks(), "off: neither");
	state.NextWindow();
	Check(!state.on, "off: the next window too");
	state.Set(quick::Rule::kCheck);
	Check(!state.on && state.Checks(), "check: the full decision is taken and compared");
	state.NextWindow();
	Check(!state.on && state.Checks(), "check: the next window too");
	state.Set(quick::Rule::kAlternate);
	Check(state.on && !state.Checks(), "alternating: the first window quick");
	state.NextWindow();
	Check(!state.on, "alternating: the second window full");
	state.NextWindow();
	Check(state.on, "alternating: the third quick again");
	Check(std::string(quick::RuleName(quick::Rule::kOff)) != quick::RuleName(quick::Rule::kOn) &&
	          std::string(quick::RuleName(quick::Rule::kCheck)) != quick::RuleName(quick::Rule::kAlternate),
	      "every rule has its name");
}

void TestApplies() {
	Section("When a draw is decided without its rectangles");
	// Geometry 0x100: phase (0x100 >> 4) % 4 = 0, its turn in the frames 4, 8, 12.
	Verdict v = Visible(0x100, 9);
	Check(quick::Applies(&v, 10, kIdentity, kEvery, LodRule::kNeverSkip), "visible, tested a frame ago, not its turn: quick");
	Check(!quick::Applies(nullptr, 10, kIdentity, kEvery, LodRule::kNeverSkip), "no verdict on record: the full decision");
	Check(!quick::Applies(&v, 10, 0, kEvery, LodRule::kNeverSkip), "the identity not said: the full decision");
	Check(!quick::Applies(&v, 10, kIdentity + 1, kEvery, LodRule::kNeverSkip), "another object at the address: the full decision (it drops the verdict)");
	Check(!quick::Applies(&v, 12, kIdentity, kEvery, LodRule::kNeverSkip), "its turn for a test: the full decision");
	Check(!quick::Applies(&v, 13, kIdentity, kEvery, LodRule::kNeverSkip), "four frames since its last test: the full decision");
	Check(!quick::Applies(&v, 10, kIdentity, 1, LodRule::kNeverSkip), "every draw tested in every frame: never quick");
	Verdict hidden = v;
	hidden.hiddenFrame = 8;
	Check(!quick::Applies(&hidden, 10, kIdentity, kEvery, LodRule::kNeverSkip), "a hidden verdict on record: the full decision");
	Verdict never = Visible(0x100, 0);
	Check(!quick::Applies(&never, 10, kIdentity, kEvery, LodRule::kNeverSkip), "never tested: the full decision (its first test is due)");
	Verdict lod = Visible(0x100, 0);
	lod.lod = true;
	Check(quick::Applies(&lod, 10, kIdentity, kEvery, LodRule::kNeverSkip), "a far LOD piece that gets no test: quick in every frame");
	Check(quick::Applies(&lod, 10, kIdentity, 1, LodRule::kNeverSkip), "and with every draw tested in every frame too");
	Check(!quick::Applies(&lod, 10, kIdentity, kEvery, LodRule::kMaySkip), "a far LOD piece that is tested and due: the full decision");
	Check(!quick::Applies(&lod, 10, kIdentity, kEvery, LodRule::kCount), "counted LOD is tested too");
}

void TestDecide() {
	Section("The quick decision");
	Stats stats;
	Verdict v = Visible(0x100, 9);
	v.decision = Decision::kSkip;  // a stale decision of an earlier frame
	const Decided d = quick::Decide(v, 10, &stats);
	Check(d.decision == Decision::kDraw && d.newThisFrame && d.verdict == &v, "drawn, new this frame, the verdict handed on");
	Check(v.seenFrame == 10 && v.decision == Decision::kDraw && v.testedFrame == 9 && v.hiddenFrame == 0, "the verdict: seen now, drawn, its test's frame kept");
	Check(stats.guard.lodAsked == 0, "no far LOD counted");
	Verdict lod = Visible(0x100, 0);
	lod.lod = true;
	quick::Decide(lod, 10, &stats);
	Check(stats.guard.lodAsked == 1, "a far LOD piece counted as asked");
}

// The table after a decision, as far as a later decision reads it.
bool SameVerdict(const Verdict& a, const Verdict& b) {
	return a.geometry == b.geometry && a.seenFrame == b.seenFrame && a.hiddenFrame == b.hiddenFrame && a.testedFrame == b.testedFrame &&
	       a.identity == b.identity && a.lod == b.lod && a.decision == b.decision;
}

void TestSameAsFull() {
	Section("Where it applies, the quick decision is the full one's");
	cull::Settings settings;
	cull::Cameras cameras;
	cull::Camera still;
	still.direction[1] = 1.0f;
	for (std::uint32_t f = 0; f < cull::kCameraRing; ++f) {
		cameras.Set(f, still);
	}
	auto full = std::make_unique<Table>();
	std::uint32_t applied = 0;
	std::uint32_t same = 0;
	std::uint32_t outside = 0;
	std::uint32_t outsideSkipped = 0;
	std::uint32_t noTestDue = 0;
	for (std::uint32_t geometry = 0x100; geometry < 0x100 + 16 * 8; geometry += 16) {
		for (std::uint32_t tested = 0; tested < 12; ++tested) {  // seen last in a frame before this one, as the caller has it
			for (int lodCase = 0; lodCase < 2; ++lodCase) {
				for (LodRule rule : {LodRule::kNeverSkip, LodRule::kMaySkip, LodRule::kCount}) {
					for (std::uint32_t every : {1u, 2u, 4u}) {
						Verdict start = Visible(geometry, tested);
						start.lod = lodCase == 1;
						if (!quick::Applies(&start, 12, kIdentity, every, rule)) {
							continue;
						}
						++applied;
						settings.lodRule = rule;
						for (TestKind kind : {TestKind::kTest, TestKind::kNone, TestKind::kBothOutside}) {
							*full = Table{};
							Stats fullStats;
							*full->Claim(geometry, 12) = start;
							const Decided f = cull::Decide(*full, geometry, 12, kind, settings, cameras, &fullStats, kIdentity);
							Verdict q = start;
							Stats quickStats;
							const Decided d = quick::Decide(q, 12, &quickStats);
							const bool due = cull::TestDue(*f.verdict, f.decision, 12, every) && (!f.verdict->lod || cull::guard::LodTested(rule));
							if (kind == TestKind::kBothOutside) {
								++outside;
								outsideSkipped += f.decision == Decision::kSkipFrustum && d.decision == Decision::kDraw ? 1u : 0u;
								continue;
							}
							noTestDue += due ? 0u : 1u;
							same += f.decision == d.decision && f.newThisFrame == d.newThisFrame && SameVerdict(*f.verdict, q) &&
							                fullStats.guard.lodAsked == quickStats.guard.lodAsked && fullStats.guard.identityResets == 0
							            ? 1u
							            : 0u;
						}
					}
				}
			}
		}
	}
	Check(applied > 100, "the sweep reaches cases where it applies");
	Check(same == applied * 2, "testable and untestable bounds: the same decision, the same verdict, the same counts");
	Check(noTestDue == applied * 2, "and no test is due after the full decision either");
	Check(outside == applied && outsideSkipped == applied,
	      "a bound outside both frustums is the one difference: the full decision leaves it out, the quick one draws it");
}

void TestCheck() {
	Section("The check's counts ([Route] OcclusionCullQuick=2)");
	quick::Counts counts;
	Decided full;
	full.decision = Decision::kDraw;
	quick::Compare(&counts, full, TestKind::kTest);
	quick::Compare(&counts, full, TestKind::kNone);
	Check(counts.checked == 2 && counts.same == 2 && counts.outside == 0 && counts.other == 0, "drawn by the full decision, testable or not: the same");
	full.decision = Decision::kSkipFrustum;
	quick::Compare(&counts, full, TestKind::kBothOutside);
	Check(counts.checked == 3 && counts.outside == 1 && counts.other == 0, "left out as outside both frustums: counted as that");
	full.decision = Decision::kSkip;
	quick::Compare(&counts, full, TestKind::kTest);
	Check(counts.checked == 4 && counts.other == 1 && counts.same == 2, "left out on a hidden verdict: a mismatch");
}

void TestSummary() {
	Section("The summary line");
	quick::Counts counts;
	counts.quick = 3000;
	counts.full = 1000;
	char line[400];
	quick::State state;
	state.Set(quick::Rule::kOn);
	int n = quick::FormatSummary(counts, state, 10, line, sizeof(line));
	Check(n > 0 && static_cast<std::size_t>(n) == std::strlen(line), "the length is the line's");
	Check(std::string(line).find("300.0 a pass without their rectangles") != std::string::npos && std::string(line).find("100.0 with") != std::string::npos,
	      "the counts a pass");
	Check(std::string(line).find("on in this window") != std::string::npos, "the window's state");
	state.Set(quick::Rule::kCheck);
	counts.checked = 50;
	counts.same = 48;
	counts.outside = 2;
	quick::FormatSummary(counts, state, 10, line, sizeof(line));
	Check(std::string(line).find("check: 50 quick decisions compared, 48 the same, 2 outside both frustums (drawn where the full decision leaves out), 0 "
	                             "MISMATCH") != std::string::npos,
	      "the check's account");
	state.Set(quick::Rule::kOff);
	quick::FormatSummary(counts, state, 0, line, sizeof(line));
	Check(std::string(line).find("off in this window") != std::string::npos, "off, and no pass does not divide by zero");
	struct {
		char tiny[8];
		char guard[4];
	} box = {{}, {'a', 'b', 'c', 0}};
	quick::FormatSummary(counts, state, 10, box.tiny, sizeof(box.tiny));
	Check(box.tiny[7] == '\0' && std::strcmp(box.guard, "abc") == 0, "a short buffer is not overrun");
}

}  // namespace

int main() {
	TestRule();
	TestApplies();
	TestDecide();
	TestSameAsFull();
	TestCheck();
	TestSummary();
	return Failures();
}
