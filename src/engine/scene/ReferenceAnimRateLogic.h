#pragma once
// The references' animation update at a lower rate for the references that
// have nothing to animate - the decisions, over plain values ([Route]
// ReferenceAnimEvery in the plugin's ReferenceAnimRate.cpp).
//
// TESObjectCELL::UpdateReferenceAnimations 0x004CCC50 (proposal for the
// name) calls slot +0x160 of every reference of every loaded cell once a
// frame (measured 2026-11-12 at Anvil's docks with a large mod list: 25
// cells, 4 760 references a frame, 0.9-1.7 ms). For a plain reference that
// is 0x004E3F30, which finds out by a chain of dependent loads whether there
// is anything to do, and for nearly every reference there is not: no
// controller manager with running sequences on the node's first child, and
// no animation data to step.
//
// A reference found with nothing to do is asked again only in one frame of
// n; one found with something to do is called every frame, as before. What
// that changes on purpose (class C): a reference that starts to need the call
// (a sequence set going, animation data attached) is noticed up to n - 1
// frames late. The frame of n comes from the reference's handle, so a cell's
// references are not all asked together.
//
// 64-bit clean: a reference is a std::uint32_t handle.
#include <cstdint>

namespace obng::engine::scene::reference_anim_rate {

// The phase of a reference among n turns, from its handle (objects are
// aligned, so the low bits say nothing).
constexpr std::uint32_t PhaseOf(std::uint32_t reference, std::uint32_t every) {
	return every > 1 ? (reference >> 4) % every : 0u;
}

enum class Call : std::uint8_t {
	Run,   // call the original, then say what was found (Table::Found)
	Skip,  // leave the call out
};

struct Counts {
	std::uint64_t calls = 0;
	std::uint64_t skipped = 0;
	std::uint64_t runBusy = 0;       // run because the reference had something to do last time
	std::uint64_t runTurn = 0;       // run because it was the idle reference's turn
	std::uint64_t runNew = 0;        // run because the table did not know it
	std::uint64_t runNoRoom = 0;     // run because the table had no slot for it
	std::uint64_t becameBusy = 0;    // an idle reference found with something to do on its turn: noticed late
	std::uint64_t becameIdle = 0;
};

// What is known about the references: an open-addressed table by handle. A
// slot not touched for kStale frames may be taken over (its reference is no
// longer in a loaded cell, or its address belongs to another now - which a
// stale "idle" would hide for at most one turn, and a stale "busy" not at
// all).
class Table {
public:
	static constexpr std::uint32_t kSlots = 16384;  // a power of two
	static constexpr std::uint32_t kProbe = 16;
	static constexpr std::uint32_t kStale = 256;    // frames

	// One call of a reference's animation update in `frame`. every <= 1: the
	// route is off for this call - Run, and nothing is noted.
	Call Decide(std::uint32_t reference, std::uint32_t frame, std::uint32_t every, Counts& counts) {
		++counts.calls;
		pending_ = nullptr;
		if (every <= 1 || reference == 0) {
			return Call::Run;
		}
		bool isNew = false;
		Slot* slot = Claim(reference, frame, isNew);
		if (slot == nullptr) {
			++counts.runNoRoom;
			return Call::Run;
		}
		slot->seenFrame = frame;
		pending_ = slot;
		if (isNew) {
			++counts.runNew;
			return Call::Run;
		}
		if (slot->busy) {
			++counts.runBusy;
			return Call::Run;
		}
		if ((frame + PhaseOf(reference, every)) % every == 0) {
			++counts.runTurn;
			return Call::Run;
		}
		pending_ = nullptr;
		++counts.skipped;
		return Call::Skip;
	}

	// After a call that Decide let run: whether the reference has something
	// to do every frame. Without a slot from the last Decide: nothing.
	void Found(bool busy, Counts& counts) {
		if (pending_ == nullptr) {
			return;
		}
		if (pending_->known && busy != pending_->busy) {
			++(busy ? counts.becameBusy : counts.becameIdle);
		}
		pending_->busy = busy;
		pending_->known = true;
		pending_ = nullptr;
	}

	// For the tests and the summary: how many slots are in use.
	std::uint32_t Used() const {
		std::uint32_t n = 0;
		for (const Slot& s : slots_) {
			n += s.used ? 1u : 0u;
		}
		return n;
	}

private:
	struct Slot {
		std::uint32_t reference = 0;
		std::uint32_t seenFrame = 0;
		bool used = false;
		bool known = false;  // Found has been called for it
		bool busy = false;
	};
	static constexpr std::uint32_t Home(std::uint32_t reference) { return (reference * 2654435761u) >> 18; }

	// The reference's slot, or a free or stale one made its own (isNew);
	// null when the probe run is all in use by others seen lately.
	Slot* Claim(std::uint32_t reference, std::uint32_t frame, bool& isNew) {
		Slot* spare = nullptr;
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			Slot& s = slots_[(Home(reference) + i) & (kSlots - 1)];
			if (s.used && s.reference == reference) {
				// Not seen for long: what is known may be about another object at this address.
				if (frame - s.seenFrame > kStale) {
					s.known = false;
					s.busy = false;
					isNew = true;
				}
				return &s;
			}
			if (spare == nullptr && (!s.used || frame - s.seenFrame > kStale)) {
				spare = &s;
			}
		}
		if (spare != nullptr) {
			*spare = Slot{};
			spare->reference = reference;
			spare->used = true;
			isNew = true;
		}
		return spare;
	}

	Slot slots_[kSlots];
	Slot* pending_ = nullptr;
};
static_assert((Table::kSlots & (Table::kSlots - 1)) == 0, "the table's size is a power of two");

// The frame counter: a frame is one change of the engine's own frame clock
// (TimeInfo's clockMs, knowledge/frame-clock.md) - not one call of anything.
struct Clock {
	std::uint32_t frame = 0;
	std::uint32_t lastClockMs = 0;
	bool seen = false;
	constexpr void See(std::uint32_t clockMs) {
		if (!seen || clockMs != lastClockMs) {
			seen = true;
			lastClockMs = clockMs;
			++frame;
		}
	}
};

}  // namespace obng::engine::scene::reference_anim_rate
