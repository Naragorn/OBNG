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
// A step split by its return site, as the probes see it: the first N
// distinct sites keep a slot (first come, first kept), the rest add to
// others. Eight for the pass steps (eighteen calls a frame), thirty-two for
// NiAVObject::Update 0x00707370, which the exe calls from 145 sites.
template <unsigned N>
struct SiteTable {
	static constexpr unsigned kSites = N;
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
using StepSites = SiteTable<8>;
using ObjectUpdateSites = SiteTable<32>;

// " name by return site: 0x... N calls X ms, ...[, +N others X ms];" with the
// calibration (the raw ticks without one); nothing but " name by return
// site: none;" when no call was seen. Returns the characters written.
template <unsigned N>
inline int FormatStepSites(const char* name, const SiteTable<N>& s, double ticksPerMillisecond, char* out, std::size_t capacity) {
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
	std::uint32_t chip;  // the stream index since the keyed probe (the field name stayed)
	std::uint32_t decl = 0;  // the shader declaration the pack ran under (its layout)
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
	std::uint64_t sliceSame = 0;      // packs whose (buffer, offset, size) equal the key's last pack's
	std::uint64_t sliceMoved = 0;     // packs whose slice differs from the key's last
	std::uint64_t lockDiscard = 0;    // packs whose chip flags hold D3DLOCK_DISCARD 0x2000
	std::uint64_t lockNoOverwrite = 0;  // ... D3DLOCK_NOOVERWRITE 0x1000
	std::uint64_t lockOther = 0;      // neither bit
	std::uint64_t volatileSame = 0;   // Same verdicts on packs whose data was volatile at the packer's entry
	std::uint64_t volatileChanged = 0;
	std::uint64_t otherSame = 0;      // Same verdicts on mutable or static data
	std::uint64_t otherChanged = 0;
	std::uint64_t volatileCalls = 0;  // packer calls with volatile data (timed)
	std::uint64_t volatileTicks = 0;  // their TSC ticks, inclusive (the whole 0x00763130 call, the probe's hashing included)
	std::uint64_t hashTicks = 0;      // the probe's own hashing inside those calls, to subtract
	void Reset() { *this = PackStats{}; }
};

// The verdict split by the data's consistency at the packer's entry.
inline void CountVerdictByConsistency(PackVerdict verdict, bool volatileData, PackStats& stats) {
	if (verdict == PackVerdict::Same) {
		++(volatileData ? stats.volatileSame : stats.otherSame);
	} else if (verdict == PackVerdict::Changed) {
		++(volatileData ? stats.volatileChanged : stats.otherChanged);
	}
}

// The D3D slice a pack went into: the chip's buffer, lock offset and size.
struct PackSlice {
	std::uint32_t buffer;
	std::uint32_t offset;
	std::uint32_t size;
	bool operator==(const PackSlice&) const = default;
};

inline constexpr std::uint32_t kLockDiscard = 0x2000u;
inline constexpr std::uint32_t kLockNoOverwrite = 0x1000u;

inline void CountLockFlags(std::uint32_t flags, PackStats& stats) {
	if ((flags & kLockDiscard) != 0) {
		++stats.lockDiscard;
	} else if ((flags & kLockNoOverwrite) != 0) {
		++stats.lockNoOverwrite;
	} else {
		++stats.lockOther;
	}
}

struct PackCache {
	struct Slot {
		PackKey key{};
		std::uint64_t hash = 0;
		PackSlice slice{};
		bool used = false;
		std::uint64_t same = 0;     // per key, over the table's life
		std::uint64_t changed = 0;
	};

	explicit PackCache(std::uint32_t slotsPowerOfTwo = 16) : slots_(std::size_t{1} << slotsPowerOfTwo), mask_((1u << slotsPowerOfTwo) - 1) {}

