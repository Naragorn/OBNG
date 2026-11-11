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

}  // namespace obng::engine::scene::actor_far_update
