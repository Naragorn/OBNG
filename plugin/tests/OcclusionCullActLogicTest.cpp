// [Route] OcclusionCullAct (plugin/src/OcclusionCullActLogic.h): the culler's
// verdicts acted on, only tested, or acted on in every second summary
// window; the decision turned into a draw, and the window's account behind
// the summary line.

#include "Check.h"
#include "OcclusionCullActLogic.h"

#include <cstring>
#include <string>

namespace cull = obng::plugin::occlusion_cull;
namespace act = obng::plugin::occlusion_cull::act;
using cull::Camera;
using cull::Cameras;
using cull::Decided;
using cull::Decision;
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

void TestRule() {
	Section("The rule from the INI, and the windows it gives");
	Check(act::RuleFromIni(1) == act::Rule::kAct && act::RuleFromIni(0) == act::Rule::kTestOnly && act::RuleFromIni(2) == act::Rule::kAlternate,
	      "the INI's 1, 0 and 2");
	Check(act::RuleFromIni(7) == act::Rule::kAct && act::RuleFromIni(-1) == act::Rule::kAct, "any other value: acted on, as without the key");
	Check(act::ActsInWindow(act::Rule::kAct, true) && act::ActsInWindow(act::Rule::kAct, false), "acting: every window");
	Check(!act::ActsInWindow(act::Rule::kTestOnly, true) && !act::ActsInWindow(act::Rule::kTestOnly, false), "test only: no window");
	Check(act::ActsInWindow(act::Rule::kAlternate, false) && !act::ActsInWindow(act::Rule::kAlternate, true),
	      "alternating: the window after a test-only one acts, the one after an acting one does not");
	Check(std::string(act::RuleName(act::Rule::kAct)) == "acted on" && std::string(act::RuleName(act::Rule::kTestOnly)) == "not acted on (test only)" &&
	          std::string(act::RuleName(act::Rule::kAlternate)) == "acted on in every second summary window (a measurement)",
	      "the rules' names for the log");

	act::State state;
	Check(state.rule == act::Rule::kAct && state.acting, "without a setting: acting");
	state.Set(act::Rule::kTestOnly);
	Check(!state.acting, "test only: the first window does not act");
	state.heldBack = 5;
	state.NextWindow();
	Check(!state.acting && state.heldBack == 0, "nor the next; the count starts over with the window");
	state.Set(act::Rule::kAlternate);
	Check(state.acting, "alternating: the first window acts");
	state.NextWindow();
	Check(!state.acting, "the second does not");
	state.NextWindow();
	Check(state.acting, "the third does again");
	state.heldBack = 9;
	state.Set(act::Rule::kAct);
	state.NextWindow();
	Check(state.acting && state.heldBack == 0, "set to acting: every window from then, nothing held back on record");
}

void TestDecide() {
	Section("Test only: a verdict decided and not acted on");
	Settings settings;
	settings.maxAge = 2;
	const Cameras cameras = StillCameras();
	Stats stats;
	Table table;
	act::State state;
	state.Set(act::Rule::kTestOnly);
	act::Decide(state, table, 0x1000, 1, TestKind::kTest, settings, cameras, &stats);
	Check(state.heldBack == 0, "nothing decided hidden: nothing held back");
	table.Record(0x1000, 1, true);
	Decided d = act::Decide(state, table, 0x1000, 2, TestKind::kTest, settings, cameras, &stats);
	Check(d.decision == Decision::kDraw && d.newThisFrame && state.heldBack == 1, "hidden one frame ago, test only: drawn, counted as held back");
	Check(d.verdict->hiddenFrame == 1 && cull::TestDue(*d.verdict, d.decision, 2, 4), "its verdict stays hidden on record, and it is tested again this frame");
	Decided again = act::Decide(state, table, 0x1000, 2, TestKind::kTest, settings, cameras, &stats);
	Check(again.decision == Decision::kDraw && !again.newThisFrame && state.heldBack == 1, "its later passes of the frame: drawn, counted once");
	Check(act::Decide(state, table, 0x2000, 2, TestKind::kBothOutside, settings, cameras, &stats).decision == Decision::kDraw && state.heldBack == 2,
	      "outside both frustums, test only: drawn too");
	Check(act::Decide(state, table, 0x1000, 5, TestKind::kTest, settings, cameras, &stats).decision == Decision::kDraw && state.heldBack == 2,
	      "a verdict too old is drawn by itself: not held back");
	table.Record(0x1000, 5, true);
	state.Set(act::Rule::kAct);
	Check(act::Decide(state, table, 0x1000, 6, TestKind::kTest, settings, cameras, &stats).decision == Decision::kSkip && state.heldBack == 0,
	      "acting: the verdict the tests kept warm is acted on at once");
	Decided repeat = act::Decide(state, table, 0x1000, 6, TestKind::kTest, settings, cameras, &stats);
	Check(repeat.decision == Decision::kSkip && !repeat.newThisFrame, "and so are the geometry's later passes of that frame");
	// a table with no room answers a draw without a verdict: nothing to turn
	Table full;
	for (std::uint32_t i = 0; i < cull::kTableSlots + cull::kProbeLength; ++i) {
		full.Claim(0x10000 + i * 16, 7);
	}
	state.Set(act::Rule::kTestOnly);
	Stats fullStats;
	Decided none = act::Decide(state, full, 0x9990000, 7, TestKind::kBothOutside, settings, cameras, &fullStats);
	Check(none.decision == Decision::kDraw && (none.verdict != nullptr || fullStats.tableFull == 1) && (none.verdict != nullptr || state.heldBack == 0),
	      "no slot for the geometry: drawn as ever, nothing held back");
}

void TestSummary() {
	Section("The window's account behind the summary line");
	act::State state;
	state.Set(act::Rule::kAlternate);
	char line[200] = "Route: occlusion cull - ... passMs 1.000 cullMs 0.100";
	const int head = static_cast<int>(std::strlen(line));
	int at = act::AppendSummary(state, 10, line, sizeof(line), head);
	Check(std::string(line) == "Route: occlusion cull - ... passMs 1.000 cullMs 0.100 heldBack 0 acted 1; verdicts acted on" &&
	          at == static_cast<int>(std::strlen(line)),
	      "an acting window: the two totals right behind the line's own, then what it did");
	state.NextWindow();
	state.heldBack = 250;
	line[head] = '\0';
	at = act::AppendSummary(state, 10, line, sizeof(line), head);
	Check(std::string(line) == "Route: occlusion cull - ... passMs 1.000 cullMs 0.100 heldBack 250 acted 0; verdicts not acted on (test only): 25 a pass "
	                           "decided hidden or outside and drawn" &&
	          at == static_cast<int>(std::strlen(line)),
	      "a test-only window: what it held back, in all and a pass");
	line[head] = '\0';
	act::AppendSummary(state, 0, line, sizeof(line), head);
	Check(std::string(line).find("250 a pass") != std::string::npos, "no eye passes: nothing divides by zero");
	char small[8] = "abcdefg";
	Check(act::AppendSummary(state, 10, small, sizeof(small), 8) == 8 && std::string(small) == "abcdefg", "a line already full: left as it is");
	Check(act::AppendSummary(state, 10, small, sizeof(small), -1) == -1 && std::string(small) == "abcdefg", "a line that failed before: left as it is");
}

}  // namespace

int main() {
	TestRule();
	TestDecide();
	TestSummary();
	return Failures();
}