	// slice: where the bytes went; its stability against the key's last pack
	// is counted (sliceSame / sliceMoved) for a known key.
	PackVerdict Add(PackKey key, std::uint64_t hash, std::uint32_t bytes, PackStats& stats, PackSlice slice = PackSlice{}) {
		++stats.packs;
		stats.bytes += bytes;
		Slot& slot = slots_[Index(key)];
		if (!slot.used) {
			slot = Slot{key, hash, slice, true};
			++stats.fresh;
			++keys_;
			return PackVerdict::New;
		}
		if (!(slot.key == key)) {
			slot = Slot{key, hash, slice, true};
			++stats.fresh;
			++stats.evictions;
			return PackVerdict::New;
		}
		if (slot.slice == slice) {
			++stats.sliceSame;
		} else {
			++stats.sliceMoved;
			slot.slice = slice;
		}
		if (slot.hash == hash) {
			++stats.same;
			stats.sameBytes += bytes;
			++slot.same;
			return PackVerdict::Same;
		}
		slot.hash = hash;
		++stats.changed;
		++slot.changed;
		return PackVerdict::Changed;
	}
	std::uint64_t Keys() const { return keys_; }

	// The keys by their history: never changed, never the same, mixed; and
	// how many distinct buffers they went into. Over the table's life.
	struct KeyShape {
		std::uint64_t alwaysSame = 0;
		std::uint64_t alwaysChanged = 0;
		std::uint64_t mixed = 0;
		std::uint64_t untested = 0;  // one pack only
		std::uint64_t buffers = 0;   // distinct slice buffers among the keys
	};
	KeyShape Shape() const {
		KeyShape k;
		std::vector<std::uint32_t> buffers;
		for (const Slot& s : slots_) {
			if (!s.used) {
				continue;
			}
			if (s.same == 0 && s.changed == 0) {
				++k.untested;
			} else if (s.changed == 0) {
				++k.alwaysSame;
			} else if (s.same == 0) {
				++k.alwaysChanged;
			} else {
				++k.mixed;
			}
			bool seen = false;
			for (std::uint32_t b : buffers) {
				seen = seen || b == s.slice.buffer;
			}
			if (!seen) {
				buffers.push_back(s.slice.buffer);
			}
		}
		k.buffers = buffers.size();
		return k;
	}
	// The first `limit` keys that ever changed, for the log: (stream, same, changed).
	template <class Visit>
	void VisitChangedKeys(unsigned limit, Visit&& visit) const {
		unsigned n = 0;
		for (const Slot& s : slots_) {
			if (s.used && s.changed != 0 && n < limit) {
				visit(s.key, s.same, s.changed);
				++n;
			}
		}
	}

private:
	std::uint32_t Index(PackKey k) const {
		std::uint32_t h = k.geometry * 0x9E3779B1u;
		h ^= k.partition * 0x85EBCA77u;
		h ^= k.chip * 0xC2B2AE3Du;
		h ^= k.decl * 0x27D4EB2Fu;
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
	                     "mutable %llu, dirty %llu; slice same %llu, moved %llu; lock flags discard %llu, no-overwrite %llu, other %llu; "
	                     "by data: volatile same %llu changed %llu, other same %llu changed %llu",
	                     static_cast<unsigned long long>(s.packs), static_cast<unsigned long long>(s.same), PercentOf(s.same, s.packs),
	                     static_cast<unsigned long long>(s.changed), static_cast<unsigned long long>(s.fresh),
	                     static_cast<unsigned long long>(s.evictions), static_cast<unsigned long long>(keys), Megabytes(s.bytes),
	                     PercentOf(s.sameBytes, s.bytes), static_cast<unsigned long long>(s.outside),
	                     static_cast<unsigned long long>(s.volatileData), static_cast<unsigned long long>(s.staticData),
	                     static_cast<unsigned long long>(s.mutableData), static_cast<unsigned long long>(s.dirtyBits),
	                     static_cast<unsigned long long>(s.sliceSame), static_cast<unsigned long long>(s.sliceMoved),
	                     static_cast<unsigned long long>(s.lockDiscard), static_cast<unsigned long long>(s.lockNoOverwrite),
	                     static_cast<unsigned long long>(s.lockOther), static_cast<unsigned long long>(s.volatileSame),
	                     static_cast<unsigned long long>(s.volatileChanged), static_cast<unsigned long long>(s.otherSame),
	                     static_cast<unsigned long long>(s.otherChanged));
}

// " volatile packer calls N, X.XX ms (Y.YY us per call)" with a calibration,
// ticks without one.
inline int FormatVolatilePackTime(const PackStats& s, double ticksPerMillisecond, char* out, std::size_t capacity) {
	const std::uint64_t own = s.volatileTicks > s.hashTicks ? s.volatileTicks - s.hashTicks : 0;
	int at = FormatStepTiming("volatile packer calls without the probe's hashing", StepTiming{s.volatileCalls, own}, ticksPerMillisecond, out, capacity);
	if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
		at += FormatStepTiming("the probe's hashing", StepTiming{s.volatileCalls, s.hashTicks}, ticksPerMillisecond, out + at, capacity - at);
	}
	return at;
}

