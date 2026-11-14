// [Timed] WritesOf (plugin/src/TimedWritesLogic.h): which call of a watched
// timed function is probed, when its probe ends, when it is abandoned, and
// the INI's list of names.

#include "Check.h"
#include "TimedWritesLogic.h"

#include <cstring>

namespace tw = obng::plugin::timed_writes;

namespace {

void TestWhichCall() {
	Section("Which call is probed");
	tw::Probe p;
	const std::uint32_t watched[] = {3, 5};
	p.Set(watched, 2, 2);
	Check(p.Count() == 2 && p.IndexOf(0) == 3 && p.IndexOf(1) == 5, "the watched functions");
	Check(!p.Entered(4, 1, true) && !p.Open(), "a function not watched: no probe");
	Check(p.Exited(0) == tw::kNone, "and its return ends none");
	Check(p.Entered(3, 1, true) && p.Open(), "the first call of a watched one: probed");
	Check(p.Exited(0) == 0 && !p.Open(), "its return ends the probe of watch 0");
	Check(!p.Entered(3, 1, true), "the second call: not probed (every second)");
	Check(p.Exited(0) == tw::kNone, "its return ends none");
	Check(p.Entered(3, 1, true), "the third: probed again");
	Check(p.Exited(0) == 0, "and ended");
	Check(p.Entered(5, 2, true) && p.Exited(1) == 1, "the other watched function has its own count");
	Check(p.Calls(0) == 3 && p.Probes(0) == 2 && p.Calls(1) == 1 && p.Probes(1) == 1 && p.Abandoned() == 0, "the counts");
	tw::Probe each;
	each.Set(watched, 2, 0);
	Check(each.Entered(3, 1, true) && each.Exited(0) == 0 && each.Entered(3, 1, true), "every 0 counts as every call");
	Check(!p.Entered(3, 1, false) && p.Calls(0) == 3, "a call without a frame of its own is not counted: it never comes to the return");
}

void TestInside() {
	Section("Calls inside a probed call");
	tw::Probe p;
	const std::uint32_t watched[] = {3, 5};
	p.Set(watched, 2, 1);
	Check(p.Entered(3, 2, true), "probed at depth 2");
	Check(!p.Entered(5, 3, true) && p.Calls(1) == 0, "a watched call inside it: not probed, not counted");
	Check(p.Exited(2) == tw::kNone && p.Open(), "the inner return leaves the probe open");
	Check(!p.Entered(9, 3, true) && p.Exited(2) == tw::kNone, "an unwatched one inside too");
	Check(!p.Entered(9, 2, false) && p.Open(), "the probed call ending in a jump to a timed function: that one returns for it");
	Check(p.Exited(1) == 0, "the probed call's return ends it");
	Check(p.Abandoned() == 0, "nothing abandoned");
}

void TestAbandoned() {
	Section("A probed call whose frame went without returning");
	tw::Probe p;
	const std::uint32_t watched[] = {3};
	p.Set(watched, 1, 1);
	Check(p.Entered(3, 3, true), "probed at depth 3");
	Check(p.Exited(1) == tw::kNone && !p.Open() && p.Abandoned() == 1, "an outer call returned over it: abandoned");
	Check(p.Entered(3, 3, true), "probed again at depth 3");
	Check(p.Entered(3, 3, true) && p.Abandoned() == 2, "a new frame at its depth: the old one was dropped, this call probed");
	Check(!p.Entered(9, 2, true) && !p.Open() && p.Abandoned() == 3, "a new frame below its depth: abandoned");
	Check(p.Entered(3, 3, true) && !p.Entered(9, 2, false) && p.Abandoned() == 4, "a jump into a timed function below its depth: abandoned too");
	Check(p.Probes(0) == 0, "none of them counted as probed");
}

void TestSet() {
	Section("Setting the watch");
	tw::Probe p;
	const std::uint32_t many[] = {1, 2, 3, 4, 5, 6, 7, 8};
	p.Set(many, 8, 1);
	Check(p.Count() == tw::kWatchedAtMost, "more than the room: cut off");
	Check(!p.Entered(8, 1, true), "the ones cut off are not watched");
	Check(p.Entered(1, 1, true), "the first is");
	p.Set(many, 1, 1);
	Check(!p.Open() && p.Calls(0) == 0 && p.Count() == 1, "set again: all counts and the open probe gone");
	tw::Probe none;
	Check(!none.Entered(0, 1, true) && none.Exited(0) == tw::kNone, "nothing set: nothing watched");
}

void TestNames() {
	Section("The INI's list of names");
	char names[3][8];
	Check(tw::SplitNames("a, bc ,def", names[0], 8, 3) == 3 && std::strcmp(names[0], "a") == 0 && std::strcmp(names[1], "bc") == 0 &&
	          std::strcmp(names[2], "def") == 0,
	      "three names, the blanks around them dropped");
	Check(tw::SplitNames("", names[0], 8, 3) == 0 && tw::SplitNames(" , ,", names[0], 8, 3) == 0, "an empty list, and empty names");
	Check(tw::SplitNames("a,b,c,d", names[0], 8, 3) == 3, "more names than room: the first ones");
	Check(tw::SplitNames("actors:Vitals", names[0], 8, 3) == 1 && std::strcmp(names[0], "actors:") == 0, "a long name is cut to the room");
	Check(tw::SplitNames("a b,c", names[0], 8, 3) == 2 && std::strcmp(names[0], "a b") == 0, "a blank inside a name stays");
}

}  // namespace

int main() {
	TestWhichCall();
	TestInside();
	TestAbandoned();
	TestSet();
	TestNames();
	return Failures();
}
