#pragma once

// [Probe] ActorCensus: which actors the engine updates in a frame - the
// counting and the lines, over plain values (ActorCensus.cpp reads them from
// the game at Actor::Update 0x00603CA0's entry).
//
// One sample is one frame's actors: collecting starts at the first update
// after the summary asked for a sample and ends when an actor comes by a
// second time (the next frame's loop has begun) or the table is full.
//
// 64-bit clean: the actor is a std::uint32_t handle.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obng::plugin::census {

constexpr std::size_t kActorsAtMost = 256;
// Distance classes in game units (about 70 to a metre): to 1000, 2000, 4000, 8000, beyond.
constexpr float kDistanceBounds[] = {1000.0f, 2000.0f, 4000.0f, 8000.0f};
constexpr std::size_t kDistanceClasses = 5;

struct Seen {
	std::uint32_t actor = 0;     // the handle, for telling a second visit
	std::uint32_t refId = 0;     // the reference's form id
	std::uint32_t baseId = 0;    // its base form's id, 0 without a base
	std::uint8_t baseType = 0;   // the base form's type byte
	bool hasNode = false;        // the reference has a scene node
	bool culled = false;         // the node's flags bit 0 (hidden from the cull walk)
	float distance = 0.0f;       // from the player's position
};

inline float Distance(const float a[3], const float b[3]) {
	const float x = a[0] - b[0];
	const float y = a[1] - b[1];
	const float z = a[2] - b[2];
	return std::sqrt(x * x + y * y + z * z);
}

inline std::size_t DistanceClass(float distance) {
	for (std::size_t i = 0; i < kDistanceClasses - 1; ++i) {
		if (distance <= kDistanceBounds[i]) {
			return i;
		}
	}
	return kDistanceClasses - 1;
}

enum class State : std::uint8_t { Idle, Wanted, Collecting, Ready };

class Sample {
public:
	State Now() const { return state_; }
	std::size_t Size() const { return count_; }
	const Seen& At(std::size_t i) const { return seen_[i]; }

	// The summary asks for a frame's actors (nothing while one is collected or waits to be read).
	void Ask() {
		if (state_ == State::Idle) {
			state_ = State::Wanted;
		}
	}

	// An actor's update begins. Returns whether the caller should describe
	// this actor with Add - false while no sample is wanted, and when this
	// actor closes the sample.
	bool Visit(std::uint32_t actor) {
		if (state_ == State::Wanted) {
			count_ = 0;
			state_ = State::Collecting;
		}
		if (state_ != State::Collecting) {
			return false;
		}
		for (std::size_t i = 0; i < count_; ++i) {
			if (seen_[i].actor == actor) {
				state_ = State::Ready;  // the next frame's loop
				return false;
			}
		}
		if (count_ >= kActorsAtMost) {
			state_ = State::Ready;
			return false;
		}
		return true;
	}

	void Add(const Seen& s) {
		if (state_ == State::Collecting && count_ < kActorsAtMost) {
			seen_[count_++] = s;
		}
	}

	// The summary has read the sample.
	void Taken() {
		if (state_ == State::Ready) {
			state_ = State::Idle;
		}
	}

private:
	State state_ = State::Idle;
	std::size_t count_ = 0;
	Seen seen_[kActorsAtMost] = {};
};

struct Counts {
	std::size_t actors = 0;
	std::size_t byDistance[kDistanceClasses] = {};
	std::size_t culledByDistance[kDistanceClasses] = {};
	std::size_t noNode = 0;
	std::size_t culled = 0;
	// The base forms' type bytes and the plugins (the base id's top byte), the eight most frequent each.
	std::uint8_t type[8] = {};
	std::size_t typeCount[8] = {};
	std::size_t types = 0;
	std::uint8_t plugin[8] = {};
	std::size_t pluginCount[8] = {};
	std::size_t plugins = 0;
};

namespace detail {
inline void Tally(std::uint8_t key, std::uint8_t* keys, std::size_t* counts, std::size_t* used) {
	for (std::size_t i = 0; i < *used; ++i) {
		if (keys[i] == key) {
			++counts[i];
			return;
		}
	}
	if (*used < 8) {
		keys[*used] = key;
		counts[*used] = 1;
		++*used;
	}
}
}  // namespace detail

