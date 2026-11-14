#pragma once
// The character controller's contact pass 0x008CE770 - the decisions, over
// plain values ([Route] ContactDedup in the plugin's ContactDedup.cpp).
//
// bhkCharacterPointCollector's addCdPoint 0x008CED20 calls 0x008CE770 after
// every triangle hit it appends (read: the call at 0x008CF0D2, its only
// caller). The pass (read 0x008CE770-0x008CE8E2, the predicate 0x008CE690):
//   1. for i = n-1 .. 2, for j = i-2 .. 0: when Behind(hit[j], hit[i]), hit[i]
//      is replaced by the last hit, the count drops, and i goes on (j = i-1
//      is never asked; a hit moved into i is not asked again in this pass);
//   2. with a start collector (+0x1A0): for s = m-1 .. 0, for k = n-1 .. 0 (n
//      as it stands when s begins): when Behind(start[s], hit[k]), hit[k] is
//      replaced by the last hit.
// Behind(a, b): a is a face hit (kind 1), b is not, and b lies on the inner
// side of a's plane. Run in full after every hit, a cast that collects N
// triangle hits costs about N^3 / 6 predicate calls - measured as 20 ms a
// frame for one actor at Anvil's harbour (Windows, 2026-11-13).
//
// The rebuild makes the same removals in the same order (class A: the hit
// array bit for bit) and asks only the pairs whose answer is not known: a
// memo per collector remembers how many hits the last pass left and which of
// them are new or were moved, and a pass in the same cast asks
//   - a hit that is new or was moved since against every earlier face hit,
//   - an old hit only against the face hits that are new or were moved,
//   - in step 2 the new hits against every start hit, and the old ones only
//     against start hits that are new.
// Why that is the same: a pair (a at j, b at i) with both hits in place since
// an earlier pass asked it was answered "no" there - a "yes" would have
// removed b - and the answer depends on the two hits' content alone. Every
// hit that a removal moves is marked, so no pair of a changed arrangement
// counts as asked.
//
// When the memo may be trusted: inside one cast a collector's array changes
// only by addCdPoint's appends and by this pass (read 0x008CED20, 0x008CE770);
// the proxy's integrate empties both collectors right before each cast (read
// 0x008AD7D0: the counts set to 0 at the two places before the phantom's
// linear cast 0x008ECC60). So the plugin counts the linear casts, and a memo
// of another count ("generation") is not used. A pass that moves hits in a
// collector drops what other memos know about it as their start collector.
// Beside that the last remembered hit is kept and compared: an array emptied
// and filled by a way that does not pass the counted cast is seen by it
// unless it ends in the same hit. The first version compared the whole array
// each pass - 37 MB of compares and copies in one integrate with 220 hits,
// most of the 7 ms such a call still took (measured 2026-11-14, run
// obng-stress-ride/20261114-083530).
//
// 64-bit clean: the collector is a std::uint64_t key, the hits plain arrays.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obng::engine::havok::contact_dedup {

// One collected hit, 0x30 bytes (read: the copies at 0x008CE7E1-0x008CE80C
// move two aligned 16-byte rows and four dwords).
struct Hit {
	float position[4];
	float normal[4];
	std::uint32_t word20;
	std::uint32_t kind;  // 1 a triangle's face, 2 its edge, 0 not a triangle (set by 0x008CED20)
	std::uint32_t word28;
	std::uint32_t word2C;
};
static_assert(sizeof(Hit) == 0x30, "a hit is 0x30 bytes");

inline constexpr std::uint32_t kFace = 1;

// The plane test of 0x008CE690 without the kinds: three single products
// summed as z + (y + x) (read 0x008CE6B6-0x008CE6D6), then 0 < sum by an x87
// compare that a NaN fails (0x008CE6DF-0x008CE706).
inline bool Inner(const Hit& a, const Hit& b) {
	const float x = a.normal[0] * (a.position[0] - b.position[0]);
	const float y = a.normal[1] * (a.position[1] - b.position[1]);
	const float z = a.normal[2] * (a.position[2] - b.position[2]);
	const float sum = z + (y + x);
	return 0.0f < sum;
}

