#pragma once
// [Route] CellEvictionSpread: an exterior cell's eviction from the cell buffer spread over frames - the decisions over
// plain values (the plugin's CellEvictionSpread.cpp is the caller). knowledge/background-loading.md has the reading
// (2026-11-20) and the measurements: an eviction is 6 - 26 ms in one frame, nearly all of it the walk that lets every
// reference's 3D go (0x004CB4D0).
//
// The engine: a background cell task ends on the main thread in its slot 5 (0x004BDDF0), called by the IO manager's
// pump 0x00433590 at 0x00433632; the end pushes the loaded cell into the buffer (0x00442740 -> 0x0043FED0), and the
// cell shifted out of the last slot is unloaded at once (0x00447BA0).
//
// The route: inside the frame's own pump call (0x0040D8D8) a cell task's end that would evict is held - a task done on
// the loader's thread whose end has not run is a state the engine has every frame. While it is held the last slot's
// cell, still a buffered cell, has its references' 3D let go in slices of a few ms a frame (the engine's own Set3D(null)
// under its owner lock; placed references up to the budget, or one actor a slice; never the player); then the end runs and its eviction finds
// the 3D gone. Everything held ends at once ("flush") before anything that looks at the buffer or the tasks: the cell
// request 0x004BE090, the grid move's task ends 0x004BE4E0, the grid's attach 0x00444340, every pump call outside the
// frame's (the waits of 0x00434020), in menu mode and after too many frames. So a stripped cell is never refreshed or
// attached: it is always the last slot's cell and is evicted by the next end.
//
// Class B: the same unload, the same calls, the task's end a few frames later.
// 64-bit clean: plain values only.
#include <cstdint>

namespace obng::engine::world::cell_eviction_spread {

enum class Mode : std::uint8_t { Off, On };

inline Mode ModeOf(int value) {
	return value == 1 ? Mode::On : Mode::Off;
}

// TESForm's type byte (+4) of the three kinds of reference (measured 2026-11-20, run 20261120-122256, [Probe]
// UnloadRefs: an unload's Set3D(null) calls by type - 0x31 4 305 references at 14 us each, 0x32 51 at 0.66 ms, the
// longest 4.7 ms, 0x33 50 at 0.57 ms, the longest 5.3 ms; half of an unload's time is the hundred actors).
constexpr std::uint32_t kPlacedReference = 0x31;
constexpr std::uint32_t kCharacter = 0x32;
constexpr std::uint32_t kCreature = 0x33;
constexpr std::uint32_t kMaxHeld = 16;
constexpr std::uint32_t kMaxFrames = 40;          // a hold longer than this ends at once

// What a task's end found when the pump called it.
struct EndFacts {
	bool enabled = false;
	bool framePump = false;      // inside the frame's own pump call
	bool flushing = false;       // the route is ending held tasks itself
	bool menuMode = false;
	bool cellTask = false;       // the task's vtable is the cell task's
	bool cancelled = false;      // task state 6
	bool hasCell = false;        // the task's record has a cell
	bool queued = false;         // the record's flag: the end will push
	bool cellHasNode = false;    // cell +0x54: the end pushes only then
	bool exterior = false;
	bool inBuffer = false;       // the cell is in the buffer already: the push only moves it
	bool lastSlotTaken = false;  // the push will evict
	std::uint32_t held = 0;
};

enum class EndWay : std::uint8_t { Run, Hold };

// A cell task is held when its end would evict, or to keep the order when one is held already.
inline EndWay WayOf(const EndFacts& f) {
	if (!f.enabled || !f.framePump || f.flushing || f.menuMode || !f.cellTask || f.held >= kMaxHeld) {
		return EndWay::Run;
	}
	if (f.held > 0) {
		return EndWay::Hold;
	}
	const bool pushes = !f.cancelled && f.hasCell && f.queued && f.cellHasNode && f.exterior;
	return pushes && !f.inBuffer && f.lastSlotTaken ? EndWay::Hold : EndWay::Run;
}

enum class StepWay : std::uint8_t { None, Flush, RunEnd, Strip };

// A frame's step with tasks held: strip the last slot's cell, or run the oldest end when nothing is left to strip.
inline StepWay StepOf(std::uint32_t held, std::uint32_t frames, bool menuMode, bool lastSlotTaken, bool victimStripped) {
	if (held == 0) {
		return StepWay::None;
	}
	if (menuMode || frames > kMaxFrames) {
		return StepWay::Flush;
	}
	if (!lastSlotTaken || victimStripped) {
		return StepWay::RunEnd;
	}
	return StepWay::Strip;
}

inline bool IsActor(std::uint32_t formType) {
	return formType == kCharacter || formType == kCreature;
}

// A reference is stripped ahead when it is a placed reference or an actor with 3D that is not the player. An actor
// without 3D in a buffered cell is a state the engine has anyway (most of a buffered cell's characters are in it).
inline bool Strippable(std::uint32_t formType, bool isPlayer, bool has3D) {
	return (formType == kPlacedReference || IsActor(formType)) && !isPlayer && has3D;
}

// The slice goes on to the next reference while its budget lasts. An actor's Set3D(null) takes up to 5 ms, so one is
// started only as a slice's first work: a slice is either cheap references up to the budget or one actor.
inline bool GoOn(std::uint64_t elapsedUs, std::uint64_t budgetUs, bool nextIsActor, std::uint32_t doneInSlice) {
	if (nextIsActor) {
		return doneInSlice == 0;
	}
	return elapsedUs < budgetUs;
}

}  // namespace obng::engine::world::cell_eviction_spread
