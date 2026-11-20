#include "SoundFileCache.h"

#include <windows.h>

#include <mmsystem.h>

#include <intrin.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Memory.h"
#include "engine/sound/SoundFileCacheLogic.h"

namespace obng::plugin {

namespace {

namespace sfc = obng::engine::sound::file_cache;

static_assert(offsetof(MMIOINFO, lDiskOffset) == sfc::kDiskOffset, "MMIOINFO::lDiskOffset as the game's procedure writes it");
static_assert(offsetof(MMIOINFO, adwInfo) == sfc::kInfo, "MMIOINFO::adwInfo[0], the game's file");

constexpr const char* kPickName = "the sound folder's pick 0x006A9CD0 (sound file cache)";
// __stdcall(char* path) -> al, RET 4 (read 2026-11-20): "<path>*.wav" listed, one name copied over the path.
constexpr UInt32 kPick = 0x006A9CD0;
constexpr UInt8 kPickBytes[] = {0x81, 0xEC, 0x08, 0x01, 0x00, 0x00};  // sub esp, 0x108 (read)
constexpr UInt32 kLister = 0x00431970;           // __cdecl(pattern, folder, 8, 0) -> the list's head: {name, next}
constexpr UInt32 kRandom = 0x0047DF80;           // __cdecl(seed) -> rand()
constexpr UInt32 kFree = 0x00401F20;             // __cdecl(pointer), the game's allocator
constexpr UInt32 kListClear = 0x004526E0;        // __thiscall(head): frees the nodes after the head
constexpr UInt32 kInvalidateOlderFiles = 0x00B04460;  // bInvalidateOlderFiles; without it 0x006A9CD0 lists another way
constexpr char kPattern[] = "*.wav";             // the six bytes at 0x00A5215C
constexpr UInt32 kChunk = 0x10000;
constexpr unsigned kMismatchLines = 20;

using ProcedureFn = LRESULT(__stdcall*)(LPSTR info, UINT message, LPARAM first, LPARAM second);
using PickFn = bool(__stdcall*)(char* path);
using ListerFn = void*(__cdecl*)(const char* pattern, const char* folder, UInt32 flags, UInt32 zero);
using RandomFn = int(__cdecl*)(int seed);
using FreeFn = void(__cdecl*)(void* pointer);
using ListClearFn = void(__thiscall*)(void* head);

struct Node {
	char* name;
	Node* next;
};

// One file of ours open in mmio, at adwInfo[1]. In the check the game's file is open beside it (adwInfo[0]).
struct Open {
	std::shared_ptr<const sfc::Bytes> bytes;
	sfc::Cursor cursor;
	bool beside = false;
};

sfc::Mode g_mode = sfc::Mode::Off;
LogFn g_log = nullptr;
void* g_originalPick = nullptr;
CRITICAL_SECTION g_lock;  // the tables and the counts
std::unique_ptr<sfc::Store> g_store;
std::unique_ptr<sfc::Folders> g_folders;
sfc::Counts g_counts;
unsigned g_mismatchLines = 0;
unsigned __int64 g_fillTicks = 0;
unsigned __int64 g_serveTicks = 0;
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};

LRESULT Game(LPSTR info, UINT message, LPARAM first, LPARAM second) {
	return reinterpret_cast<ProcedureFn>(sfc::kProcedure)(info, message, first, second);
}

MMIOINFO* Record(LPSTR info) {
	return reinterpret_cast<MMIOINFO*>(info);
}

Open* OpenOf(LPSTR info) {
	return reinterpret_cast<Open*>(Record(info)->adwInfo[1]);
}

void Mismatch(const char* what, const char* path, unsigned long long ours, unsigned long long games) {
	EnterCriticalSection(&g_lock);
	++g_counts.mismatches;
	const bool line = g_mismatchLines < kMismatchLines;
	g_mismatchLines += line ? 1u : 0u;
	LeaveCriticalSection(&g_lock);
	if (line) {
		g_log("Route: sound file cache - MISMATCH %s%s%s: kept %llu, the game's file %llu", what, path != nullptr ? " for " : "", path != nullptr ? path : "", ours,
		      games);
	}
}

// The game's file read whole through the game's procedure, from where it stands (its start). False when it is larger
// than a kept file may be.
bool ReadWhole(LPSTR info, sfc::Bytes* bytes) {
	std::vector<std::uint8_t> chunk(kChunk);
	for (;;) {
		const LRESULT got = Game(info, MMIOM_READ, reinterpret_cast<LPARAM>(chunk.data()), static_cast<LPARAM>(kChunk));
		if (got <= 0 || static_cast<UInt32>(got) > kChunk) {
			return true;
		}
		bytes->insert(bytes->end(), chunk.begin(), chunk.begin() + got);
		if (bytes->size() > sfc::kFileMax) {
			return false;
		}
	}
}

// The game's file closed and opened again: its state as after a first open.
LRESULT Reopen(LPSTR info, LPARAM first, LPARAM second) {
	Game(info, MMIOM_CLOSE, 0, 0);
	Record(info)->adwInfo[0] = 0;
	return Game(info, MMIOM_OPEN, first, second);
}

// The record handed over to kept bytes; with `beside` the game's open file stays at adwInfo[0].
LRESULT Serve(LPSTR info, std::shared_ptr<const sfc::Bytes> bytes, bool beside) {
	Open* open = new Open{std::move(bytes), {}, beside};
	MMIOINFO* record = Record(info);
	if (!beside) {
		record->adwInfo[0] = 0;
	}
	record->adwInfo[1] = reinterpret_cast<DWORD>(open);
	record->lDiskOffset = 0;
	return sfc::kAnswerOk;
}

LRESULT OnOpenKept(LPSTR info, LPARAM first, LPARAM second, std::shared_ptr<const sfc::Bytes> kept) {
	bool beside = false;
	if (g_mode == sfc::Mode::Check) {
		const LRESULT games = Game(info, MMIOM_OPEN, first, second);
		if (games != sfc::kAnswerOk) {
			Mismatch("an open", reinterpret_cast<const char*>(first), sfc::kAnswerOk, static_cast<unsigned long long>(games));
			return games;
		}
		beside = true;
	}
	EnterCriticalSection(&g_lock);
	++g_counts.served;
	LeaveCriticalSection(&g_lock);
	return Serve(info, std::move(kept), beside);
}

LRESULT OnOpenFill(LPSTR info, LPARAM first, LPARAM second, const std::string& key) {
	const unsigned __int64 t0 = __rdtsc();
	const LRESULT opened = Game(info, MMIOM_OPEN, first, second);
	if (opened != sfc::kAnswerOk) {
		EnterCriticalSection(&g_lock);
		++g_counts.notFound;
		LeaveCriticalSection(&g_lock);
		return opened;
	}
	auto bytes = std::make_shared<sfc::Bytes>();
	const bool fits = ReadWhole(info, bytes.get());
	EnterCriticalSection(&g_lock);
	const sfc::Kept kept = g_store->Keep(key, bytes);
	const bool serve = fits && kept != sfc::Kept::TooLarge;
	++(serve ? g_counts.filled : g_counts.tooLarge);
	g_fillTicks += __rdtsc() - t0;
	LeaveCriticalSection(&g_lock);
	if (!serve || g_mode == sfc::Mode::Check) {
		// The game's file again from its start: for a file not kept, and beside the kept bytes in the check.
		const LRESULT again = Reopen(info, first, second);
		if (again != sfc::kAnswerOk || !serve) {
			Record(info)->adwInfo[1] = 0;
			return again;
		}
		return Serve(info, std::move(bytes), true);
	}
	Game(info, MMIOM_CLOSE, 0, 0);
	return Serve(info, std::move(bytes), false);
}

LRESULT OnOpen(LPSTR info, LPARAM first, LPARAM second) {
	Record(info)->adwInfo[1] = 0;
	std::string key;
	const bool keyOk = sfc::KeyOf(reinterpret_cast<const char*>(first), &key);
	EnterCriticalSection(&g_lock);
	++g_counts.opens;
	std::shared_ptr<const sfc::Bytes> kept = keyOk ? g_store->Find(key) : nullptr;
	const sfc::Plan plan = sfc::PlanOf(g_mode, keyOk, kept != nullptr, keyOk && g_store->Refused(key));
	g_counts.original += plan == sfc::Plan::Original ? 1u : 0u;
	LeaveCriticalSection(&g_lock);
	switch (plan) {
		case sfc::Plan::Serve:
			return OnOpenKept(info, first, second, std::move(kept));
		case sfc::Plan::Fill:
			return OnOpenFill(info, first, second, key);
		case sfc::Plan::Original:
			break;
	}
	return Game(info, MMIOM_OPEN, first, second);
}

LRESULT OnRead(LPSTR info, Open* open, LPARAM first, LPARAM second) {
	MMIOINFO* record = Record(info);
	const UInt32 before = static_cast<UInt32>(record->lDiskOffset);
	const UInt32 wanted = static_cast<UInt32>(second);
	if (!open->beside) {
		const sfc::Answer answer = sfc::Read(*open->bytes, &open->cursor, reinterpret_cast<std::uint8_t*>(first), wanted, before);
		record->lDiskOffset = static_cast<LONG>(answer.diskOffset);
		return static_cast<LRESULT>(answer.result);
	}
	const LRESULT games = Game(info, MMIOM_READ, first, second);
	std::vector<std::uint8_t> ours(wanted != 0 ? wanted : 1);
	const sfc::Answer answer = sfc::Read(*open->bytes, &open->cursor, ours.data(), wanted, before);
	const sfc::Answer theirs{static_cast<std::uint32_t>(games), static_cast<std::uint32_t>(record->lDiskOffset)};
	EnterCriticalSection(&g_lock);
	++g_counts.compared;
	LeaveCriticalSection(&g_lock);
	if (!(answer == theirs)) {
		Mismatch("a read's answer", nullptr, answer.result, theirs.result);
	} else if (answer.result != 0 && std::memcmp(ours.data(), reinterpret_cast<const void*>(first), answer.result) != 0) {
		Mismatch("a read's bytes at", nullptr, before, before);
	}
	return games;
}

LRESULT OnSeek(LPSTR info, Open* open, LPARAM first, LPARAM second) {
	MMIOINFO* record = Record(info);
	const sfc::Answer answer = sfc::Seek(*open->bytes, &open->cursor, static_cast<std::int32_t>(first), static_cast<std::uint32_t>(second));
	if (!open->beside) {
		record->lDiskOffset = static_cast<LONG>(answer.diskOffset);
		return static_cast<LRESULT>(answer.result);
	}
	const LRESULT games = Game(info, MMIOM_SEEK, first, second);
	const sfc::Answer theirs{static_cast<std::uint32_t>(games), static_cast<std::uint32_t>(record->lDiskOffset)};
	EnterCriticalSection(&g_lock);
	++g_counts.compared;
	LeaveCriticalSection(&g_lock);
	if (!(answer == theirs)) {
		Mismatch("a seek's place", nullptr, answer.diskOffset, theirs.diskOffset);
	}
	return games;
}

LRESULT OnClose(LPSTR info, Open* open) {
	LRESULT result = sfc::kAnswerOk;
	if (open->beside) {
		result = Game(info, MMIOM_CLOSE, 0, 0);
	}
	Record(info)->adwInfo[1] = 0;
	delete open;
	return result;
}

// The procedure mmio calls for a WAV the reader 0x006AABD0 opens, in place of the game's 0x006AAAA0.
LRESULT __stdcall Procedure(LPSTR info, UINT message, LPARAM first, LPARAM second) {
	if (message == MMIOM_OPEN) {
		return OnOpen(info, first, second);
	}
	Open* open = OpenOf(info);
	if (open == nullptr) {
		return Game(info, message, first, second);
	}
	const unsigned __int64 t0 = __rdtsc();
	LRESULT result;
	switch (message) {
		case MMIOM_READ:
			result = OnRead(info, open, first, second);
			break;
		case MMIOM_SEEK:
			result = OnSeek(info, open, first, second);
			break;
		case MMIOM_CLOSE:
			result = OnClose(info, open);
			break;
		default:
			return Game(info, message, first, second);  // a write or an unknown message: logged and refused by the game's
	}
	EnterCriticalSection(&g_lock);
	g_serveTicks += __rdtsc() - t0;
	LeaveCriticalSection(&g_lock);
	return result;
}

// The folder's files through the game's lister, copied, and the list freed as 0x006A9CD0 frees it (0x006A9D80 -
// 0x006A9DDE): with a named node every name, the nodes after the head and the head; without one the head alone.
std::shared_ptr<const sfc::Names> Listing(const char* pattern, const char* folder) {
	auto names = std::make_shared<sfc::Names>();
	Node* head = static_cast<Node*>(reinterpret_cast<ListerFn>(kLister)(pattern, folder, 8, 0));
	if (head == nullptr) {
		return names;
	}
	for (const Node* node = head; node != nullptr; node = node->next) {
		names->push_back(node->name != nullptr ? sfc::Name{node->name, true} : sfc::Name{std::string(), false});
	}
	const FreeFn release = reinterpret_cast<FreeFn>(kFree);
	if (sfc::CountOf(*names) != 0) {
		for (Node* node = head; node != nullptr; node = node->next) {
			release(node->name);
		}
		reinterpret_cast<ListClearFn>(kListClear)(head);
	}
	release(head);
	return names;
}

// 0x006A9CD0's replacement: the listing kept by its pattern, the pick by the game's random number.
bool __stdcall Pick(char* path) {
	char pattern[sfc::kPathMax + sizeof(kPattern)];
	std::string key;
	const std::size_t length = path != nullptr ? std::strlen(path) : 0;
	if (*reinterpret_cast<const UInt8*>(kInvalidateOlderFiles) == 0 || length == 0 || length + sizeof(kPattern) > sfc::kPathMax) {
		return reinterpret_cast<PickFn>(g_originalPick)(path);
	}
	std::memcpy(pattern, path, length);
	std::memcpy(pattern + length, kPattern, sizeof(kPattern));
	sfc::KeyOf(pattern, &key);
	EnterCriticalSection(&g_lock);
	++g_counts.picks;
	std::shared_ptr<const sfc::Names> names = g_folders->Find(key);
	g_counts.picksKept += names != nullptr ? 1u : 0u;
	LeaveCriticalSection(&g_lock);
	if (names == nullptr || g_mode == sfc::Mode::Check) {
		std::shared_ptr<const sfc::Names> listed = Listing(pattern, path);
		EnterCriticalSection(&g_lock);
		++g_counts.listings;
		const bool differs = names != nullptr && !sfc::SameNames(*names, *listed);
		g_counts.listMismatches += differs ? 1u : 0u;
		if (names == nullptr) {
			g_folders->Keep(key, listed);
			names = listed;
		}
		LeaveCriticalSection(&g_lock);
		if (differs) {
			Mismatch("a folder's listing", pattern, names->size(), listed->size());
		}
	}
	if (sfc::CountOf(*names) == 0) {
		return false;
	}
	const int pick = sfc::PickOf(*names, static_cast<std::uint32_t>(reinterpret_cast<RandomFn>(kRandom)(0)));
	if (pick != sfc::kNoPick) {
		const std::string& name = (*names)[static_cast<std::size_t>(pick)].text;
		std::memcpy(path, name.c_str(), name.size() + 1);
	}
	return true;
}

bool PutProcedure(LogFn log) {
	const UInt32 games = sfc::kProcedure;
	if (!Verify(sfc::kProcedureSite, reinterpret_cast<const UInt8*>(&games), sizeof(games))) {
		log("Route: sound file cache - the WAV reader's procedure at %08X is not the game's own; not installed", sfc::kProcedureSite);
		return false;
	}
	const UInt32 ours = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Procedure));
	if (!SafeWrite(sfc::kProcedureSite, &ours, sizeof(ours))) {
		log("Route: sound file cache - the WAV reader's procedure at %08X could not be written; not installed", sfc::kProcedureSite);
		return false;
	}
	return true;
}

}  // namespace