// 0x008CE690 whole.
inline bool Behind(const Hit& a, const Hit& b) {
	return a.kind == kFace && b.kind != kFace && Inner(a, b);
}

// The original's pass, loop for loop (the reference of the tests and of the
// in-game check). `start` may be null.
inline void PassOriginal(Hit* hits, std::uint32_t* count, const Hit* start, std::uint32_t startCount) {
	for (std::int32_t i = static_cast<std::int32_t>(*count) - 1; i > 0; --i) {
		for (std::int32_t j = i - 2; j >= 0; --j) {
			if (Behind(hits[j], hits[i])) {
				--*count;
				hits[i] = hits[*count];
				break;
			}
		}
	}
	if (start == nullptr) {
		return;
	}
	for (std::int32_t s = static_cast<std::int32_t>(startCount) - 1; s >= 0; --s) {
		for (std::int32_t k = static_cast<std::int32_t>(*count) - 1; k >= 0; --k) {
			if (Behind(start[s], hits[k])) {
				--*count;
				hits[k] = hits[*count];
			}
		}
	}
}

// What a pass did, for the summary.
struct Work {
	std::uint64_t asked = 0;      // plane tests made
	std::uint64_t original = 0;   // predicate calls the original's loops would have made at the least: (n-1)(n-2)/2 + m*n
	std::uint64_t removed = 0;
	bool remembered = false;      // the memo matched: the array began as the last pass left it
	bool plain = false;           // more hits than a memo holds: the original's loops
};

// What is remembered of one collector's last pass.
struct Memo {
	static constexpr std::uint32_t kCap = 256;
	std::uint64_t key = 0;         // 0: empty
	std::uint64_t generation = 0;  // the cast it was made in
	std::uint32_t count = 0;       // hits the pass left
	std::uint64_t startKey = 0;    // the start collector its step 2 asked
	std::uint32_t startCount = 0;  // ... and how many of its hits
	Hit last;                      // the hit at count - 1, as left
	std::uint8_t fresh[kCap];      // the hit at this index is new or was moved since it was last asked as b
};

// The memos of the collectors in work: a controller's integrate uses two at a time.
struct Memos {
	static constexpr unsigned kSlots = 4;
	Memo slots[kSlots];
	unsigned next = 0;

	Memo& For(std::uint64_t key) {
		for (Memo& memo : slots) {
			if (memo.key == key && key != 0) {
				return memo;
			}
		}
		Memo& memo = slots[next];
		next = (next + 1) % kSlots;
		memo.key = 0;
		return memo;
	}

	// A pass moved hits in this collector: what others asked of it as their start collector is not known any more.
	void Moved(std::uint64_t key) {
		for (Memo& memo : slots) {
			if (memo.startKey == key) {
				memo.startCount = 0;
			}
		}
	}
};

