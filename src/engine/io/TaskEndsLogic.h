#pragma once
// [Probe] TaskEnds: what the IO manager's pump 0x00433590 spends on which kind of background task when it ends them
// on the main thread (a task's slot 5, called at 0x00433632) - the table over plain values (the plugin's
// TaskEndProbe.cpp calls). A kind is a task's vtable. Measured 2026-11-20 with OBVR's late pose wait: the pump holds
// 8 - 15 ms in six of a ride's eighteen missed periods, and no cell unload is among them.
// 64-bit clean: a vtable as a 32-bit handle, times in counter ticks.
#include <cstddef>
#include <cstdint>

namespace obng::engine::io::task_ends {

constexpr std::size_t kKinds = 16;
constexpr unsigned kLongEnds = 32;

struct Kind {
	std::uint32_t vtable = 0;
	std::uint64_t calls = 0;
	std::uint64_t ticks = 0;
	std::uint64_t longest = 0;
};

struct Table {
	Kind kinds[kKinds];
	Kind other;  // the kinds beyond the table's rows, as one
};

inline std::size_t Used(const Table& table) {
	std::size_t used = 0;
	while (used < kKinds && table.kinds[used].vtable != 0) {
		++used;
	}
	return used;
}

inline const Kind* Find(const Table& table, std::uint32_t vtable) {
	for (std::size_t i = 0; i < kKinds && table.kinds[i].vtable != 0; ++i) {
		if (table.kinds[i].vtable == vtable) {
			return &table.kinds[i];
		}
	}
	return nullptr;
}

inline void Add(Table& table, std::uint32_t vtable, std::uint64_t ticks) {
	Kind* kind = &table.other;
	for (std::size_t i = 0; i < kKinds; ++i) {
		if (table.kinds[i].vtable == vtable || table.kinds[i].vtable == 0) {
			kind = &table.kinds[i];
			kind->vtable = vtable;
			break;
		}
	}
	++kind->calls;
	kind->ticks += ticks;
	kind->longest = ticks > kind->longest ? ticks : kind->longest;
}

inline bool IsLong(std::uint64_t ticks, std::uint64_t limit) {
	return ticks >= limit;
}

// One long end, for the log: its kind, its time, when it ended, and two words of the task's record (a cell task's
// cell coordinates).
struct LongEnd {
	std::uint32_t vtable = 0;
	std::uint64_t ticks = 0;
	std::uint64_t endedAt = 0;
	std::int32_t a = 0;
	std::int32_t b = 0;
};

struct LongEnds {
	LongEnd ends[kLongEnds];
	unsigned count = 0;
	unsigned dropped = 0;
};

inline void Keep(LongEnds& list, const LongEnd& end) {
	if (list.count >= kLongEnds) {
		++list.dropped;
		return;
	}
	list.ends[list.count++] = end;
}

}  // namespace obng::engine::io::task_ends
