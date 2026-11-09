// Every flow of the probes' logic (plugin/src/ProbeLogic.h): the return-site
// table (new site, known site, the table full), the call counter, the
// memory peak over samples, the address-space arithmetic, and both lines.

#include "Check.h"
#include "ProbeLogic.h"

#include <cstring>
#include <string>

using namespace obng::plugin::probe;

namespace {

void TestReturnSites() {
	Section("Return sites: first come, first kept; the rest counted as others");
	ReturnSites sites;
	sites.Add(0x00401000u);
	sites.Add(0x00401000u);
	sites.Add(0x00402000u);
	Check(sites.used == 2 && sites.sites[0].address == 0x00401000u && sites.sites[0].count == 2 &&
	          sites.sites[1].address == 0x00402000u && sites.sites[1].count == 1 && sites.others == 0,
	      "two sites, the first seen twice, in first-seen order");
	for (unsigned i = 2; i < ReturnSites::kSites; ++i) {
		sites.Add(0x00410000u + i);
	}
	Check(sites.used == ReturnSites::kSites, "the table holds kSites distinct addresses");
	sites.Add(0x00499999u);
	sites.Add(0x00499998u);
	sites.Add(0x00401000u);
	Check(sites.used == ReturnSites::kSites && sites.others == 2 && sites.sites[0].count == 3,
	      "a ninth address is counted as others; a known one still counts in its slot");
	sites.Reset();
	Check(sites.used == 0 && sites.others == 0 && sites.sites[0].count == 0, "reset empties it");
}

void TestCallCounts() {
	Section("Call counts: every call counted once and attributed to its site");
	CallCounts counts;
	counts.Add(0x00500000u);
	counts.Add(0x00500000u);
	counts.Add(0x00500010u);
	Check(counts.calls == 3 && counts.sites.used == 2, "three calls, two sites");
	counts.Reset();
	Check(counts.calls == 0 && counts.sites.used == 0, "reset");
}

void TestMemoryPeak() {
	Section("Memory peak: the largest of each, the latest total, the smallest available");
	MemoryPeak peak;
	Check(!peak.any, "nothing sampled yet");
	MemorySample a{100, 200, 250, 4000, 3000};
	MemorySample b{150, 180, 260, 4000, 2500};
	MemorySample c{120, 190, 260, 4000, 2800};
	peak.Add(a);
	Check(peak.any && peak.peak.commitBytes == 100 && peak.peak.virtualAvailableBytes == 3000, "the first sample is the peak");
	peak.Add(b);
	peak.Add(c);
	Check(peak.peak.commitBytes == 150 && peak.peak.workingSetBytes == 200 && peak.peak.peakWorkingSetBytes == 260 &&
	          peak.peak.virtualTotalBytes == 4000 && peak.peak.virtualAvailableBytes == 2500,
	      "commit 150 (b), working set 200 (a), peak working set 260, available 2500 (b)");
	Check(VirtualUsedBytes(peak.peak) == 1500 && VirtualUsedBytes(c) == 1200, "used = total - available");
	MemorySample odd{0, 0, 0, 10, 20};
	Check(VirtualUsedBytes(odd) == 0, "an available above the total (never on Windows) gives 0, not a wrap");
}

void TestArithmetic() {
	Section("Megabytes and percent");
	Check(Megabytes(0) == 0 && Megabytes(1024ull * 1024ull * 3ull + 5) == 3, "megabytes round down");
	Check(PercentOf(0, 0) == 0 && PercentOf(1, 4) == 25 && PercentOf(4ull << 30, 4ull << 30) == 100, "percent, 0 for an empty whole");
}

void TestFormatCalls() {
	Section("The calls line: name, count, sites in first-seen order, others");
	char out[300];
	CallCounts none;
	FormatCallCounts("NiBound::WhichSide", none, out, sizeof(out));
	Check(std::string(out) == " NiBound::WhichSide 0 calls;", (std::string("no sites: ") + out).c_str());
	CallCounts counts;
	counts.Add(0x0070DFFC);
	counts.Add(0x0070DFFC);
	counts.Add(0x007D2F27);
	FormatCallCounts("NiBound::WhichSide", counts, out, sizeof(out));
	Check(std::string(out) == " NiBound::WhichSide 3 calls (from 0x0070DFFC 2, 0x007D2F27 1);", (std::string("two sites: ") + out).c_str());
	for (unsigned i = 0; i < ReturnSites::kSites + 2; ++i) {
		counts.Add(0x00600000u + 16 * i);
	}
	FormatCallCounts("X", counts, out, sizeof(out));
	Check(std::strstr(out, ", +4 others)") != nullptr, (std::string("two sites were already there, six more fill the table, four are others: ") + out).c_str());
	char tiny[20];
	const int at = FormatCallCounts("NiBound::WhichSide", counts, tiny, sizeof(tiny));
	Check(at >= static_cast<int>(sizeof(tiny)) - 1 && tiny[sizeof(tiny) - 1] == '\0', "a small buffer is cut, never overrun");
}

void TestFormatMemory() {
	Section("The memory line");
	char out[300];
	const std::uint64_t mb = 1024ull * 1024ull;
	MemorySample now{1500 * mb, 1200 * mb, 1300 * mb, 4096 * mb, 2048 * mb};
	MemoryPeak peak;
	peak.Add(MemorySample{1600 * mb, 1250 * mb, 1300 * mb, 4096 * mb, 1536 * mb});
	peak.Add(now);
	FormatMemory(now, peak, out, sizeof(out));
	Check(std::string(out) == "Probe: memory commit 1500 MB (peak 1600), working set 1200 MB (peak 1300), address space 2048 of 4096 MB used (50 %, peak 2560 MB)",
	      (std::string("line: ") + out).c_str());
	MemoryPeak none;
	FormatMemory(now, none, out, sizeof(out));
	Check(std::strstr(out, "(peak 1500)") != nullptr, "without a peak the sample stands in for it");
}

}  // namespace

