#pragma once
// A far actor's whole update at a lower rate - the decisions, over plain
// values ([Route] ActorFarWhole in the plugin's ActorFarUpdate.cpp).
//
// ActorFarRateLogic.h leaves out a far actor's skeleton update only. This
// leaves out the call of Actor::Update 0x00603CA0 itself - the AI, the
// animation step, the character controller, the skeleton - on all but every
// n-th call of an actor farther from the player than a distance. A different
// computation on purpose (class C): not comparable with the original.
//
// Actor::Update does not take the frame's time as its argument: it reads
// frameSeconds 0x00B33E9C (read, seven places; the animation step
// 0x00603500 six more). So the call that runs gets the time of the frames
// it stands for: the plugin puts the summed seconds into that global around
// the one call and restores it. The sum comes from a clock ticked once a
// frame by the player's own update, and is capped, so an entry that comes
// back after a long absence (or whose address a new actor took) does not
// leap.
//
// The clock ticks when the engine's own frame clock moved (TimeInfo's
// clockMs 0x00B33EA0, knowledge/frame-clock.md), not on a call counted as
// "once a frame": the first build ticked on the player's update, which runs
// more than once a frame (measured 2026-11-11, run 20261111-130350: every
// call got doubled time).
//
// A far actor runs in one frame of n, the frame taken from its handle so a
// crowd does not update together; whatever calls it gets in that frame run.
// An actor's first call always runs; a near actor runs every call, and gets
// the summed time only on the first call after it was far.
//
// 64-bit clean: an actor is a std::uint32_t handle.
#include <array>
#include <cstddef>
#include <cstdint>

#include "ActorFarGlideLogic.h"
#include "ActorFarRateLogic.h"

namespace obng::engine::scene::actor_far_update {

inline constexpr float kMaxSeconds = 0.5f;        // the most one call stands for
inline constexpr float kMaxFrameSeconds = 1.0f;   // a frame longer than this does not count (a load, a pause)
inline constexpr std::uint32_t kStaleFrames = 600;  // an entry not seen for this long may be given away

// Frames and game seconds, ticked once a frame.
struct Clock {
	std::uint32_t frame = 0;
	double seconds = 0.0;
	std::uint32_t stamp = 0;  // the engine's frame clock at the last tick
	bool stamped = false;

	// A call's look at the engine's frame clock: a tick when it moved.
	constexpr void See(std::uint32_t engineStamp, float frameSeconds) {
		if (stamped && engineStamp == stamp) {
			return;
		}
		stamped = true;
		stamp = engineStamp;
		Tick(frameSeconds);
	}