// ---- The scene graph update probe ------------------------------------------------
//
// What Gamebryo's update tree does per frame outside the scene pass
// (knowledge/render-thread-outside-the-pass.md: 1.6 ms a frame on the Gold
// Road, the largest block): how many nodes NiNode::UpdateDownwardPass visits,
// how many leaves, how many controller walks find nothing to walk, and the
// time of the cell grid's update and of the actors' NiAVObject::Update.
struct SceneUpdateStats {
	std::uint64_t gridUpdates = 0;        // GridCellArray::Update 0x004823D0 calls (one a frame: the denominator)
	std::uint64_t gridTicks = 0;          // its inclusive time
	std::uint64_t cells = 0;              // TESObjectCELL::UpdateSelectedNodes 0x004D4970 calls
	std::uint64_t nodeDownward = 0;       // NiNode::UpdateDownwardPass 0x0070A0D0 calls (recursive: one per node visited)
	std::uint64_t nodeSelected = 0;       // NiNode::UpdateSelectedDownwardPass 0x0070A190 calls
	std::uint64_t leafDownward = 0;       // NiAVObject::UpdateDownwardPass 0x00707580 calls
	std::uint64_t controllerWalks = 0;    // NiAVObject::UpdateObjectControllers 0x0047C930 calls
	std::uint64_t controllerWalksIdle = 0;  // of them with no property list to walk and no controller: nothing to do
	std::uint64_t worldData = 0;          // NiAVObject::UpdateWorldData 0x0070C120 calls
	std::uint64_t objectUpdates = 0;      // NiAVObject::Update 0x00707370 calls (the actors' and others' explicit updates)
	std::uint64_t objectUpdateTicks = 0;  // their inclusive time
};

// "Probe: scene update - N grid updates, X ms each; per grid update: cells C,
// nodes N (selected S), leaves L, world data W, controller walks K (I with
// nothing to do, P %); NiAVObject::Update U calls a grid update, Y ms".
inline int FormatSceneUpdate(const SceneUpdateStats& s, double ticksPerMillisecond, char* out, std::size_t capacity) {
	const double frames = s.gridUpdates == 0 ? 1.0 : static_cast<double>(s.gridUpdates);
	auto per = [&](std::uint64_t n) { return static_cast<double>(n) / frames; };
	auto ms = [&](std::uint64_t ticks) { return ticksPerMillisecond > 0.0 ? static_cast<double>(ticks) / ticksPerMillisecond / frames : 0.0; };
	int at = std::snprintf(out, capacity,
	                       "Probe: scene update - %llu grid updates; per grid update: cells %.1f, nodes %.0f (selected %.0f), leaves %.0f, world data %.0f, "
	                       "controller walks %.0f (%.0f with nothing to do, %u %%), NiAVObject::Update %.1f calls",
	                       static_cast<unsigned long long>(s.gridUpdates), per(s.cells), per(s.nodeDownward), per(s.nodeSelected), per(s.leafDownward),
	                       per(s.worldData), per(s.controllerWalks), per(s.controllerWalksIdle), PercentOf(s.controllerWalksIdle, s.controllerWalks),
	                       per(s.objectUpdates));
	if (at < 0 || static_cast<std::size_t>(at) >= capacity) {
		return at;
	}
	if (ticksPerMillisecond > 0.0) {
		at += std::snprintf(out + at, capacity - static_cast<std::size_t>(at), "; grid update %.3f ms, NiAVObject::Update %.3f ms per grid update",
		                    ms(s.gridTicks), ms(s.objectUpdateTicks));
	} else {
		at += std::snprintf(out + at, capacity - static_cast<std::size_t>(at), "; grid update %llu ticks, NiAVObject::Update %llu ticks",
		                    static_cast<unsigned long long>(s.gridTicks), static_cast<unsigned long long>(s.objectUpdateTicks));
	}
	return at;
}

