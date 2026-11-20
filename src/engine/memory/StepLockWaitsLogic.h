#pragma once
// [Probe] LockCalls, the main thread's part: the critical section waits of one update step, by section, so that a long
// step (a cell's eviction, a crossing) can be laid beside the locks the main thread waited on in it. Plain values; the
// plugin's LockProbe.cpp adds a wait when the main thread's contended enter returns and writes a line at the next
// step's start when the step's waits reach the floor.
// 64-bit clean: sections and sites as 32-bit handles.
#include <cstdint>

namespace obng::engine::memory::step_lock_waits {

constexpr std::uint32_t kSlots = 6;

struct Slot {
	std::uint32_t section = 0;
	std::uint32_t waits = 0;
	std::uint64_t us = 0;
	std::uint32_t longestUs = 0;
	std::uint32_t longestSite = 0;  // the enter site of the section's longest wait
};

struct StepWaits {
	Slot slots[kSlots];
	std::uint32_t used = 0;
	std::uint32_t otherWaits = 0;  // sections past the slots
	std::uint64_t otherUs = 0;
};

inline void Add(StepWaits& step, std::uint32_t section, std::uint32_t us, std::uint32_t site) {
	for (std::uint32_t i = 0; i < step.used; ++i) {
		Slot& slot = step.slots[i];
		if (slot.section == section) {
			++slot.waits;
			slot.us += us;
			if (us > slot.longestUs) {
				slot.longestUs = us;
				slot.longestSite = site;
			}
			return;
		}
	}
	if (step.used < kSlots) {
		Slot& slot = step.slots[step.used++];
		slot.section = section;
		slot.waits = 1;
		slot.us = us;
		slot.longestUs = us;
		slot.longestSite = site;
		return;
	}
	++step.otherWaits;
	step.otherUs += us;
}

inline std::uint64_t TotalUs(const StepWaits& step) {
	std::uint64_t total = step.otherUs;
	for (std::uint32_t i = 0; i < step.used; ++i) {
		total += step.slots[i].us;
	}
	return total;
}

// A step's waits are written when they reach the floor.
inline bool Worth(const StepWaits& step, std::uint64_t floorUs) {
	return TotalUs(step) >= floorUs;
}

inline void Reset(StepWaits& step) {
	step = StepWaits{};
}

}  // namespace obng::engine::memory::step_lock_waits