	constexpr void Tick(float frameSeconds) {
		++frame;
		if (frameSeconds > 0.0f && frameSeconds <= kMaxFrameSeconds) {  // a NaN compares false: not counted
			seconds += static_cast<double>(frameSeconds);
		}
	}
};

// What is kept for one actor.
struct Entry {
	std::uint32_t actor = 0;      // 0: free
	std::uint32_t lastFrame = 0;  // the clock at its last update that ran
	double lastSeconds = 0.0;
	float lastPosition[3] = {};   // where it stood at that update's entry (the travel measurement)
	bool hasPosition = false;
	std::uint32_t animData = 0;   // the animation data the steps below came from
	float rootMotion[3] = {};     // the animation's steps since the actor's last update that ran
	bool hasRootMotion = false;
	std::uint32_t rootFrame = 0;  // the clock at the last step noted
	actor_far_glide::State glide; // the actor shown on its way between its updates (ActorFarGlideLogic.h)
};

// One call's verdict.
struct Step {
	bool runs = true;
	bool summed = false;   // the call stands for more than this frame: seconds is to go into the global
	float seconds = 0.0f;
};

// One call of an actor's update. isNew: the entry was made for this call.
// every: 1 for a near actor or with the route off (actor_far_rate::EveryFor).
constexpr Step Decide(Entry& entry, bool isNew, std::uint32_t every, const Clock& clock, float frameSeconds) {
	Step step;
	if (!isNew) {
		if (every > 1 && (clock.frame + actor_far_rate::PhaseOf(entry.actor, every)) % every != 0) {
			step.runs = false;
			return step;
		}
		const double since = clock.seconds - entry.lastSeconds;
		const std::uint32_t frames = clock.frame - entry.lastFrame;
		if (frames > 1 && since > static_cast<double>(frameSeconds)) {
			step.summed = true;
			step.seconds = since < static_cast<double>(kMaxSeconds) ? static_cast<float>(since) : kMaxSeconds;
		}
	}
	entry.lastFrame = clock.frame;
	entry.lastSeconds = clock.seconds;
	return step;
}

// The measurement of whether the summed time carries a far actor as far as
// every frame's own would: the way an actor made between two of its updates
// that ran, over the game seconds between them. Summed over the far actors of
// a window it is their speed, to compare between windows with the lower rate
// and without.
inline constexpr double kMovingSpeed = 5.0;    // units a second; at or below: standing (idle sway, settling)
inline constexpr double kLeapSpeed = 3000.0;   // units a second; above: moved by something else than walking

enum class Travel { None, Still, Moving };  // None: not counted

constexpr Travel TravelOf(double units, double seconds) {
	if (!(seconds > 0.0) || !(units >= 0.0) || units > kLeapSpeed * seconds) {  // a NaN compares false: not counted
		return Travel::None;
	}
	return units > kMovingSpeed * seconds ? Travel::Moving : Travel::Still;
}

// The way a far actor is to go. The animation's update 0x00476D10 runs every
// frame whether the actor's update does or not (measured 2026-11-11, run
// obng-profile-anvil-docks/20261111-191517: never inside Actor::Update), and
// each run of it overwrites the step the animation made (its +0x0C..+0x14,
// read: zeroed at its entry, written at its end). The actor's update takes
// that step as the way to go (0x004723A0), so one that runs every 16th frame
// went one frame's way in sixteen frames' time. The steps of the frames
// between are summed here and handed to the update that runs.
constexpr float kMaxRootStep = 1000000.0f;  // a component beyond this is no step

constexpr void DropRootMotion(Entry& entry) {
	entry.rootMotion[0] = entry.rootMotion[1] = entry.rootMotion[2] = 0.0f;
	entry.hasRootMotion = false;
}

// One run of the animation's update for the actor: its step noted.
constexpr void AddRootMotion(Entry& entry, std::uint32_t animData, const float step[3], std::uint32_t frame = 0) {
	if (animData == 0) {
		return;
	}
	entry.rootFrame = frame;
	if (animData != entry.animData) {
		DropRootMotion(entry);
		entry.animData = animData;
	}
	for (int i = 0; i < 3; ++i) {
		if (!(step[i] >= -kMaxRootStep && step[i] <= kMaxRootStep)) {  // a NaN compares false
			return;
		}
	}
	for (int i = 0; i < 3; ++i) {
		entry.rootMotion[i] += step[i];
	}
	entry.hasRootMotion = true;
}

// The animation data the noted steps came from, when the last of them is of
// this frame or the one before (the animation's update runs every frame: an
// older note is of an object that may be gone); else 0.
constexpr std::uint32_t FreshAnimData(const Entry& entry, std::uint32_t frame) {
	return entry.hasRootMotion && frame - entry.rootFrame <= 1 ? entry.animData : 0;
}

// Where the summed step may be written: the noted animation data when the
// note is fresh and the actor's own animation data (its virtual +0x164, asked
// at the write) is still that object; else 0. The note alone is a pointer of
// a frame ago - the object can be freed or replaced since.
constexpr std::uint32_t RootMotionTarget(const Entry& entry, std::uint32_t frame, std::uint32_t liveAnimData) {
	const std::uint32_t noted = FreshAnimData(entry, frame);
	return noted != 0 && noted == liveAnimData ? noted : 0;
}

// A fresh note whose animation data is not the actor's any more (counted).
constexpr bool RootMotionStale(const Entry& entry, std::uint32_t frame, std::uint32_t liveAnimData) {
	const std::uint32_t noted = FreshAnimData(entry, frame);
	return noted != 0 && noted != liveAnimData;
}

// The summed step for an update that runs with summed time; the sum is gone
// after it either way. False (out untouched) when nothing was noted or the
// actor's animation data is not the one the steps came from.
constexpr bool TakeRootMotion(Entry& entry, std::uint32_t animDataNow, float out[3]) {
	const bool take = entry.hasRootMotion && animDataNow != 0 && animDataNow == entry.animData;
	if (take) {
		out[0] = entry.rootMotion[0];
		out[1] = entry.rootMotion[1];
		out[2] = entry.rootMotion[2];
	}
	DropRootMotion(entry);
	return take;
}

// The actors' entries: a fixed table, a short window probed from the
// handle's hash. A full window gives away a stale entry, else its oldest.
class Table {
public:
	static constexpr std::size_t kSlots = 2048;
	static constexpr std::size_t kWindow = 16;

	// The actor's entry; isNew when it was made (or taken over) for this call.
	Entry& Find(std::uint32_t actor, std::uint32_t frame, bool& isNew) {
		const std::size_t home = Home(actor);
		Entry* take = nullptr;
		for (std::size_t i = 0; i < kWindow; ++i) {
			Entry& e = slots_[(home + i) % kSlots];
			if (e.actor == actor) {
				isNew = false;
				return e;
			}
			if (take == nullptr || Better(e, *take, frame)) {
				take = &e;
			}
		}
		*take = Entry{};
		take->actor = actor;
		isNew = true;
		return *take;
	}

