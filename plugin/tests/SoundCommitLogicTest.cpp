// [Route] SoundCommit (plugin/src/SoundCommitLogic.h): which of the sound
// update's two commits of deferred 3D settings is made under each rule, the
// counts, and the summary line.

#include "Check.h"
#include "SoundCommitLogic.h"

#include <cstring>
#include <string>

namespace sc = obng::plugin::sound_commit;

namespace {

void TestRule() {
	Section("The rule from the INI");
	Check(sc::RuleFromIni(0) == sc::Rule::kOff && sc::RuleFromIni(1) == sc::Rule::kTimed && sc::RuleFromIni(2) == sc::Rule::kOnce &&
	          sc::RuleFromIni(3) == sc::Rule::kEverySecond,
	      "0, 1, 2 and 3");
	Check(sc::RuleFromIni(4) == sc::Rule::kAlternate, "4: alternating");
	Check(sc::RuleFromIni(5) == sc::Rule::kOff && sc::RuleFromIni(-1) == sc::Rule::kOff, "any other value: off");
	Check(std::string(sc::RuleName(sc::Rule::kAlternate)) != sc::RuleName(sc::Rule::kOff), "the alternating rule has its name too");
	Check(std::string(sc::RuleName(sc::Rule::kOff)) != sc::RuleName(sc::Rule::kTimed) &&
	          std::string(sc::RuleName(sc::Rule::kOnce)) != sc::RuleName(sc::Rule::kEverySecond),
	      "every rule has its name");
}

void TestTimed() {
	Section("Off and timed: both commits, as the game");
	for (sc::Rule rule : {sc::Rule::kOff, sc::Rule::kTimed}) {
		sc::State s;
		s.Set(rule);
		bool all = true;
		for (int frame = 0; frame < 4; ++frame) {
			all = all && s.First() && s.Second();
		}
		Check(all && s.Total().commits == 8 && s.Total().skipped == 0 && s.Total().frames == 4, "two commits in every frame");
		Check(s.TheRule() == rule, "the rule kept");
	}
}

void TestOnce() {
	Section("Once: the first left out, the second made");
	sc::State s;
	s.Set(sc::Rule::kOnce);
	bool right = true;
	for (int frame = 0; frame < 4; ++frame) {
		right = right && !s.First() && s.Second();
	}
	Check(right, "in every frame");
	Check(s.Total().commits == 4 && s.Total().skipped == 4 && s.Total().frames == 4, "the counts");
}

void TestEverySecond() {
	Section("Every second frame");
	sc::State s;
	s.Set(sc::Rule::kEverySecond);
	Check(!s.First() && s.Second(), "the first frame commits at the second site");
	Check(!s.First() && !s.Second(), "the second frame commits nothing");
	Check(!s.First() && s.Second(), "the third commits again");
	Check(!s.First() && !s.Second(), "the fourth not");
	Check(s.Total().commits == 2 && s.Total().skipped == 6 && s.Total().frames == 4, "the counts");
	// A sound update that returns before the second site: only the first is asked.
	Check(!s.First() && !s.First() && s.Second(), "frames that never reach the second site do not move the turn");
	s.Set(sc::Rule::kEverySecond);
	Check(s.Total().frames == 0 && s.Second(), "set again: the counts gone, the first frame commits");
}

void TestAlternate() {
	Section("Alternating windows");
	sc::State s;
	s.Set(sc::Rule::kAlternate);
	Check(s.TheRule() == sc::Rule::kAlternate && s.Now() == sc::Rule::kTimed, "the first window: both commits, timed");
	Check(s.First() && s.Second() && s.First() && s.Second(), "two commits a frame in it");
	s.NextWindow();
	Check(s.Now() == sc::Rule::kEverySecond, "the second window: every second frame");
	Check(!s.First() && s.Second() && !s.First() && !s.Second(), "its first frame commits, its second not");
	s.NextWindow();
	Check(s.Now() == sc::Rule::kTimed && s.First() && s.Second(), "the third window: both again");
	s.NextWindow();
	Check(!s.First() && s.Second(), "the fourth window's first frame commits whatever the second window's last did");
	Check(s.Total().frames == 6 && s.Total().commits == 8 && s.Total().skipped == 4, "the counts run through the windows");
	sc::State fixed;
	fixed.Set(sc::Rule::kOnce);
	fixed.NextWindow();
	Check(fixed.Now() == sc::Rule::kOnce && !fixed.First() && fixed.Second(), "another rule stays what it is over a window's end");
}

void TestTimes() {
	Section("The commits' time, the window and the summary line");
	sc::State s;
	s.Set(sc::Rule::kOnce);
	s.First();
	s.Second();
	s.Took(3000);
	const sc::Counts first = s.Total();
	s.First();
	s.Second();
	s.Took(1000);
	Check(s.Total().ticks == 4000 && s.Total().longest == 3000, "the ticks summed, the longest kept");
	const sc::Counts w = sc::Window(s.Total(), first);
	Check(w.frames == 1 && w.commits == 1 && w.skipped == 1 && w.ticks == 1000 && w.longest == 3000, "a window is the difference; the longest is the run's");
	char line[400];
	const int n = sc::FormatSummary(w, sc::Rule::kOnce, 1000.0, line, sizeof(line));
	Check(n > 0 && static_cast<std::size_t>(n) == std::strlen(line), "the length is the line's");
	const std::string text(line);
	Check(text.find("one commit a frame") != std::string::npos && text.find("1 sound updates, 1 commits made, 1 left out") != std::string::npos, "the rule and the counts");
	Check(text.find("1.000 ms a sound update") != std::string::npos && text.find("1.000 ms each") != std::string::npos &&
	          text.find("longest since the start 3.000 ms") != std::string::npos,
	      "the times");
	sc::FormatSummary(sc::Counts{}, sc::Rule::kTimed, 0.0, line, sizeof(line));
	Check(std::string(line).find("0.000 ms a sound update") != std::string::npos, "no frame, no commit and no clock do not divide by zero");
	struct {
		char tiny[8];
		char guard[4];
	} box = {{}, {'a', 'b', 'c', 0}};
	sc::FormatSummary(w, sc::Rule::kOnce, 1000.0, box.tiny, sizeof(box.tiny));
	Check(box.tiny[7] == '\0' && std::strcmp(box.guard, "abc") == 0, "a short buffer is not overrun");
}

}  // namespace

int main() {
	TestRule();
	TestTimed();
	TestOnce();
	TestEverySecond();
	TestAlternate();
	TestTimes();
	return Failures();
}
