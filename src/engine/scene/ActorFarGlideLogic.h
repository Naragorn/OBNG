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
// left out the offset the summed step so far stands for is added to the local
// translate of the actor's scene node. The engine composes a loaded actor's
// world transforms from the local ones every frame, update or not (measured
// 2026-11-14, run obng-stress-ride/20261114-080839: a shift written into the
// world transforms was gone by the next call in 93 % of 60 000 cases), so the
// whole subtree follows, bounds and all, at the cost of three additions.
//
// How far a unit of summed step carries the actor is measured, not derived:
// at every update that runs, the way the actor made since the last one over
// the length of the step that update took is the gain for the frames that
// follow (it holds the actor's heading, its scale, the slope, and is small
// when something blocked the way). The direction is therefore one update
// late; what is left of the leap at each update is counted beside the leap
// itself, so the summary says what the glide takes away.
//
// Before an update runs, and whenever the actor leaves the glide, the offset
// is taken out of the local translate again - when the translate is still
// what the glide wrote; when someone else wrote it meanwhile, the offset went
// with that and there is nothing to take out. Class C.
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
	float applied[3] = {};  // the offset in the node's local translate now
	float shown[3] = {};    // ... when the last update that ran began
	float wrote[3] = {};    // the node's local translate after the last shift
	bool wroteSet = false;
	bool hasLast = false;   // an update ran since the entry was made
	// An actor that moves without an animation step (many creatures): its
	// speed over the last update's time, shown on when two updates in a row
	// gave about the same.
	float seconds = 0.0f;     // the time the last update that ran stood for
	float velocity[3] = {};   // the way that update made over that time
	bool hasVelocity = false; // ... measured
	bool steady = false;      // ... and within half of the one before
};

inline constexpr float kSteadyShare = 0.5f;   // two speeds whose difference is at most this share of the newer one are the same pace
inline constexpr float kMinSeconds = 0.01f;   // an update that stood for less gives no speed
inline constexpr float kOverrun = 1.25f;      // the speed is shown on for at most this many times the last update's time

struct Measured {
	bool counted = false;
	float leap = 0.0f;  // the way the last update moved the actor: what a frame showed at once without the glide
	float left = 0.0f;  // what was left of it after the offset shown before
};

// An update that runs begins (after TakeBack). `moved`: the way the actor
// made since the last update that ran began (hasMoved false: not known).
// `step`: the summed step this update takes (tookStep false: none).
// `seconds`: the time this update stands for.
inline Measured AtUpdate(State& s, bool hasMoved, const float moved[3], bool tookStep, const float step[3], float seconds = 0.0f) {
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
	// The speed: `moved` is the way of the last update, which stood for s.seconds.
	const bool measured = s.hasLast && hasMoved && s.seconds >= kMinSeconds;
	float velocity[3] = {};
	if (measured) {
		const float change[3] = {moved[0] / s.seconds - s.velocity[0], moved[1] / s.seconds - s.velocity[1], moved[2] / s.seconds - s.velocity[2]};
		for (int i = 0; i < 3; ++i) {
			velocity[i] = moved[i] / s.seconds;
		}
		const float pace = Length(velocity);
		s.steady = s.hasVelocity && pace * s.seconds >= kMinStep && Length(change) <= kSteadyShare * pace;
	} else {
		s.steady = false;
	}
	for (int i = 0; i < 3; ++i) {
		s.velocity[i] = velocity[i];
		s.shown[i] = s.applied[i];
		s.applied[i] = 0.0f;
	}
	s.hasVelocity = measured;
	s.seconds = seconds;
	s.wroteSet = false;
	s.consumed = tookStep ? Length(step) : 0.0f;
	s.hasLast = true;
	return m;
}

// The offset to show now: the gain times the step summed so far; without a
// gain or a step, the steady speed times the time since the last update
// (`elapsed`, capped). False when there is none to show (neither, or an
// offset beyond kMaxOffset).
inline bool Offset(const State& s, bool hasStep, const float stepSoFar[3], float out[3], float elapsed = 0.0f) {
	if (s.hasGain && hasStep) {
		const float length = Length(stepSoFar);
		for (int i = 0; i < 3; ++i) {
			out[i] = s.gain[i] * length;
		}
	} else if (s.steady && elapsed > 0.0f) {
		const float most = kOverrun * s.seconds;
		const float time = elapsed < most ? elapsed : most;
		for (int i = 0; i < 3; ++i) {
			out[i] = s.velocity[i] * time;
		}
	} else {
		return false;
	}
	return Length(out) <= kMaxOffset;
}

// The shift that brings the node to `offset`. rootMoved: its local translate
// is not what the last shift left there - someone else placed the node, and
// the offset in it is gone with that.
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

inline void Wrote(State& s, const float local[3]) {
	for (int i = 0; i < 3; ++i) {
		s.wrote[i] = local[i];
	}
	s.wroteSet = true;
}

// Whether the node's local translate is no longer what the last shift left.
inline bool RootMoved(const State& s, const float local[3]) {
	return s.wroteSet && (local[0] != s.wrote[0] || local[1] != s.wrote[1] || local[2] != s.wrote[2]);
}

// The offset to take out of the node's local translate before an update runs
// or the glide lets the actor go: true with `back` to add when an offset is
// in it and the translate is still the glide's; the state holds none after.
inline bool TakeBack(State& s, const float local[3], float back[3]) {
	const bool there = s.wroteSet && !RootMoved(s, local) && (s.applied[0] != 0.0f || s.applied[1] != 0.0f || s.applied[2] != 0.0f);
	for (int i = 0; i < 3; ++i) {
		back[i] = -s.applied[i];
	}
	s.wroteSet = false;
	return there;
}

}  // namespace obng::engine::scene::actor_far_glide
