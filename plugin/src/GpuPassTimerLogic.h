#pragma once
// [Probe] GpuPasses (a measurement, off in play): the GPU time of the frame's
// render steps from timestamp queries - the actor shadow maps, every pass list
// of every accumulator finish (by finish and pass id, with the list's records
// and its first record's shader class), the image space - every n-th frame.
// The bookkeeping over plain values; GpuPassTimer.cpp issues and reads the
// queries. 64-bit clean: handles are std::uint32_t.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::gpu_passes {

// What one stamped span is.
enum class Kind : std::uint8_t { kActorShadows, kList, kImageSpace };

// A span of one sampled frame, its two timestamps read back in ticks.
struct Span {
	Kind kind = Kind::kList;
	std::uint32_t finish = 0;   // the accumulator finish within the frame, from 0 (lists only)
	std::uint32_t pass = 0;     // the pass id (lists only)
	std::uint32_t records = 0;  // the list's records at its begin
	std::uint32_t shader = 0;   // the vtable of the list's first record's shader
	std::uint32_t target = 0;   // the render target at the finish's first list, width << 16 | height
	std::uint64_t begin = 0;
	std::uint64_t end = 0;
};

// Within a frame a new finish begins with the first list, when the
// accumulator changes, or when the pass id does not rise (the finish 0x007AE070
// walks the ids upward, re/functions/0x007AC9A0.md C3).
struct FinishTracker {
	bool any = false;
	std::uint32_t accumulator = 0;
	std::uint32_t pass = 0;
	std::uint32_t finish = 0;
	// The finish of this list; *fresh when it is a new one.
	std::uint32_t Next(std::uint32_t acc, std::uint32_t passId, bool* fresh) {
		*fresh = !any || acc != accumulator || passId <= pass;
		if (*fresh && any) {
			++finish;
		}
		any = true;
		accumulator = acc;
		pass = passId;
		return finish;
	}
};

// The window's sums.
struct Totals {
	static constexpr std::uint32_t kFinishes = 16;
	static constexpr std::uint32_t kLists = 512;
	std::uint64_t frames = 0;
	std::uint64_t lost = 0;      // sampled frames whose queries never came back
	std::uint64_t disjoint = 0;  // sampled frames the device called disjoint
	double spanMs = 0.0;         // from the first stamp to the last
	double shadowsMs = 0.0;
	double imageSpaceMs = 0.0;
	struct Finish {
		double ms = 0.0;
		std::uint64_t lists = 0;
		std::uint64_t records = 0;
		std::uint32_t target = 0;
	};
	Finish finish[kFinishes];
	struct List {
		std::uint32_t key = 0;  // finish << 16 | pass
		std::uint32_t shader = 0;
		double ms = 0.0;
		std::uint64_t records = 0;
		std::uint64_t seen = 0;
	};
	List list[kLists];
	std::uint32_t listCount = 0;
	std::uint64_t listsDropped = 0;

	// One read-back frame: its spans with the device's tick frequency.
	void AddFrame(const Span* spans, std::uint32_t count, std::uint64_t frequency) {
		if (count == 0 || frequency == 0) {
			return;
		}
		++frames;
		const double toMs = 1000.0 / static_cast<double>(frequency);
		std::uint64_t first = spans[0].begin;
		std::uint64_t last = spans[0].end;
		for (std::uint32_t i = 0; i < count; ++i) {
			const Span& s = spans[i];
			first = s.begin < first ? s.begin : first;
			last = s.end > last ? s.end : last;
			const double ms = s.end > s.begin ? static_cast<double>(s.end - s.begin) * toMs : 0.0;
			if (s.kind == Kind::kActorShadows) {
				shadowsMs += ms;
				continue;
			}
			if (s.kind == Kind::kImageSpace) {
				imageSpaceMs += ms;
				continue;
			}
			AddList(s, ms);
		}
		spanMs += static_cast<double>(last - first) * toMs;
	}

	void AddList(const Span& s, double ms) {
		if (s.finish < kFinishes) {
			Finish& f = finish[s.finish];
			f.ms += ms;
			++f.lists;
			f.records += s.records;
			f.target = s.target != 0 ? s.target : f.target;
		}
		const std::uint32_t key = (s.finish << 16) | (s.pass & 0xFFFFu);
		for (std::uint32_t i = 0; i < listCount; ++i) {
			if (list[i].key == key) {
				list[i].ms += ms;
				list[i].records += s.records;
				++list[i].seen;
				return;
			}
		}
		if (listCount == kLists) {
			++listsDropped;
			return;
		}
		list[listCount++] = List{key, s.shader, ms, s.records, 1};
	}
};

// The summary's first line: the frame's GPU time by step and finish.
inline void FormatFrame(const Totals& t, char* out, std::size_t size) {
	const double n = t.frames != 0 ? static_cast<double>(t.frames) : 1.0;
	int used = std::snprintf(out, size,
	                         "Probe: GPU passes - %llu frames read back (%llu lost, %llu disjoint), ms a frame: from the first stamp to the last %.3f; actor shadows %.3f; image space %.3f; by finish:",
	                         static_cast<unsigned long long>(t.frames), static_cast<unsigned long long>(t.lost), static_cast<unsigned long long>(t.disjoint),
	                         t.spanMs / n, t.shadowsMs / n, t.imageSpaceMs / n);
	for (std::uint32_t i = 0; i < Totals::kFinishes && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		const Totals::Finish& f = t.finish[i];
		if (f.lists == 0) {
			continue;
		}
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " #%u %.3f (%.1f lists, %.1f records, target %ux%u);", i, f.ms / n,
		                      static_cast<double>(f.lists) / n, static_cast<double>(f.records) / n, f.target >> 16, f.target & 0xFFFFu);
	}
}

// The lists by GPU time, the largest first: index order into t.list.
inline std::uint32_t LargestLists(const Totals& t, std::uint32_t* order, std::uint32_t capacity) {
	std::uint32_t n = 0;
	for (std::uint32_t i = 0; i < t.listCount; ++i) {
		std::uint32_t at = n < capacity ? n++ : capacity;
		if (at == capacity) {
			if (t.list[i].ms <= t.list[order[capacity - 1]].ms) {
				continue;
			}
			at = capacity - 1;
		}
		while (at > 0 && t.list[order[at - 1]].ms < t.list[i].ms) {
			order[at] = order[at - 1];
			--at;
		}
		order[at] = i;
	}
	return n;
}

}  // namespace obng::plugin::gpu_passes
