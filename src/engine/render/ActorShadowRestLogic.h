#pragma once
// The actor shadows at rest - the decisions, over plain values ([Route]
// ActorShadowEvery in the plugin's ActorShadowRest.cpp).
//
// The actor shadow step 0x004073D0 runs once a frame (knowledge/actor-shadows.md,
// re/functions/0x004073D0.md): its reset 0x007C5BE0 gives every shadow
// light's map (+0x114) back to the pool [0x00B42F50] and hides the light,
// the casters' registration 0x007C6C30 shows again the lights of the casters
// it accepts, the light walk places each shown light at its caster
// (0x007D2280), sets its camera and fade (0x007D6390) and collects the
// geometry in its reach (0x007D5ED0, 0x007D6900, 0x007D6A40), and 0x007C6770
// draws the map of every shown light (0x007D59C0 -> 0x007D46C0, which takes a
// map from the pool when the light holds none and stores the light's
// projection at +0x10). All read 2026-11-12.
//
// At rest, a light keeps its map, its projection and its geometry from the
// step it was last drawn in: the reset does not give its map back, the walk
// counts it without placing it, the map render leaves it out. What that
// changes on purpose (class C): a resting light's shadow is up to n - 1 steps
// old. Its tolerance is stated and held by construction: a light rests only
// when its caster's bound centre is within `still` units of where it was at
// the light's last draw, it was drawn or at rest in the step before, it is
// faded in, in view and holds its map; it is drawn again on its turn, one step
// of n (the turns spread the lights over the steps).
//
// 64-bit clean: lights, casters and maps are std::uint32_t handles.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::engine::render::actor_shadow_rest {

// What the plugin reads of one shadow light, as plain values.
struct LightView {
	std::uint32_t light = 0;
	std::uint32_t caster = 0;  // +0x130, the caster's node
	float center[3] = {};      // the caster node's world bound centre (+0x20..+0x28)
	std::uint32_t map = 0;     // +0x114, the map it holds
	bool visible = false;      // +0x118 != 0xFF: in the camera's view at its last fade update
	float fade = 0.0f;         // +0xD8
	float fadeTarget = 0.0f;   // +0xDC (1 in, 0 out)
	float fadeTimer = 0.0f;    // +0xE0, the time since the target was set
	float fadeTime = 1.0f;     // [0x00B2C680]
	bool actorKind = false;    // +0xF5 == 0: drawn by 0x007D46C0, as 0x007C6C30 makes the casters' lights
	float source[3] = {};      // the light's NiLight (+0x100) world translate (+0x88), where the placement put it
};

struct Settings {
	std::uint32_t every = 1;  // a still light's map is drawn in one step of n; <= 1 off
	float still = 4.0f;       // units its caster may move from its last draw and still rest
	bool alternate = false;   // on in blocks of n steps, off in the next n (a measurement)
};

// Why a light is or is not kept at the reset.
enum class Why : std::uint8_t {
	kKept,
	kOff,            // the rest is off, or off in this step (the alternation)
	kUnsafe,         // the map count changed or the step follows a gap
	kOtherKind,      // not a casters' light (+0xF5 set)
	kNew,            // not drawn or at rest in the step before
	kCasterChanged,  // another caster than at its last draw
	kNoMap,
	kNotVisible,
	kFaint,          // fade below 0.05: the walk would hide it
	kFading,         // fading in or out
	kMoved,          // its caster moved more than `still`
	kLightMoves,     // its source moved against the caster between its last draws (a carried torch)
	kTurn,           // its turn to be drawn
	kCount
};

constexpr const char* kWhyName[static_cast<int>(Why::kCount)] = {
	"kept", "off", "unsafe step", "other kind", "new", "caster changed", "no map", "not in view", "faint", "fading", "moved", "light moves", "its turn"};

// Whether the rest is on in a step: always, or with the alternation in the
// first n of every 2n steps.
constexpr bool OnInStep(std::uint32_t step, std::uint32_t every, bool alternate) {
	return every > 1 && (!alternate || (step / every) % 2 == 0);
}

// Whether a light may keep its map through this step's reset. The pool is
// resized right after the reset (0x007C2710 to the step's map count, which
// pops a shrinking pool's maps off its free list without a check, read in
// 0x007C2710): with the count changed the kept maps could be missing there.
// A gap (a loading screen, a menu; gapMs < 0 for no step before) leaves the
// kept maps' age unknown.
constexpr bool SafeStep(std::uint32_t count, std::uint32_t lastCount, double gapMs, double maxGapMs) {
	return count == lastCount && gapMs >= 0.0 && gapMs <= maxGapMs;
}

