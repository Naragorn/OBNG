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

int main() {
	TestStepTiming();
	TestReturnSites();
	TestCallCounts();
	TestMemoryPeak();
	TestArithmetic();
	TestFormatCalls();
	TestFormatMemory();
	return Failures();
}
