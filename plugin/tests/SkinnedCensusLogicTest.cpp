// [Probe] SkinnedCensus (plugin/src/SkinnedCensusLogic.h): a frame's skinned
// draw calls by actor, part and shader class, the calls left under the two
// merges, a full table, and the line.

#include "Check.h"
#include "SkinnedCensusLogic.h"

#include <cstring>
#include <memory>
#include <string>

namespace sc = obng::plugin::skinned_census;

int main() {
	auto frame = std::make_unique<sc::Frame>();
	sc::Totals t;

	Section("An empty frame");
	Check(frame->Empty(), "nothing noted: empty");
	frame->End(&t);
	Check(t.frames == 1 && t.calls == 0 && t.actors == 0 && t.byClass == 0 && t.whole == 0, "counted as a frame with nothing in it");

	Section("One actor: three parts, two shader classes, one part drawn in three passes");
	// Root 0x1000: body (class A) in 3 passes, hands (class A) in 1, hair (class B) in 2.
	for (int i = 0; i < 3; ++i) {
		frame->Note(0x1000, 0x2000, 0xA0);
	}
	frame->Note(0x1000, 0x2100, 0xA0);
	frame->Note(0x1000, 0x2200, 0xB0);
	frame->Note(0x1000, 0x2200, 0xB0);
	Check(!frame->Empty(), "noted: not empty");
	t = sc::Totals{};
	frame->End(&t);
	Check(t.frames == 1 && t.calls == 6 && t.parts == 3 && t.actors == 1, "6 calls, 3 parts, 1 actor");
	Check(t.byClass == 5, "by class: class A keeps its part's 3 passes, class B its 2 - 5 calls");
	Check(t.whole == 3, "the whole actor as one: the most passes any part had - 3 calls");
	Check(t.mostCallsAnActor == 6 && t.mostPartsAnActor == 3, "the actor's 6 calls and 3 parts are the most seen");
	Check(frame->Empty(), "the frame is cleared at its end");

	Section("Two actors, and the totals over two frames");
	frame->Note(0x1000, 0x2000, 0xA0);
	frame->Note(0x3000, 0x4000, 0xA0);
	frame->Note(0x3000, 0x4100, 0xA0);
	frame->End(&t);
	Check(t.frames == 2 && t.calls == 9 && t.parts == 6 && t.actors == 3, "the second frame added: 3 calls, 3 parts, 2 actors");
	Check(t.byClass == 7 && t.whole == 5, "the same shader class on two actors is two groups: 2 more by class, 2 more whole");
	Check(t.mostCallsAnActor == 6, "the most calls an actor stays the first frame's");

	Section("The same geometry under two roots is two parts");
	t = sc::Totals{};
	frame->Note(0x1000, 0x2000, 0xA0);
	frame->Note(0x3000, 0x2000, 0xA0);
	frame->End(&t);
	Check(t.parts == 2 && t.actors == 2, "two parts, two actors");

	Section("A full table");
	t = sc::Totals{};
	for (std::uint32_t i = 0; i < sc::kActors + 600; ++i) {
		frame->Note(0x10000 + i * 0x100, 0x900000 + i * 0x100, 0xA0);
	}
	frame->End(&t);
	Check(t.dropped > 0 && t.calls + t.dropped == sc::kActors + 600, "more actors than the table holds: the rest is counted as not counted");
	Check(t.actors <= sc::kActors && t.actors > 0, "and no more actors than the table has room for");

	Section("The line");
	sc::Totals l;
	l.frames = 10;
	l.calls = 1500;
	l.parts = 1000;
	l.actors = 100;
	l.byClass = 600;
	l.whole = 200;
	l.mostCallsAnActor = 40;
	l.mostPartsAnActor = 17;
	char line[700];
	const int n = sc::Format(l, line, sizeof(line));
	const std::string s(line);
	Check(n > 0 && static_cast<std::size_t>(n) == std::strlen(line), "the length is the line's");
	Check(s.find("10 eye passes; a pass: 150.0 skinned draw calls of 10.0 actors in 100.0 parts (15.0 calls and 10.0 parts an actor, 1.50 calls a part; "
	             "the most an actor 40 calls, 17 parts)") != std::string::npos,
	      "the counts a pass and an actor");
	Check(s.find("drawn as one: 60.0 calls a pass (6.0 an actor), with the whole actor as one: 20.0 (2.0 an actor); 0 calls not counted") !=
	          std::string::npos,
	      "the two merges");
	sc::Format(sc::Totals{}, line, sizeof(line));
	Check(std::string(line).find("0 eye passes; a pass: 0.0 skinned draw calls") != std::string::npos, "nothing counted: zeros, no division by zero");
	struct {
		char tiny[8];
		char guard[4];
	} box = {{}, {'a', 'b', 'c', 0}};
	sc::Format(l, box.tiny, sizeof(box.tiny));
	Check(box.tiny[7] == '\0' && std::strcmp(box.guard, "abc") == 0, "a short buffer is not overrun");
	Check(sc::Format(l, box.tiny, 0) == 0, "no room: nothing written");
	return Failures();
}