void InstallSoundFileCache(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = sfc::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "SoundFileCache", sfc::Shipped(), iniPath)));
	if (g_mode == sfc::Mode::Off) {
		return;
	}
	g_log = log;
	InitializeCriticalSection(&g_lock);
	g_store = std::make_unique<sfc::Store>();
	g_folders = std::make_unique<sfc::Folders>();
	if (!PutProcedure(log)) {
		g_mode = sfc::Mode::Off;
		return;
	}
	const bool pick = InstallEntryDetour(kPickName, kPick, kPickBytes, sizeof(kPickBytes), reinterpret_cast<const void*>(&Pick), &g_originalPick, log,
	                                     g_mode == sfc::Mode::Route ? DetourRole::Route : DetourRole::Shadow);
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log("Route: sound file cache%s - a WAV file is read from its file once and from memory after that (files up to %u KiB, %u MiB in all); a sound folder's "
	    "files are %s",
	    g_mode == sfc::Mode::Check ? " (the check: the game's file read beside the kept bytes, every folder listed again)" : "",
	    static_cast<unsigned>(sfc::kFileMax >> 10), static_cast<unsigned>(sfc::kTotalMax >> 20), pick ? "listed once" : "listed every time (the pick's detour was refused)");
}

void LogSoundFileCacheSummary() {
	if (g_mode == sfc::Mode::Off) {
		return;
	}
	EnterCriticalSection(&g_lock);
	const sfc::Counts c = g_counts;
	const unsigned __int64 fillTicks = g_fillTicks;
	const unsigned __int64 serveTicks = g_serveTicks;
	const std::size_t files = g_store->Files();
	const std::size_t total = g_store->Total();
	const std::uint64_t dropped = g_store->Dropped();
	const std::size_t folders = g_folders->Size();
	LeaveCriticalSection(&g_lock);
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	const auto ms = [ticksPerMs](unsigned __int64 ticks) { return ticksPerMs > 0.0 ? static_cast<double>(ticks) / ticksPerMs : 0.0; };
	g_log("Route: sound file cache (%s) - since the start %llu opens: %llu from memory (their reads, seeks and closes %.3f ms in all), %llu read whole and "
	      "kept (%.3f ms in all), %llu left to the game's file (%llu too large, %llu not found); %zu files kept, %.1f MiB, %llu dropped for the budget; folder "
	      "picks %llu, %llu from a kept listing, %llu listings asked, %zu kept; compared %llu, mismatches %llu, listing mismatches %llu",
	      g_mode == sfc::Mode::Route ? "route" : "check", c.opens, c.served, ms(serveTicks), c.filled, ms(fillTicks), c.original + c.tooLarge + c.notFound,
	      c.tooLarge, c.notFound, files, static_cast<double>(total) / (1024.0 * 1024.0), dropped, c.picks, c.picksKept, c.listings, folders, c.compared,
	      c.mismatches, c.listMismatches);
}

}  // namespace obng::plugin
