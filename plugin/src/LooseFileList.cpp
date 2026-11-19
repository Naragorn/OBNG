#include "LooseFileList.h"

#include <windows.h>

#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/io/LooseFileListLogic.h"

namespace obng::plugin {

namespace {

namespace lfl = obng::engine::io::loose_file_list;

// FileFinder's vtable 0x00A36414 slot 3 (by RTTI): __thiscall(char* path, flags, mask) -> 0/1/2, RET 0x0C at
// 0x00431947 (read 2026-11-19).
constexpr UInt32 kSlot = 0x00431800;
constexpr UInt8 kSlotBytes[] = {0x81, 0xEC, 0x10, 0x01, 0x00, 0x00};  // sub esp, 0x110 (read)
constexpr UInt32 kArchives = 0x0042EBA0;                               // cdecl bool(path, mask), called by the slot (read)
constexpr UInt32 kStrnicmp = 0x009864D9;                               // the CRT's _strnicmp, called by the slot (read)
constexpr unsigned kMismatchLines = 20;

using SlotFn = UInt32(__thiscall*)(void* self, char* path, UInt32 flags, UInt32 mask);
using ArchivesFn = bool(__cdecl*)(const char* path, UInt32 mask);
using StrnicmpFn = int(__cdecl*)(const char* a, const char* b, std::size_t n);

enum class Mode { kOff, kRoute, kCheck };

void* g_original = nullptr;
Mode g_mode = Mode::kOff;
LogFn g_log = nullptr;
std::atomic<const lfl::Listing*> g_listing{nullptr};
char g_root[MAX_PATH] = {};
double g_buildMs = 0.0;
std::atomic<unsigned> g_twins{0};  // folders' names with a case twin, marked ambiguous

struct Counts {
	std::atomic<unsigned long long> calls{0}, notReady{0}, refused{0}, archive{0}, loose{0}, looseInFolder{0}, notFound{0}, mismatches{0};
	std::atomic<unsigned long long> answeredTicks{0}, originalTicks{0};
};
Counts g_counts;
std::atomic<unsigned> g_mismatchLines{0};
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};

// Every file and folder under root, by its path relative to root; a folder's own tree after it (no recursion: an
// explicit stack, at most 64 levels deep).
void ListTree(const std::string& root, lfl::Listing* listing) {
	std::vector<std::string> pending{std::string()};
	while (!pending.empty()) {
		const std::string relative = std::move(pending.back());
		pending.pop_back();
		const std::string pattern = root + "\\" + (relative.empty() ? std::string() : relative + "\\") + "*";
		WIN32_FIND_DATAA found;
		HANDLE find = FindFirstFileExA(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
		if (find == INVALID_HANDLE_VALUE) {
			continue;
		}
		std::vector<std::string> names;
		do {
			const char* name = found.cFileName;
			if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
				continue;
			}
			names.emplace_back(name);
			const std::string path = relative.empty() ? std::string(name) : relative + "\\" + name;
			listing->Add(path.c_str());
			const bool folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			if (folder && std::count(path.begin(), path.end(), '\\') < 64) {
				pending.push_back(path);
			}
		} while (FindNextFileA(find, &found));
		FindClose(find);
		for (const std::string& twin : lfl::CaseTwins(names)) {
			listing->MarkAmbiguous((relative.empty() ? twin : relative + "\\" + twin).c_str());
			g_twins.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

DWORD WINAPI BuildListing(void*) {
	LARGE_INTEGER t0{}, t1{}, frequency{};
	QueryPerformanceCounter(&t0);
	auto* listing = new lfl::Listing();  // kept for the process's life: answers are read from it on every thread
	ListTree(g_root, listing);
	listing->Seal();
	QueryPerformanceCounter(&t1);
	QueryPerformanceFrequency(&frequency);
	g_buildMs = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	g_listing.store(listing, std::memory_order_release);
	return 0;
}

UInt32 Original(void* self, char* path, UInt32 flags, UInt32 mask) {
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 result = reinterpret_cast<SlotFn>(g_original)(self, path, flags, mask);
	g_counts.originalTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
	return result;
}

lfl::Folders FoldersOf(const void* self) {
	const auto* base = static_cast<const UInt8*>(self);
	lfl::Folders folders;
	std::memcpy(&folders.names, base + 0x08, sizeof(folders.names));
	UInt16 count = 0;
	std::memcpy(&count, base + 0x10, sizeof(count));
	folders.count = count;
	return folders;
}

void Note(lfl::Outcome outcome) {
	switch (outcome) {
	case lfl::Outcome::Refused: g_counts.refused.fetch_add(1, std::memory_order_relaxed); break;
	case lfl::Outcome::Archive: g_counts.archive.fetch_add(1, std::memory_order_relaxed); break;
	case lfl::Outcome::Loose: g_counts.loose.fetch_add(1, std::memory_order_relaxed); break;
	case lfl::Outcome::LooseInFolder: g_counts.looseInFolder.fetch_add(1, std::memory_order_relaxed); break;
	case lfl::Outcome::NotFound: g_counts.notFound.fetch_add(1, std::memory_order_relaxed); break;
	}
}

// The check: the listing's answer on a copy of the path, then the original on the real one, both compared.
UInt32 Check(void* self, char* path, UInt32 flags, UInt32 mask, const lfl::Listing& listing) {
	char before[lfl::kKeyMax];
	const std::size_t length = std::strlen(path);
	if (length + 1 > sizeof(before)) {
		g_counts.refused.fetch_add(1, std::memory_order_relaxed);
		return Original(self, path, flags, mask);
	}
	std::memcpy(before, path, length + 1);
	std::string combined;
	const auto archives = [mask](const char* p) { return reinterpret_cast<ArchivesFn>(kArchives)(p, mask); };
	const lfl::Outcome outcome = lfl::Search(before, flags, archives, reinterpret_cast<StrnicmpFn>(kStrnicmp), FoldersOf(self), listing, &combined);
	Note(outcome);
	const UInt32 result = Original(self, path, flags, mask);
	if (outcome == lfl::Outcome::Refused) {
		return result;
	}
	const char* expected = outcome == lfl::Outcome::LooseInFolder ? combined.c_str() : before;
	if (result != lfl::SlotResult(outcome) || std::strcmp(path, expected) != 0) {
		g_counts.mismatches.fetch_add(1, std::memory_order_relaxed);
		if (g_mismatchLines.fetch_add(1, std::memory_order_relaxed) < kMismatchLines) {
			g_log("Route: loose file list - MISMATCH for \"%s\" (flags %u): the listing %u \"%s\", the original %u \"%s\"", before, flags,
			      lfl::SlotResult(outcome), expected, result, path);
		}
	}
	return result;
}

// The entry's replacement: __fastcall with an unused second register takes the __thiscall's this; three stack
// arguments, callee-cleaned like the original's RET 0x0C.
UInt32 __fastcall Slot(void* self, void*, char* path, UInt32 flags, UInt32 mask) {
	g_counts.calls.fetch_add(1, std::memory_order_relaxed);
	const lfl::Listing* listing = g_listing.load(std::memory_order_acquire);
	if (listing == nullptr) {
		g_counts.notReady.fetch_add(1, std::memory_order_relaxed);
		return Original(self, path, flags, mask);
	}
	if (g_mode == Mode::kCheck) {
		return Check(self, path, flags, mask, *listing);
	}
	const unsigned __int64 t0 = __rdtsc();
	std::string combined;
	const auto archives = [mask](const char* p) { return reinterpret_cast<ArchivesFn>(kArchives)(p, mask); };
	const lfl::Outcome outcome = lfl::Search(path, flags, archives, reinterpret_cast<StrnicmpFn>(kStrnicmp), FoldersOf(self), *listing, &combined);
	Note(outcome);
	if (outcome == lfl::Outcome::Refused) {
		return Original(self, path, flags, mask);
	}
	if (outcome == lfl::Outcome::LooseInFolder) {
		std::memcpy(path, combined.c_str(), combined.size() + 1);  // as 0x0043194A copies its local over the path
	}
	g_counts.answeredTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
	return lfl::SlotResult(outcome);
}

unsigned long long Take(std::atomic<unsigned long long>& counter) {
	return counter.exchange(0, std::memory_order_relaxed);
}

}  // namespace

void InstallLooseFileList(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const UINT value = GetPrivateProfileIntA("Route", "LooseFileList", 0, iniPath);
	g_mode = value == 1 ? Mode::kRoute : value == 2 ? Mode::kCheck : Mode::kOff;
	if (g_mode == Mode::kOff) {
		return;
	}
	g_log = log;
	const DWORD rootLength = GetCurrentDirectoryA(sizeof(g_root), g_root);
	if (rootLength == 0 || rootLength >= sizeof(g_root)) {
		log("Route: loose file list - the working folder could not be read; not routed");
		g_mode = Mode::kOff;
		return;
	}
	if (!InstallEntryDetour("FileFinder search slot 0x00431800 (loose file list)", kSlot, kSlotBytes, sizeof(kSlotBytes), reinterpret_cast<const void*>(&Slot),
	                        &g_original, log, DetourRole::Route)) {
		g_mode = Mode::kOff;
		return;
	}
	HANDLE thread = CreateThread(nullptr, 0, &BuildListing, nullptr, 0, nullptr);
	if (thread != nullptr) {
		SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
		CloseHandle(thread);
	}
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log("Route: loose file list (%s) - the files under \"%s\" listed on a thread of their own; every call before that goes to the original",
	    g_mode == Mode::kRoute ? "route" : "check", g_root);
}

void LogLooseFileListSummary() {
	if (g_mode == Mode::kOff) {
		return;
	}
	const unsigned long long calls = Take(g_counts.calls), notReady = Take(g_counts.notReady), refused = Take(g_counts.refused);
	const unsigned long long archive = Take(g_counts.archive), loose = Take(g_counts.loose), inFolder = Take(g_counts.looseInFolder);
	const unsigned long long notFound = Take(g_counts.notFound), mismatches = Take(g_counts.mismatches);
	const unsigned long long answeredTicks = Take(g_counts.answeredTicks), originalTicks = Take(g_counts.originalTicks);
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{}, frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	g_summaryTsc = tsc;
	g_summaryQpc = qpc;
	const double answeredMs = ticksPerMs > 0.0 ? static_cast<double>(answeredTicks) / ticksPerMs : 0.0;
	const double originalMs = ticksPerMs > 0.0 ? static_cast<double>(originalTicks) / ticksPerMs : 0.0;
	const lfl::Listing* listing = g_listing.load(std::memory_order_acquire);
	g_log("Route: loose file list (%s) - %llu calls; answered: archive %llu, loose %llu, under a search folder %llu, not found %llu (%.3f ms); to the "
	      "original: not ready %llu, refused %llu (%.3f ms); mismatches %llu; the listing %zu entries, %u case twins (%s, %.0f ms)",
	      g_mode == Mode::kRoute ? "route" : "check", calls, archive, loose, inFolder, notFound, answeredMs, notReady, refused, originalMs, mismatches,
	      listing != nullptr ? listing->Size() : 0, g_twins.load(std::memory_order_relaxed), listing != nullptr ? "ready" : "not ready", g_buildMs);
}

}  // namespace obng::plugin
