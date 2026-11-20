// Every flow of FaceGen's data loads moved out of the cache's lock (src/engine/facegen/FaceGenLoadLogic.h).

#include <cstring>

#include "Check.h"
#include "engine/facegen/FaceGenLoadLogic.h"

using namespace obng::engine::facegen::load;

int main() {
	Section("The INI's values");
	Check(!OnOf(0) && OnOf(1) && !OnOf(2) && !OnOf(-1), "1 is on, anything else is off");
	Check(Shipped() == 0, "off as shipped until the game's gates have run");

	Section("A data object's path length (the engine's string: a 16-bit count, 0xFFFF = count the characters)");
	Check(PathLength(0, 99) == 0, "a stored 0 is an empty path");
	Check(PathLength(41, 99) == 41, "a stored count is the length");
	Check(PathLength(0xFFFF, 70000) == 70000, "0xFFFF: the counted characters");

	Section("Whether a load would read the file (0x00559190 / 0x00558520, read)");
	Check(!Needs(Part{false, false, 20}), "no data object: nothing to load");
	Check(!Needs(Part{true, true, 20}), "the data is there: nothing to load");
	Check(!Needs(Part{true, false, 0}), "an empty path: nothing to load");
	Check(Needs(Part{true, false, 20}), "an object with a path and no data: the file is read");

	Section("Where the load runs");
	Check(WhereOf(true, true, 1, 1) == Where::Outside, "routed, a file to read, the lock entered once: outside the cache's lock");
	Check(WhereOf(false, true, 1, 1) == Where::UnderCacheLock, "not routed: as the game does it");
	Check(WhereOf(true, false, 1, 1) == Where::UnderCacheLock, "nothing to read: the lock is not given up for it");
	Check(WhereOf(true, true, 2, 1) == Where::UnderCacheLock, "the engine's own depth says a caller holds the lock too: it cannot be given up");
	Check(WhereOf(true, true, 1, 2) == Where::UnderCacheLock, "the critical section's count says so: the same");
	Check(WhereOf(true, true, 0, 1) == Where::UnderCacheLock && WhereOf(true, true, 1, 0) == Where::UnderCacheLock,
	      "a depth of 0 is not the state the patch was read in: as the game does it");

	Section("The head synthesis's lock");
	std::uint8_t bytes[5];
	MovEcx(kCacheLock, bytes);
	Check(bytes[0] == 0xB9 && bytes[1] == 0x00 && bytes[2] == 0x9C && bytes[3] == 0xB3 && bytes[4] == 0x00, "the game's own instruction: mov ecx, 0x00B39C00");
	MovEcx(0x12345678u, bytes);
	Check(bytes[0] == 0xB9 && bytes[1] == 0x78 && bytes[2] == 0x56 && bytes[3] == 0x34 && bytes[4] == 0x12, "another lock's address, little endian");
	Check(sizeof(kSynthesisLockSites) / sizeof(kSynthesisLockSites[0]) == 4, "two brackets, an enter and a leave each");
	Check(kLockOwner == 0x78 && kLockDepth == 0x7C && kLockSize == 0x80, "the engine lock's shape the helpers write to");

	Section("The lock a model's loads take in turn");
	Check(kStripes == 16, "sixteen");
	Check(StripeOf(0x12345670u) == StripeOf(0x12345670u), "the same model, the same lock");
	Check(StripeOf(0x12345670u) < kStripes && StripeOf(0xFFFFFFFFu) < kStripes && StripeOf(0) < kStripes, "always one of them");
	bool seen[kStripes] = {};
	for (std::uint32_t i = 0; i < 64; ++i) {
		seen[StripeOf(0x10000000u + i * 0x20u)] = true;  // models are heap objects of 0x1C bytes, some 0x20 apart
	}
	unsigned used = 0;
	for (bool s : seen) {
		used += s ? 1 : 0;
	}
	Check(used == kStripes, "neighbouring heap objects spread over all of them");

	Section("The totals");
	Totals totals;
	Add(totals, Load{Kind::Egm, true, false, 100, 0});
	Add(totals, Load{Kind::Egm, true, false, 300, 0});
	Add(totals, Load{Kind::Egt, false, true, 900, 40});
	Add(totals, Load{Kind::Egt, false, false, 50, 0});
	Check(totals.of[0][0].loads == 2 && totals.of[0][0].ticks == 400 && totals.of[0][0].longest == 300, "shape loads on the loader's thread: count, sum, longest");
	Check(totals.of[1][1].loads == 2 && totals.of[1][1].ticks == 950 && totals.of[1][1].longest == 900, "texture loads on another thread");
	Check(totals.of[0][1].loads == 0 && totals.of[1][0].loads == 0, "the other two cells untouched");
	Check(totals.outside == 1 && totals.waitTicks == 40 && totals.longestWait == 40, "one of them outside the lock, with its wait for the locks");
	Add(totals, Load{Kind::Egm, true, true, 10, 15});
	Check(totals.outside == 2 && totals.waitTicks == 55 && totals.longestWait == 40, "waits sum, the longest stays");
	CountNested(totals);
	Check(totals.nested == 1, "a load kept under the lock because a caller held it is counted");

	Section("The long loads kept for the log");
	Check(!IsLong(99, 100) && IsLong(100, 100) && IsLong(101, 100), "at the limit or over");
	LongLoads longs;
	char name[kNameSize];
	Tail("meshes\\characters\\imperial\\headhuman.egt", name);
	Check(std::strcmp(name, "meshes\\characters\\imperial\\headhuman.egt") == 0, "a short path is kept whole");
	char longPath[200];
	std::memset(longPath, 'a', sizeof(longPath));
	std::memcpy(longPath + 190, "tail.egt", 9);
	Tail(longPath, name);
	Check(std::strlen(name) == kNameSize - 1 && std::strcmp(name + kNameSize - 9, "tail.egt") == 0, "a long path keeps its end");
	Tail(nullptr, name);
	Check(name[0] == '\0', "no path: an empty name");
	for (unsigned i = 0; i < kLongLoads + 3; ++i) {
		LongLoad one;
		one.load = Load{Kind::Egt, false, true, 1000 + i, 0};
		one.thread = 7;
		one.endedAt = 5000 + i;
		Tail("x.egt", one.name);
		Keep(longs, one);
	}
	Check(longs.count == kLongLoads && longs.dropped == 3, "the list is full at its size; the rest is counted");
	Check(longs.loads[0].load.ticks == 1000 && longs.loads[kLongLoads - 1].load.ticks == 1000 + kLongLoads - 1, "the first ones are the kept ones");
	return Failures();
}
