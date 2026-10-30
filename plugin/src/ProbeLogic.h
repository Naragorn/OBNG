#pragma once

// The probes' decisions over plain values, so every flow runs in a test
// without the game: how a probed function's calls are counted per return
// site, how memory samples are kept at their peak, and how both are
// written. The detours and the Windows calls live in Probes.cpp.

#include <cstdint>
#include <cstdio>

namespace obng::plugin::probe {

// The first kSites distinct return addresses a probed function was called
// from, each with its count; every call from another address is counted in
// `others`. First come, first kept: a run's hot callers show up first.
struct ReturnSites {
	static constexpr unsigned kSites = 8;
	struct Site {
		std::uint32_t address;
		std::uint64_t count;
	};
	Site sites[kSites] = {};
	unsigned used = 0;
	std::uint64_t others = 0;

	void Add(std::uint32_t address) {
		for (unsigned i = 0; i < used; ++i) {
			if (sites[i].address == address) {
				++sites[i].count;
				return;
			}
		}
		if (used < kSites) {
			sites[used++] = Site{address, 1};
			return;
		}
		++others;
	}
	void Reset() { *this = ReturnSites{}; }
};

struct CallCounts {
	std::uint64_t calls = 0;
	ReturnSites sites;

	void Add(std::uint32_t returnAddress) {
		++calls;
		sites.Add(returnAddress);
	}
	void Reset() { *this = CallCounts{}; }
};

// One sample of the process's memory: the commit charge and working set
// (K32GetProcessMemoryInfo) and the user address space (GlobalMemoryStatusEx:
// the total is what the image's large-address-aware flag allows, 2 or 4 GB).
struct MemorySample {
	std::uint64_t commitBytes = 0;
	std::uint64_t workingSetBytes = 0;
	std::uint64_t peakWorkingSetBytes = 0;
	std::uint64_t virtualTotalBytes = 0;
	std::uint64_t virtualAvailableBytes = 0;
};

inline std::uint64_t VirtualUsedBytes(const MemorySample& s) {
	return s.virtualTotalBytes >= s.virtualAvailableBytes ? s.virtualTotalBytes - s.virtualAvailableBytes : 0;
}

// The peak over the samples seen: the largest commit, working set and
// address-space use; the total is the latest sample's.
struct MemoryPeak {
	bool any = false;
	MemorySample peak;

	void Add(const MemorySample& s) {
		if (!any) {
			peak = s;
			any = true;
			return;
		}
		if (s.commitBytes > peak.commitBytes) peak.commitBytes = s.commitBytes;
		if (s.workingSetBytes > peak.workingSetBytes) peak.workingSetBytes = s.workingSetBytes;
		if (s.peakWorkingSetBytes > peak.peakWorkingSetBytes) peak.peakWorkingSetBytes = s.peakWorkingSetBytes;
		peak.virtualTotalBytes = s.virtualTotalBytes;
		if (s.virtualAvailableBytes < peak.virtualAvailableBytes) peak.virtualAvailableBytes = s.virtualAvailableBytes;
	}
};

inline unsigned Megabytes(std::uint64_t bytes) {
	return static_cast<unsigned>(bytes / (1024u * 1024u));
}

inline unsigned PercentOf(std::uint64_t part, std::uint64_t whole) {
	return whole == 0 ? 0u : static_cast<unsigned>(part * 100u / whole);
}

// " name N calls (from 0x... n, 0x... n[, +k others]);" - the sites in the
// order they were first seen. Returns the characters written.
inline int FormatCallCounts(const char* name, const CallCounts& c, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity, " %s %llu calls", name, static_cast<unsigned long long>(c.calls));
	if (c.sites.used != 0 && at >= 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - at, " (from");
		for (unsigned i = 0; i < c.sites.used && at >= 0 && static_cast<std::size_t>(at) < capacity; ++i) {
			at += std::snprintf(out + at, capacity - at, "%s 0x%08X %llu", i == 0 ? "" : ",", c.sites.sites[i].address,
			                    static_cast<unsigned long long>(c.sites.sites[i].count));
		}
		if (c.sites.others != 0 && at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += std::snprintf(out + at, capacity - at, ", +%llu others", static_cast<unsigned long long>(c.sites.others));
		}
		if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += std::snprintf(out + at, capacity - at, ")");
		}
	}
	if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - at, ";");
	}
	return at;
}

// "Probe: memory commit N MB (peak N), working set N MB (peak N), address
// space N of N MB used (N %, peak N MB)".
inline int FormatMemory(const MemorySample& now, const MemoryPeak& peak, char* out, std::size_t capacity) {
	const MemorySample& p = peak.any ? peak.peak : now;
	return std::snprintf(out, capacity,
	                     "Probe: memory commit %u MB (peak %u), working set %u MB (peak %u), address space %u of %u MB used (%u %%, peak %u MB)",
	                     Megabytes(now.commitBytes), Megabytes(p.commitBytes), Megabytes(now.workingSetBytes),
	                     Megabytes(p.peakWorkingSetBytes), Megabytes(VirtualUsedBytes(now)), Megabytes(now.virtualTotalBytes),
	                     PercentOf(VirtualUsedBytes(now), now.virtualTotalBytes), Megabytes(VirtualUsedBytes(p)));
}

}  // namespace obng::plugin::probe
