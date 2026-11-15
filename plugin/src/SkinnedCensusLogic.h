#pragma once

// [Probe] SkinnedCensus: how many skinned draws an actor costs, and how many
// would be left if its parts were drawn together. Stage 0 of
// docs/subsystems/render-prep-parallel.md ("one draw an actor"): the question
// before anything is built is how the 1 000-1 800 skinned draws a pass of a
// horde divide - by actor (the skin's root), by part (the geometry), by the
// passes a part is drawn in (calls per geometry), and by shader class.
//
// Per eye pass every skinned draw call inside the pass is noted with its
// skeleton root, its geometry and its shader's class (the vtable). At the
// pass's end the frame is counted: actors, parts, calls, and the calls that
// would be left under two merges - every part of an actor that shares a shader
// class drawn as one, per pass it is drawn in ("by class"), and every part of
// an actor as one ("whole"). The passes a merged group needs are the most
// calls any one of its parts had (a part drawn in three passes keeps three).
//
// A third merge is the one that can be built without new textures: every part
// of an actor that was drawn with the same textures and programs as one ("by
// look"; the look is the caller's number for what the device held at the
// call - 0 when it is not told, then all of an actor's parts count as one
// look). The looks an actor has say how many draws it needs at the least
// without an atlas.
// Pure; SkinnedCensusLogicTest.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::skinned_census {

constexpr std::uint32_t kParts = 4096;   // distinct (root, geometry) a frame; a power of two
constexpr std::uint32_t kGroups = 2048;  // distinct (root, shader class) a frame
constexpr std::uint32_t kLooks = 4096;   // distinct (root, look) a frame
constexpr std::uint32_t kActors = 512;   // distinct roots a frame
constexpr std::uint32_t kProbe = 32;

struct Totals {
	std::uint64_t frames = 0;
	std::uint64_t calls = 0;
	std::uint64_t parts = 0;
	std::uint64_t actors = 0;
	std::uint64_t byClass = 0;       // calls left with an actor's parts of one shader class drawn as one
	std::uint64_t whole = 0;         // calls left with an actor's parts drawn as one
	std::uint64_t byLook = 0;        // calls left with an actor's parts of one look drawn as one
	std::uint64_t looks = 0;         // distinct (actor, look)
	std::uint32_t mostLooksAnActor = 0;
	std::uint64_t dropped = 0;       // calls not counted: a table was full
	std::uint32_t mostCallsAnActor = 0;
	std::uint32_t mostPartsAnActor = 0;
};

class Frame {
public:
	// One skinned draw call: the skin's root (the actor), the geometry (the part), the shader's vtable (its class).
	// `look`: the caller's number for the textures and programs the call was drawn with.
	void Note(std::uint32_t root, std::uint32_t geometry, std::uint32_t shaderClass, std::uint32_t look = 0) {
		Slot* part = Claim(parts_, kParts, root, geometry);
		Slot* group = Claim(groups_, kGroups, root, shaderClass);
		Slot* actor = Claim(actors_, kActors, root, 0);
		bool newLook = false;
		Slot* seen = part != nullptr && group != nullptr && actor != nullptr ? Claim(looks_, kLooks, root, look, &newLook) : nullptr;
		if (seen == nullptr) {
			++dropped_;
			return;
		}
		++calls_;
		if (++part->calls == 1) {
			++actor->parts;
		}
		++actor->calls;
		group->most = part->calls > group->most ? part->calls : group->most;
		seen->most = part->calls > seen->most ? part->calls : seen->most;
		actor->looks += newLook ? 1u : 0u;
		actor->most = part->calls > actor->most ? part->calls : actor->most;
	}

	bool Empty() const { return calls_ == 0 && dropped_ == 0; }

