#include "AnimationNameArena.h"

#include <windows.h>

#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/io/AnimationNameArenaLogic.h"

namespace obng::plugin {

namespace {

namespace na = obng::engine::io::name_arena;

constexpr UInt32 kKeptTable = 0x00B362C0;  // the table of kept lists by folder; null before it is made (read)
constexpr UInt32 kKept = 0x00521190;       // __thiscall(table, folder) -> the folder's kept list or null, RET 4 (read)
constexpr unsigned kMismatchLines = 20;

// A list node as the game has it.
struct Node {
	char* name;
	Node* next;
};

using NamesFn = Node*(__thiscall*)(void* self, const char* folder, UInt32 zero);
using KeptFn = Node*(__thiscall*)(void* table, const char* folder);
using FreeFn = void(__cdecl*)(void* pointer);

// One thread's block and the lists it has out. A list is made and taken apart on the same thread.
struct Local {
	na::Arena arena;
	na::Frames frames;
	explicit Local(std::uint8_t* memory) : arena(memory, na::kArenaBytes) {}
};

LogFn g_log = nullptr;
na::Mode g_mode = na::Mode::Off;
thread_local Local* t_local = nullptr;
std::atomic<unsigned long long> g_lists{0}, g_built{0}, g_names{0}, g_noKept{0}, g_noRoom{0}, g_released{0}, g_freesLeft{0}, g_compared{0}, g_mismatches{0};
std::atomic<unsigned long long> g_ourTicks{0}, g_gameTicks{0};
std::atomic<unsigned> g_mismatchLines{0};
unsigned __int64 g_startTsc = 0;
LARGE_INTEGER g_startQpc{};

Local* LocalOf() {
	if (t_local == nullptr) {
		void* memory = VirtualAlloc(nullptr, na::kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (memory != nullptr) {
			t_local = new Local(static_cast<std::uint8_t*>(memory));
		}
	}
	return t_local;
}

Node* Game(void* self, const char* folder, UInt32 zero) {
	const unsigned __int64 t0 = __rdtsc();
	Node* list = reinterpret_cast<NamesFn>(na::kNames)(self, folder, zero);
	g_gameTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
	return list;
}

unsigned Length(const Node* list) {
	unsigned n = 0;
	for (; list != nullptr; list = list->next) {
		n += list->name != nullptr ? 1u : 0u;
	}
	return n;
}

// In place of 0x00435830 at its two calls in the animation set up 0x004E3490.
Node* __fastcall Names(void* self, void*, const char* folder, UInt32 zero) {
	g_lists.fetch_add(1, std::memory_order_relaxed);
	void* table = *reinterpret_cast<void**>(kKeptTable);
	Node* kept = table != nullptr ? reinterpret_cast<KeptFn>(kKept)(table, folder) : nullptr;
	if (kept == nullptr) {
		g_noKept.fetch_add(1, std::memory_order_relaxed);
		return Game(self, folder, zero);
	}
	Local* local = LocalOf();
	const unsigned __int64 t0 = __rdtsc();
	const std::size_t mark = local != nullptr ? local->arena.Mark() : 0;
	Node* ours = local != nullptr ? na::Build<Node, Node>(&local->arena, kept) : nullptr;
	if (ours != nullptr && !local->frames.Push(mark, ours)) {
		local->arena.Rewind(mark);
		ours = nullptr;
	}
	g_ourTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
	if (ours == nullptr) {
		g_noRoom.fetch_add(1, std::memory_order_relaxed);
		return Game(self, folder, zero);
	}
	g_built.fetch_add(1, std::memory_order_relaxed);
	g_names.fetch_add(Length(ours), std::memory_order_relaxed);
	if (g_mode != na::Mode::Check) {
		return ours;
	}
	// The check: the game's own list beside ours, compared, and the game goes on with its own.
	Node* games = Game(self, folder, zero);
	g_compared.fetch_add(1, std::memory_order_relaxed);
	if (!na::SameList(ours, games)) {
		g_mismatches.fetch_add(1, std::memory_order_relaxed);
		if (g_mismatchLines.fetch_add(1, std::memory_order_relaxed) < kMismatchLines) {
			g_log("Route: animation name arena - MISMATCH for \"%s\": ours %u names, the game's %u", folder != nullptr ? folder : "", Length(ours), Length(games));
		}
	}
	local->arena.Rewind(local->frames.Pop());
	return games;
}

void GiveBack(Local* local, const void* list) {
	if (local != nullptr && local->frames.TopIs(list)) {
		local->arena.Rewind(local->frames.Pop());
		g_released.fetch_add(1, std::memory_order_relaxed);
	}
}

// In place of the game's free at the three places 0x00475D80 frees its list: a name, a node behind the head, the head.
void __cdecl Release(void* pointer) {
	Local* local = t_local;
	if (local != nullptr && local->arena.Holds(pointer)) {
		g_freesLeft.fetch_add(1, std::memory_order_relaxed);
		GiveBack(local, pointer);  // the head, freed last, gives the block back
		return;
	}
	reinterpret_cast<FreeFn>(na::kFree)(pointer);
}

struct Site {
	UInt32 address;
	UInt32 games;
	UInt32 ours;
};

template <typename Function>
UInt32 AddressOf(Function* function) {
	return static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(function));
}

bool PutCalls(LogFn log) {
	const Site sites[] = {
	    {na::kNamesCalls[0], na::kNames, AddressOf(&Names)},  {na::kNamesCalls[1], na::kNames, AddressOf(&Names)},
	    {na::kFreeCalls[0], na::kFree, AddressOf(&Release)},  {na::kFreeCalls[1], na::kFree, AddressOf(&Release)},
	    {na::kFreeCalls[2], na::kFree, AddressOf(&Release)},
	};
	UInt8 bytes[5];
	for (const Site& site : sites) {
		na::CallBytes(site.address, site.games, bytes);
		if (!Verify(site.address, bytes, sizeof(bytes))) {
			log("Route: animation name arena - the call at %08X is not the game's own; not installed", site.address);
			return false;
		}
	}
	// The frees first: a list of ours must never meet the game's free.
	for (int i = 4; i >= 0; --i) {
		const Site& site = sites[i];
		na::CallBytes(site.address, site.ours, bytes);
		if (!SafeWrite(site.address, bytes, sizeof(bytes))) {
			// Put the game's calls back at the name sites (a free of ours beside the game's names is harmless).
			for (int undo = 0; undo < 2; ++undo) {
				na::CallBytes(sites[undo].address, sites[undo].games, bytes);
				SafeWrite(sites[undo].address, bytes, sizeof(bytes));
			}
			log("Route: animation name arena - the call at %08X could not be written; not installed", site.address);
			return false;
		}
	}
	return true;
}

}  // namespace

void AnimationNameArenaSetupEnds(void* list) {
	if (g_mode == na::Mode::Route) {
		GiveBack(t_local, list);
	}
}

void InstallAnimationNameArena(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = na::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "AnimationNameArena", na::Shipped(), iniPath)));
	if (g_mode == na::Mode::Off) {
		return;
	}
	g_log = log;
	if (!PutCalls(log)) {
		g_mode = na::Mode::Off;
		return;
	}
	g_startTsc = __rdtsc();
	QueryPerformanceCounter(&g_startQpc);
	log(g_mode == na::Mode::Route
	        ? "Route: animation name arena - the list of an actor's animation file names is made in a block of OBNG's own (the calls of 0x00435830 at "
	          "0x004E38C5 and 0x004E3B94); the frees in 0x00475D80 leave it alone"
	        : "Route: animation name arena - the check: the list is made both ways and compared name for name; the game goes on with its own");
}