void TestStepTiming() {
	Section("The pass steps: TSC calibration over the summary window");
	const ClockPair before{1000, 50};
	const ClockPair after{1000 + 3000000, 50 + 10000000};  // 10 M QPC units at 10 MHz = 1000 ms; 3 M ticks
	Check(TicksPerMillisecond(before, after, 10000000) == 3000.0, "3 M ticks over 1 s at 10 MHz: 3000 ticks per ms");
	Check(TicksPerMillisecond(before, before, 10000000) == 0.0, "an empty window: no calibration");
	Check(TicksPerMillisecond(before, after, 0) == 0.0, "an unknown frequency: no calibration");
	Check(TicksPerMillisecond(after, before, 10000000) == 0.0, "a window running backwards: no calibration");

	Section("The step line: calls and milliseconds with a calibration, ticks without, idle steps");
	char line[160];
	StepTiming t{4000, 6000000};  // 6 M ticks at 3000 per ms = 2000 ms, 0.5 ms per call
	FormatStepTiming("FinishAccumulating", t, 3000.0, line, sizeof(line));
	Check(std::string(line) == " FinishAccumulating 4000 calls, 2000.00 ms (500.00 us per call);", "calibrated: calls, ms and us per call");
	FormatStepTiming("FinishAccumulating", t, 0.0, line, sizeof(line));
	Check(std::string(line) == " FinishAccumulating 4000 calls, 6000000 ticks;", "uncalibrated: the raw ticks");
	FormatStepTiming("SetVertexShaderConstant", StepTiming{}, 3000.0, line, sizeof(line));
	Check(std::string(line) == " SetVertexShaderConstant 0 calls;", "idle: 0 calls, no division");
	char tiny[8];
	const int wrote = FormatStepTiming("FinishAccumulating", t, 3000.0, tiny, sizeof(tiny));
	Check(std::strlen(tiny) < sizeof(tiny) && wrote > 0, "a small buffer is never overrun");

	Section("A step by return site: first come, first kept, the rest in others; calls and ms per site");
	StepSites sites;
	sites.Add(0x007C7D54, 3000);
	sites.Add(0x007D6A10, 6000);
	sites.Add(0x007C7D54, 3000);
	Check(sites.used == 2 && sites.sites[0].timing.calls == 2 && sites.sites[0].timing.ticks == 6000 &&
	          sites.sites[1].timing.calls == 1 && sites.sites[1].timing.ticks == 6000,
	      "two sites, the repeat added to its site");
	for (unsigned i = 0; i < StepSites::kSites; ++i) {
		sites.Add(0x00400000u + i, 300);
	}
	Check(sites.used == StepSites::kSites && sites.others.calls == 2 && sites.others.ticks == 600,
	      "beyond eight sites the calls and ticks go to others");
	char wide[400];
	StepSites two;
	two.Add(0x007C7D54, 3000);
	two.Add(0x007D6A10, 6000);
	two.Add(0x007C7D54, 3000);
	FormatStepSites("FinishAccumulating", two, 3000.0, wide, sizeof(wide));
	Check(std::string(wide) == " FinishAccumulating by return site: 0x007C7D54 2 calls 2.00 ms, 0x007D6A10 1 calls 2.00 ms;",
	      "calibrated: each site with calls and ms, in first-seen order");
	FormatStepSites("FinishAccumulating", two, 0.0, wide, sizeof(wide));
	Check(std::string(wide) == " FinishAccumulating by return site: 0x007C7D54 2 calls 6000 ticks, 0x007D6A10 1 calls 6000 ticks;",
	      "uncalibrated: the raw ticks");
	FormatStepSites("FinishAccumulating", sites, 3000.0, wide, sizeof(wide));
	Check(std::strstr(wide, ", +others 2 calls 0.20 ms;") != nullptr, "others appended when present");
	FormatStepSites("FinishAccumulating", StepSites{}, 3000.0, wide, sizeof(wide));
	Check(std::string(wide) == " FinishAccumulating by return site: none;", "no call: none");
	char small[12];
	FormatStepSites("FinishAccumulating", sites, 3000.0, small, sizeof(small));
	Check(std::strlen(small) < sizeof(small), "a small buffer is never overrun");
}