// ---- The constant uploads' redundancy probe -----------------------------------------
//
// Does a vertex constant upload write what its register range already holds?
// The uploads that cost computation reach the device through the program's
// slots 0x007830C0 (one entry: register entry+0x1C, count entry+0x20 vec4s)
// and 0x00783180 (one bone palette: count bones of stride vec4s from reg0,
// gathered through a u16 index list; re/functions/0x00783180.md). The probe
// hashes the bytes of every upload and keeps the last hash per register:
// "same" means the computation that produced the upload could have been
// skipped by a check of its inputs; "same in pass" the same within one
// accumulator finish, which is what a per-pass cache would see. The vertex
// constant file has at most 256 float4 registers on D3D9 (d3d9caps
// MaxVertexShaderConst; unverified here - the table holds 1024 and counts
// anything beyond as out of range).

struct UploadStats {
	std::uint64_t uploads = 0;        // single-entry uploads plus one per bone
	std::uint64_t same = 0;
	std::uint64_t sameInPass = 0;     // same, and the last upload to that register was in this pass
	std::uint64_t changed = 0;
	std::uint64_t fresh = 0;          // first upload to a register since the summary
	std::uint64_t bytes = 0;
	std::uint64_t sameBytes = 0;
	std::uint64_t outOfRange = 0;     // register >= kRegisters, not tracked
	std::uint64_t paletteCalls = 0;   // 0x00783180 calls that passed the type check
	std::uint64_t bones = 0;          // bones uploaded by them
	std::uint64_t consecutivePalettes = 0;  // palettes whose indices form one ascending run (one call, no gather)
	std::uint64_t byType[8] = {};     // single-entry uploads by the entry's type nibble (entry+0x14 >> 28)
	void Reset() { *this = UploadStats{}; }
};

struct RegisterCache {
	static constexpr std::uint32_t kRegisters = 1024;
	struct Slot {
		std::uint64_t hash = 0;
		std::uint32_t pass = 0;
		bool used = false;
	};

	RegisterCache() : slots_(kRegisters) {}

	// One upload of `bytes` bytes to `reg` in pass `pass`.
	PackVerdict Add(std::uint32_t reg, std::uint64_t hash, std::uint32_t bytes, std::uint32_t pass, UploadStats& stats) {
		++stats.uploads;
		stats.bytes += bytes;
		if (reg >= kRegisters) {
			++stats.outOfRange;
			return PackVerdict::New;
		}
		Slot& slot = slots_[reg];
		if (!slot.used) {
			slot = Slot{hash, pass, true};
			++stats.fresh;
			return PackVerdict::New;
		}
		const bool samePass = slot.pass == pass;
		slot.pass = pass;
		if (slot.hash == hash) {
			++stats.same;
			stats.sameBytes += bytes;
			if (samePass) {
				++stats.sameInPass;
			}
			return PackVerdict::Same;
		}
		slot.hash = hash;
		++stats.changed;
		return PackVerdict::Changed;
	}
	void Reset() {
		for (Slot& s : slots_) {
			s = Slot{};
		}
	}

private:
	std::vector<Slot> slots_;
};

// indices[i] == indices[0] + i for every bone: the source blocks are one
// contiguous run and the single call needs no gather (0x00783180.md C7).
inline bool ConsecutiveRun(const std::uint16_t* indices, std::uint32_t count) {
	for (std::uint32_t i = 1; i < count; ++i) {
		if (indices[i] != static_cast<std::uint16_t>(indices[0] + i)) {
			return false;
		}
	}
	return true;
}