void LogAnimationNameArenaSummary() {
	if (g_mode == na::Mode::Off) {
		return;
	}
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_startQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 ? static_cast<double>(__rdtsc() - g_startTsc) / (seconds * 1000.0) : 0.0;
	const auto ms = [ticksPerMs](unsigned long long ticks) { return ticksPerMs > 0.0 ? static_cast<double>(ticks) / ticksPerMs : 0.0; };
	const unsigned long long built = g_built.load(std::memory_order_relaxed);
	const unsigned long long games = g_noKept.load(std::memory_order_relaxed) + g_noRoom.load(std::memory_order_relaxed) + g_compared.load(std::memory_order_relaxed);
	g_log("Route: animation name arena (%s) - since the start %llu lists asked for: %llu made in the arena (%llu names, %.3f ms in all, %.1f us a list), "
	      "the game's own %llu times (%.3f ms in all, %.1f us a list; no kept list %llu, no room %llu); %llu given back, %llu frees left alone; compared "
	      "%llu, mismatches %llu",
	      g_mode == na::Mode::Route ? "route" : "check", g_lists.load(std::memory_order_relaxed), built, g_names.load(std::memory_order_relaxed),
	      ms(g_ourTicks.load(std::memory_order_relaxed)), built != 0 ? ms(g_ourTicks.load(std::memory_order_relaxed)) * 1000.0 / static_cast<double>(built) : 0.0,
	      games, ms(g_gameTicks.load(std::memory_order_relaxed)), games != 0 ? ms(g_gameTicks.load(std::memory_order_relaxed)) * 1000.0 / static_cast<double>(games) : 0.0,
	      g_noKept.load(std::memory_order_relaxed), g_noRoom.load(std::memory_order_relaxed), g_released.load(std::memory_order_relaxed),
	      g_freesLeft.load(std::memory_order_relaxed), g_compared.load(std::memory_order_relaxed), g_mismatches.load(std::memory_order_relaxed));
}

}  // namespace obng::plugin