void TestPackCache() {
	Section("The pack cache: new, same, changed, evicted; the bytes and the keys");
	PackCache cache(4);  // 16 slots
	PackStats stats;
	const PackKey a{0x1000, 0x2000, 0x3000};
	const PackKey b{0x1000, 0x2000, 0x3004};
	Check(cache.Add(a, 11, 100, stats) == PackVerdict::New && stats.fresh == 1 && cache.Keys() == 1, "first pack of a key: new");
	Check(cache.Add(a, 11, 100, stats) == PackVerdict::Same && stats.same == 1 && stats.sameBytes == 100, "the same hash again: same, its bytes counted");
	Check(cache.Add(a, 12, 100, stats) == PackVerdict::Changed && stats.changed == 1, "a different hash: changed");
	Check(cache.Add(a, 12, 100, stats) == PackVerdict::Same && stats.same == 2, "the changed hash is now the last one");
	Check(cache.Add(b, 12, 50, stats) == PackVerdict::New && cache.Keys() == 2, "another chip of the same partition is another key");
	const PackKey aOtherDecl{0x1000, 0x2000, 0x3000, 0x4000};
	Check(cache.Add(aOtherDecl, 12, 100, stats) == PackVerdict::New && cache.Keys() == 3, "the same stream under another declaration is another key");
	Check(stats.packs == 6 && stats.bytes == 550, "six packs, 550 bytes hashed");
	Check(stats.sliceSame == 3 && stats.sliceMoved == 0, "a known key with the same (default) slice each time: slice same");
	{
		const PackCache::KeyShape k = cache.Shape();
		Check(k.mixed == 1 && k.untested == 2 && k.alwaysSame == 0 && k.alwaysChanged == 0 && k.buffers == 1,
		      "key a mixed (same and changed), keys b and a-under-another-declaration untested (one pack each), one distinct buffer (the default slice)");
		unsigned visited = 0;
		cache.VisitChangedKeys(8, [&](PackKey key, std::uint64_t same, std::uint64_t changed) {
			visited += key == a && same == 2 && changed == 1 ? 1 : 0;
		});
		Check(visited == 1, "the changed key is visited with its counts");
	}
	PackCache sl(4);
	PackStats st;
	sl.Add(a, 1, 10, st, PackSlice{0x100, 0, 512});
	Check(sl.Add(a, 1, 10, st, PackSlice{0x100, 0, 512}) == PackVerdict::Same && st.sliceSame == 1, "the same buffer, offset and size: slice same");
	Check(sl.Add(a, 1, 10, st, PackSlice{0x100, 512, 512}) == PackVerdict::Same && st.sliceMoved == 1, "another offset: slice moved, the bytes still the same");
	Check(sl.Add(a, 1, 10, st, PackSlice{0x100, 512, 512}) == PackVerdict::Same && st.sliceSame == 2, "the moved slice is now the last one");
	PackStats lf;
	CountLockFlags(0x2000, lf);
	CountLockFlags(0x3000, lf);
	CountLockFlags(0x1000, lf);
	CountLockFlags(0x0, lf);
	Check(lf.lockDiscard == 2 && lf.lockNoOverwrite == 1 && lf.lockOther == 1, "lock flags: discard wins over no-overwrite, other counted");
	PackStats bc;
	CountVerdictByConsistency(PackVerdict::Same, true, bc);
	CountVerdictByConsistency(PackVerdict::Changed, true, bc);
	CountVerdictByConsistency(PackVerdict::Same, false, bc);
	CountVerdictByConsistency(PackVerdict::New, true, bc);
	Check(bc.volatileSame == 1 && bc.volatileChanged == 1 && bc.otherSame == 1 && bc.otherChanged == 0, "verdicts split by consistency; New is not counted");
	PackStats vt;
	vt.volatileCalls = 100; vt.volatileTicks = 300000; vt.hashTicks = 60000;
	char vline[240];
	FormatVolatilePackTime(vt, 3000.0, vline, sizeof(vline));
	Check(std::string(vline) == " volatile packer calls without the probe's hashing 100 calls, 80.00 ms (800.00 us per call); the probe's hashing 100 calls, 20.00 ms (200.00 us per call);",
	      "the volatile packer time line: the hashing subtracted and shown");
	vt.hashTicks = 400000;
	FormatVolatilePackTime(vt, 3000.0, vline, sizeof(vline));
	Check(std::strstr(vline, "100 calls, 0.00 ms") != nullptr, "hashing beyond the total clamps to 0");
	PackCache tiny(0);  // one slot: every other key evicts
	PackStats t;
	tiny.Add(a, 1, 10, t);
	Check(tiny.Add(b, 1, 10, t) == PackVerdict::New && t.evictions == 1, "a collision overwrites the slot and counts an eviction, never a same");
	Check(tiny.Add(a, 1, 10, t) == PackVerdict::New && t.evictions == 2 && t.same == 0, "the evicted key comes back as new");

	Section("HashBytes: equal bytes equal hashes, one byte apart differs, the tail counts");
	const unsigned char x[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
	unsigned char y[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
	Check(HashBytes(x, 13) == HashBytes(y, 13), "equal bytes: equal");
	y[12] = 14;
	Check(HashBytes(x, 13) != HashBytes(y, 13), "a tail byte differs: different");
	y[12] = 13;
	y[3] = 0;
	Check(HashBytes(x, 13) != HashBytes(y, 13), "a word byte differs: different");
	Check(HashBytes(x, 13) != HashBytes(x, 12), "the count is part of it");
	Check(HashBytes(x, 0) == HashBytes(y, 0), "empty: equal");

	Section("The consistency nibble: volatile, static, mutable, dirty bits");
	PackStats c;
	CountConsistency(0x8000, c);
	CountConsistency(0x8FFF, c);
	CountConsistency(0x4000, c);
	CountConsistency(0x0001, c);
	CountConsistency(0x2000, c);
	Check(c.volatileData == 2 && c.staticData == 1 && c.mutableData == 2 && c.dirtyBits == 2, "2 volatile (one dirty), 1 static, 2 mutable (one dirty)");

	Section("The pack cache line");
	PackStats s;
	s.packs = 1000; s.same = 750; s.changed = 200; s.fresh = 50; s.evictions = 5; s.bytes = 3u << 20; s.sameBytes = 1u << 20;
	s.outside = 7; s.volatileData = 900; s.staticData = 10; s.mutableData = 90; s.dirtyBits = 95;
	s.sliceSame = 940; s.sliceMoved = 10; s.lockDiscard = 1; s.lockNoOverwrite = 998; s.lockOther = 1;
	s.volatileSame = 700; s.volatileChanged = 150; s.otherSame = 50; s.otherChanged = 50;
	char line[400];
	FormatPackStats(s, 60, line, sizeof(line));
	Check(std::string(line) ==
	          "Probe: pack cache - 1000 packs, 750 same (75 %), 200 changed, 50 new (5 evictions), 60 keys; 3 MB hashed, 33 % of the "
	          "bytes same; 7 unlocks outside the packer; data volatile 900, static 10, mutable 90, dirty 95; slice same 940, moved 10; "
	          "lock flags discard 1, no-overwrite 998, other 1; by data: volatile same 700 changed 150, other same 50 changed 50",
	      "the line");
	FormatPackStats(PackStats{}, 0, line, sizeof(line));
	Check(std::strstr(line, "0 packs, 0 same (0 %)") != nullptr, "no pack: no division");
}

void TestUploads() {
	Section("The register cache: fresh, same, same in pass, changed, out of range");
	RegisterCache cache;
	UploadStats s;
	Check(cache.Add(4, 0xAA, 64, 1, s) == PackVerdict::New && s.fresh == 1, "first upload to register 4: fresh");
	Check(cache.Add(4, 0xAA, 64, 1, s) == PackVerdict::Same && s.same == 1 && s.sameInPass == 1 && s.sameBytes == 64,
	      "the same bytes in the same pass: same, in pass");
	Check(cache.Add(4, 0xAA, 64, 2, s) == PackVerdict::Same && s.same == 2 && s.sameInPass == 1, "the same bytes in the next pass: same, not in pass");
	Check(cache.Add(4, 0xAB, 64, 2, s) == PackVerdict::Changed && s.changed == 1, "other bytes: changed");
	Check(cache.Add(4, 0xAB, 64, 2, s) == PackVerdict::Same && s.sameInPass == 2, "the changed bytes are now the last ones");
	Check(cache.Add(5000, 0x1, 16, 2, s) == PackVerdict::New && s.outOfRange == 1 && s.uploads == 6 && s.bytes == 336,
	      "a register beyond the table: counted out of range, bytes counted");
	cache.Reset();
	Check(cache.Add(4, 0xAB, 64, 3, s) == PackVerdict::New && s.fresh == 2, "after a reset every register is fresh again");

	Section("ConsecutiveRun: one ascending run or not");
	const std::uint16_t run[] = {7, 8, 9, 10};
	const std::uint16_t gap[] = {7, 8, 10, 11};
	const std::uint16_t one[] = {3};
	Check(ConsecutiveRun(run, 4) && ConsecutiveRun(one, 1) && ConsecutiveRun(run, 0), "7..10, a single bone and no bone are runs");
	Check(!ConsecutiveRun(gap, 4), "7, 8, 10, 11 is not");

	Section("The uploads line");
	UploadStats u;
	u.uploads = 2000; u.same = 1500; u.sameInPass = 300; u.changed = 450; u.fresh = 50; u.outOfRange = 0; u.bytes = 4u << 20; u.sameBytes = 2u << 20;
	u.paletteCalls = 100; u.bones = 1200; u.consecutivePalettes = 80; u.byType[2] = 700; u.byType[6] = 100;
	char line[400];
	FormatUploadStats(u, line, sizeof(line));
	Check(std::string(line) ==
	          "Probe: constant uploads - 2000 uploads, 1500 same (75 %, 300 in the same pass), 450 changed, 50 fresh, 0 out of range; "
	          "4 MB, 50 % of the bytes same; palettes 100 with 1200 bones (80 % consecutive); single uploads by type 2:700 6:100",
	      "the line");
	FormatUploadStats(UploadStats{}, line, sizeof(line));
	Check(std::strstr(line, "0 uploads, 0 same (0 %") != nullptr, "no upload: no division");
}

void TestViewConstants() {
	Section("ApplyTable: a row holds its register range; beyond the rows is dropped");
	ApplyTable t;
	t.Add(ApplyRow{4, 1, 2, 3});
	t.Add(ApplyRow{20, 4, 2, 7});
	t.Add(ApplyRow{100, 0, 6, 0});
	Check(t.Find(4) != nullptr && t.Find(4)->id == 3, "register 4: the first row");
	Check(t.Find(5) == nullptr, "register 5: no row");
	Check(t.Find(23) != nullptr && t.Find(23)->id == 7 && t.Find(24) == nullptr, "a four-register row holds 20..23");
	Check(t.Find(100) != nullptr && t.Find(100)->type == 6, "count 0 reads as one register");
	for (std::uint32_t i = 0; i < ApplyTable::kMaxRows; ++i) {
		t.Add(ApplyRow{1000 + i, 1, 1, 0});
	}
	Check(t.size == ApplyTable::kMaxRows && t.dropped == 3, "the table holds kMaxRows, the rest is counted");
	t.Clear();
	Check(t.size == 0 && t.Find(4) == nullptr, "cleared");

	Section("ViewStats: keyed by (type, id), the verdict sorts into changed / same / fresh");
	ViewStats v;
	const ApplyRow view{4, 1, 2, 3};
	const ApplyRow world{8, 1, 2, 1};
	v.Add(&view, PackVerdict::Changed, 64);
	v.Add(&view, PackVerdict::Changed, 64);
	v.Add(&view, PackVerdict::Same, 64);
	v.Add(&world, PackVerdict::Same, 64);
	v.Add(&world, PackVerdict::New, 64);
	v.Add(nullptr, PackVerdict::Same, 16);
	Check(v.uploads == 6 && v.unknown == 1 && v.keyCount == 2, "six uploads, one unknown, two keys");
	Check(v.keys[0].key == ViewKey{2, 3} && v.keys[0].changed == 2 && v.keys[0].same == 1 && v.keys[0].bytes == 192, "the view key");
	Check(v.keys[1].key == ViewKey{2, 1} && v.keys[1].same == 1 && v.keys[1].fresh == 1, "the world key");
	v.applies = 10;
	v.Tabled(view);
	v.Tabled(ApplyRow{40, 1, 2, 5});
	v.Tabled(ApplyRow{40, 1, 2, 5});
	Check(v.keys[0].tabled == 1 && v.keyCount == 3 && v.keys[2].tabled == 2 && v.keys[2].uploads == 0, "tabled entries count, with or without uploads");
	char line[600];
	FormatViewStats(v, line, sizeof(line));
	Check(std::string(line) == "Probe: view constants - 10 applies, 6 re-apply uploads (1 unknown, 0 overflow); changed: t2:i3 2/1/0; same only: t2:i1 2; "
	                           "tabled, no upload: t2:i5 2",
	      "the line");
	for (std::uint32_t i = 0; i < ViewStats::kMaxKeys + 5; ++i) {
		const ApplyRow r{i, 1, 1, static_cast<std::uint16_t>(i)};
		v.Add(&r, PackVerdict::Same, 16);
	}
	Check(v.keyCount == ViewStats::kMaxKeys && v.overflow == 8, "keys beyond the table are counted as overflow");
	v.Reset();
	Check(v.uploads == 0 && v.keyCount == 0, "reset");
	FormatViewStats(v, line, sizeof(line));
	Check(std::string(line) == "Probe: view constants - 0 applies, 0 re-apply uploads (0 unknown, 0 overflow); changed:; same only:; tabled, no upload:",
	      "the empty line");
}

void TestSceneUpdate() {
	Section("The scene update probe's line: per grid update, the idle controller walks as a share");
	obng::plugin::probe::SceneUpdateStats s;
	s.gridUpdates = 100;
	s.gridTicks = 300000;  // at 3000 ticks per ms: 100 ms over 100 updates = 1.0 ms each
	s.cells = 2500;
	s.nodeDownward = 400000;
	s.nodeSelected = 50000;
	s.leafDownward = 900000;
	s.controllerWalks = 1300000;
	s.controllerWalksIdle = 1170000;
	s.worldData = 1200000;
	s.objectUpdates = 1500;
	s.objectUpdateTicks = 60000;  // 0.2 ms each grid update
	char line[600];
	obng::plugin::probe::FormatSceneUpdate(s, 3000.0, line, sizeof(line));
	Check(std::string(line) == "Probe: scene update - 100 grid updates; per grid update: cells 25.0, nodes 4000 (selected 500), leaves 9000, world data 12000, "
	                           "controller walks 13000 (11700 with nothing to do, 90 %), NiAVObject::Update 15.0 calls; grid update 1.000 ms, "
	                           "NiAVObject::Update 0.200 ms per grid update",
	      "calibrated: counts per grid update, the idle share, the two times");
	obng::plugin::probe::FormatSceneUpdate(s, 0.0, line, sizeof(line));
	Check(std::strstr(line, "; grid update 300000 ticks, NiAVObject::Update 60000 ticks") != nullptr, "uncalibrated: the raw ticks");
	obng::plugin::probe::FormatSceneUpdate(obng::plugin::probe::SceneUpdateStats{}, 3000.0, line, sizeof(line));
	Check(std::strstr(line, "0 grid updates; per grid update: cells 0.0, nodes 0") != nullptr && std::strstr(line, "0 %") != nullptr,
	      "nothing counted: no division by zero");
}

void TestObjectUpdateSites() {
	Section("NiAVObject::Update by return site: a wider table of the same shape (160 sites, more than the exe's 145 call sites), the 161st in others");
	Check(ObjectUpdateSites::kSites == 160 && StepSites::kSites == 8, "the two tables differ in width only");
	ObjectUpdateSites sites;
	for (unsigned i = 0; i < ObjectUpdateSites::kSites; ++i) {
		sites.Add(0x00471F20u + 16 * i, 30000);  // 10 us each at 3000 ticks per ms
	}
	sites.Add(0x00471F20u, 30000);
	Check(sites.used == ObjectUpdateSites::kSites && sites.sites[0].timing.calls == 2 && sites.sites[0].timing.ticks == 60000 &&
	          sites.others.calls == 0,
	      "160 distinct sites fill the table; a repeat adds to its site");
	sites.Add(0x00603CA0u, 30000);
	Check(sites.used == ObjectUpdateSites::kSites && sites.others.calls == 1 && sites.others.ticks == 30000, "a 161st site goes to others");
	char line[6400];
	const int wrote = FormatStepSites("NiAVObject::Update", sites, 3000.0, line, sizeof(line));
	Check(wrote > 0 && static_cast<std::size_t>(wrote) < sizeof(line) && std::strlen(line) == static_cast<std::size_t>(wrote),
	      "160 sites and others fit one summary line of 6400");
	const char* head = " NiAVObject::Update by return site: 0x00471F20 2 calls 20.00 ms, 0x00471F30 1 calls 10.00 ms,";
	Check(std::strncmp(line, head, std::strlen(head)) == 0, "the same format as the pass steps' sites");
	Check(std::strstr(line, ", +others 1 calls 10.00 ms;") != nullptr, "others at the end");
	FormatStepSites("NiAVObject::Update", ObjectUpdateSites{}, 3000.0, line, sizeof(line));
	Check(std::string(line) == " NiAVObject::Update by return site: none;", "no call: none");
}

int main() {
	TestSceneUpdate();
	TestObjectUpdateSites();
	TestUploads();
	TestPackCache();
	TestStepTiming();
	TestReturnSites();
	TestCallCounts();
	TestMemoryPeak();
	TestArithmetic();
	TestFormatCalls();
	TestFormatMemory();
	TestViewConstants();
	return Failures();
}
