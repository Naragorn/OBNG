#pragma once
// [Route] AnimationPauseBatch: an actor's animation setup 0x00475D80 loads its KF files one by one through 0x00439FF0,
// and each miss off the loader's thread pauses the background loader (0x00432860 at 0x0043A04D, waiting for the job the
// loader is on) and resumes it (0x00432890 at 0x0043A088). Measured 2026-11-19 (run 20261119-214333, 119.9 s of the
// ride): a horse's four KFs, 14 - 21 ms each, almost all of it the pause's wait. The route keeps the loader paused from
// the setup's first miss to its end: the inner resumes are held back and given at the end, so the later misses find it
// paused already (the pause counts: 0x004322B0 waits only on the first, 0x00432350 resumes only on the last; read).
// One wait a setup instead of one a file. The decisions over plain values; the plugin's AnimationPauseBatch.cpp calls.
//
// Class B: the same files loaded in the same order; only the loader rests longer (to the setup's end).
#include <cstdint>

namespace obng::engine::io::animation_pause_batch {

// One thread's state: how deep inside setups it is, and how many resumes it holds back.
struct Batch {
	std::uint32_t depth = 0;
	std::uint32_t held = 0;
};

inline void Enter(Batch& batch) {
	++batch.depth;
}

// A resume asked inside a setup is held back (true); outside, it goes through (false).
inline bool HoldResume(Batch& batch) {
	if (batch.depth == 0) {
		return false;
	}
	++batch.held;
	return true;
}

// Leaving a setup: the number of held resumes to give now - all of them when the outermost setup ends, none before.
inline std::uint32_t Leave(Batch& batch) {
	if (batch.depth == 0) {
		return 0;
	}
	--batch.depth;
	if (batch.depth != 0) {
		return 0;
	}
	const std::uint32_t give = batch.held;
	batch.held = 0;
	return give;
}

}  // namespace obng::engine::io::animation_pause_batch