	// The actor's entry when it has one; none is made.
	Entry* Peek(std::uint32_t actor) {
		if (actor == 0) {
			return nullptr;
		}
		const std::size_t home = Home(actor);
		for (std::size_t i = 0; i < kWindow; ++i) {
			Entry& e = slots_[(home + i) % kSlots];
			if (e.actor == actor) {
				return &e;
			}
		}
		return nullptr;
	}

	static constexpr std::size_t Home(std::uint32_t actor) {
		return static_cast<std::size_t>(((actor >> 4) * 2654435761u) >> 21) % kSlots;  // objects are aligned: the low bits say nothing
	}

private:
	// Whether a is the better slot to give away than b: free before stale before older.
	static constexpr int Rank(const Entry& e, std::uint32_t frame) {
		return e.actor == 0 ? 0 : frame - e.lastFrame > kStaleFrames ? 1 : 2;
	}
	static constexpr bool Better(const Entry& a, const Entry& b, std::uint32_t frame) {
		const int ra = Rank(a, frame);
		const int rb = Rank(b, frame);
		return ra != rb ? ra < rb : ra == 2 && frame - a.lastFrame > frame - b.lastFrame;
	}

	std::array<Entry, kSlots> slots_{};
};

// ---- Near actors by where the camera looks ([Route] ActorViewCount) ----------
//
// A count for a possible next step: how many of the near actors (those the
// distance rule leaves alone) stand outside a cone around the camera's
// direction, by their distance from the camera. A measurement only - nothing
// is left out by it. The cone is wider than the view on purpose; what stands
// outside it cannot be on screen.
inline constexpr unsigned kViewBands = 3;
inline constexpr float kViewBandEnd[kViewBands - 1] = {1000.0f, 3000.0f};  // game units; the last band is everything beyond

enum class ViewSide : std::uint8_t { Inside, Outside, Behind };

struct ViewOf {
	ViewSide side = ViewSide::Inside;
	unsigned band = 0;
};

// Where the actor stands for the camera. An actor at the camera, a direction
// of no length or a value that is not a number reads as inside the cone,
// band 0: the count must not claim an actor unseen that it cannot place.
inline ViewOf ViewOfActor(const float actor[3], const float camera[3], const float direction[3], float coneCosine) {
	const float x = actor[0] - camera[0];
	const float y = actor[1] - camera[1];
	const float z = actor[2] - camera[2];
	const float squared = x * x + y * y + z * z;
	const float directionSquared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
	ViewOf out;
	if (!(squared > 0.0f) || !(directionSquared > 0.0f) || !(squared < 1.0e30f) || !(directionSquared < 1.0e30f)) {
		return out;
	}
	float distance = 0.0f;
	// A plain Newton root is enough for a band and a cosine (constexpr-free, no <cmath> in a logic header).
	distance = squared;
	for (int i = 0; i < 40; ++i) {
		distance = 0.5f * (distance + squared / distance);
	}
	float directionLength = directionSquared;
	for (int i = 0; i < 40; ++i) {
		directionLength = 0.5f * (directionLength + directionSquared / directionLength);
	}
	while (out.band < kViewBands - 1 && distance >= kViewBandEnd[out.band]) {
		++out.band;
	}
	const float cosine = (x * direction[0] + y * direction[1] + z * direction[2]) / (distance * directionLength);
	if (cosine < 0.0f) {
		out.side = ViewSide::Behind;
	} else if (cosine < coneCosine) {
		out.side = ViewSide::Outside;
	}
	return out;
}

// ---- The view rule ([Route] ActorViewRule) -----------------------------------
//
// A near actor that stands outside the cone around the camera's direction,
// at least a distance away, is updated at the far actors' rate. It cannot be
// on screen there; when it enters the cone its next call runs (with the time
// it missed), as for an actor that comes near. An actor in combat is never
// taken: a fight behind the player goes on at the full rate. Class C, like
// the distance rule.
// The rate of an actor beyond the far distance: every n-th call, but every
// call while it is in combat - a fighting actor's update left out fifteen
// times of sixteen and then run with the summed time is not a look question
// any more (it reacts late, and what its update frees or swaps others may
// hold).
constexpr std::uint32_t FarEvery(bool isFar, bool inCombat, bool onNow, std::uint32_t every) {
	return isFar && !inCombat && onNow ? every : 1u;
}

// Whether a point lies inside the cone around the camera's direction, without
// a root: the cosine's sign and its square against the lengths' squares. A
// point at the camera, a direction of no length or a value that is not a
// number counts as inside (it cannot be called unseen).
inline bool InsideCone(const float point[3], const float eye[3], const float direction[3], float coneCosine) {
	const float x = point[0] - eye[0];
	const float y = point[1] - eye[1];
	const float z = point[2] - eye[2];
	const float squared = x * x + y * y + z * z;
	const float directionSquared = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
	if (!(squared > 0.0f) || !(directionSquared > 0.0f) || !(squared < 1.0e30f) || !(directionSquared < 1.0e30f)) {
		return true;
	}
	const float dot = x * direction[0] + y * direction[1] + z * direction[2];
	const float bound = coneCosine * coneCosine * squared * directionSquared;
	return coneCosine >= 0.0f ? dot >= 0.0f && dot * dot >= bound : dot >= 0.0f || dot * dot <= bound;
}

// [Route] ActorFarSeenEvery: a far actor inside the cone around the camera's
// direction - one that can be on screen - gets this rate instead of the far
// one (Nadi on Windows, 2026-11-13: NPCs in the distance stutter - at every
// 16th call an actor stands for sixteen frames and then leaps). Never slower
// than the far rate; in combat every call, as before.
constexpr std::uint32_t FarSeenEvery(std::uint32_t farEvery, std::uint32_t seenEvery, bool insideCone) {
	return insideCone && seenEvery >= 1 && seenEvery < farEvery ? seenEvery : farEvery;
}

struct ViewRule {
	bool on = false;
	float coneCosine = 0.2588f;   // 75 degrees half angle: wider than a headset's view by 20 degrees
	float minDistance = 1000.0f;  // nearer than this an actor is never taken
};

// Whether the view rule takes this actor. No camera, the rule off, the actor
// in combat or nearer than the distance: no. A place the count cannot read
// (ViewOfActor's cases) counts as inside the cone: no.
inline bool TakenByView(const ViewRule& rule, bool cameraKnown, bool inCombat, const float actor[3], const float eye[3], const float direction[3]) {
	if (!rule.on || !cameraKnown || inCombat) {
		return false;
	}
	const float x = actor[0] - eye[0];
	const float y = actor[1] - eye[1];
	const float z = actor[2] - eye[2];
	const float squared = x * x + y * y + z * z;
	if (!(squared >= rule.minDistance * rule.minDistance)) {
		return false;
	}
	return ViewOfActor(actor, eye, direction, rule.coneCosine).side != ViewSide::Inside;
}

// The view rule's second part ([Route] ActorViewHidden): an actor inside the
// cone that the occlusion culler left out of the scene's walk - its node's
// bound drew no pixel against the frame's depth, so nothing of it shows, and
// what shows through a grating's gaps is not hidden by that test - gets a
// lower rate of its own (gentler than the far one: it can come round a corner
// with no margin of angle). Where the culler does not run there is no such
// answer and only the cone decides.
enum class Taken : std::uint8_t { No, OutsideCone, Hidden };

inline Taken TakenByViewOrHidden(const ViewRule& rule, bool hiddenOn, bool cameraKnown, bool inCombat, bool leftOutByCuller, const float actor[3],
                                 const float eye[3], const float direction[3]) {
	if (TakenByView(rule, cameraKnown, inCombat, actor, eye, direction)) {
		return Taken::OutsideCone;
	}
	if (!rule.on || !hiddenOn || !cameraKnown || inCombat || !leftOutByCuller) {
		return Taken::No;
	}
	const float x = actor[0] - eye[0];
	const float y = actor[1] - eye[1];
	const float z = actor[2] - eye[2];
	return x * x + y * y + z * z >= rule.minDistance * rule.minDistance ? Taken::Hidden : Taken::No;
}

struct ViewCounts {
	std::uint64_t inside[kViewBands] = {};
	std::uint64_t outside[kViewBands] = {};  // in front, outside the cone
	std::uint64_t behind[kViewBands] = {};
	void Add(const ViewOf& view) {
		(view.side == ViewSide::Inside ? inside : view.side == ViewSide::Outside ? outside : behind)[view.band] += 1;
	}
	std::uint64_t All() const {
		std::uint64_t n = 0;
		for (unsigned b = 0; b < kViewBands; ++b) {
			n += inside[b] + outside[b] + behind[b];
		}
		return n;
	}
	std::uint64_t Unseen(unsigned fromBand) const {
		std::uint64_t n = 0;
		for (unsigned b = fromBand; b < kViewBands; ++b) {
			n += outside[b] + behind[b];
		}
		return n;
	}
};

}  // namespace obng::engine::scene::actor_far_update