// "Probe: constant uploads - N uploads, N same (N %, N in the same pass), N
// changed, N fresh, N out of range; N MB, N % of the bytes same; palettes N
// with N bones (N % consecutive); single uploads by type 1:N 2:N ...".
inline int FormatUploadStats(const UploadStats& s, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity,
	                       "Probe: constant uploads - %llu uploads, %llu same (%u %%, %llu in the same pass), %llu changed, %llu fresh, "
	                       "%llu out of range; %u MB, %u %% of the bytes same; palettes %llu with %llu bones (%u %% consecutive); "
	                       "single uploads by type",
	                       static_cast<unsigned long long>(s.uploads), static_cast<unsigned long long>(s.same), PercentOf(s.same, s.uploads),
	                       static_cast<unsigned long long>(s.sameInPass), static_cast<unsigned long long>(s.changed),
	                       static_cast<unsigned long long>(s.fresh), static_cast<unsigned long long>(s.outOfRange), Megabytes(s.bytes),
	                       PercentOf(s.sameBytes, s.bytes), static_cast<unsigned long long>(s.paletteCalls),
	                       static_cast<unsigned long long>(s.bones), PercentOf(s.consecutivePalettes, s.paletteCalls));
	for (unsigned t = 0; t < 8 && at >= 0 && static_cast<std::size_t>(at) < capacity; ++t) {
		if (s.byType[t] != 0) {
			at += std::snprintf(out + at, capacity - at, " %u:%llu", t, static_cast<unsigned long long>(s.byType[t]));
		}
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

// ---- The view-dependence of the constant uploads ------------------------------------
//
// In the stereo route's test mode the second eye re-applies the shader's
// constants (slot 14 again) with the view shifted by the eye offset. The
// uploads inside that re-apply are compared with the register's bytes from
// the first eye's apply (the RegisterCache above): a register whose bytes
// changed depends on the view, one whose bytes are the same does not. To
// say WHICH entries those are, the apply's entries (NiD3DShaderConstantMap
// +0x10 pointer array, +0x16 count; entry +0x08 live, +0x10 predefined id,
// +0x14 type nibble, +0x1C register, +0x20 register count -
// re/functions/0x009A8E30.md) are tabled at the apply's entry, and every
// phase-1 upload is keyed by the row that holds its register.

struct ApplyRow {
	std::uint32_t reg = 0;
	std::uint32_t count = 1;  // registers from reg on
	std::uint8_t type = 0;    // the type nibble 1..6
	std::uint16_t id = 0;     // the predefined id (meaningful for type 2)
};

struct ApplyTable {
	static constexpr std::uint32_t kMaxRows = 256;
	ApplyRow rows[kMaxRows] = {};
	std::uint32_t size = 0;
	std::uint64_t dropped = 0;  // rows beyond kMaxRows

	void Clear() { size = 0; }
	void Add(const ApplyRow& row) {
		if (size >= kMaxRows) {
			++dropped;
			return;
		}
		rows[size++] = row;
	}
	// The row whose register range holds reg; null when none does.
	const ApplyRow* Find(std::uint32_t reg) const {
		for (std::uint32_t i = 0; i < size; ++i) {
			const ApplyRow& r = rows[i];
			const std::uint32_t span = r.count != 0 ? r.count : 1u;
			if (reg >= r.reg && reg < r.reg + span) {
				return &r;
			}
		}
		return nullptr;
	}
};

struct ViewKey {
	std::uint8_t type = 0;
	std::uint16_t id = 0;
	constexpr bool operator==(const ViewKey&) const = default;
};

struct ViewKeyStats {
	ViewKey key;
	std::uint64_t tabled = 0;   // entries of this key seen in the phase-1 applies (uploaded or not: the pixel map's uploads are unseen)
	std::uint64_t uploads = 0;
	std::uint64_t same = 0;     // bytes equal to the first eye's: view-independent
	std::uint64_t changed = 0;  // bytes differ: view-dependent
	std::uint64_t fresh = 0;    // no first-eye bytes to compare with
	std::uint64_t bytes = 0;
};

struct ViewStats {
	static constexpr std::uint32_t kMaxKeys = 128;
	ViewKeyStats keys[kMaxKeys] = {};
	std::uint32_t keyCount = 0;
	std::uint64_t uploads = 0;   // phase-1 uploads in all
	std::uint64_t unknown = 0;   // uploads to a register no tabled row holds
	std::uint64_t overflow = 0;  // uploads whose key found no free slot
	std::uint64_t applies = 0;   // phase-1 applies tabled

	ViewKeyStats* Slot(ViewKey key) {
		for (std::uint32_t i = 0; i < keyCount; ++i) {
			if (keys[i].key == key) {
				return &keys[i];
			}
		}
		if (keyCount >= kMaxKeys) {
			return nullptr;
		}
		keys[keyCount] = ViewKeyStats{};
		keys[keyCount].key = key;
		return &keys[keyCount++];
	}

	// An entry tabled at a phase-1 apply (counted whether or not it uploads
	// through the probed slots).
	void Tabled(const ApplyRow& row) {
		ViewKeyStats* k = Slot(ViewKey{row.type, row.id});
		if (k == nullptr) {
			++overflow;
			return;
		}
		++k->tabled;
	}

	void Add(const ApplyRow* row, PackVerdict verdict, std::uint32_t bytes) {
		++uploads;
		if (row == nullptr) {
			++unknown;
			return;
		}
		ViewKeyStats* k = Slot(ViewKey{row->type, row->id});
		if (k == nullptr) {
			++overflow;
			return;
		}
		++k->uploads;
		k->bytes += bytes;
		switch (verdict) {
		case PackVerdict::Same: ++k->same; break;
		case PackVerdict::Changed: ++k->changed; break;
		default: ++k->fresh; break;
		}
	}
	void Reset() { *this = ViewStats{}; }
};

// "Probe: view constants - N applies, N re-apply uploads (N unknown, N overflow); changed: t2:i3 1200/0/0, ...; same only: t2:i1 500, ...; tabled, no upload: t2:i5 300, ..."
// (changed/same/fresh per key; keys with any changed upload first, then the
// keys with uploads but none changed, then the keys only tabled - entries
// whose uploads the probe does not see, the pixel map's among them).
inline int FormatViewStats(const ViewStats& s, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity, "Probe: view constants - %llu applies, %llu re-apply uploads (%llu unknown, %llu overflow); changed:",
	                       static_cast<unsigned long long>(s.applies), static_cast<unsigned long long>(s.uploads),
	                       static_cast<unsigned long long>(s.unknown), static_cast<unsigned long long>(s.overflow));
	auto room = [&] { return at >= 0 && static_cast<std::size_t>(at) < capacity; };
	for (std::uint32_t i = 0; i < s.keyCount && room(); ++i) {
		const ViewKeyStats& k = s.keys[i];
		if (k.changed != 0) {
			at += std::snprintf(out + at, capacity - at, " t%u:i%u %llu/%llu/%llu", k.key.type, k.key.id, static_cast<unsigned long long>(k.changed),
			                    static_cast<unsigned long long>(k.same), static_cast<unsigned long long>(k.fresh));
		}
	}
	if (room()) {
		at += std::snprintf(out + at, capacity - at, "; same only:");
	}
	for (std::uint32_t i = 0; i < s.keyCount && room(); ++i) {
		const ViewKeyStats& k = s.keys[i];
		if (k.changed == 0 && k.uploads != 0) {
			at += std::snprintf(out + at, capacity - at, " t%u:i%u %llu", k.key.type, k.key.id, static_cast<unsigned long long>(k.same + k.fresh));
		}
	}
	if (room()) {
		at += std::snprintf(out + at, capacity - at, "; tabled, no upload:");
	}
	for (std::uint32_t i = 0; i < s.keyCount && room(); ++i) {
		const ViewKeyStats& k = s.keys[i];
		if (k.uploads == 0) {
			at += std::snprintf(out + at, capacity - at, " t%u:i%u %llu", k.key.type, k.key.id, static_cast<unsigned long long>(k.tabled));
		}
	}
	return at;
}

}  // namespace obng::plugin::probe
