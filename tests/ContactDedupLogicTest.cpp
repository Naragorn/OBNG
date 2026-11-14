// The character controller's contact pass (src/engine/havok/ContactDedupLogic.h):
// the predicate, the original's loops on hand-made arrays, and the pass with
// a memo against the original's loops over many random runs of a collector's
// life - hits appended one at a time with a pass after each, hits appended
// without a pass, casts begun anew, start hits growing and changing, arrays
// written from outside, and more hits than a memo holds.

#include "Check.h"
#include "engine/havok/ContactDedupLogic.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace cd = obng::engine::havok::contact_dedup;
using cd::Hit;

namespace {

Hit Make(float x, float y, float z, float nx, float ny, float nz, std::uint32_t kind, std::uint32_t tag = 0) {
	Hit h{};
	h.position[0] = x;
	h.position[1] = y;
	h.position[2] = z;
	h.normal[0] = nx;
	h.normal[1] = ny;
	h.normal[2] = nz;
	h.kind = kind;
	h.word20 = tag;
	h.word28 = tag * 3 + 1;
	h.word2C = tag ^ 0x5A5A5A5Au;
	return h;
}

bool Same(const std::vector<Hit>& a, std::uint32_t na, const std::vector<Hit>& b, std::uint32_t nb) {
	return na == nb && (na == 0 || std::memcmp(a.data(), b.data(), na * sizeof(Hit)) == 0);
}

struct Random {
	std::uint32_t state;
	std::uint32_t Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	std::uint32_t Below(std::uint32_t n) { return Next() % n; }
	float Coordinate() { return static_cast<float>(static_cast<int>(Below(7)) - 3); }
	Hit RandomHit(std::uint32_t tag, unsigned facePercent) {
		const std::uint32_t roll = Below(100);
		const std::uint32_t kind = roll < facePercent ? 1u : roll < facePercent + (100 - facePercent) / 2 ? 2u : 0u;
		Hit h = Make(Coordinate(), Coordinate(), Coordinate(), Coordinate(), Coordinate(), Coordinate(), kind, tag);
		if (Below(40) == 0) {
			h.position[Below(3)] = std::numeric_limits<float>::quiet_NaN();
		}
		return h;
	}
};

void TestPredicate() {
	Section("Behind: a face hit, a hit that is no face, and the plane's inner side");
	const Hit face = Make(0, 0, 0, 0, 0, 1, 1);
	Check(cd::Behind(face, Make(0, 0, -1, 0, 0, 0, 2)) && cd::Behind(face, Make(5, 5, -0.5f, 0, 0, 0, 0)), "an edge hit and a plain hit below the face's plane");
	Check(!cd::Behind(face, Make(0, 0, 1, 0, 0, 0, 2)), "above the plane: no");
	Check(!cd::Behind(face, Make(3, 4, 0, 0, 0, 0, 2)), "in the plane (the sum is 0): no");
	Check(!cd::Behind(face, Make(0, 0, -1, 0, 0, 0, 1)), "b a face hit: no");
	Check(!cd::Behind(Make(0, 0, 0, 0, 0, 1, 2), Make(0, 0, -1, 0, 0, 0, 2)) && !cd::Behind(Make(0, 0, 0, 0, 0, 1, 0), Make(0, 0, -1, 0, 0, 0, 2)), "a no face hit: no");
	Hit nan = Make(0, 0, -1, 0, 0, 0, 2);
	nan.position[2] = std::numeric_limits<float>::quiet_NaN();
	Check(!cd::Behind(face, nan), "a NaN compares false");
	const Hit tilted = Make(1, 2, 3, 0.5f, -0.25f, 2.0f, 1);
	const Hit b = Make(0.5f, 1.0f, 2.5f, 9, 9, 9, 2);
	Check(cd::Inner(tilted, b) == (0.0f < 2.0f * 0.5f + (-0.25f * 1.0f + 0.5f * 0.5f)), "the sum is z + (y + x) of single products; b's normal plays no part");
}

void TestOriginal() {
	Section("The original's loops");
	const Hit face = Make(0, 0, 0, 0, 0, 1, 1, 1);
	const Hit below1 = Make(0, 0, -1, 0, 0, 0, 2, 2);
	const Hit below2 = Make(1, 0, -2, 0, 0, 0, 2, 3);
	const Hit above = Make(0, 0, 4, 0, 0, 0, 2, 4);
	std::vector<Hit> hits = {face, below1, below2, above};
	std::uint32_t count = 4;
	cd::PassOriginal(hits.data(), &count, nullptr, 0);
	Check(count == 3 && hits[0].word20 == 1 && hits[1].word20 == 2 && hits[2].word20 == 4,
	      "the neighbour above a face (j = i - 1) is never asked; a removed hit is replaced by the last");
	hits = {below1, face, below2};
	count = 3;
	cd::PassOriginal(hits.data(), &count, nullptr, 0);
	Check(count == 3, "a face is asked only for hits after it, two or more on");
	hits = {face, above, below1, below2};
	count = 4;
	cd::PassOriginal(hits.data(), &count, nullptr, 0);
	Check(count == 2 && hits[0].word20 == 1 && hits[1].word20 == 4, "two removed: the last by itself, the one before by the count");
	for (std::uint32_t n = 0; n <= 2; ++n) {
		hits = {face, below1};
		count = n;
		cd::PassOriginal(hits.data(), &count, nullptr, 0);
		Check(count == n, "none, one or two hits: nothing asked");
	}
	Section("The start collector's hits against every hit");
	std::vector<Hit> start = {Make(0, 0, 10, 0, 0, 1, 2, 9), face};
	hits = {below1, above, below2};
	count = 3;
	cd::PassOriginal(hits.data(), &count, start.data(), 2);
	Check(count == 1 && hits[0].word20 == 4, "everything behind a start face hit goes, index 0 and 1 too; a start hit that is no face removes nothing");
	hits = {below1, above};
	count = 2;
	cd::PassOriginal(hits.data(), &count, start.data(), 0);
	Check(count == 2, "no start hits: nothing");
	count = 0;
	cd::PassOriginal(hits.data(), &count, start.data(), 2);
	Check(count == 0, "no hits: nothing");
}

// One collector's life: what the engine does to the two arrays between
// passes, by a random script; after every pass the memo's array must be the
// original's, bit for bit.
struct Tally {
	std::uint64_t passes = 0;
	std::uint64_t remembered = 0;
	std::uint64_t plain = 0;
	std::uint64_t asked = 0;
	std::uint64_t original = 0;
	std::uint64_t removed = 0;
	std::uint64_t failures = 0;
};

void RunLife(std::uint32_t seed, unsigned facePercent, std::uint32_t steps, std::uint32_t capacity, Tally* tally, cd::Memo* memo) {
	Random random{seed};
	std::vector<Hit> reference(capacity);
	std::vector<Hit> rebuilt(capacity);
	std::vector<Hit> start(capacity);
	std::uint32_t count = 0;
	std::uint32_t startCount = 0;
	std::uint32_t tag = 1;
	std::uint64_t key = 0x1000 + seed % 3;
	for (std::uint32_t step = 0; step < steps; ++step) {
		const std::uint32_t what = random.Below(100);
		bool pass = false;
		if (what < 62 && count < capacity) {  // a triangle hit: appended, then the pass
			reference[count] = rebuilt[count] = random.RandomHit(tag++, facePercent);
			++count;
			pass = true;
		} else if (what < 70 && count < capacity) {  // a hit of a body that is no triangle: appended, no pass
			Hit h = random.RandomHit(tag++, 0);
			h.kind = 0;
			reference[count] = rebuilt[count] = h;
			++count;
		} else if (what < 80 && startCount < capacity) {  // the start collector gets a hit
			start[startCount++] = random.RandomHit(tag++, facePercent);
		} else if (what < 84) {  // a new cast: both arrays emptied
			count = 0;
			startCount = random.Below(2) == 0 ? 0 : startCount;
		} else if (what < 87 && startCount > 1) {  // the start collector's own pass moved its hits
			start[random.Below(startCount)] = start[--startCount];
		} else if (what < 89 && count > 0) {  // the array written by someone else
			const std::uint32_t at = random.Below(count);
			reference[at] = rebuilt[at] = random.RandomHit(tag++, facePercent);
		} else if (what < 91 && count > 0) {  // truncated
			count = random.Below(count + 1);
		} else if (what < 93) {  // another collector at the memo's key, or the memo's key at another collector
			key = 0x1000 + random.Below(3);
		} else if (what < 95 && startCount > 0) {  // a start hit rewritten in place
			start[random.Below(startCount)] = random.RandomHit(tag++, facePercent);
		} else if (count > 0) {
			pass = true;  // a pass with nothing new
		}
		if (!pass) {
			continue;
		}
		const bool withStart = random.Below(8) != 0;
		std::uint32_t referenceCount = count;
		std::uint32_t rebuiltCount = count;
		cd::PassOriginal(reference.data(), &referenceCount, withStart ? start.data() : nullptr, startCount);
		const cd::Work work = cd::Pass(*memo, key, rebuilt.data(), &rebuiltCount, withStart ? start.data() : nullptr, startCount);
		++tally->passes;
		tally->remembered += work.remembered ? 1 : 0;
		tally->plain += work.plain ? 1 : 0;
		tally->asked += work.asked;
		tally->original += work.original;
		tally->removed += work.removed;
		if (!Same(reference, referenceCount, rebuilt, rebuiltCount) || work.removed != count - referenceCount) {
			++tally->failures;
			rebuilt = reference;  // go on from the original's state
		}
		count = referenceCount;
	}
}

void TestAgainstOriginal() {
	Section("The pass with a memo against the original's loops, random lives of a collector");
	static cd::Memo memo;
	for (const unsigned facePercent : {0u, 10u, 35u, 70u, 100u}) {
		Tally tally;
		for (std::uint32_t seed = 1; seed <= 400; ++seed) {
			RunLife(seed * 2654435761u + facePercent, facePercent, 400, 64, &tally, &memo);
		}
		std::printf("    %u %% faces: %llu passes, %llu with the memo, %llu hits removed, %llu plane tests against %llu predicate calls of the original\n", facePercent,
		            static_cast<unsigned long long>(tally.passes), static_cast<unsigned long long>(tally.remembered),
		            static_cast<unsigned long long>(tally.removed), static_cast<unsigned long long>(tally.asked), static_cast<unsigned long long>(tally.original));
		Check(tally.failures == 0 && tally.passes > 50000, "every pass leaves the original's array, bit for bit");
		Check(tally.remembered > tally.passes / 2, "most passes found their array as the last one left it");
		Check(facePercent == 0 || facePercent == 100 || tally.removed > 1000, "hits were removed");
		Check(tally.asked < tally.original, "fewer plane tests than the original's predicate calls");
	}

	Section("More hits than a memo holds: the original's loops, and the memo forgotten");
	static cd::Memo big;
	const std::uint32_t most = cd::Memo::kCap + 40;
	std::vector<Hit> reference(most);
	std::vector<Hit> rebuilt(most);
	std::vector<Hit> starts(most);
	Random fill{99};
	std::uint32_t size = 0;
	std::uint32_t plain = 0;
	std::uint32_t remembered = 0;
	std::uint32_t failures = 0;
	auto grow = [&](std::uint32_t until, std::uint32_t startCount) {
		while (size < until) {
			Hit h = fill.RandomHit(size, 60);
			h.position[2] = h.kind == 1 ? 0.0f : 3.0f;  // edge hits outside every face's plane: none removed, the array grows
			h.normal[0] = h.normal[1] = 0.0f;
			h.normal[2] = 1.0f;
			reference[size] = rebuilt[size] = h;
			++size;
			std::uint32_t a = size;
			std::uint32_t b = size;
			cd::PassOriginal(reference.data(), &a, starts.data(), startCount);
			const cd::Work work = cd::Pass(big, 3, rebuilt.data(), &b, starts.data(), startCount);
			plain += work.plain ? 1 : 0;
			remembered += work.remembered ? 1 : 0;
			failures += Same(reference, a, rebuilt, b) ? 0 : 1;
			size = a;
		}
	};
	grow(most, 0);
	Check(failures == 0 && plain == 40 && remembered == cd::Memo::kCap - 1, "up to the memo's size with it, beyond it by the original's loops");
	size = 100;
	plain = remembered = 0;
	grow(140, 0);
	Check(failures == 0 && plain == 0 && remembered == 39, "back under the size: the first pass asks anew (the memo was forgotten), then it is trusted again");
	for (std::uint32_t i = 0; i < most; ++i) {
		starts[i] = Make(0, 0, -50, 0, 0, 1, i % 2 == 0 ? 1u : 2u, 1000 + i);
	}
	size = 20;
	plain = remembered = 0;
	grow(30, most);
	Check(failures == 0 && plain == 10, "more start hits than the memo holds: the original's loops too");

	Section("A cast that collects N hits: the work");
	static cd::Memo line;
	std::vector<Hit> hits(200);
	std::uint32_t count = 0;
	std::uint64_t asked = 0;
	std::uint64_t original = 0;
	Random random{12345};
	for (std::uint32_t i = 0; i < 120; ++i) {
		// Faces of one tilted plane and edge hits on its outer side: nothing is removed, the array only grows.
		const bool face = i % 2 == 0;
		hits[count++] = Make(static_cast<float>(i), random.Coordinate(), face ? 0.0f : 5.0f, 0, 0, 1, face ? 1 : 2, i);
		const cd::Work work = cd::Pass(line, 77, hits.data(), &count, nullptr, 0);
		asked += work.asked;
		original += work.original;
	}
	std::printf("    120 hits, half faces, none removed: %llu plane tests against %llu predicate calls of the original\n", static_cast<unsigned long long>(asked),
	            static_cast<unsigned long long>(original));
	Check(count == 120 && original == 280840 && asked < 4000, "each new hit is asked once against the faces before it: thousands instead of hundreds of thousands");
}

void TestMemoEdges() {
	Section("The memo is trusted only for the same key and the same bytes");
	static cd::Memo memo;
	std::vector<Hit> hits = {Make(0, 0, 0, 0, 0, 1, 1, 1), Make(0, 0, 3, 0, 0, 0, 2, 2), Make(0, 0, 4, 0, 0, 0, 2, 3)};
	std::uint32_t count = 3;
	cd::Work work = cd::Pass(memo, 5, hits.data(), &count, nullptr, 0);
	Check(!work.remembered && count == 3 && work.asked == 1, "a first pass: nothing remembered, the one pair asked");
	work = cd::Pass(memo, 5, hits.data(), &count, nullptr, 0);
	Check(work.remembered && work.asked == 0, "the same array again: nothing to ask");
	work = cd::Pass(memo, 6, hits.data(), &count, nullptr, 0);
	Check(!work.remembered && work.asked == 1, "another key: asked anew");
	work = cd::Pass(memo, 0, hits.data(), &count, nullptr, 0);
	Check(!work.remembered, "key 0 is never remembered");
	work = cd::Pass(memo, 0, hits.data(), &count, nullptr, 0);
	Check(!work.remembered, "not even by itself");
	cd::Pass(memo, 5, hits.data(), &count, nullptr, 0);
	hits[2].position[2] = -4.0f;  // the edge hit now lies behind the face
	work = cd::Pass(memo, 5, hits.data(), &count, nullptr, 0);
	Check(!work.remembered && count == 2 && work.removed == 1, "a byte changed: asked anew, and the hit goes");
	std::uint32_t none = 0;
	work = cd::Pass(memo, 5, hits.data(), &none, nullptr, 0);
	Check(none == 0 && work.asked == 0 && work.original == 0 && !work.remembered, "no hits: nothing");
	std::vector<Hit> start = {Make(0, 0, 10, 0, 0, 1, 1, 8)};
	hits = {Make(0, 0, 3, 0, 0, 0, 2, 2), Make(0, 0, 4, 0, 0, 0, 2, 3)};
	count = 2;
	work = cd::Pass(memo, 9, hits.data(), &count, start.data(), 1);
	Check(count == 0 && work.removed == 2 && work.original == 2, "two hits, both behind the start collector's face: both go");
}

void TestSummary() {
	Section("The summary line");
	cd::Counts c;
	cd::Work w;
	w.asked = 10;
	w.original = 400;
	w.removed = 2;
	w.remembered = true;
	c.Add(w, 30, 12);
	cd::Work p;
	p.plain = true;
	p.asked = p.original = 50000;
	c.Add(p, 300, 5);
	c.busy = 1;
	char line[600];
	cd::FormatSummary(c, false, line, sizeof(line));
	Check(std::string(line) ==
	          "Route: contact pass 0x008CE770 - 2 passes (1 on a remembered array, 1 by the original's loops for their size, 1 left to the original for "
	          "another thread), the most hits 300 and start hits 12; 50010 plane tests against 50400 predicate calls of the original; 2 hits removed",
	      "the counts summed, the largest arrays kept");
	c.compared = 7;
	c.mismatches = 0;
	cd::FormatSummary(c, true, line, sizeof(line));
	Check(std::string(line).find("; checked against the original: 7 passes, 0 mismatches") != std::string::npos, "with the check: its two counts");
	char small[30];
	Check(cd::FormatSummary(c, true, small, sizeof(small)) >= static_cast<int>(sizeof(small)) && small[sizeof(small) - 1] == 0, "a buffer too small is not overrun");
}

}  // namespace

int main() {
	TestSummary();
	TestPredicate();
	TestOriginal();
	TestAgainstOriginal();
	TestMemoEdges();
	return Failures();
}