	// The frame added to the totals, and the frame cleared.
	void End(Totals* t) {
		++t->frames;
		t->calls += calls_;
		t->dropped += dropped_;
		for (std::uint32_t i = 0; i < kParts; ++i) {
			t->parts += parts_[i].used ? 1u : 0u;
		}
		for (std::uint32_t i = 0; i < kGroups; ++i) {
			t->byClass += groups_[i].used ? groups_[i].most : 0u;
		}
		for (std::uint32_t i = 0; i < kLooks; ++i) {
			t->byLook += looks_[i].used ? looks_[i].most : 0u;
			t->looks += looks_[i].used ? 1u : 0u;
		}
		for (std::uint32_t i = 0; i < kActors; ++i) {
			const Slot& a = actors_[i];
			if (!a.used) {
				continue;
			}
			++t->actors;
			t->whole += a.most;
			t->mostCallsAnActor = a.calls > t->mostCallsAnActor ? a.calls : t->mostCallsAnActor;
			t->mostPartsAnActor = a.parts > t->mostPartsAnActor ? a.parts : t->mostPartsAnActor;
			t->mostLooksAnActor = a.looks > t->mostLooksAnActor ? a.looks : t->mostLooksAnActor;
		}
		*this = Frame{};
	}

private:
	struct Slot {
		std::uint32_t a = 0;
		std::uint32_t b = 0;
		std::uint32_t calls = 0;  // a part's calls; an actor's calls
		std::uint32_t parts = 0;  // an actor's parts
		std::uint32_t most = 0;   // the most calls of any part in the group / of the actor
		std::uint32_t looks = 0;  // an actor's looks
		bool used = false;
	};
	static Slot* Claim(Slot* table, std::uint32_t size, std::uint32_t a, std::uint32_t b, bool* fresh = nullptr) {
		const std::uint32_t home = ((a >> 3) * 2654435761u ^ (b >> 3) * 2246822519u) & (size - 1);
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			Slot& s = table[(home + i) & (size - 1)];
			if (!s.used) {
				s.used = true;
				s.a = a;
				s.b = b;
				if (fresh != nullptr) {
					*fresh = true;
				}
				return &s;
			}
			if (s.a == a && s.b == b) {
				return &s;
			}
		}
		return nullptr;
	}
	Slot parts_[kParts];
	Slot groups_[kGroups];
	Slot looks_[kLooks];
	Slot actors_[kActors];
	std::uint32_t calls_ = 0;
	std::uint32_t dropped_ = 0;
};

inline int Format(const Totals& t, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return 0;
	}
	const double f = t.frames == 0 ? 1.0 : static_cast<double>(t.frames);
	const double a = t.actors == 0 ? 1.0 : static_cast<double>(t.actors);
	const double p = t.parts == 0 ? 1.0 : static_cast<double>(t.parts);
	return std::snprintf(out, capacity,
	                     "Probe: skinned census - %llu eye passes; a pass: %.1f skinned draw calls of %.1f actors in %.1f parts (%.1f calls and %.1f "
	                     "parts an actor, %.2f calls a part; the most an actor %u calls, %u parts); left with an actor's parts of one shader class "
	                     "drawn as one: %.1f calls a pass (%.1f an actor), with the whole actor as one: %.1f (%.1f an actor); %llu calls not counted "
	                     "(a table full)",
	                     static_cast<unsigned long long>(t.frames), static_cast<double>(t.calls) / f, static_cast<double>(t.actors) / f,
	                     static_cast<double>(t.parts) / f, static_cast<double>(t.calls) / a, static_cast<double>(t.parts) / a,
	                     static_cast<double>(t.calls) / p, t.mostCallsAnActor, t.mostPartsAnActor, static_cast<double>(t.byClass) / f,
	                     static_cast<double>(t.byClass) / a, static_cast<double>(t.whole) / f, static_cast<double>(t.whole) / a,
	                     static_cast<unsigned long long>(t.dropped));
}

// The second line: the looks, and the time of the skinned draw calls (`ms` in all of the window's calls; below 0
// writes no time).
inline int FormatLooks(const Totals& t, double ms, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return 0;
	}
	const double f = t.frames == 0 ? 1.0 : static_cast<double>(t.frames);
	const double a = t.actors == 0 ? 1.0 : static_cast<double>(t.actors);
	const double c = t.calls == 0 ? 1.0 : static_cast<double>(t.calls);
	int used = std::snprintf(out, capacity,
	                         "Probe: skinned census looks - %.1f looks an actor (textures and programs the same; the most an actor %u); left with an "
	                         "actor's parts of one look drawn as one: %.1f calls a pass (%.1f an actor)",
	                         static_cast<double>(t.looks) / a, t.mostLooksAnActor, static_cast<double>(t.byLook) / f, static_cast<double>(t.byLook) / a);
	if (ms >= 0.0 && used > 0 && static_cast<std::size_t>(used) < capacity) {
		used += std::snprintf(out + used, capacity - static_cast<std::size_t>(used), "; the skinned draw calls took %.3f ms a pass (%.1f us a call)",
		                      ms / f, ms * 1000.0 / c);
	}
	return used;
}

}  // namespace obng::plugin::skinned_census
