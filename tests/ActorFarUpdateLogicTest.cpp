// Every flow of the far actors' whole update rate (src/engine/scene/ActorFarUpdateLogic.h).

#include "Check.h"
#include "engine/scene/ActorFarUpdateLogic.h"

#include <limits>
#include <memory>

using namespace obng::engine::scene::actor_far_update;

namespace {

bool Near(double a, double b) { return a - b < 1e-6 && b - a < 1e-6; }

}  // namespace

int main() {
	Section("Clock: a frame and its seconds a tick; a frame that is no frame counts no time");
	Clock c;
	c.Tick(0.011f);
	c.Tick(0.012f);
	Check(c.frame == 2 && Near(c.seconds, 0.023), "two frames summed");
	c.Tick(0.0f);
	c.Tick(-1.0f);
	c.Tick(5.0f);
	c.Tick(std::numeric_limits<float>::quiet_NaN());
	Check(c.frame == 6 && Near(c.seconds, 0.023), "zero, negative, longer than a second, not a number: the frame counted, no time");
	c.Tick(1.0f);
	Check(Near(c.seconds, 1.023), "a whole second still counts");

	Section("Decide: the first call runs with the frame's own time");
	Clock clock;
	clock.Tick(0.01f);
	Entry e;
	e.actor = 0x1040;  // phase 1 of 4
	Step s = Decide(e, true, 4, clock, 0.01f);
	Check(s.runs && !s.summed && e.calls == 1 && e.lastFrame == 1, "new: runs, nothing summed, the clock noted");

	Section("Decide: a far actor runs one call in n and gets the time of the frames between");
	// calls 1, 2, 3 with phase 1: (call + 1) % 4 == 0 at call 3
	bool ran[3];
	Step last;
	for (int i = 0; i < 3; ++i) {
		clock.Tick(0.01f);
		last = Decide(e, false, 4, clock, 0.01f);
		ran[i] = last.runs;
	}
	Check(!ran[0] && !ran[1] && ran[2], "the two calls between left out, the third runs");
	Check(last.summed && Near(last.seconds, 0.03), "it stands for three frames: 0.03 s");
	Check(e.lastFrame == 4 && e.calls == 4, "the clock noted at the run only, every call counted");

	Section("Decide: a near actor runs every call and its time is left alone");
	clock.Tick(0.01f);
	s = Decide(e, false, 1, clock, 0.01f);
	Check(s.runs && !s.summed, "the frame after a run: every 1, nothing summed");

	Section("Decide: a far actor that comes near gets the left-out time once");
	Entry f;
	f.actor = 0x1000;  // phase 0 of 4: runs at calls 4, 8, ..
	Clock k;
	k.Tick(0.01f);
	Decide(f, true, 4, k, 0.01f);
	k.Tick(0.01f);
	Check(!Decide(f, false, 4, k, 0.01f).runs, "far: left out");
	k.Tick(0.01f);
	s = Decide(f, false, 1, k, 0.01f);
	Check(s.runs && s.summed && Near(s.seconds, 0.02), "near now: runs with the two frames' time");
	k.Tick(0.01f);
	s = Decide(f, false, 1, k, 0.01f);
	Check(s.runs && !s.summed, "and from then on with the frame's own");

	Section("Decide: the summed time is capped, and a clock that stood sums nothing");
	Entry g;
	g.actor = 0x1000;
	Clock m;
	m.Tick(0.01f);
	Decide(g, true, 1, m, 0.01f);
	for (int i = 0; i < 100; ++i) {
		m.Tick(0.02f);
	}
	s = Decide(g, false, 1, m, 0.02f);
	Check(s.runs && s.summed && s.seconds == kMaxSeconds, "two seconds away: half a second");
	m.Tick(0.0f);
	m.Tick(0.0f);
	s = Decide(g, false, 1, m, 0.02f);
	Check(s.runs && !s.summed, "frames without time between: nothing summed");
	m.Tick(0.02f);
	m.Tick(0.02f);
	s = Decide(g, false, 0, m, 0.02f);
	Check(s.runs && s.summed, "every 0 is every call");

	Section("Table: an actor keeps its entry; a new one is said to be new");
	auto table = std::make_unique<Table>();
	bool isNew = false;
	Entry& a = table->Find(0x2000, 1, isNew);
	Check(isNew && a.actor == 0x2000 && a.calls == 0, "made");
	a.calls = 7;
	a.lastFrame = 1;
	Entry& again = table->Find(0x2000, 2, isNew);
	Check(!isNew && &again == &a && again.calls == 7, "found again, as left");

	Section("Table: a full window gives away a stale entry before a fresh one, else the oldest");
	// Handles with the same home slot: found by search.
	std::uint32_t same[Table::kWindow + 2];
	std::size_t found = 0;
	const std::size_t home = Table::Home(0x10000);
	for (std::uint32_t h = 0x10000; found < Table::kWindow + 2; h += 0x10) {
		if (Table::Home(h) == home) {
			same[found++] = h;
		}
	}
	auto t = std::make_unique<Table>();
	for (std::size_t i = 0; i < Table::kWindow; ++i) {
		Entry& x = t->Find(same[i], 1000, isNew);
		x.lastFrame = 1000 + static_cast<std::uint32_t>(i);  // the first is the oldest
		Check(isNew, "a free slot of the window taken");
	}
	Entry& kept = t->Find(same[3], 1100, isNew);
	Check(!isNew && kept.lastFrame == 1003, "all sixteen still there");
	kept.lastFrame = 100;  // stale at frame 1100
	Entry& over = t->Find(same[Table::kWindow], 1100, isNew);
	Check(isNew && &over == &kept && over.calls == 0 && over.actor == same[Table::kWindow], "the stale one given away, cleared");
	over.lastFrame = 1100;
	Entry& oldest = t->Find(same[Table::kWindow + 1], 1100, isNew);
	Check(isNew && oldest.actor == same[Table::kWindow + 1], "none stale: one given away");
	t->Find(same[0], 1100, isNew);
	Check(isNew, "it was the oldest (the first)");
	t->Find(same[5], 1100, isNew);
	Check(!isNew, "a younger one stayed");

	return Failures();
}