namespace detail {

inline bool SameHit(const Hit& a, const Hit& b) {
	return std::memcmp(&a, &b, sizeof(Hit)) == 0;
}

// Step 1. `fresh` holds the pass's begin on entry (1 for a hit new or moved)
// and the next pass's on return; `untested` (step 2's) follows the hits.
inline void StepOne(Hit* hits, std::uint32_t* count, std::uint8_t* fresh, std::uint8_t* untested, Work* work) {
	std::uint32_t freshAt[Memo::kCap];
	std::uint32_t freshCount = 0;
	const std::uint32_t n = *count;
	for (std::uint32_t j = 0; j < n; ++j) {
		if (fresh[j] != 0 && hits[j].kind == kFace) {
			freshAt[freshCount++] = j;  // ascending
		}
	}
	for (std::int32_t i = static_cast<std::int32_t>(n) - 1; i >= 2; --i) {
		const Hit& b = hits[i];
		bool remove = false;
		if (b.kind != kFace) {
			if (fresh[i] != 0) {
				for (std::int32_t j = i - 2; j >= 0 && !remove; --j) {
					if (hits[j].kind == kFace) {
						++work->asked;
						remove = Inner(hits[j], b);
					}
				}
			} else {
				for (std::uint32_t at = freshCount; at-- > 0 && !remove;) {
					if (freshAt[at] + 2 <= static_cast<std::uint32_t>(i)) {
						++work->asked;
						remove = Inner(hits[freshAt[at]], b);
					}
				}
			}
		}
		if (!remove) {
			fresh[i] = 0;
			continue;
		}
		const std::uint32_t last = --*count;
		++work->removed;
		hits[i] = hits[last];
		fresh[i] = 1;  // moved: not asked at this index, and new to the hits above it
		untested[i] = untested[last];
	}
	if (n > 0) {
		fresh[0] = 0;
	}
	if (n > 1) {
		fresh[1] = 0;
	}
}

// Step 2. Start hits from `knownStart` on are new to every hit; the ones
// before it only to the hits marked `untested`.
inline void StepTwo(Hit* hits, std::uint32_t* count, const Hit* start, std::uint32_t startCount, std::uint32_t knownStart, std::uint8_t* fresh,
                    std::uint8_t* untested, Work* work) {
	auto removeAt = [&](std::uint32_t k) {
		const std::uint32_t last = --*count;
		++work->removed;
		hits[k] = hits[last];
		fresh[k] = 1;
		untested[k] = untested[last];
		untested[last] = 0;
	};
	std::int32_t s = static_cast<std::int32_t>(startCount) - 1;
	for (; s >= static_cast<std::int32_t>(knownStart); --s) {
		if (start[s].kind != kFace) {
			continue;
		}
		for (std::int32_t k = static_cast<std::int32_t>(*count) - 1; k >= 0; --k) {
			if (hits[k].kind != kFace) {
				++work->asked;
				if (Inner(start[s], hits[k])) {
					removeAt(static_cast<std::uint32_t>(k));
				}
			}
		}
	}
	if (s < 0) {
		return;
	}
	// The hits the old start hits have not been asked about, ascending. A
	// removal takes the array's last hit; when that one is in the list it is
	// the list's last entry.
	std::uint32_t list[Memo::kCap];
	std::uint32_t listed = 0;
	for (std::uint32_t k = 0; k < *count; ++k) {
		if (untested[k] != 0 && hits[k].kind != kFace) {
			list[listed++] = k;
		}
	}
	for (; s >= 0 && listed != 0; --s) {
		if (start[s].kind != kFace) {
			continue;
		}
		for (std::uint32_t at = listed; at-- > 0;) {
			const std::uint32_t k = list[at];
			++work->asked;
			if (!Inner(start[s], hits[k])) {
				continue;
			}
			const std::uint32_t last = *count - 1;
			const bool lastListed = list[listed - 1] == last;
			removeAt(k);
			if (lastListed) {
				--listed;  // the last hit left its index: to k (asked already for this s), or away when it was k itself
			} else {
				for (std::uint32_t i = at; i + 1 < listed; ++i) {  // an old hit or a face came to k: nothing to ask there
					list[i] = list[i + 1];
				}
				--listed;
			}
		}
	}
}

}  // namespace detail

