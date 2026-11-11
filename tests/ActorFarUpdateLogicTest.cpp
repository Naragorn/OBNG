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

	Section("Clock: See ticks when the engine's frame clock moved, once");
	Clock seen;
	seen.See(5000, 0.01f);
	Check(seen.frame == 1 && Near(seen.seconds, 0.01), "the first look is a tick, whatever the stamp");
	seen.See(5000, 0.01f);
	seen.See(5000, 0.01f);
	Check(seen.frame == 1, "the same stamp again: the same frame");
	seen.See(5011, 0.011f);
	Check(seen.frame == 2 && Near(seen.seconds, 0.021), "the stamp moved: the next frame");
	Clock zero;
	zero.See(0, 0.01f);
	Check(zero.frame == 1, "a first stamp of 0 is a tick too");

	Section("Decide: the first call runs with the frame's own time");
	Clock clock;
	clock.Tick(0.01f);
	Entry e;
	e.actor = 0x1040;  // phase 1 of 4: runs in frames 3, 7, ..
	Step s = Decide(e, true, 4, clock, 0.01f);
	Check(s.runs && !s.summed && e.lastFrame == 1, "new: runs, nothing summed, the clock noted");

	Section("Decide: a far actor runs in one frame of n and gets the time of the frames between");
	clock.Tick(0.01f);
	Check(!Decide(e, false, 4, clock, 0.01f).runs && e.lastFrame == 1, "frame 2: left out, the clock not noted");
	clock.Tick(0.01f);
	s = Decide(e, false, 4, clock, 0.01f);
	Check(s.runs && s.summed && Near(s.seconds, 0.02), "frame 3: runs for two frames, 0.02 s");
	s = Decide(e, false, 4, clock, 0.01f);
	Check(s.runs && !s.summed, "a second call in its frame runs too, with the frame's own time");
	bool ran[4];
	Step last;
	for (int i = 0; i < 4; ++i) {
		clock.Tick(0.01f);
		last = Decide(e, false, 4, clock, 0.01f);
		ran[i] = last.runs;
	}
	Check(!ran[0] && !ran[1] && !ran[2] && ran[3], "frames 4, 5, 6 left out, 7 runs");
	Check(last.summed && Near(last.seconds, 0.04) && e.lastFrame == 7, "it stands for four frames: 0.04 s");

	Section("Decide: a near actor runs every call and its time is left alone");
	clock.Tick(0.01f);
	s = Decide(e, false, 1, clock, 0.01f);
	Check(s.runs && !s.summed, "the frame after a run: every 1, nothing summed");

	Section("Decide: a far actor that comes near gets the left-out time once");
	Entry f;
	f.actor = 0x1000;  // phase 0 of 4: runs in frames 4, 8, ..
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
	Check(isNew && a.actor == 0x2000 && a.lastFrame == 0, "made");
	a.lastFrame = 7;
	Entry& again = table->Find(0x2000, 2, isNew);
	Check(!isNew && &again == &a && again.lastFrame == 7, "found again, as left");

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
	Check(isNew && &over == &kept && over.lastSeconds == 0.0 && over.actor == same[Table::kWindow], "the stale one given away, cleared");
	over.lastFrame = 1100;
	Entry& oldest = t->Find(same[Table::kWindow + 1], 1100, isNew);
	Check(isNew && oldest.actor == same[Table::kWindow + 1], "none stale: one given away");
	t->Find(same[0], 1100, isNew);
	Check(isNew, "it was the oldest (the first)");
	t->Find(same[5], 1100, isNew);
	Check(!isNew, "a younger one stayed");

	Section("TravelOf: what an actor moved between two updates that ran, as counted or not");
	Check(TravelOf(20.0, 0.2) == Travel::Moving, "100 units a second: moving");
	Check(TravelOf(0.5, 0.2) == Travel::Still, "2.5 units a second: still");
	Check(TravelOf(kMovingSpeed * 0.2, 0.2) == Travel::Still, "exactly the moving speed: still");
	Check(TravelOf(0.0, 0.2) == Travel::Still, "no way at all: still");
	Check(TravelOf(20.0, 0.0) == Travel::None, "no time between (a second call in its frame): not counted");
	Check(TravelOf(20.0, -0.1) == Travel::None, "time backwards: not counted");
	Check(TravelOf(2000.0, 0.2) == Travel::None, "10000 units a second: a leap (moved by a script, a cell change), not counted");
	Check(TravelOf(kLeapSpeed * 0.2, 0.2) == Travel::Moving, "exactly the leap speed: still moving");
	Check(TravelOf(std::numeric_limits<double>::quiet_NaN(), 0.2) == Travel::None, "a way that is not a number: not counted");
	Check(TravelOf(-1.0, 0.2) == Travel::None, "a negative way: not counted");

	return Failures();
}
