#pragma once
// A far actor shown on its way between its updates - the decisions, over
// plain values ([Route] ActorFarGlide in the plugin's ActorFarGlide.cpp).
//
// ActorFarUpdateLogic.h runs a far actor's whole update on every n-th call
// with the summed time, so the actor stands for n frames and then leaps the
// way of n frames: 5.6 leaps a second at 90 frames a second, 2 at 30 (Nadi
// on Windows, 2026-11-13: "NPCs in the distance stutter"). The animation's
// update still runs every frame and its steps are summed for the update that
// runs (AddRootMotion). The glide shows that sum as it grows: on every call
// left out the actor's scene subtree - world transforms and bounds, nothing
// the update reads - is moved to where the summed step so far would put it.
//
// How far a unit of summed step carries the actor is measured, not derived:
// at every update that runs, the way the actor made since the last one over
// the length of the step that update took is the gain for the frames that
// follow (it holds the actor's heading, its scale, the slope, and is small
// when something blocked the way). The direction is therefore one update
// late; what is left of the leap at each update is counted beside the leap
// itself, so the summary says what the glide takes away.
//
// The update that runs composes the world transforms anew from the local
// ones; the shift is gone by then and nothing has to be taken back. Class C.
// 64-bit clean: plain values only.
#include <cstdint>

namespace obng::engine::scene::actor_far_glide {

inline constexpr float kMinStep = 2.0f;     // a summed step shorter than this (units) gives no gain: the actor stood
inline constexpr float kMaxGain = 2.0f;     // a way longer than this many times the step was not the animation's (a push, a teleport)
inline constexpr float kMinShift = 0.05f;   // a shift smaller than this in length is not made
inline constexpr float kMaxOffset = 400.0f; // the offset shown never exceeds this (units)

// The length of (x, y, z). A NaN or a negative comes out as 0.
inline float Length(const float v[3]) {
	const float squared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (!(squared > 0.0f) || !(squared < 1.0e30f)) {
		return 0.0f;
	}
	return __builtin_sqrtf(squared);  // one instruction, correctly rounded (clang builds the plugin and the tests)
}

struct State {
	float gain[3] = {};     // world units shown per unit of summed step
	bool hasGain = false;
	float consumed = 0.0f;  // the length of the summed step the last update that ran took; 0: it took none
	float applied[3] = {};  // the offset the subtree is shifted by now
	float shown[3] = {};    // ... when the last update that ran began
	float wrote[3] = {};    // the root's world translate after the last shift
	bool wroteSet = false;
	bool hasLast = false;   // an update ran since the entry was made
};

struct Measured {
	bool counted = false;
	float leap = 0.0f;  // the way the last update moved the actor: what a frame showed at once without the glide
	float left = 0.0f;  // what was left of it after the offset shown before
};

// An update that runs begins. `moved`: the way the actor made since the last
// update that ran began (hasMoved false: not known). `step`: the summed step
// this update takes (tookStep false: none).
inline Measured AtUpdate(State& s, bool hasMoved, const float moved[3], bool tookStep, const float step[3]) {
	Measured m;
	if (s.hasLast && hasMoved) {
		const float rest[3] = {moved[0] - s.shown[0], moved[1] - s.shown[1], moved[2] - s.shown[2]};
		m.leap = Length(moved);
		m.left = Length(rest);
		m.counted = m.leap > kMinShift || Length(s.shown) > kMinShift;
	}
	s.hasGain = false;
	if (s.hasLast && hasMoved && s.consumed >= kMinStep) {
		const float way = Length(moved);
		if (way <= kMaxGain * s.consumed) {
			for (int i = 0; i < 3; ++i) {
				s.gain[i] = moved[i] / s.consumed;
			}
			s.hasGain = true;
		}
	}
	for (int i = 0; i < 3; ++i) {
		s.shown[i] = s.applied[i];
		s.applied[i] = 0.0f;
	}
	s.wroteSet = false;
	s.consumed = tookStep ? Length(step) : 0.0f;
	s.hasLast = true;
	return m;
}

// The offset to show for the step summed so far; false when there is none
// to show (no gain, no step, or an offset beyond kMaxOffset).
inline bool Offset(const State& s, bool hasStep, const float stepSoFar[3], float out[3]) {
	if (!s.hasGain || !hasStep) {
		return false;
	}
	const float length = Length(stepSoFar);
	for (int i = 0; i < 3; ++i) {
		out[i] = s.gain[i] * length;
	}
	return Length(out) <= kMaxOffset;
}

// The shift that brings the subtree to `offset`. rootMoved: the root's world
// translate is not what the last shift left there - something composed the
// transforms anew, and the shift made so far is gone with it.
inline bool Shift(State& s, const float offset[3], bool rootMoved, float delta[3]) {
	if (rootMoved) {
		s.applied[0] = s.applied[1] = s.applied[2] = 0.0f;
	}
	for (int i = 0; i < 3; ++i) {
		delta[i] = offset[i] - s.applied[i];
	}
	if (!(Length(delta) >= kMinShift)) {
		return false;
	}
	for (int i = 0; i < 3; ++i) {
		s.applied[i] = offset[i];
	}
	return true;
}

inline void Wrote(State& s, const float rootWorld[3]) {
	for (int i = 0; i < 3; ++i) {
		s.wrote[i] = rootWorld[i];
	}
	s.wroteSet = true;
}

// Whether the root's world translate is still what the last shift left.
inline bool RootMoved(const State& s, const float rootWorld[3]) {
	return s.wroteSet && (rootWorld[0] != s.wrote[0] || rootWorld[1] != s.wrote[1] || rootWorld[2] != s.wrote[2]);
}

}  // namespace obng::engine::scene::actor_far_glide