// The pass with the memos. `generation`: the count of casts begun (a memo of
// another one is not used). `startKey`: the start collector, 0 with none. The
// hits and the count are left as PassOriginal leaves them.
inline Work Pass(Memos& memos, std::uint64_t key, std::uint64_t generation, Hit* hits, std::uint32_t* count, std::uint64_t startKey, const Hit* start,
                 std::uint32_t startCount) {
	Work work;
	const std::uint32_t n = *count;
	const std::uint32_t m = start != nullptr ? startCount : 0;
	work.original = (n >= 3 ? static_cast<std::uint64_t>(n - 1) * (n - 2) / 2 : 0) + static_cast<std::uint64_t>(m) * n;
	Memo& memo = memos.For(key);
	if (n > Memo::kCap || m > Memo::kCap) {
		work.plain = true;
		work.asked = work.original;
		PassOriginal(hits, count, start, startCount);
		work.removed = n - *count;
		memo.key = 0;
		if (work.removed != 0) {
			memos.Moved(key);
		}
		return work;
	}
	std::uint8_t untested[Memo::kCap];
	std::uint32_t known = 0;       // hits from the last pass, unchanged
	std::uint32_t knownStart = 0;  // start hits the known hits were asked against
	if (memo.key == key && key != 0 && memo.generation == generation && memo.count <= n &&
	    (memo.count == 0 || detail::SameHit(hits[memo.count - 1], memo.last))) {
		known = memo.count;
		work.remembered = known != 0;
		if (memo.startKey == startKey && memo.startCount <= m) {
			knownStart = memo.startCount;
		}
	}
	for (std::uint32_t k = 0; k < n; ++k) {
		if (k >= known) {
			memo.fresh[k] = 1;
		}
		untested[k] = k >= known ? 1 : 0;
	}
	detail::StepOne(hits, count, memo.fresh, untested, &work);
	if (m != 0) {
		detail::StepTwo(hits, count, start, m, knownStart, memo.fresh, untested, &work);
	}
	memo.key = key;
	memo.generation = generation;
	memo.count = *count;
	memo.startKey = startKey;
	memo.startCount = m;
	if (*count != 0) {
		memo.last = hits[*count - 1];
	}
	if (work.removed != 0) {
		memos.Moved(key);
	}
	return work;
}

// ---- The summary ---------------------------------------------------------------------

struct Counts {
	std::uint64_t passes = 0;
	std::uint64_t remembered = 0;
	std::uint64_t plain = 0;
	std::uint64_t busy = 0;         // left to the original: another thread was inside the rebuild
	std::uint64_t asked = 0;
	std::uint64_t original = 0;
	std::uint64_t removed = 0;
	std::uint32_t mostHits = 0;
	std::uint32_t mostStart = 0;
	std::uint64_t compared = 0;     // the check: passes run both ways
	std::uint64_t mismatches = 0;

	void Add(const Work& w, std::uint32_t hits, std::uint32_t start) {
		++passes;
		remembered += w.remembered ? 1 : 0;
		plain += w.plain ? 1 : 0;
		asked += w.asked;
		original += w.original;
		removed += w.removed;
		mostHits = hits > mostHits ? hits : mostHits;
		mostStart = start > mostStart ? start : mostStart;
	}
};

inline int FormatSummary(const Counts& c, bool check, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity,
	                       "Route: contact pass 0x008CE770 - %llu passes (%llu on a remembered array, %llu by the original's loops for their size, %llu left to "
	                       "the original for another thread), the most hits %u and start hits %u; %llu plane tests against %llu predicate calls of the "
	                       "original; %llu hits removed",
	                       static_cast<unsigned long long>(c.passes), static_cast<unsigned long long>(c.remembered), static_cast<unsigned long long>(c.plain),
	                       static_cast<unsigned long long>(c.busy), c.mostHits, c.mostStart, static_cast<unsigned long long>(c.asked),
	                       static_cast<unsigned long long>(c.original), static_cast<unsigned long long>(c.removed));
	if (check && at > 0 && static_cast<std::size_t>(at) < capacity) {
		at += std::snprintf(out + at, capacity - static_cast<std::size_t>(at), "; checked against the original: %llu passes, %llu mismatches",
		                    static_cast<unsigned long long>(c.compared), static_cast<unsigned long long>(c.mismatches));
	}
	return at;
}

}  // namespace obng::engine::havok::contact_dedup
