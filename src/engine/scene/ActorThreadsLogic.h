#pragma once
// The actors' down passes on threads - the decisions over plain values
// ([Route] ActorThreads and [Probe] ActorThreadsCheck in the plugin's
// ActorThreads.cpp; docs/subsystems/actor-batch.md).
//
// The actors' batch ([Route] ActorBatch) runs every high actor's skeleton
// update after the actors' loop: the down passes, then the upward passes in
// the loop's order. A down pass touches one actor's subtree - except its
// collision objects: bhkNiCollisionObject::UpdateWorldData 0x0088EDC0 and the
// blend object's slot +0x64 0x0088F3D0 write into the Havok world, use the
// global list 0x00BA7A40 and some of them write the node. Here every
// collision object is put in a class by the fields its update reads, read in
// the instructions (re/export/actor-threads/, 2026-11-12):
//
//   0x0088EDC0: mode [0x00BA7A8C] 3 -> nothing; flags +0x0C bit 6 -> 0x0089EAE0
//   (nothing in mode 2); bit 0 clear -> nothing; slot +0x64 is called when
//   the body +0x10 is in the world (0x00607840: body, body+8, (body+8)+8) or
//   the blend +0x14 is below 1 or the motion type +0x1C is 1.
//   0x0088F3D0: blend 0 -> in mode 1 nothing, else case 1 (the hard set, the
//   node written from the body, 0x0089EA70) when the previous blend +0x18 is 0
//   or +0x0D bit 0 is set, else case 0; blend 1 -> in mode 2 nothing, else case 2 (+0x68, the
//   node from the body) wanting motion type 6; between (and NaN) -> case 1
//   in mode 2, else case 0, wanting motion type 1. A motion type other than
//   the wanted one changes first: from 6 through +0x68 (the node written),
//   else the global list and 0x0089ED20. Case 0 writes the node through
//   +0x78 in mode 0 and keys the body.
//
// A skeleton whose collision objects are all in a safe class (nothing, the
// keyframe alone) leaves its nodes to the down pass: its
// collision updates can wait until after the parallel down passes and run
// on the loop's thread in the batch's order. Any other skeleton runs whole on
// that thread in its place.
//
// 64-bit clean: plain values only.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::engine::scene::actor_threads {

struct CollisionFields {
	std::uint32_t mode = 0;        // [0x00BA7A8C]
	std::uint8_t flags = 0;        // +0x0C, the low byte
	bool inWorld = false;          // 0x00607840 on the body +0x10
	float blend = 0.0f;            // +0x14
	float previous = 0.0f;         // +0x18
	bool keepFlag = false;         // +0x0D bit 0
	std::uint32_t motionType = 0;  // +0x1C
	bool blendClass = false;       // its slot +0x64 is 0x0088F3D0
};

enum class CollisionClass : std::uint8_t {
	kNothing,
	kHardSet,       // case 1: 0x0089EA70, the node from the body
	kKeyframeOnly,  // case 0 outside mode 0: the body keyed, the node left
	kBlendWrite,    // case 0 in mode 0: the blended transform written to the node
	kNodeFromBody,  // case 2
	kMotionFrom6,   // a change of motion type from 6: +0x68 first
	kMotionChange,  // another change: the global list, 0x0089ED20
	kBit6,          // 0x0089EAE0 (unread)
	kOtherClass,    // slot +0x64 of another class (unread)
	kCount
};

constexpr const char* kClassName[static_cast<int>(CollisionClass::kCount)] = {
	"nothing", "hard set", "keyframe only", "blended write", "node from body", "motion from 6", "motion change", "bit 6", "other class"};

// The hard set 0x0089EA70 reads the body's transform (0x004D6950, 0x004D6900)
// and writes the node with it through slot +0x78 (read 2026-11-12; the reading
// of 2026-11-10 had it the other way) - measured to change the node in 13-45 %
// of its calls in the Market District (run obng-profile-market/20261112-221344).
constexpr bool Safe(CollisionClass c) {
	return c == CollisionClass::kNothing || c == CollisionClass::kKeyframeOnly;
}

inline CollisionClass Classify(const CollisionFields& f) {
	if (f.mode == 3) {
		return CollisionClass::kNothing;
	}
	if ((f.flags & 0x40u) != 0) {
		return f.mode == 2 ? CollisionClass::kNothing : CollisionClass::kBit6;
	}
	if ((f.flags & 0x01u) == 0) {
		return CollisionClass::kNothing;
	}
	if (!(f.inWorld || f.blend < 1.0f || f.motionType == 1)) {
		return CollisionClass::kNothing;  // slot +0x64 not called (a NaN blend is not below 1)
	}
	if (!f.blendClass) {
		return CollisionClass::kOtherClass;
	}
	int caseNo = 0;
	std::uint32_t wanted = 1;
	if (f.blend == 0.0f) {
		if (f.mode == 1) {
			return CollisionClass::kNothing;
		}
		caseNo = (f.previous == 0.0f || f.keepFlag) ? 1 : 0;
	} else if (f.blend == 1.0f) {
		if (f.mode == 2) {
			return CollisionClass::kNothing;
		}
		caseNo = 2;
		wanted = 6;
	} else {
		caseNo = f.mode == 2 ? 1 : 0;
	}
	if (f.motionType != wanted) {
		return f.motionType == 6 ? CollisionClass::kMotionFrom6 : CollisionClass::kMotionChange;
	}
	if (caseNo == 2) {
		return CollisionClass::kNodeFromBody;
	}
	if (caseNo == 1) {
		return CollisionClass::kHardSet;
	}
	return f.mode == 0 ? CollisionClass::kBlendWrite : CollisionClass::kKeyframeOnly;
}

