// [Probe] CodePatches' decisions (plugin/src/CodePatchLogic.h): a PE file's
// sections, the runs of differing bytes, where a patched call or jump leads,
// the line a run and the counts by module.

#include "Check.h"
#include "CodePatchLogic.h"

#include <cstring>
#include <string>
#include <vector>

namespace cp = obng::plugin::code_patch;

namespace {

void Put16(std::vector<std::uint8_t>& f, std::size_t at, std::uint16_t v) {
	f[at] = static_cast<std::uint8_t>(v);
	f[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void Put32(std::vector<std::uint8_t>& f, std::size_t at, std::uint32_t v) {
	Put16(f, at, static_cast<std::uint16_t>(v));
	Put16(f, at + 2, static_cast<std::uint16_t>(v >> 16));
}

// A PE32 file of `size` bytes with its headers at 0x80 and `sections` section headers.
std::vector<std::uint8_t> Pe(std::size_t size, std::uint16_t sections) {
	std::vector<std::uint8_t> f(size, 0);
	Put16(f, 0, 0x5A4D);
	Put32(f, 0x3C, 0x80);
	Put32(f, 0x80, 0x00004550);
	Put16(f, 0x80 + 6, sections);
	Put16(f, 0x80 + 0x14, 0xE0);
	Put16(f, 0x80 + 0x18, 0x010B);
	Put32(f, 0x80 + 0x18 + 0x1C, 0x00400000);
	return f;
}

void PutSection(std::vector<std::uint8_t>& f, unsigned index, std::uint32_t virtualSize, std::uint32_t rva, std::uint32_t rawSize, std::uint32_t rawOffset,
                std::uint32_t flags) {
	const std::size_t at = 0x80 + 0x18 + 0xE0 + index * 40;
	Put32(f, at + 8, virtualSize);
	Put32(f, at + 12, rva);
	Put32(f, at + 16, rawSize);
	Put32(f, at + 20, rawOffset);
	Put32(f, at + 36, flags);
}

void TestSections() {
	Section("A PE file's sections");
	std::vector<std::uint8_t> f = Pe(0x3000, 3);
	PutSection(f, 0, 0x0F00, 0x1000, 0x1000, 0x400, 0x60000020);   // code, less in memory than in the file
	PutSection(f, 1, 0x5000, 0x2000, 0x800, 0x1400, 0x40000040);   // data, more in memory than in the file
	PutSection(f, 2, 0x1000, 0x8000, 0x2000, 0x2800, 0x20000000);  // executable, its bytes run past the file's end
	cp::Section s[cp::kMaxSections];
	Check(cp::ParseSections(f.data(), f.size(), s) == 3, "three sections");
	Check(s[0].address == 0x00401000 && s[0].size == 0x0F00 && s[0].fileOffset == 0x400 && s[0].code, "code: the image base plus the RVA, the smaller of the two sizes");
	Check(s[1].address == 0x00402000 && s[1].size == 0x800 && !s[1].code, "data: only what the file holds");
	Check(s[2].size == 0x800 && s[2].code, "cut at the file's end; the execute flag counts as code");
	std::vector<std::uint8_t> g = Pe(0x3000, 1);
	PutSection(g, 0, 0x100, 0x1000, 0x100, 0x4000, 0x20);
	Check(cp::ParseSections(g.data(), g.size(), s) == 1 && s[0].size == 0, "bytes wholly past the file's end: nothing to compare");

	Section("Not such a file");
	Check(cp::ParseSections(nullptr, 100, s) == 0 && cp::ParseSections(f.data(), 0x20, s) == 0, "no bytes, too few");
	std::vector<std::uint8_t> bad = f;
	bad[0] = 'X';
	Check(cp::ParseSections(bad.data(), bad.size(), s) == 0, "no MZ");
	bad = f;
	Put32(bad, 0x3C, 0x2FF0);
	Check(cp::ParseSections(bad.data(), bad.size(), s) == 0, "the PE header past the end");
	bad = f;
	Put32(bad, 0x3C, 0x5000);
	Check(cp::ParseSections(bad.data(), bad.size(), s) == 0, "the PE offset past the file");
	bad = f;
	Put32(bad, 0x80, 0x12345678);
	Check(cp::ParseSections(bad.data(), bad.size(), s) == 0, "no PE signature");
	bad = f;
	Put16(bad, 0x80 + 0x18, 0x020B);
	Check(cp::ParseSections(bad.data(), bad.size(), s) == 0, "a 64-bit optional header");
	std::vector<std::uint8_t> many = Pe(0x3000, 40);
	Check(cp::ParseSections(many.data(), many.size(), s) == cp::kMaxSections, "more sections than the table holds: the first ones");
	std::vector<std::uint8_t> shortTable = Pe(0x80 + 0x18 + 0xE0 + 60, 3);
	Check(cp::ParseSections(shortTable.data(), shortTable.size(), s) == 1, "a section table cut by the file's end: the whole headers only");
}

void TestRuns() {
	Section("The runs of differing bytes");
	std::uint8_t file[40];
	std::uint8_t memory[40];
	for (int i = 0; i < 40; ++i) {
		file[i] = memory[i] = static_cast<std::uint8_t>(i);
	}
	std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
	auto sink = [&](std::uint32_t offset, std::uint32_t length) { runs.emplace_back(offset, length); };
	Check(cp::DiffRuns(file, memory, 40, 4, sink) == 0 && runs.empty(), "the same bytes: none");
	memory[3] = 0xE8;
	memory[4] = 0xAA;
	memory[6] = 0xBB;   // 5 unchanged inside a patched call: one run
	memory[7] = 0xCC;
	memory[20] = 0xEE;  // far from the first: its own run
	memory[39] = 0xFF;  // the last byte
	Check(cp::DiffRuns(file, memory, 40, 4, sink) == 3, "three runs");
	Check(runs.size() == 3 && runs[0] == std::make_pair(3u, 5u) && runs[1] == std::make_pair(20u, 1u) && runs[2] == std::make_pair(39u, 1u),
	      "equal bytes inside a patch do not split it; a run ends at its last differing byte; the last byte is seen");
	runs.clear();
	memory[11] = 0x99;  // 3 equal bytes after the first run's end (8, 9, 10): still within the gap of 4
	cp::DiffRuns(file, memory, 40, 4, sink);
	Check(runs[0] == std::make_pair(3u, 9u), "fewer equal bytes than the gap between two runs: one run");
	runs.clear();
	cp::DiffRuns(file, memory, 40, 3, sink);
	Check(runs[0] == std::make_pair(3u, 5u) && runs[1] == std::make_pair(11u, 1u), "as many as the gap: two");
	runs.clear();
	Check(cp::DiffRuns(file, memory, 0, 4, sink) == 0, "no bytes: none");
}

void TestBranch() {
	Section("Where a patched call or jump leads");
	const std::uint8_t call[] = {0xE8, 0x10, 0x00, 0x00, 0x00};
	const std::uint8_t jump[] = {0xE9, 0xFB, 0xFF, 0xFF, 0xFF, 0x90};
	const std::uint8_t other[] = {0x90, 0x90, 0x90, 0x90, 0x90};
	std::uint32_t target = 0;
	Check(cp::BranchAt(call, 5, 0x00401000, &target) == cp::Branch::kCall && target == 0x00401015, "a call: the next instruction plus the displacement");
	Check(cp::BranchAt(jump, 6, 0x00401000, &target) == cp::Branch::kJump && target == 0x00401000, "a jump backwards onto itself");
	Check(cp::BranchAt(call, 4, 0x00401000, &target) == cp::Branch::kNone, "fewer than five bytes: none");
	Check(cp::BranchAt(other, 5, 0x00401000, &target) == cp::Branch::kNone, "another instruction: none");

	Section("A run that begins inside the instruction");
	const std::uint8_t memory[] = {0x90, 0xE8, 0x20, 0x00, 0x00, 0x00, 0x90, 0x90};
	std::uint32_t at = 0;
	Check(cp::BranchAround(memory, 1, 5, 0x00500000, &target, &at) == cp::Branch::kCall && at == 0x00500001 && target == 0x00500026, "the run begins at the opcode");
	Check(cp::BranchAround(memory, 2, 4, 0x00500000, &target, &at) == cp::Branch::kCall && at == 0x00500001 && target == 0x00500026,
	      "only the displacement differs: the opcode one byte back");
	Check(cp::BranchAround(memory, 2, 1, 0x00500000, &target, &at) == cp::Branch::kCall && at == 0x00500001, "one byte of it differs");
	Check(cp::BranchAround(memory, 6, 2, 0x00500000, &target, &at) == cp::Branch::kNone, "no branch there or before");
	Check(cp::BranchAround(other, 0, 4, 0x00500000, &target, &at) == cp::Branch::kNone, "at the range's first byte nothing lies before");
}

void TestFormat() {
	Section("The line a run");
	const std::uint8_t memory[] = {0xE8, 0x12, 0x34, 0x56, 0x78, 1, 2, 3, 4, 5};
	const std::uint8_t file[] = {0xFF, 0x15, 0x50, 0x7E, 0xB2, 0, 0, 0, 0, 0};
	char line[300];
	cp::FormatRun(0x006A9E57, 5, memory, file, cp::Branch::kCall, 0x006A9E57, 0x10001234, "Elys_USV.dll", line, sizeof(line));
	Check(std::string(line) == "Probe: code patch at 0x006A9E57, 5 bytes: E8 12 34 56 78 (the file: FF 15 50 7E B2) - a call to 0x10001234 in Elys_USV.dll", "a call into a module");
	cp::FormatRun(0x006A9E58, 4, memory + 1, file + 1, cp::Branch::kJump, 0x006A9E57, 0x0A001234, "", line, sizeof(line));
	Check(std::string(line) ==
	          "Probe: code patch at 0x006A9E58, 4 bytes: 12 34 56 78 (the file: 15 50 7E B2) - a jump at 0x006A9E57 to 0x0A001234 in no module (allocated memory)",
	      "a jump whose opcode lies before the run, into memory of no module");
	cp::FormatRun(0x00401000, 10, memory, file, cp::Branch::kNone, 0, 0, nullptr, line, sizeof(line));
	Check(std::string(line) == "Probe: code patch at 0x00401000, 10 bytes: E8 12 34 56 78 01 02 03 .. (the file: FF 15 50 7E B2 00 00 00 ..)", "no branch; more bytes than shown");
	char small[24];
	Check(cp::FormatRun(0x00401000, 10, memory, file, cp::Branch::kCall, 0x00401000, 1, "x", small, sizeof(small)) >= static_cast<int>(sizeof(small)) - 1 &&
	          small[sizeof(small) - 1] == '\0',
	      "a buffer too small is not overrun");
}

void TestModules() {
	Section("The counts by module");
	cp::ModuleCounts counts;
	counts.Add("obse_1_2_416.dll");
	counts.Add("OBNG.dll");
	counts.Add("obse_1_2_416.dll");
	counts.Add("");
	Check(counts.count == 3 && counts.rows[0].runs == 2 && counts.rows[1].runs == 1 && std::string(counts.rows[2].name).empty() && counts.others == 0,
	      "a row a name, the empty name too");
	const std::string longName(60, 'a');
	counts.Add(longName.c_str());
	counts.Add(longName.c_str());
	Check(counts.count == 4 && counts.rows[3].runs == 2 && std::strlen(counts.rows[3].name) == cp::kModuleName - 1, "a long name is cut and still found again");
	counts.Add("OBNG.dll.bak");
	Check(counts.count == 5 && counts.rows[1].runs == 1, "a name that only begins like another is its own row");
	for (std::uint32_t i = counts.count; i < cp::kModules; ++i) {
		counts.Add(std::to_string(i).c_str());
	}
	counts.Add("one more");
	counts.Add("OBNG.dll");
	Check(counts.count == cp::kModules && counts.others == 1 && counts.rows[1].runs == 2, "a full table counts further modules together and still finds the kept ones");
}

}  // namespace

int main() {
	TestSections();
	TestRuns();
	TestBranch();
	TestFormat();
	TestModules();
	return Failures();
}
