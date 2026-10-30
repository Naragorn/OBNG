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

// ---- The pass steps: calls and time per summary window --------------------------

// One timed function: how often it ran since the last summary and the sum
// of the TSC ticks its calls took (inclusive: the original and everything
// under it, the detour's own two reads included).
struct StepTiming {
	std::uint64_t calls = 0;
	std::uint64_t ticks = 0;
};

// The TSC is calibrated against the performance counter over the summary
// window itself: both are read at every summary, and the ratio of the two
// deltas converts the window's ticks to milliseconds. 0 when the window is
// empty or the frequency unknown (the first summary has no window yet).
struct ClockPair {
	std::uint64_t tsc = 0;
	std::uint64_t qpc = 0;
};

inline double TicksPerMillisecond(ClockPair before, ClockPair after, std::uint64_t qpcFrequency) {
	if (qpcFrequency == 0 || after.qpc <= before.qpc || after.tsc <= before.tsc) {
		return 0.0;
	}
	const double milliseconds = static_cast<double>(after.qpc - before.qpc) * 1000.0 / static_cast<double>(qpcFrequency);
	return static_cast<double>(after.tsc - before.tsc) / milliseconds;
}

// " name N calls, X.XX ms (Y.YY us per call);" with the calibration, or
// " name N calls, T ticks;" without one; " name 0 calls;" for an idle step.
// Returns the characters written.
inline int FormatStepTiming(const char* name, const StepTiming& t, double ticksPerMillisecond, char* out, std::size_t capacity) {
	if (t.calls == 0) {
		return std::snprintf(out, capacity, " %s 0 calls;", name);
	}
	if (ticksPerMillisecond <= 0.0) {
		return std::snprintf(out, capacity, " %s %llu calls, %llu ticks;", name, static_cast<unsigned long long>(t.calls),
		                     static_cast<unsigned long long>(t.ticks));
	}
	const double ms = static_cast<double>(t.ticks) / ticksPerMillisecond;
	return std::snprintf(out, capacity, " %s %llu calls, %.2f ms (%.2f us per call);", name,
	                     static_cast<unsigned long long>(t.calls), ms, ms * 1000.0 / static_cast<double>(t.calls));
}

// A timed step split by return site: the first kSites distinct callers
// with their calls and ticks, every other caller summed in `others`.
struct StepSites {
	static constexpr unsigned kSites = 8;
	struct Site {
		std::uint32_t address;
		StepTiming timing;
	};
	Site sites[kSites] = {};
	unsigned used = 0;
	StepTiming others;

	void Add(std::uint32_t address, std::uint64_t ticks) {
		for (unsigned i = 0; i < used; ++i) {
			if (sites[i].address == address) {
				++sites[i].timing.calls;
				sites[i].timing.ticks += ticks;
				return;
			}
		}
		if (used < kSites) {
			sites[used++] = Site{address, StepTiming{1, ticks}};
			return;
		}
		++others.calls;
		others.ticks += ticks;
	}
};

// " name by return site: 0x... N calls X ms, ...[, +N others X ms];" with the
// calibration (the raw ticks without one); nothing but " name by return
// site: none;" when no call was seen. Returns the characters written.
inline int FormatStepSites(const char* name, const StepSites& s, double ticksPerMillisecond, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity, " %s by return site:", name);
	if (s.used == 0) {
		if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += std::snprintf(out + at, capacity - at, " none;");
		}
		return at;
	}
	const auto amount = [&](const StepTiming& t, char* o, std::size_t c) {
		return ticksPerMillisecond > 0.0
			? std::snprintf(o, c, "%llu calls %.2f ms", static_cast<unsigned long long>(t.calls), static_cast<double>(t.ticks) / ticksPerMillisecond)
			: std::snprintf(o, c, "%llu calls %llu ticks", static_cast<unsigned long long>(t.calls), static_cast<unsigned long long>(t.ticks));
	};
	for (unsigned i = 0; i < s.used && at >= 0 && static_cast<std::size_t>(at) < capacity; ++i) {
		at += std::snprintf(out + at, capacity - at, "%s 0x%08X ", i == 0 ? "" : ",", s.sites[i].address);
		if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += amount(s.sites[i].timing, out + at, capacity - at);
		}
	}
	if (s.others.calls != 0 && at >= 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - at, ", +others ");
		if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
			at += amount(s.others, out + at, capacity - at);
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
