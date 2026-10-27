#pragma once

// The shadow check's decisions, over plain values, so every flow runs in a
// test without the game: what a snapshot of the clock state is, how two
// snapshots are compared bit for bit, how the original's GetTickCount
// reading is recovered from what it wrote, and how the counts and the log
// budget behave. The detours in ShadowTimeInfo.cpp only move bytes and call
// these.

#include <cstdint>
#include <cstring>

#include "engine/time/TimeInfo.h"

namespace obng::plugin::shadow {

using engine::time::TimeInfo;

// Everything the four TimeInfo functions can write: the instance and the
// two clock globals. Compared as bytes, so a pad byte or a -0.0f shows.
struct State {
	TimeInfo info;
	std::uint32_t clockMs;          // 0x00B39DB0
	std::uint32_t previousClockMs;  // 0x00B39DB4
};

enum class Field : std::uint8_t {
	FreezeDepth, Pad, FixedStepMs, FixedStepCarryMs, FrameSeconds, ClockMs, StartTick, GlobalClockMs,
	GlobalPreviousClockMs, Count
};

inline const char* FieldName(Field field) {
	switch (field) {
	case Field::FreezeDepth: return "freezeDepth";
	case Field::Pad: return "pad";
	case Field::FixedStepMs: return "fixedStepMs";
	case Field::FixedStepCarryMs: return "fixedStepCarryMs";
	case Field::FrameSeconds: return "frameSeconds";
	case Field::ClockMs: return "clockMs";
	case Field::StartTick: return "startTick";
	case Field::GlobalClockMs: return "g_clockMs";
	case Field::GlobalPreviousClockMs: return "g_previousClockMs";
	default: return "?";
	}
}

// The 32-bit pattern of a field, for the log and for the comparison.
inline std::uint32_t Bits(const State& s, Field field) {
	std::uint32_t v = 0;
	switch (field) {
	case Field::FreezeDepth: return s.info.freezeDepth;
	case Field::Pad: return static_cast<std::uint32_t>(s.info.pad[0]) | (static_cast<std::uint32_t>(s.info.pad[1]) << 8) |
		(static_cast<std::uint32_t>(s.info.pad[2]) << 16);
	case Field::FixedStepMs: std::memcpy(&v, &s.info.fixedStepMs, 4); return v;
	case Field::FixedStepCarryMs: std::memcpy(&v, &s.info.fixedStepCarryMs, 4); return v;
	case Field::FrameSeconds: std::memcpy(&v, &s.info.frameSeconds, 4); return v;
	case Field::ClockMs: return s.info.clockMs;
	case Field::StartTick: return s.info.startTick;
	case Field::GlobalClockMs: return s.clockMs;
	case Field::GlobalPreviousClockMs: return s.previousClockMs;
	default: return 0;
	}
}

// Which fields differ between what the original left and what the rebuild
// left, as a bit mask over Field. Zero means identical, bit for bit.
inline std::uint32_t Differences(const State& original, const State& rebuild) {
	std::uint32_t mask = 0;
	for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(Field::Count); ++i) {
		const Field f = static_cast<Field>(i);
		if (Bits(original, f) != Bits(rebuild, f)) {
			mask |= 1u << i;
		}
	}
	return mask;
}

// Freeze and Unfreeze read GetTickCount themselves, once, on the outermost
// transition, and write now - field into +0x10 and +0x14. The rebuild runs
// on a copy and cannot read the same tick; but the original's reading is
// recoverable from what it wrote: now = after + before (mod 2^32). Both
// fields must give the same now, or the original did not do what the
// mapping says - which the comparison then reports as a mismatch anyway.
struct RecoveredTick {
	bool transition;      // the outermost transition happened, a tick was read
	std::uint32_t now;    // the recovered reading, from +0x10
	bool consistent;      // +0x14 recovers the same reading
};

inline RecoveredTick RecoverFreezeTick(const State& before, const State& after, bool outermost) {
	if (!outermost) {
		return RecoveredTick{false, 0, true};
	}
	const std::uint32_t fromClock = after.info.clockMs + before.info.clockMs;
	const std::uint32_t fromStart = after.info.startTick + before.info.startTick;
	return RecoveredTick{true, fromClock, fromClock == fromStart};
}

// The counts of one shadowed function and the log budget: the first
// `lineBudget` mismatches are written in full, the rest only counted.
struct Counter {
	std::uint64_t calls = 0;
	std::uint64_t mismatches = 0;
	std::uint32_t linesWritten = 0;

	// Records a call; true when this mismatch should be written in full.
	bool Record(bool mismatch, std::uint32_t lineBudget) {
		++calls;
		if (!mismatch) {
			return false;
		}
		++mismatches;
		if (linesWritten < lineBudget) {
			++linesWritten;
			return true;
		}
		return false;
	}
};

// Whether a summary line is due: every `everyMs` of GetTickCount, and the
// first one `everyMs` after the first call (nothing is written at the first
// call, when nothing has been checked yet).
struct SummaryClock {
	std::uint32_t lastTick = 0;
	bool started = false;

	bool Due(std::uint32_t tick, std::uint32_t everyMs) {
		if (!started) {
			started = true;
			lastTick = tick;
			return false;
		}
		if (tick - lastTick >= everyMs) {
			lastTick = tick;
			return true;
		}
		return false;
	}
};

// The Havok planner's outputs sampled once a frame (re/functions/0x00889810.md):
// the update type and a histogram of the step counts seen since the last
// summary, so the static reading of the planner meets the game's run.
struct PlannerSample {
	std::uint32_t stepCounts[4] = {0, 0, 0, 0};  // 0, 1, 2, 3-or-more steps
	std::uint32_t frames = 0;
	std::uint32_t lastType = 0;

	void Add(std::uint32_t type, std::uint32_t steps) {
		++frames;
		lastType = type;
		++stepCounts[steps >= 3 ? 3 : steps];
	}
	void Reset() {
		*this = PlannerSample{};
	}
};

}  // namespace obng::plugin::shadow
