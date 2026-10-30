#pragma once

// The probes' decisions over plain values, so every flow runs in a test
// without the game: how a probed function's calls are counted per return
// site, how memory samples are kept at their peak, and how both are
// written. The detours and the Windows calls live in Probes.cpp.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

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

// ---- The packing's cross-frame probe --------------------------------------------
//
// Does a partition's packed stream change from one pack to the next? The
// packer 0x00763130 packs per (geometry, partition) into the record's chips,
// one unlock 0x00776D80 per stream; the probe hashes the staging bytes at
// every unlock and keeps the last hash per (geometry, partition, chip). A
// pack whose hash equals the key's last one is a "same" pack: a cache
// keyed like this would have skipped it (docs/subsystems/stream-packing.md,
// lever L3). The table is open addressing with overwrite on collision: a
// collision costs a "new" verdict, never a false "same".

struct PackKey {
	std::uint32_t geometry;
	std::uint32_t partition;
	std::uint32_t chip;
	bool operator==(const PackKey&) const = default;
};

// A fast 64-bit hash over the bytes (8 at a time, a tail by bytes); no CRT
// call. Equal bytes give equal hashes; a hash collision would count a changed
// pack as the same, at odds of about 2^-64 per pack (derived).
inline std::uint64_t HashBytes(const unsigned char* bytes, std::uint32_t count) {
	std::uint64_t h = 0x9E3779B97F4A7C15ull ^ count;
	std::uint32_t i = 0;
	for (; i + 8 <= count; i += 8) {
		std::uint64_t w = 0;
		for (unsigned b = 0; b < 8; ++b) {
			w |= static_cast<std::uint64_t>(bytes[i + b]) << (8 * b);
		}
		h = (h ^ w) * 0x100000001B3ull;
		h ^= h >> 29;
	}
	for (; i < count; ++i) {
		h = (h ^ bytes[i]) * 0x100000001B3ull;
	}
	return h ^ (h >> 32);
}

enum class PackVerdict : std::uint8_t {
	New,      // the key was not in the table (first pack, or evicted)
	Same,     // the bytes equal the key's last pack
	Changed,  // the bytes differ from the key's last pack
};

struct PackStats {
	std::uint64_t packs = 0;          // unlocks inside a packer call
	std::uint64_t same = 0;
	std::uint64_t changed = 0;
	std::uint64_t fresh = 0;          // New verdicts
	std::uint64_t bytes = 0;          // staging bytes hashed
	std::uint64_t sameBytes = 0;      // of packs judged Same
	std::uint64_t evictions = 0;      // a key overwritten another's slot
	std::uint64_t outside = 0;        // unlocks with no packer on the stack (index buffers, other)
	std::uint64_t volatileData = 0;   // packer entries whose geometry data nibble is 0x8000
	std::uint64_t staticData = 0;     // 0x4000
	std::uint64_t mutableData = 0;    // any other nibble
	std::uint64_t dirtyBits = 0;      // packer entries with dirty bits set (low 12 bits of +0x2E)
	void Reset() { *this = PackStats{}; }
};

struct PackCache {
	struct Slot {
		PackKey key{};
		std::uint64_t hash = 0;
		bool used = false;
	};

	explicit PackCache(std::uint32_t slotsPowerOfTwo = 16) : slots_(std::size_t{1} << slotsPowerOfTwo), mask_((1u << slotsPowerOfTwo) - 1) {}

	PackVerdict Add(PackKey key, std::uint64_t hash, std::uint32_t bytes, PackStats& stats) {
		++stats.packs;
		stats.bytes += bytes;
		Slot& slot = slots_[Index(key)];
		if (!slot.used) {
			slot = Slot{key, hash, true};
			++stats.fresh;
			++keys_;
			return PackVerdict::New;
		}
		if (!(slot.key == key)) {
			slot = Slot{key, hash, true};
			++stats.fresh;
			++stats.evictions;
			return PackVerdict::New;
		}
		if (slot.hash == hash) {
			++stats.same;
			stats.sameBytes += bytes;
			return PackVerdict::Same;
		}
		slot.hash = hash;
		++stats.changed;
		return PackVerdict::Changed;
	}
	std::uint64_t Keys() const { return keys_; }

private:
	std::uint32_t Index(PackKey k) const {
		std::uint32_t h = k.geometry * 0x9E3779B1u;
		h ^= k.partition * 0x85EBCA77u;
		h ^= k.chip * 0xC2B2AE3Du;
		h ^= h >> 15;
		return h & mask_;
	}
	std::vector<Slot> slots_;
	std::uint32_t mask_;
	std::uint64_t keys_ = 0;
};

// The data's consistency nibble at the packer's entry (geometry +0x2E bits
// 12-15; re/functions/0x00763130.md): 0x8000 volatile, 0x4000 static, else
// mutable; the low 12 bits are the dirty bits.
inline void CountConsistency(std::uint16_t consistencyAndDirty, PackStats& stats) {
	switch (consistencyAndDirty & 0xF000u) {
	case 0x8000u:
		++stats.volatileData;
		break;
	case 0x4000u:
		++stats.staticData;
		break;
	default:
		++stats.mutableData;
		break;
	}
	if ((consistencyAndDirty & 0x0FFFu) != 0) {
		++stats.dirtyBits;
	}
}

// "Probe: pack cache - N packs, N same (N %), N changed, N new (N evictions),
// N keys; N MB hashed, N % of the bytes same; N unlocks outside the packer;
// data volatile N, static N, mutable N, dirty N".
inline int FormatPackStats(const PackStats& s, std::uint64_t keys, char* out, std::size_t capacity) {
	return std::snprintf(out, capacity,
	                     "Probe: pack cache - %llu packs, %llu same (%u %%), %llu changed, %llu new (%llu evictions), %llu keys; "
	                     "%u MB hashed, %u %% of the bytes same; %llu unlocks outside the packer; data volatile %llu, static %llu, "
	                     "mutable %llu, dirty %llu",
	                     static_cast<unsigned long long>(s.packs), static_cast<unsigned long long>(s.same), PercentOf(s.same, s.packs),
	                     static_cast<unsigned long long>(s.changed), static_cast<unsigned long long>(s.fresh),
	                     static_cast<unsigned long long>(s.evictions), static_cast<unsigned long long>(keys), Megabytes(s.bytes),
	                     PercentOf(s.sameBytes, s.bytes), static_cast<unsigned long long>(s.outside),
	                     static_cast<unsigned long long>(s.volatileData), static_cast<unsigned long long>(s.staticData),
	                     static_cast<unsigned long long>(s.mutableData), static_cast<unsigned long long>(s.dirtyBits));
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