struct Counts {
	std::uint64_t steps[2] = {};      // by side: [0] off, [1] on
	std::uint64_t stepTicks[2] = {};  // the step's own time by side (TSC ticks)
	std::uint64_t drawn[2] = {};      // lights whose map was drawn, by side
	std::uint64_t rested = 0;
	std::uint64_t kept = 0;
	std::uint64_t released = 0;       // kept, then not at rest: given back before the map render
	std::uint64_t fadingAtDraw = 0;   // kept, then shown by the walk to fade out: drawn into the map it holds
	std::uint64_t notKept[static_cast<int>(Why::kCount)] = {};
	std::uint64_t noSlot = 0;
};

class Table {
public:
	static constexpr std::uint32_t kSlots = 32;
	static constexpr std::uint32_t kNoTurn = 0xFFFFFFFFu;
	// The light's source against its caster: moved more than kSourceStill
	// units between two draws, the light is drawn every step until kCalmDraws
	// draws in a row found it standing (Nadi, Windows 2026-11-12: the shadows
	// of NPCs carrying a torch flickered - a shadow redrawn every 4th step
	// jumps when its light moves).
	static constexpr float kSourceStill = 1.0f;
	static constexpr std::uint32_t kCalmDraws = 30;

	explicit Table(const Settings& settings) : settings_(settings) {}

	bool On() const { return settings_.every > 1; }
	const Settings& settings() const { return settings_; }

	// A step begins: its index (from 1, rising by one a step), whether the
	// rest is on in it, whether keeping maps is safe in it (SafeStep).
	void BeginStep(std::uint32_t step, bool on, bool safe) {
		step_ = step;
		on_ = On() && on;
		safe_ = safe;
		++counts.steps[on_ ? 1 : 0];
	}

	void AddStepTicks(bool on, std::uint64_t ticks) { counts.stepTicks[on ? 1 : 0] += ticks; }

	// At the reset, for a light whose NiLight is not null: kKept = its map
	// stays with it.
	Why Keep(const LightView& v) {
		const Why why = Decide(v);
		++counts.notKept[static_cast<int>(why)];
		if (why == Why::kKept) {
			++counts.kept;
			Find(v.light)->kept = step_;
		}
		return why;
	}

	// At the walk's draw block, for a shown light under the count with its
	// caster shown: true = at rest (counted as drawn, not placed). A kept
	// light whose caster was not registered reaches the block too: the walk
	// shows it again with the fade target 0 to fade it out (0x004073D0,
	// 0x0040777x-0x00407868 with 0x007D1ED0) - drawn, so the fade goes on.
	bool Rest(std::uint32_t light, std::uint32_t caster, float fadeTarget) {
		Entry* e = Find(light);
		if (e == nullptr || e->kept != step_ || step_ == 0 || e->caster != caster) {
			return false;
		}
		if (fadeTarget != 1.0f) {
			++counts.fadingAtDraw;
			return false;
		}
		e->rested = step_;
		e->seen = step_;
		++counts.rested;
		return true;
	}

	// At the map render: whether its map is left as it is.
	bool Rested(std::uint32_t light) const {
		const Entry* e = Find(light);
		return e != nullptr && step_ != 0 && e->rested == step_;
	}

	// After the map render ran for a light: with a map it was drawn now.
	void Drawn(const LightView& v) {
		if (v.map == 0 || v.light == 0) {
			return;
		}
		Entry* e = Find(v.light);
		if (e == nullptr) {
			e = Claim(v.light);
			if (e == nullptr) {
				++counts.noSlot;
				return;
			}
		}
		e->caster = v.caster;
		e->center[0] = v.center[0];
		e->center[1] = v.center[1];
		e->center[2] = v.center[2];
		const float offset[3] = {v.source[0] - v.center[0], v.source[1] - v.center[1], v.source[2] - v.center[2]};
		if (e->drawnOnce) {
			const float dx = offset[0] - e->offset[0];
			const float dy = offset[1] - e->offset[1];
			const float dz = offset[2] - e->offset[2];
			if (!(dx * dx + dy * dy + dz * dz <= kSourceStill * kSourceStill)) {
				e->restless = kCalmDraws;  // NaN counts as moved
			} else if (e->restless != 0) {
				--e->restless;
			}
		}
		e->offset[0] = offset[0];
		e->offset[1] = offset[1];
		e->offset[2] = offset[2];
		e->drawnOnce = true;
		e->seen = step_;
		++counts.drawn[on_ ? 1 : 0];
	}

	// The lights kept at this step's reset that did not come to rest: their
	// maps go back to the pool before the map render (Released after each).
	std::uint32_t Unrested(std::uint32_t* out, std::uint32_t capacity) const {
		std::uint32_t n = 0;
		for (const Entry& e : slots_) {
			if (e.light != 0 && step_ != 0 && e.kept == step_ && e.rested != step_ && n < capacity) {
				out[n++] = e.light;
			}
		}
		return n;
	}

	void Released(std::uint32_t light) {
		Entry* e = Find(light);
		if (e != nullptr) {
			e->kept = 0;
			++counts.released;
		}
	}

	// For the tests and the summary: a known light's turn.
	std::uint32_t TurnOf(std::uint32_t light) const {
		const Entry* e = Find(light);
		return e != nullptr ? e->turn : kNoTurn;
	}

