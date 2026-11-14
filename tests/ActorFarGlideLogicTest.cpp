// A far actor shown on its way between its updates
// (src/engine/scene/ActorFarGlideLogic.h): the length, the gain measured at
// an update, the offset for the step so far, the shift, and what is left of
// the leap.

#include "Check.h"
#include "engine/scene/ActorFarGlideLogic.h"

#include <cmath>
#include <limits>

namespace glide = obng::engine::scene::actor_far_glide;

namespace {

bool Near(float a, float b, float tolerance = 1.0e-4f) {
	return std::fabs(a - b) <= tolerance * (1.0f + std::fabs(b));
}

void TestLength() {
	Section("The length");
	const float a[3] = {3.0f, 4.0f, 12.0f};
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	const float tiny[3] = {0.001f, 0.0f, 0.0f};
	const float big[3] = {30000.0f, 40000.0f, 0.0f};
	const float nan[3] = {std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f};
	const float huge[3] = {1.0e20f, 0.0f, 0.0f};
	Check(Near(glide::Length(a), 13.0f) && glide::Length(zero) == 0.0f && Near(glide::Length(tiny), 0.001f) && Near(glide::Length(big), 50000.0f),
	      "exact on a triple, zero, small and large");
	Check(glide::Length(nan) == 0.0f && glide::Length(huge) == 0.0f, "a NaN and a value out of range give 0");
}

void TestGain() {
	Section("The gain is measured at the update after the one that took the step");
	glide::State s;
	const float none[3] = {0, 0, 0};
	const float step1[3] = {0.0f, 40.0f, 0.0f};  // in the animation's space: forward
	glide::Measured m = glide::AtUpdate(s, false, none, true, step1);
	Check(!m.counted && !s.hasGain && Near(s.consumed, 40.0f) && s.hasLast, "the first update: no way known, the step's length kept");
	float offset[3];
	Check(!glide::Offset(s, true, step1, offset), "no gain yet: nothing to show");
	const float moved[3] = {30.0f, 0.0f, 6.0f};  // the world's way that step made: east and uphill, shorter than the step
	const float step2[3] = {0.0f, 20.0f, 0.0f};
	m = glide::AtUpdate(s, true, moved, true, step2);
	Check(m.counted && Near(m.leap, glide::Length(moved)) && Near(m.left, m.leap), "the second: the leap counted, nothing was shown before it");
	Check(s.hasGain && Near(s.gain[0], 0.75f) && s.gain[1] == 0.0f && Near(s.gain[2], 0.15f) && Near(s.consumed, 20.0f), "the gain: the way over the step's length, by axis");

	Section("The offset for the step so far, and the shift");
	const float soFar[3] = {0.0f, 10.0f, 0.0f};
	Check(glide::Offset(s, true, soFar, offset) && Near(offset[0], 7.5f) && offset[1] == 0.0f && Near(offset[2], 1.5f), "the gain times the summed step's length");
	Check(!glide::Offset(s, false, soFar, offset), "no step noted: none");
	float delta[3];
	Check(glide::Shift(s, offset, false, delta) && Near(delta[0], 7.5f) && Near(delta[2], 1.5f) && Near(s.applied[0], 7.5f), "the first shift is the whole offset");
	const float root[3] = {100.0f, 200.0f, 300.0f};
	glide::Wrote(s, root);
	Check(!glide::RootMoved(s, root), "the root where the shift left it");
	Check(!glide::Shift(s, offset, false, delta), "the same offset again: nothing to shift");
	const float further[3] = {0.0f, 12.0f, 0.0f};
	glide::Offset(s, true, further, offset);
	Check(glide::Shift(s, offset, false, delta) && Near(delta[0], 1.5f) && Near(delta[2], 0.3f), "a longer step: only the difference");
	const float elsewhere[3] = {100.0f, 200.5f, 300.0f};
	Check(glide::RootMoved(s, elsewhere), "the root's world translate changed by someone else");
	Check(glide::Shift(s, offset, true, delta) && Near(delta[0], 9.0f) && Near(delta[2], 1.8f), "then the whole offset is shifted again");
	const float hair[3] = {0.0f, 12.02f, 0.0f};
	glide::Offset(s, true, hair, offset);
	Check(!glide::Shift(s, offset, false, delta) && Near(s.applied[0], 9.0f), "a shift shorter than the least is not made and not booked");

	Section("What is left of the leap: known one update later, when the way that update made can be read");
	const float moved2[3] = {10.0f, 0.0f, 2.0f};  // the way the update now beginning will make: a little further than was shown (9, 0, 1.8)
	m = glide::AtUpdate(s, true, moved, false, none);
	Check(Near(s.shown[0], 9.0f) && Near(s.shown[2], 1.8f), "the offset shown when this update begins is kept");
	Check(s.applied[0] == 0.0f && !s.wroteSet && s.consumed == 0.0f, "the update composes the transforms anew: nothing applied, nothing written; it took no step");
	Check(s.hasGain && Near(s.gain[0], 1.5f), "the gain is the way over the step the update before took (20 units)");
	m = glide::AtUpdate(s, true, moved2, true, step1);
	Check(m.counted && Near(m.leap, glide::Length(moved2)) && Near(m.left, std::sqrt(1.0f + 0.04f)), "at the next update: the leap against what the frames before it showed");
	Check(!s.hasGain, "an update that took no step leaves no gain for the next");

	Section("No gain from a stand, a push or an unknown way");
	glide::State t;
	const float shortStep[3] = {0.0f, 1.0f, 0.0f};
	glide::AtUpdate(t, false, none, true, shortStep);
	glide::AtUpdate(t, true, moved, true, step1);
	Check(!t.hasGain, "a step under two units: the actor stood");
	const float far[3] = {0.0f, 500.0f, 0.0f};
	glide::AtUpdate(t, true, far, true, step1);
	Check(!t.hasGain, "a way of more than twice the step: not the animation's");
	glide::AtUpdate(t, false, none, true, step1);
	Check(!t.hasGain, "the way not known");
	const float still[3] = {0.0f, 0.0f, 0.0f};
	const glide::Measured quiet = glide::AtUpdate(t, true, still, true, step1);
	Check(!quiet.counted, "no way and nothing shown: not counted");

	Section("An offset out of bounds is not shown");
	glide::State u;
	glide::AtUpdate(u, false, none, true, step1);
	const float twice[3] = {0.0f, 80.0f, 0.0f};
	glide::AtUpdate(u, true, twice, true, step1);
	const float long1[3] = {0.0f, 150.0f, 0.0f};
	const float long2[3] = {0.0f, 250.0f, 0.0f};
	Check(u.hasGain && glide::Offset(u, true, long1, offset) && Near(offset[1], 300.0f), "within the bound");
	Check(!glide::Offset(u, true, long2, offset), "beyond it: none");
}

}  // namespace

int main() {
	TestLength();
	TestGain();
	return Failures();
}