// ---- The written statics ------------------------------------------------------

struct WrittenRun {
	std::uint32_t first = 0;  // dword index
	std::uint32_t count = 0;
};

// The runs of dwords that differ between two copies, the first `capacity`.
inline std::uint32_t WrittenRuns(const std::uint32_t* before, const std::uint32_t* after, std::uint32_t dwords, WrittenRun* out, std::uint32_t capacity) {
	std::uint32_t n = 0;
	std::uint32_t i = 0;
	while (i < dwords && n < capacity) {
		if (before[i] == after[i]) {
			++i;
			continue;
		}
		WrittenRun r;
		r.first = i;
		while (i < dwords && before[i] != after[i]) {
			++i;
		}
		r.count = i - r.first;
		out[n++] = r;
	}
	return n;
}

// How often each place was written over the probes (open addressing by the
// run's first dword; a full probe run counts as dropped).
class WriteTally {
public:
	static constexpr std::uint32_t kSlots = 1024;  // a power of two
	static constexpr std::uint32_t kProbe = 16;

	void Note(std::uint32_t first, std::uint32_t count) {
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			Slot& s = slots_[(Home(first) + i) & (kSlots - 1)];
			if (s.times != 0 && s.first == first) {
				++s.times;
				s.count = count > s.count ? count : s.count;
				return;
			}
			if (s.times == 0) {
				s.first = first;
				s.count = count;
				s.times = 1;
				++distinct_;
				return;
			}
		}
		++dropped_;
	}

	// The places written most often, the most first.
	std::uint32_t Most(std::uint32_t* first, std::uint32_t* times, std::uint32_t capacity) const {
		std::uint32_t n = 0;
		for (const Slot& s : slots_) {
			if (s.times == 0) {
				continue;
			}
			std::uint32_t at = n < capacity ? n++ : capacity;
			if (at == capacity) {
				if (s.times <= times[capacity - 1]) {
					continue;
				}
				at = capacity - 1;
			}
			while (at > 0 && times[at - 1] < s.times) {
				first[at] = first[at - 1];
				times[at] = times[at - 1];
				--at;
			}
			first[at] = s.first;
			times[at] = s.times;
		}
		return n;
	}

	std::uint32_t Distinct() const { return distinct_; }
	std::uint64_t Dropped() const { return dropped_; }
	void Clear() { *this = WriteTally{}; }

private:
	struct Slot {
		std::uint32_t first = 0;
		std::uint32_t count = 0;
		std::uint32_t times = 0;
	};
	static constexpr std::uint32_t Home(std::uint32_t x) { return (x * 2654435761u) >> 22; }
	Slot slots_[kSlots];
	std::uint32_t distinct_ = 0;
	std::uint64_t dropped_ = 0;
};

// ---- The counts ------------------------------------------------------------------

struct Counts {
	std::uint64_t batches = 0;
	std::uint64_t jobs = 0;               // skeletons split into down and up
	std::uint64_t safeJobs = 0;           // every collision object safe, nothing unread under an engine-run subtree
	std::uint64_t otherWithCollision = 0; // jobs whose engine-run subtrees (Kind::Other) hold collision objects: not safe
	std::uint64_t classes[static_cast<int>(CollisionClass::kCount)] = {};
	std::uint64_t classesWrote[static_cast<int>(CollisionClass::kCount)] = {};  // the check: the node's transforms changed
	std::uint64_t mispredicted = 0;       // the check: a safe job's collision update changed a node
	std::uint64_t parallelJobs = 0;       // run on the threads
	std::uint64_t downTicks[2] = {};      // the down phase by side: [0] one thread, [1] threads
	std::uint64_t downBatches[2] = {};
};

inline void FormatSummary(const Counts& c, std::uint32_t threads, bool check, char* out, std::size_t size) {
	int used = std::snprintf(out, size,
	                         "Route: actors' down passes on threads (%u%s) - %llu batches, %llu skeletons, %llu safe, %llu with collision objects under an "
	                         "engine-run subtree, %llu run on the threads; collision updates by class:",
	                         threads, check ? ", the check: one thread, the classes compared with the nodes" : "", static_cast<unsigned long long>(c.batches),
	                         static_cast<unsigned long long>(c.jobs), static_cast<unsigned long long>(c.safeJobs),
	                         static_cast<unsigned long long>(c.otherWithCollision), static_cast<unsigned long long>(c.parallelJobs));
	for (int i = 0; i < static_cast<int>(CollisionClass::kCount) && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		if (c.classes[i] != 0) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %llu (%llu wrote their node);", kClassName[i],
			                      static_cast<unsigned long long>(c.classes[i]), static_cast<unsigned long long>(c.classesWrote[i]));
		}
	}
	if (used > 0 && static_cast<std::size_t>(used) < size) {
		std::snprintf(out + used, size - static_cast<std::size_t>(used), " mispredicted %llu", static_cast<unsigned long long>(c.mispredicted));
	}
}

}  // namespace obng::engine::scene::actor_threads
