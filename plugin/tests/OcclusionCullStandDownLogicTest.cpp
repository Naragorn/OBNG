// [Route] OcclusionCullStandDown (plugin/src/OcclusionCullStandDownLogic.h):
// the culler rests while its skips save less than it costs; the rule from
// the INI, a window's saving, the windows and rests from pass to pass, and
// the summary line.

#include "Check.h"
#include "OcclusionCullStandDownLogic.h"

#include <cstring>
#include <string>

namespace sd = obng::plugin::occlusion_cull::stand_down;

namespace {

// A pass's work added to the totals: `draws` asked, `skipped` of them left
// out, `nodeDraws` more below nodes left out; the pass 1000 ticks a draw
// drawn, the culler `cull` ticks.
sd::Totals Add(sd::Totals t, std::uint64_t draws, std::uint64_t skipped, std::uint64_t nodeDraws, std::uint64_t cull) {
	t.draws += draws;
	t.skippedDraws += skipped;
	t.nodeDraws += nodeDraws;
	t.cullTicks += cull;
	t.passTicks += (draws - skipped) * 1000 + cull;
	return t;
}

// `passes` passes of the same work; answers whether the culler rests after the last.
bool Run(sd::State& s, const sd::Settings& set, sd::Rule rule, sd::Totals& t, std::uint32_t passes, std::uint64_t skipped, std::uint64_t cull) {
	bool rests = s.resting;
	for (std::uint32_t i = 0; i < passes; ++i) {
		if (!s.resting) {
			t = Add(t, 1000, skipped, 0, cull);
		}
		rests = sd::Pass(s, set, rule, t);
	}
	return rests;
}

void TestRule() {
	Section("The rule from the INI");
	Check(sd::RuleFromIni(0) == sd::Rule::kOff && sd::RuleFromIni(1) == sd::Rule::kOn && sd::RuleFromIni(2) == sd::Rule::kCount, "the INI's 0, 1 and 2");
	Check(sd::RuleFromIni(5) == sd::Rule::kOff && sd::RuleFromIni(-1) == sd::Rule::kOff, "any other value: off");
	Check(std::string(sd::RuleName(sd::Rule::kOff)) != sd::RuleName(sd::Rule::kOn) && std::string(sd::RuleName(sd::Rule::kOn)) != sd::RuleName(sd::Rule::kCount),
	      "every rule has its name");
}

void TestSaving() {
	Section("What a window's skips saved");
	sd::Totals w = Add({}, 1000, 100, 0, 50000);
	Check(sd::Saving(w) == 100.0 * 1000.0, "the draws left out at the mean cost of a draw that was drawn");
	w = Add({}, 1000, 100, 50, 50000);
	Check(sd::Saving(w) == 150.0 * 1000.0, "the draws below a node left out count as well");
	Check(sd::Saving(Add({}, 1000, 0, 0, 50000)) == 0.0, "nothing left out: nothing saved");
	Check(sd::Saving({}) == 0.0, "no draw at all: nothing saved");
	sd::Totals all;
	all.draws = 10;
	all.skippedDraws = 10;
	all.cullTicks = 500;
	all.passTicks = 500;
	Check(sd::Saving(all) > 500.0, "every draw left out: no mean to go by, taken as paying");
	sd::Totals odd = Add({}, 1000, 100, 0, 50000);
	odd.passTicks = 40000;
	Check(sd::Saving(odd) == 0.0, "a pass shorter than the culler's own time (the collect lies outside it): nothing claimed");
	const sd::Totals a = Add({}, 1000, 100, 5, 700);
	const sd::Totals b = Add(a, 500, 20, 1, 300);
	const sd::Totals d = b.Since(a);
	Check(d.draws == 500 && d.skippedDraws == 20 && d.nodeDraws == 1 && d.cullTicks == 300 && d.passTicks == 480300, "the difference of two totals");
}

void TestOff() {
	Section("Off: the culler never rests");
	sd::State s;
	sd::Settings set;
	sd::Totals t;
	Check(!Run(s, set, sd::Rule::kOff, t, 500, 0, 100000), "500 passes that save nothing");
	Check(s.rests == 0 && s.windows == 0, "and nothing is judged");
}

void TestWindows() {
	Section("The windows of a working culler");
	sd::Settings set;
	sd::State s;
	sd::Totals t;
	// 100 of 1000 left out: 100 000 ticks saved a pass against 50 000.
	Check(!Run(s, set, sd::Rule::kOn, t, set.windowPasses * 4, 100, 50000), "skips that save twice the cost: no rest");
	Check(s.windows == 4 && s.windowsPaid == 4 && s.rests == 0, "four windows judged, all paid");
	// 10 of 1000 left out: 10 000 saved against 50 000.
	Check(!Run(s, set, sd::Rule::kOn, t, set.windowPasses, 10, 50000), "one window that does not pay: not yet");
	Check(s.windows == 5 && s.windowsPaid == 4 && s.failed == 1, "counted as failed");
	Check(!Run(s, set, sd::Rule::kOn, t, set.windowPasses, 100, 50000) && s.failed == 0, "a paying window between: the count begins again");
	Check(!Run(s, set, sd::Rule::kOn, t, set.windowPasses, 10, 50000), "failed once");
	Check(!Run(s, set, sd::Rule::kOn, t, set.windowPasses - 1, 10, 50000), "the second failing window, a pass before its end");
	Check(Run(s, set, sd::Rule::kOn, t, 1, 10, 50000), "two failed windows in a row: the culler rests");
	Check(s.resting && s.rests == 1 && s.rest == set.restPasses, "the first rest's length");
	Check(s.lastSaving == 10.0 * 1000.0 * set.windowPasses && s.lastCost == 50000.0 * set.windowPasses, "the last window's account kept");
}

void TestRestAndProbe() {
	Section("The rest, and the probe after it");
	sd::Settings set;
	sd::State s;
	sd::Totals t;
	Run(s, set, sd::Rule::kOn, t, set.windowPasses * 2, 10, 50000);
	Check(s.resting, "resting");
	Check(Run(s, set, sd::Rule::kOn, t, set.restPasses - 1, 0, 0), "still resting a pass before the rest's end");
	Check(!Run(s, set, sd::Rule::kOn, t, 1, 0, 0) && s.probing, "the rest over: the culler works again, as a probe");
	Check(s.restedPasses == set.restPasses, "the rested passes counted");
	const std::uint64_t windowsBefore = s.windows;
	// The warm-up: its passes are not judged, whatever they did.
	Check(!Run(s, set, sd::Rule::kOn, t, set.warmPasses, 0, 500000) && s.windows == windowsBefore, "the warm-up passes are not judged");
	// 60 of 1000 left out: 60 000 saved against 50 000 - pays, but not 1.25 times.
	Check(Run(s, set, sd::Rule::kOn, t, set.windowPasses, 60, 50000), "a probe that pays less than the resume factor: a rest again, after one window");
	Check(s.rests == 2 && s.rest == set.restPasses * 2 && s.resumes == 0, "the next rest twice as long");
	Run(s, set, sd::Rule::kOn, t, s.rest + set.warmPasses, 0, 0);
	Check(!s.resting && s.probing, "probing again");
	Check(!Run(s, set, sd::Rule::kOn, t, set.windowPasses, 100, 50000) && !s.probing && s.resumes == 1, "a probe that saves twice its cost: the culler works on");
	Run(s, set, sd::Rule::kOn, t, set.windowPasses * 2, 10, 50000);
	Check(s.resting && s.rest == set.restPasses, "after a resume the first rest is the short one again");
}

void TestRestCap() {
	Section("The rest's cap");
	sd::Settings set;
	sd::State s;
	sd::Totals t;
	Run(s, set, sd::Rule::kOn, t, set.windowPasses * 2, 10, 50000);
	for (int i = 0; i < 8; ++i) {
		Run(s, set, sd::Rule::kOn, t, s.rest + set.warmPasses + set.windowPasses, 10, 50000);
	}
	Check(s.resting && s.rest == set.maxRestPasses, "probes that keep failing: the rest grows to the cap and no further");
}

void TestCount() {
	Section("Counting only ([Route] OcclusionCullStandDown=2)");
	sd::Settings set;
	sd::State s;
	sd::Totals t;
	Check(!Run(s, set, sd::Rule::kCount, t, set.windowPasses * 6, 10, 50000), "windows that do not pay: the culler works on");
	Check(!s.resting && s.rests == 3 && s.restedPasses == 0, "the rests it would have taken are counted, one per two failed windows");
}

void TestSummary() {
	Section("The summary line");
	sd::State s;
	s.workedPasses = 300;
	s.restedPasses = 512;
	s.rests = 2;
	s.resumes = 1;
	s.windows = 9;
	s.windowsPaid = 7;
	s.lastSaving = 3200.0;
	s.lastCost = 6400.0;
	char line[500];
	sd::Settings set;
	const int n = sd::FormatSummary(s, set, sd::Rule::kOn, 100.0, line, sizeof(line));
	const std::string text(line);
	Check(n > 0 && static_cast<std::size_t>(n) == std::strlen(line), "the length is the line's");
	Check(text.find("worked in 300 passes, rested in 512") != std::string::npos, "the passes");
	Check(text.find("2 rests begun, 1 ended by a probe that paid") != std::string::npos, "the rests");
	Check(text.find("7 of 9 windows paid") != std::string::npos, "the windows");
	Check(text.find("saved 1.000 ms a pass") != std::string::npos && text.find("cost 2.000 ms a pass") != std::string::npos, "the last window a pass, in ms");
	sd::FormatSummary(s, set, sd::Rule::kOn, 0.0, line, sizeof(line));
	Check(std::string(line).find("ticks") != std::string::npos, "without a calibration: ticks");
	struct {
		char tiny[8];
		char guard[4];
	} box = {{}, {'a', 'b', 'c', 0}};
	sd::FormatSummary(s, set, sd::Rule::kOn, 100.0, box.tiny, sizeof(box.tiny));
	Check(box.tiny[7] == '\0' && std::strcmp(box.guard, "abc") == 0, "a short buffer is not overrun");
}

}  // namespace

int main() {
	TestRule();
	TestSaving();
	TestOff();
	TestWindows();
	TestRestAndProbe();
	TestRestCap();
	TestCount();
	TestSummary();
	return Failures();
}
