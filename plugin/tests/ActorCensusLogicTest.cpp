// Every flow of the actor census (plugin/src/ActorCensusLogic.h).

#include "ActorCensusLogic.h"
#include "Check.h"

#include <string>
#include <vector>

using namespace obng::plugin::census;

namespace {

Seen Make(std::uint32_t actor, std::uint32_t base, std::uint8_t type, float distance, bool node, bool culled) {
	Seen s;
	s.actor = actor;
	s.refId = actor + 0x100;
	s.baseId = base;
	s.baseType = type;
	s.distance = distance;
	s.hasNode = node;
	s.culled = culled;
	return s;
}

void TestDistance() {
	Section("Distance and its classes");
	const float a[3] = {0, 0, 0};
	const float b[3] = {3, 4, 12};
	Check(Distance(a, b) == 13.0f && Distance(b, a) == 13.0f && Distance(a, a) == 0.0f, "the length of the difference");
	Check(DistanceClass(0) == 0 && DistanceClass(1000) == 0 && DistanceClass(1000.5f) == 1 && DistanceClass(2000) == 1 && DistanceClass(4000) == 2 &&
	          DistanceClass(8000) == 3 && DistanceClass(8001) == 4 && DistanceClass(1e9f) == 4,
	      "a bound belongs to the class below it");
}

void TestSample() {
	Section("A sample: asked for, one frame's actors, closed by the first second visit");
	Sample s;
	Check(s.Now() == State::Idle && !s.Visit(1) && s.Size() == 0, "idle: nothing is collected");
	s.Ask();
	Check(s.Now() == State::Wanted, "asked");
	Check(s.Visit(1), "the first actor opens the sample");
	s.Add(Make(1, 0x00000A, 0x23, 500, true, false));
	Check(s.Visit(2), "another actor");
	s.Add(Make(2, 0x37000B, 0x24, 5000, true, true));
	s.Ask();
	Check(s.Now() == State::Collecting, "asking again while collecting changes nothing");
	Check(!s.Visit(1) && s.Now() == State::Ready && s.Size() == 2, "an actor a second time closes it");
	Check(!s.Visit(3), "ready: nothing more is collected");
	s.Add(Make(3, 0, 0, 0, false, false));
	Check(s.Size() == 2, "nor added");
	s.Ask();
	Check(s.Now() == State::Ready, "asking while a sample waits changes nothing");
	s.Taken();
	Check(s.Now() == State::Idle, "taken: idle again");
	s.Taken();
	Check(s.Now() == State::Idle, "taking nothing does nothing");
	s.Ask();
	Check(s.Visit(9) && s.Size() == 0, "the next sample starts empty");

	Section("A sample: the table full closes it");
	Sample full;
	full.Ask();
	for (std::uint32_t i = 0; i < kActorsAtMost; ++i) {
		Check(full.Visit(1000 + i), "room");
		full.Add(Make(1000 + i, 0, 0, 0, true, false));
	}
	Check(!full.Visit(5000) && full.Now() == State::Ready && full.Size() == kActorsAtMost, "one more: closed, not added");
}

void TestCountAndFormat() {
	Section("The counts and their line");
	Sample s;
	s.Ask();
	const Seen all[] = {Make(1, 0x0000000A, 0x23, 500, true, false),   Make(2, 0x3700000B, 0x24, 5000, true, true), Make(3, 0x3700000C, 0x24, 9000, true, true),
	                    Make(4, 0x0000000D, 0x23, 1500, false, true), Make(5, 0x0000000E, 0x23, 3000, true, false)};
	for (const Seen& one : all) {
		s.Visit(one.actor);
		s.Add(one);
	}
	const Counts c = Count(s);
	Check(c.actors == 5 && c.byDistance[0] == 1 && c.byDistance[1] == 1 && c.byDistance[2] == 1 && c.byDistance[3] == 1 && c.byDistance[4] == 1, "one in each distance class");
	Check(c.culled == 2 && c.noNode == 1 && c.culledByDistance[3] == 1 && c.culledByDistance[4] == 1 && c.culledByDistance[1] == 0, "culled counts only with a node");
	Check(c.types == 2 && c.type[0] == 0x23 && c.typeCount[0] == 3 && c.type[1] == 0x24 && c.typeCount[1] == 2, "the base types in first-seen order");
	Check(c.plugins == 2 && c.plugin[0] == 0x00 && c.pluginCount[0] == 3 && c.plugin[1] == 0x37 && c.pluginCount[1] == 2, "the base ids' top bytes");
	char out[400];
	FormatCounts(c, out, sizeof(out));
	Check(std::string(out) ==
	          "Census: 5 actors updated in a frame - by distance to 1000 / 2000 / 4000 / 8000 / beyond: 1 1 1 1 1 (their nodes culled: 0 0 0 1 1); 2 nodes culled, "
	          "1 without a node; base types 23:3 24:2; base plugins 00:3 37:2",
	      (std::string("line: ") + out).c_str());
	Sample none;
	FormatCounts(Count(none), out, sizeof(out));
	Check(std::string(out).find("Census: 0 actors updated in a frame") == 0, "an empty sample");

	Section("More than eight types: the ninth is not listed");
	Sample many;
	many.Ask();
	for (std::uint32_t i = 0; i < 10; ++i) {
		many.Visit(i + 1);
		many.Add(Make(i + 1, i << 24, static_cast<std::uint8_t>(i), 0, true, false));
	}
	const Counts m = Count(many);
	Check(m.types == 8 && m.plugins == 8 && m.actors == 10, "eight kept of ten");

	Section("The actors' lines");
	std::size_t next = FormatActors(s, 0, out, sizeof(out));
	Check(next == 5 && std::string(out) == "Census actors: 00000101/0000000A/23 500 00000102/3700000B/24 5000c 00000103/3700000C/24 9000c 00000104/0000000D/23 1500n "
	                                       "00000105/0000000E/23 3000",
	      (std::string("all in one: ") + out).c_str());
	char small[70];
	next = FormatActors(s, 0, small, sizeof(small));
	Check(next == 2 && std::string(small) == "Census actors: 00000101/0000000A/23 500 00000102/3700000B/24 5000c", "as many as fit");
	next = FormatActors(s, next, small, sizeof(small));
	Check(next == 4, "the next line continues");
	Check(FormatActors(s, 5, small, sizeof(small)) == 5 && std::string(small) == "Census actors:", "nothing left");
}

void TestThunk() {
	Section("The note thunk's bytes");
	std::uint8_t code[32];
	const std::uint32_t n = BuildNoteThunk(code, sizeof(code), 0x10000000, 0x10001000, 0x20000000);
	const std::vector<std::uint8_t> want = {0x50, 0x51, 0x52, 0x51, 0xE8, 0xF7, 0x0F, 0x00, 0x00,        // call: 0x10001000 - 0x10000009
	                                        0x8D, 0x64, 0x24, 0x04, 0x5A, 0x59, 0x58, 0xE9, 0xEB, 0xFF, 0xFF, 0x0F};  // jmp: 0x20000000 - 0x10000015
	Check(n == kNoteThunkSize && n == want.size() && std::memcmp(code, want.data(), n) == 0, "save, push the actor, call, restore, jump on");
	Check(BuildNoteThunk(code, kNoteThunkSize - 1, 0, 0, 0) == 0, "too small a buffer: nothing");
}

}  // namespace

int main() {
	TestDistance();
	TestSample();
	TestCountAndFormat();
	TestThunk();
	return Failures();
}
