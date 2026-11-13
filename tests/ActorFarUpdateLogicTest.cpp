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
	Section("The view rule's hidden part: who is taken, and by which part");
	{
		const float eye[3] = {0.0f, 0.0f, 0.0f};
		const float ahead[3] = {0.0f, 1.0f, 0.0f};
		const float inFront[3] = {0.0f, 2000.0f, 0.0f};
		const float nearInFront[3] = {0.0f, 500.0f, 0.0f};
		const float behind[3] = {0.0f, -2000.0f, 0.0f};
		const float bad[3] = {std::numeric_limits<float>::quiet_NaN(), 2000.0f, 0.0f};
		ViewRule rule;
		rule.on = true;
		Check(TakenByViewOrHidden(rule, true, true, false, true, inFront, eye, ahead) == Taken::Hidden, "inside the cone and left out by the culler: hidden");
		Check(TakenByViewOrHidden(rule, true, true, false, false, inFront, eye, ahead) == Taken::No, "inside the cone and not left out: not taken");
		Check(TakenByViewOrHidden(rule, false, true, false, true, inFront, eye, ahead) == Taken::No, "the hidden part off: not taken");
		Check(TakenByViewOrHidden(rule, true, true, true, true, inFront, eye, ahead) == Taken::No, "in combat: not taken");
		Check(TakenByViewOrHidden(rule, true, false, false, true, inFront, eye, ahead) == Taken::No, "no camera: not taken");
		Check(TakenByViewOrHidden(rule, true, true, false, true, nearInFront, eye, ahead) == Taken::No, "nearer than the distance: not taken");
		Check(TakenByViewOrHidden(rule, true, true, false, true, bad, eye, ahead) == Taken::No, "a place that is not a number: not taken");
		Check(TakenByViewOrHidden(rule, true, true, false, true, behind, eye, ahead) == Taken::OutsideCone &&
		          TakenByViewOrHidden(rule, false, true, false, false, behind, eye, ahead) == Taken::OutsideCone,
		      "outside the cone: taken by the cone, whatever the culler says");
		rule.on = false;
		Check(TakenByViewOrHidden(rule, true, true, false, true, inFront, eye, ahead) == Taken::No, "the view rule off: the hidden part is off with it");
	}
	Section("The view rule: who is taken");
	{
		const float eye[3] = {0.0f, 0.0f, 0.0f};
		const float ahead[3] = {0.0f, 1.0f, 0.0f};
		const float behind[3] = {0.0f, -2000.0f, 0.0f};
		const float beside[3] = {2000.0f, 100.0f, 0.0f};
		const float inFront[3] = {0.0f, 2000.0f, 0.0f};
		const float nearBehind[3] = {0.0f, -999.0f, 0.0f};
		const float atDistance[3] = {0.0f, -1000.0f, 0.0f};
		ViewRule rule;
		Check(!TakenByView(rule, true, false, behind, eye, ahead), "the rule off: nobody");
		rule.on = true;
		Check(TakenByView(rule, true, false, behind, eye, ahead), "behind the camera and far enough: taken");
		Check(TakenByView(rule, true, false, beside, eye, ahead), "in front but outside the cone: taken");
		Check(!TakenByView(rule, true, false, inFront, eye, ahead), "inside the cone: not taken");
		Check(!TakenByView(rule, true, false, nearBehind, eye, ahead), "nearer than the distance: not taken, wherever it stands");
		Check(TakenByView(rule, true, false, atDistance, eye, ahead), "at the distance exactly: taken");
		Check(!TakenByView(rule, true, true, behind, eye, ahead), "in combat: not taken");
		Check(!TakenByView(rule, false, false, behind, eye, ahead), "no camera: not taken");
		const float none[3] = {0.0f, 0.0f, 0.0f};
		Check(!TakenByView(rule, true, false, behind, eye, none), "a direction of no length: not taken");
		const float bad[3] = {std::numeric_limits<float>::quiet_NaN(), -2000.0f, 0.0f};
		Check(!TakenByView(rule, true, false, bad, eye, ahead), "a place that is not a number: not taken");
		rule.coneCosine = 0.0f;  // 90 degrees: only what is behind
		Check(!TakenByView(rule, true, false, beside, eye, ahead) && TakenByView(rule, true, false, behind, eye, ahead),
		      "a cone of 90 degrees takes only what stands behind");
		rule.minDistance = 3000.0f;
		Check(!TakenByView(rule, true, false, behind, eye, ahead), "a larger distance leaves the nearer one alone");
	}
	Section("Near actors by where the camera looks");
	{
		const float camera[3] = {100.0f, 200.0f, 50.0f};
		const float ahead[3] = {0.0f, 2.0f, 0.0f};  // not unit length on purpose
		const float cone = 0.2588f;                 // 75 degrees
		const auto at = [&](float x, float y, float z) {
			const float actor[3] = {camera[0] + x, camera[1] + y, camera[2] + z};
			return ViewOfActor(actor, camera, ahead, cone);
		};
		Check(at(0, 500, 0).side == ViewSide::Inside && at(0, 500, 0).band == 0, "straight ahead at 500 units: inside, the first band");
		Check(at(0, 999, 0).band == 0 && at(0, 1000, 0).band == 1 && at(0, 2999, 0).band == 1 && at(0, 3000, 0).band == 2 &&
		          at(0, 50000, 0).band == 2,
		      "the bands end at 1000 and 3000 units, the last one is open");
		Check(at(500, 500, 0).side == ViewSide::Inside, "45 degrees to the side: inside a 75 degree cone");
		Check(at(1000, 100, 0).side == ViewSide::Outside, "84 degrees to the side: in front but outside the cone");
		Check(at(0, 100, 1000).side == ViewSide::Outside, "the same upward");
		Check(at(0, -500, 0).side == ViewSide::Behind && at(300, -1, 0).side == ViewSide::Behind, "behind the camera's plane: behind");
		Check(at(300, 0, 0).side == ViewSide::Outside, "exactly beside: in front's edge, outside the cone");
		Check(at(0, 0, 0).side == ViewSide::Inside && at(0, 0, 0).band == 0, "an actor at the camera reads as inside");
		const float none[3] = {0.0f, 0.0f, 0.0f};
		const float actor[3] = {0.0f, -900.0f, 0.0f};
		Check(ViewOfActor(actor, camera, none, cone).side == ViewSide::Inside, "a direction of no length: inside");
		const float bad[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
		Check(ViewOfActor(bad, camera, ahead, cone).side == ViewSide::Inside && ViewOfActor(actor, camera, bad, cone).side == ViewSide::Inside &&
		          ViewOfActor(actor, bad, ahead, cone).side == ViewSide::Inside,
		      "a value that is not a number anywhere: inside");
		const float huge[3] = {1.0e20f, 0.0f, 0.0f};
		Check(ViewOfActor(huge, camera, ahead, cone).side == ViewSide::Inside, "a position too far to square: inside");
		ViewCounts counts;
		counts.Add(at(0, 500, 0));
		counts.Add(at(1000, 100, 0));
		counts.Add(at(0, -500, 0));
		counts.Add(at(0, -4000, 0));
		Check(counts.All() == 4 && counts.inside[0] == 1 && counts.outside[1] == 1 && counts.behind[0] == 1 && counts.behind[2] == 1,
		      "each count lands in its side and band");
		Check(counts.Unseen(0) == 3 && counts.Unseen(1) == 2 && counts.Unseen(2) == 1, "the unseen from a band outward");
	}
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

	Section("Table: Peek finds an actor's entry and makes none");
	auto peeked = std::make_unique<Table>();
	Check(peeked->Peek(0x3000) == nullptr, "an actor never seen: none");
	Entry& made = peeked->Find(0x3000, 1, isNew);
	Check(peeked->Peek(0x3000) == &made && peeked->Peek(0x3010) == nullptr, "found once made; its neighbour still none");
	Check(peeked->Peek(0) == nullptr, "the free slots' handle 0 is no actor");

	Section("Root motion: the animation's steps of the frames an update was left out are summed for the one that runs");
	Entry r;
	r.actor = 0x1000;
	const float step1[3] = {1.0f, 2.0f, 0.5f};
	const float step2[3] = {0.5f, -1.0f, 0.0f};
	float sum[3] = {9.0f, 9.0f, 9.0f};
	Check(!TakeRootMotion(r, 0x5000, sum) && sum[0] == 9.0f, "nothing noted: nothing to take, the output left alone");
	AddRootMotion(r, 0x5000, step1);
	AddRootMotion(r, 0x5000, step2);
	Check(TakeRootMotion(r, 0x5000, sum) && Near(sum[0], 1.5) && Near(sum[1], 1.0) && Near(sum[2], 0.5), "two frames' steps: their sum");
	Check(!TakeRootMotion(r, 0x5000, sum), "taken once: gone");
	AddRootMotion(r, 0x5000, step1);
	AddRootMotion(r, 0x6000, step2);
	Check(TakeRootMotion(r, 0x6000, sum) && Near(sum[0], 0.5) && Near(sum[1], -1.0), "other animation data: the sum starts again");
	AddRootMotion(r, 0x6000, step1);
	Check(!TakeRootMotion(r, 0x7000, sum), "the actor's animation data is another one now: not taken");
	Check(!TakeRootMotion(r, 0x6000, sum), "and the sum is dropped by that");
	AddRootMotion(r, 0x6000, step1);
	Check(!TakeRootMotion(r, 0, sum), "no animation data at all: not taken");
	const float bad[3] = {std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f};
	const float huge[3] = {1.0f, std::numeric_limits<float>::infinity(), 1.0f};
	AddRootMotion(r, 0x6000, step1);
	AddRootMotion(r, 0x6000, bad);
	AddRootMotion(r, 0x6000, huge);
	Check(TakeRootMotion(r, 0x6000, sum) && Near(sum[0], 1.0) && Near(sum[1], 2.0), "a step that is not a finite number is not added");
	AddRootMotion(r, 0, step1);
	Check(!TakeRootMotion(r, 0, sum), "a step of no animation data is not noted");
	AddRootMotion(r, 0x6000, step1);
	DropRootMotion(r);
	Check(!TakeRootMotion(r, 0x6000, sum), "dropped (an update that ran with the frame's own time): nothing to take");

	Section("Root motion: the animation data is trusted only while its steps are fresh");
	Entry fr;
	fr.actor = 0x1000;
	Check(FreshAnimData(fr, 10) == 0, "nothing noted: none");
	AddRootMotion(fr, 0x5000, step1, 10);
	Check(FreshAnimData(fr, 10) == 0x5000 && FreshAnimData(fr, 11) == 0x5000, "noted this frame or the one before: it");
	Check(FreshAnimData(fr, 12) == 0, "older: none");
	Check(!TakeRootMotion(fr, FreshAnimData(fr, 12), sum), "so a stale sum is not taken");

	Section("A far actor's rate: the full rate in combat");
	Check(FarEvery(true, false, true, 16) == 16, "far, not in combat, on: every 16th");
	Check(FarEvery(true, true, true, 16) == 1, "far and in combat: every call (Windows crash 2026-11-12 in a fight's follow logic; a fighting actor must not skip updates)");
	Check(FarEvery(true, false, false, 16) == 1, "off in this window: every call");
	Check(FarEvery(false, false, true, 16) == 1, "not far: every call");

	return Failures();
}
