#pragma once
// [Timed] WritesOf: which statics of the EXE a timed function's call writes
// (TimedWrites.cpp copies the EXE's .data when a watched call begins and when
// it returns, and counts the places that differ). This header decides which
// call is probed and when its probe ends, from what the timed calls' frames
// say (TimedCallLogic.h): the depth of the frames after an entry or a return.
//
// One probe is open at a time: a watched call inside a probed one counts into
// the outer one. A probed call whose frame went without returning (an unwind
// past it) is abandoned, not counted. A measurement; it sees the statics
// only - what a call writes into objects of the heap it does not see.

#include <cstddef>
#include <cstdint>

namespace obng::plugin::timed_writes {

constexpr std::size_t kWatchedAtMost = 6;
constexpr int kNone = -1;

class Probe {
public:
	// The timed functions (by their index) to watch, and every which call of
	// each is probed (0 counts as 1). More than kWatchedAtMost are cut off.
	void Set(const std::uint32_t* indices, std::size_t count, std::uint32_t every) {
		*this = Probe{};
		count_ = count < kWatchedAtMost ? count : kWatchedAtMost;
		for (std::size_t i = 0; i < count_; ++i) {
			index_[i] = indices[i];
		}
		every_ = every == 0 ? 1 : every;
	}

	// A timed function was entered; `depthAfter` is the frames' depth after
	// it and `pushed` whether it got a frame of its own with its return
	// redirected (Calls::Enter's true). True: this call is probed from now.
	bool Entered(std::uint32_t index, std::size_t depthAfter, bool pushed) {
		if (open_) {
			// Still inside the probed call: its frame is there and this one lies above it.
			if (depthAfter > openDepth_ || (depthAfter == openDepth_ && !pushed)) {
				return false;
			}
			open_ = false;
			++abandoned_;
		}
		if (!pushed) {
			return false;
		}
		const int w = WatchOf(index);
		if (w == kNone) {
			return false;
		}
		if (calls_[w]++ % every_ != 0) {
			return false;
		}
		open_ = true;
		openDepth_ = depthAfter;
		openWatch_ = w;
		return true;
	}

	// A timed function returned; `depthAfter` is the frames' depth after it.
	// The watch (0 .. count-1) whose probe ends with it, or kNone.
	int Exited(std::size_t depthAfter) {
		if (!open_ || depthAfter >= openDepth_) {
			return kNone;
		}
		open_ = false;
		if (depthAfter + 1 != openDepth_) {
			++abandoned_;  // an outer call returned over the probed one
			return kNone;
		}
		++probes_[openWatch_];
		return openWatch_;
	}

	std::size_t Count() const { return count_; }
	std::uint32_t IndexOf(std::size_t watch) const { return index_[watch]; }
	std::uint64_t Calls(std::size_t watch) const { return calls_[watch]; }
	std::uint64_t Probes(std::size_t watch) const { return probes_[watch]; }
	std::uint64_t Abandoned() const { return abandoned_; }
	bool Open() const { return open_; }

private:
	int WatchOf(std::uint32_t index) const {
		for (std::size_t i = 0; i < count_; ++i) {
			if (index_[i] == index) {
				return static_cast<int>(i);
			}
		}
		return kNone;
	}

	std::uint32_t index_[kWatchedAtMost] = {};
	std::size_t count_ = 0;
	std::uint32_t every_ = 1;
	std::uint64_t calls_[kWatchedAtMost] = {};
	std::uint64_t probes_[kWatchedAtMost] = {};
	std::uint64_t abandoned_ = 0;
	bool open_ = false;
	std::size_t openDepth_ = 0;
	int openWatch_ = 0;
};

// The names of a "a,b,c" list, each cut out into `out` (at most `capacity`
// names of at most `nameSize - 1` characters; blanks around a name dropped,
// empty names skipped). Returns how many.
inline std::size_t SplitNames(const char* list, char* out, std::size_t nameSize, std::size_t capacity) {
	std::size_t n = 0;
	const char* p = list;
	while (*p != '\0' && n < capacity) {
		while (*p == ' ' || *p == ',') {
			++p;
		}
		const char* end = p;
		while (*end != '\0' && *end != ',') {
			++end;
		}
		const char* last = end;
		while (last > p && last[-1] == ' ') {
			--last;
		}
		const std::size_t length = static_cast<std::size_t>(last - p);
		if (length != 0) {
			const std::size_t kept = length < nameSize - 1 ? length : nameSize - 1;
			char* name = out + n * nameSize;
			for (std::size_t i = 0; i < kept; ++i) {
				name[i] = p[i];
			}
			name[kept] = '\0';
			++n;
		}
		p = end;
	}
	return n;
}

}  // namespace obng::plugin::timed_writes
