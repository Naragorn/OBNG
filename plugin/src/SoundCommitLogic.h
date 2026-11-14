#pragma once
// [Route] SoundCommit: the sound update 0x006AE860 commits DirectSound's
// deferred 3D settings twice a frame (IDirectSound3DListener::
// CommitDeferredSettings at 0x006AEB03 after the listener's position and
// orientation, at 0x006AF5BF after the playing sounds' loop). On Windows a
// commit opens registry keys inside DirectSound: 0.73 and 0.23 ms a frame in
// a horde (knowledge/voice-lines.md). This header decides which of the two
// calls is made; SoundCommit.cpp patches the two sites and times the calls.
//
//   0 off: the sites stay the game's own.
//   1 timed: both commits made as the game makes them, and timed.
//   2 once: the first is left out - the second commits everything set since
//     the last commit, the listener's settings with it. A sound started
//     between the two sites hears the listener's last position for that one
//     frame's first buffer fill.
//   3 every second frame: as 2, and the second commit in every second frame
//     only - the sounds' positions follow at half the frame rate.
//   4 alternating: the summary's windows in turn as 1 and as 3, so that one
//     session gives both times under the same load.
//
// Class C (2 and 3: another moment for the same settings; nothing to compare
// with the original but the ear and the time).

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::sound_commit {

enum class Rule { kOff, kTimed, kOnce, kEverySecond, kAlternate };

inline Rule RuleFromIni(int value) {
	switch (value) {
		case 1: return Rule::kTimed;
		case 2: return Rule::kOnce;
		case 3: return Rule::kEverySecond;
		case 4: return Rule::kAlternate;
		default: return Rule::kOff;
	}
}

inline const char* RuleName(Rule rule) {
	switch (rule) {
		case Rule::kTimed: return "both commits, timed";
		case Rule::kOnce: return "one commit a frame";
		case Rule::kEverySecond: return "one commit every second frame";
		case Rule::kAlternate: return "alternating";
		default: return "off";
	}
}

struct Counts {
	std::uint64_t frames = 0;   // sound updates that reached the second site
	std::uint64_t commits = 0;
	std::uint64_t skipped = 0;
	std::uint64_t ticks = 0;    // in the commits made
	std::uint64_t longest = 0;
};

class State {
public:
	void Set(Rule rule) {
		*this = State{};
		rule_ = rule;
		now_ = rule == Rule::kAlternate ? Rule::kTimed : rule;
	}

	// A summary window ended: under the alternating rule the next one runs
	// the other way.
	void NextWindow() {
		if (rule_ == Rule::kAlternate) {
			now_ = now_ == Rule::kTimed ? Rule::kEverySecond : Rule::kTimed;
			skippedLast_ = true;  // the window's first frame commits
		}
	}

	// The first site: whether the commit is made.
	bool First() { return Count(now_ == Rule::kOff || now_ == Rule::kTimed); }

	// The second site, the frame's last: whether the commit is made.
	bool Second() {
		++counts_.frames;
		if (now_ != Rule::kEverySecond) {
			return Count(true);
		}
		const bool due = skippedLast_;  // never two frames in a row without a commit
		skippedLast_ = !due;
		return Count(due);
	}

	// A commit made took `ticks`.
	void Took(std::uint64_t ticks) {
		counts_.ticks += ticks;
		counts_.longest = ticks > counts_.longest ? ticks : counts_.longest;
	}

	Rule TheRule() const { return rule_; }
	Rule Now() const { return now_; }  // what this window runs as
	const Counts& Total() const { return counts_; }

private:
	bool Count(bool made) {
		++(made ? counts_.commits : counts_.skipped);
		return made;
	}

	Rule rule_ = Rule::kOff;
	Rule now_ = Rule::kOff;
	bool skippedLast_ = true;  // the first frame commits
	Counts counts_;
};

inline Counts Window(const Counts& now, const Counts& last) {
	Counts w;
	w.frames = now.frames - last.frames;
	w.commits = now.commits - last.commits;
	w.skipped = now.skipped - last.skipped;
	w.ticks = now.ticks - last.ticks;
	w.longest = now.longest;  // since the start
	return w;
}

// The summary line for a window; `ticksPerMs` 0 writes no times.
inline int FormatSummary(const Counts& w, Rule rule, double ticksPerMs, char* out, std::size_t size) {
	const double frames = w.frames != 0 ? static_cast<double>(w.frames) : 1.0;
	const double ms = ticksPerMs > 0.0 ? static_cast<double>(w.ticks) / ticksPerMs : 0.0;
	const double longest = ticksPerMs > 0.0 ? static_cast<double>(w.longest) / ticksPerMs : 0.0;
	return std::snprintf(out, size,
	                     "Route: sound commit (%s) - %llu sound updates, %llu commits made, %llu left out; %.3f ms a sound update in the commits "
	                     "(%.3f ms each, the longest since the start %.3f ms)",
	                     RuleName(rule), static_cast<unsigned long long>(w.frames), static_cast<unsigned long long>(w.commits),
	                     static_cast<unsigned long long>(w.skipped), ms / frames, w.commits != 0 ? ms / static_cast<double>(w.commits) : 0.0, longest);
}

}  // namespace obng::plugin::sound_commit