inline Counts Count(const Sample& sample) {
	Counts c;
	c.actors = sample.Size();
	for (std::size_t i = 0; i < sample.Size(); ++i) {
		const Seen& s = sample.At(i);
		const std::size_t d = DistanceClass(s.distance);
		++c.byDistance[d];
		if (!s.hasNode) {
			++c.noNode;
		} else if (s.culled) {
			++c.culled;
			++c.culledByDistance[d];
		}
		detail::Tally(s.baseType, c.type, c.typeCount, &c.types);
		detail::Tally(static_cast<std::uint8_t>(s.baseId >> 24), c.plugin, c.pluginCount, &c.plugins);
	}
	return c;
}

// "Census: N actors updated in a frame - by distance to 1000 / 2000 / 4000 /
// 8000 / beyond: a b c d e (their nodes culled: a b c d e); N nodes culled,
// N without a node; base types TT:n ...; base plugins PP:n ..."
inline int FormatCounts(const Counts& c, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity,
	                       "Census: %zu actors updated in a frame - by distance to 1000 / 2000 / 4000 / 8000 / beyond: %zu %zu %zu %zu %zu "
	                       "(their nodes culled: %zu %zu %zu %zu %zu); %zu nodes culled, %zu without a node; base types",
	                       c.actors, c.byDistance[0], c.byDistance[1], c.byDistance[2], c.byDistance[3], c.byDistance[4], c.culledByDistance[0],
	                       c.culledByDistance[1], c.culledByDistance[2], c.culledByDistance[3], c.culledByDistance[4], c.culled, c.noNode);
	for (std::size_t i = 0; i < c.types && at > 0 && static_cast<std::size_t>(at) < capacity; ++i) {
		at += std::snprintf(out + at, capacity - at, " %02X:%zu", c.type[i], c.typeCount[i]);
	}
	if (at > 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - at, "; base plugins");
	}
	for (std::size_t i = 0; i < c.plugins && at > 0 && static_cast<std::size_t>(at) < capacity; ++i) {
		at += std::snprintf(out + at, capacity - at, " %02X:%zu", c.plugin[i], c.pluginCount[i]);
	}
	return at;
}

// "Census actors: ref/base/type distance[c|-] ..." for the actors from
// `from` on, as many as fit; returns the index after the last one written.
inline std::size_t FormatActors(const Sample& sample, std::size_t from, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity, "Census actors:");
	std::size_t i = from;
	for (; i < sample.Size(); ++i) {
		const Seen& s = sample.At(i);
		char one[48];
		const int n = std::snprintf(one, sizeof(one), " %08X/%08X/%02X %.0f%s", s.refId, s.baseId, s.baseType, static_cast<double>(s.distance),
		                            !s.hasNode ? "n" : s.culled ? "c" : "");
		if (at < 0 || static_cast<std::size_t>(at) + static_cast<std::size_t>(n) + 1 > capacity) {
			break;
		}
		std::memcpy(out + at, one, static_cast<std::size_t>(n) + 1);
		at += n;
	}
	return i;
}

// The thunk at Actor::Update's entry, to execute at `base`: the actor (ECX)
// handed to `note` and on into the original, every register as it came:
//   push eax / push ecx / push edx
//   push ecx
//   call note                          void __cdecl(std::uint32_t actor)
//   lea esp,[esp+4]
//   pop edx / pop ecx / pop eax
//   jmp original
constexpr std::uint32_t kNoteThunkSize = 21;
inline std::uint32_t BuildNoteThunk(std::uint8_t* buffer, std::uint32_t capacity, std::uint32_t base, std::uint32_t note, std::uint32_t original) {
	if (capacity < kNoteThunkSize) {
		return 0;
	}
	std::uint8_t* p = buffer;
	*p++ = 0x50;
	*p++ = 0x51;
	*p++ = 0x52;
	*p++ = 0x51;
	*p++ = 0xE8;
	std::uint32_t rel = note - (base + static_cast<std::uint32_t>(p - buffer) + 4);
	std::memcpy(p, &rel, 4);
	p += 4;
	*p++ = 0x8D;
	*p++ = 0x64;
	*p++ = 0x24;
	*p++ = 0x04;
	*p++ = 0x5A;
	*p++ = 0x59;
	*p++ = 0x58;
	*p++ = 0xE9;
	rel = original - (base + static_cast<std::uint32_t>(p - buffer) + 4);
	std::memcpy(p, &rel, 4);
	p += 4;
	return static_cast<std::uint32_t>(p - buffer);
}

}  // namespace obng::plugin::census
