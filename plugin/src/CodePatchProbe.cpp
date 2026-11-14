// [Probe] CodePatches: see CodePatchProbe.h.

#include "CodePatchProbe.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "CodePatchLogic.h"

namespace obng::plugin {

namespace {

namespace cp = code_patch;

constexpr std::uint32_t kRunGap = 4;        // equal bytes that do not split a patch
constexpr unsigned kSecondSummary = 6;      // the summary of the second look
constexpr unsigned kHops = 3;               // jumps followed out of memory that belongs to no module

LogFn g_log = nullptr;
bool g_on = false;
UInt32 g_linesLeft = 400;
UInt32 g_from = 0;
UInt32 g_to = 0xFFFFFFFFu;
unsigned g_summaries = 0;
std::vector<UInt32> g_seen;  // the first look's runs, by address

bool ReadFileWhole(const char* path, std::vector<std::uint8_t>* out) {
	const HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	const DWORD size = GetFileSize(file, nullptr);
	bool ok = size != INVALID_FILE_SIZE && size != 0;
	if (ok) {
		out->resize(size);
		DWORD read = 0;
		ok = ReadFile(file, out->data(), size, &read, nullptr) != 0 && read == size;
	}
	CloseHandle(file);
	return ok;
}

// Up to `count` bytes at an address that may not be readable; false then.
bool ReadBytes(UInt32 address, std::uint8_t* out, UInt32 count) {
	__try {
		std::memcpy(out, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), count);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// The file name of the module an address lies in; empty when it lies in none.
void ModuleOf(UInt32 address, char out[cp::kModuleName]) {
	out[0] = '\0';
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), &info, sizeof(info)) == 0 || info.AllocationBase == nullptr) {
		return;
	}
	char path[MAX_PATH] = {};
	if (GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), path, sizeof(path)) == 0) {
		return;
	}
	const char* name = std::strrchr(path, '\\');
	std::snprintf(out, cp::kModuleName, "%s", name != nullptr ? name + 1 : path);
}

// The module a patch's branch ends in: the target's, or - when the target is
// memory of no module (a trampoline) - that of the jump found there (E9
// rel32, FF 25 [abs], 68 imm32 C3), a few hops on.
void EndModule(UInt32 target, char out[cp::kModuleName]) {
	for (unsigned hop = 0; hop <= kHops; ++hop) {
		ModuleOf(target, out);
		std::uint8_t code[6] = {};
		if (out[0] != '\0' || !ReadBytes(target, code, sizeof(code))) {
			return;
		}
		UInt32 next = 0;
		if (code[0] == 0xE9) {
			next = target + 5 + cp::Dword(code + 1);
		} else if (code[0] == 0xFF && code[1] == 0x25) {
			std::uint8_t slot[4] = {};
			if (!ReadBytes(cp::Dword(code + 2), slot, sizeof(slot))) {
				return;
			}
			next = cp::Dword(slot);
		} else if (code[0] == 0x68 && code[5] == 0xC3) {
			next = cp::Dword(code + 1);
		} else {
			return;
		}
		target = next;
	}
}

struct Look {
	UInt32 runs = 0;
	UInt32 fresh = 0;  // runs the first look did not have
	cp::ModuleCounts modules;
};

void NoteRun(const cp::Section& section, const std::uint8_t* file, std::uint32_t offset, std::uint32_t length, bool first, Look* look) {
	const auto* memory = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(section.address));
	const UInt32 address = section.address + offset;
	++look->runs;
	UInt32 target = 0;
	UInt32 branchAt = 0;
	const cp::Branch branch = cp::BranchAround(memory, offset, length, section.address, &target, &branchAt);
	char module[cp::kModuleName] = {};
	if (branch != cp::Branch::kNone) {
		EndModule(target, module);
	}
	look->modules.Add(branch == cp::Branch::kNone ? "(bytes, no jump or call)" : module[0] != '\0' ? module : "(no module)");
	const bool known = !first && std::binary_search(g_seen.begin(), g_seen.end(), address);
	if (first) {
		g_seen.push_back(address);
	} else if (!known) {
		++look->fresh;
	}
	if (known || g_linesLeft == 0 || address < g_from || address > g_to) {
		return;
	}
	--g_linesLeft;
	char line[320];
	cp::FormatRun(address, length, memory + offset, file + offset, branch, branchAt, target, module, line, sizeof(line));
	g_log("%s", line);
}

void Scan(bool first) {
	char path[MAX_PATH] = {};
	std::vector<std::uint8_t> file;
	if (GetModuleFileNameA(nullptr, path, sizeof(path)) == 0 || !ReadFileWhole(path, &file)) {
		g_log("Probe: code patches - the executable's file could not be read (%s)", path);
		return;
	}
	cp::Section sections[cp::kMaxSections];
	const std::uint32_t count = cp::ParseSections(file.data(), file.size(), sections);
	Look look;
	for (std::uint32_t i = 0; i < count; ++i) {
		const cp::Section& s = sections[i];
		if (!s.code || s.size == 0) {
			continue;
		}
		const std::uint8_t* bytes = file.data() + s.fileOffset;
		cp::DiffRuns(bytes, reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(s.address)), s.size, kRunGap,
		             [&](std::uint32_t offset, std::uint32_t length) { NoteRun(s, bytes, offset, length, first, &look); });
	}
	if (first) {
		std::sort(g_seen.begin(), g_seen.end());
	}
	char line[1400];
	int at = std::snprintf(line, sizeof(line), "Probe: code patches (%s) - %u places where the code is not the file's%s; by where their jump or call ends:",
	                       first ? "the first summary" : "the sixth summary", look.runs, count == 0 ? " (no sections read: not a PE file?)" : "");
	for (std::uint32_t i = 0; i < look.modules.count && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++i) {
		at += std::snprintf(line + at, sizeof(line) - at, " %s %u;", look.modules.rows[i].name, look.modules.rows[i].runs);
	}
	if (at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
		std::snprintf(line + at, sizeof(line) - at, " further modules %u; %u not there at the first look; lines left %u", look.modules.others, look.fresh,
		              g_linesLeft);
	}
	g_log("%s", line);
}

}  // namespace

void InstallCodePatchProbe(const char* iniPath, LogFn log) {
	g_log = log;
	g_on = GetPrivateProfileIntA("Probe", "CodePatches", 0, iniPath) != 0;
	if (!g_on) {
		return;
	}
	g_linesLeft = GetPrivateProfileIntA("Probe", "CodePatchesLines", 400, iniPath);
	char text[32] = {};
	GetPrivateProfileStringA("Probe", "CodePatchesFrom", "0", text, sizeof(text), iniPath);
	g_from = static_cast<UInt32>(std::strtoul(text, nullptr, 16));
	GetPrivateProfileStringA("Probe", "CodePatchesTo", "FFFFFFFF", text, sizeof(text), iniPath);
	g_to = static_cast<UInt32>(std::strtoul(text, nullptr, 16));
	log("Probe: code patches on - the code against the executable's file at the first and the sixth summary, lines for 0x%08X-0x%08X, at most %u", g_from,
	    g_to, g_linesLeft);
}

void LogCodePatchSummary() {
	if (!g_on) {
		return;
	}
	++g_summaries;
	if (g_summaries == 1 || g_summaries == kSecondSummary) {
		Scan(g_summaries == 1);
	}
}

}  // namespace obng::plugin
