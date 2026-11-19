#include "LooseListing.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace obng::plugin {

namespace {

namespace lfl = obng::engine::io::loose_file_list;

std::atomic<const lfl::Listing*> g_listing{nullptr};
char g_root[MAX_PATH] = {};
double g_buildMs = 0.0;
std::atomic<unsigned> g_twins{0};  // folders' names with a case twin, marked ambiguous

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

}  // namespace

bool StartLooseListing(LogFn log) {
	static int s_state = 0;  // 0 not asked, 1 started, 2 failed; the routes are installed on one thread
	if (s_state != 0) {
		return s_state == 1;
	}
	const DWORD rootLength = GetCurrentDirectoryA(sizeof(g_root), g_root);
	if (rootLength == 0 || rootLength >= sizeof(g_root)) {
		log("Loose listing: the working folder could not be read");
		s_state = 2;
		return false;
	}
	s_state = 1;
	HANDLE thread = CreateThread(nullptr, 0, &BuildListing, nullptr, 0, nullptr);
	if (thread != nullptr) {
		SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
		CloseHandle(thread);
	}
	return true;
}

const lfl::Listing* LooseListing() {
	return g_listing.load(std::memory_order_acquire);
}

const char* LooseListingRoot() {
	return g_root;
}

double LooseListingBuildMs() {
	return g_buildMs;
}

unsigned LooseListingTwins() {
	return g_twins.load(std::memory_order_relaxed);
}

}  // namespace obng::plugin