	void ResetCounts() { counts = Counts{}; }

	Counts counts;

private:
	struct Entry {
		std::uint32_t light = 0;
		std::uint32_t caster = 0;
		float center[3] = {};
		float offset[3] = {};      // the source less the caster's centre at its last draw
		bool drawnOnce = false;
		std::uint32_t restless = 0;  // draws still to come before it may rest again
		std::uint32_t turn = 0;
		std::uint32_t seen = 0;    // the last step it was drawn or at rest in
		std::uint32_t kept = 0;    // the step its map was kept through the reset
		std::uint32_t rested = 0;  // the step it was at rest in
	};

	Why Decide(const LightView& v) const {
		if (!on_) {
			return Why::kOff;
		}
		if (!safe_) {
			return Why::kUnsafe;
		}
		if (!v.actorKind) {
			return Why::kOtherKind;
		}
		const Entry* e = Find(v.light);
		if (e == nullptr || step_ == 0 || e->seen + 1 != step_) {
			return Why::kNew;
		}
		if (e->caster != v.caster) {
			return Why::kCasterChanged;
		}
		if (v.map == 0) {
			return Why::kNoMap;
		}
		if (!v.visible) {
			return Why::kNotVisible;
		}
		if (v.fade < 0.05f) {
			return Why::kFaint;
		}
		if (v.fadeTarget != 1.0f || v.fadeTimer < v.fadeTime) {
			return Why::kFading;
		}
		const float dx = v.center[0] - e->center[0];
		const float dy = v.center[1] - e->center[1];
		const float dz = v.center[2] - e->center[2];
		if (!(dx * dx + dy * dy + dz * dz <= settings_.still * settings_.still)) {
			return Why::kMoved;  // NaN counts as moved
		}
		if (e->restless != 0) {
			return Why::kLightMoves;
		}
		if ((step_ + e->turn) % settings_.every == 0) {
			return Why::kTurn;
		}
		return Why::kKept;
	}

	Entry* Find(std::uint32_t light) {
		for (Entry& e : slots_) {
			if (e.light == light && light != 0) {
				return &e;
			}
		}
		return nullptr;
	}
	const Entry* Find(std::uint32_t light) const {
		for (const Entry& e : slots_) {
			if (e.light == light && light != 0) {
				return &e;
			}
		}
		return nullptr;
	}

	// A free slot, or one whose light was not seen in this step or the one
	// before (gone, or drawn no more): the light's own from now.
	Entry* Claim(std::uint32_t light) {
		for (Entry& e : slots_) {
			if (e.light == 0 || e.seen + 1 < step_) {
				e = Entry{};
				e.light = light;
				e.turn = settings_.every > 1 ? nextTurn_++ % settings_.every : 0;
				return &e;
			}
		}
		return nullptr;
	}

	Settings settings_;
	Entry slots_[kSlots];
	std::uint32_t step_ = 0;
	std::uint32_t nextTurn_ = 0;
	bool on_ = false;
	bool safe_ = false;
};

// The summary's line: the settings, the steps and their time by side (ticks
// to ms by ticksPerMs), the lights a step, the reasons a light was not kept.
inline void FormatSummary(const Counts& c, const Settings& s, double ticksPerMs, char* out, std::size_t size) {
	const double on = c.steps[1] != 0 ? static_cast<double>(c.steps[1]) : 1.0;
	const double off = c.steps[0] != 0 ? static_cast<double>(c.steps[0]) : 1.0;
	const double perMs = ticksPerMs > 0.0 ? ticksPerMs : 1.0;
	int used = std::snprintf(out, size,
	                         "Route: actor shadows at rest (a still light's map drawn every %u. step, still = within %.1f units%s) - steps %llu on / %llu off; "
	                         "the step %.3f / %.3f ms; lights a step: drawn %.2f / %.2f, at rest %.2f, kept %.2f, released %.2f, fading out %llu, no slot %llu; not kept:",
	                         s.every, s.still, s.alternate ? ", alternating by blocks" : "", static_cast<unsigned long long>(c.steps[1]),
	                         static_cast<unsigned long long>(c.steps[0]), static_cast<double>(c.stepTicks[1]) / perMs / on,
	                         static_cast<double>(c.stepTicks[0]) / perMs / off, static_cast<double>(c.drawn[1]) / on, static_cast<double>(c.drawn[0]) / off,
	                         static_cast<double>(c.rested) / on, static_cast<double>(c.kept) / on, static_cast<double>(c.released) / on,
	                         static_cast<unsigned long long>(c.fadingAtDraw), static_cast<unsigned long long>(c.noSlot));
	for (int i = 1; i < static_cast<int>(Why::kCount) && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		if (c.notKept[i] != 0) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %llu;", kWhyName[i], static_cast<unsigned long long>(c.notKept[i]));
		}
	}
}

}  // namespace obng::engine::render::actor_shadow_rest
