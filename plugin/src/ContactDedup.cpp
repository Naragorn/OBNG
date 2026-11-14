// [Route] ContactDedup: see ContactDedup.h.

#include "ContactDedup.h"

#include <windows.h>

#include <cstring>

#include "engine/havok/ContactDedupLogic.h"

namespace obng::plugin {

namespace {

namespace cd = obng::engine::havok::contact_dedup;

// 0x008CE770: __thiscall of the collector, no arguments, plain RET (read
// 0x008CE770-0x008CE8E2). The collector's fields it reads: the hit array at
// +0x10 with its count at +0x14, the start collector at +0x1A0 (the same
// class: its array and count at the same offsets).
constexpr UInt32 kPass = 0x008CE770;
constexpr UInt8 kPassBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-10h (read)
constexpr UInt32 kHits = 0x10;
constexpr UInt32 kCount = 0x14;
constexpr UInt32 kStartCollector = 0x1A0;
using PassFn = void(__thiscall*)(void* self);

constexpr unsigned kMemos = 4;         // a controller's integrate uses two collectors at a time
constexpr UInt32 kCheckAtMost = 512;   // the check copies this many hits at the most
constexpr unsigned kMismatchLines = 20;

enum class Mode { kOff, kRoute, kCheck };

LogFn g_log = nullptr;
Mode g_mode = Mode::kOff;
void* g_original = nullptr;
volatile LONG g_busy = 0;
cd::Memo g_memos[kMemos];
unsigned g_nextMemo = 0;
cd::Counts g_counts;  // written inside g_busy
unsigned g_mismatchLines = kMismatchLines;
alignas(16) cd::Hit g_copy[kCheckAtMost];

template <class T>
T& At(void* base, UInt32 offset) {
	return *reinterpret_cast<T*>(reinterpret_cast<UInt32>(base) + offset);
}

cd::Memo& MemoFor(std::uint64_t key) {
	for (cd::Memo& memo : g_memos) {
		if (memo.key == key) {
			return memo;
		}
	}
	cd::Memo& memo = g_memos[g_nextMemo];
	g_nextMemo = (g_nextMemo + 1) % kMemos;
	return memo;
}

void __fastcall Pass(void* self, void* /*edx*/) {
	if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
		InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&g_counts.busy));
		reinterpret_cast<PassFn>(g_original)(self);
		return;
	}
	auto* hits = At<cd::Hit*>(self, kHits);
	std::uint32_t& count = At<std::uint32_t>(self, kCount);
	void* other = At<void*>(self, kStartCollector);
	const cd::Hit* start = other != nullptr ? At<cd::Hit*>(other, kHits) : nullptr;
	const std::uint32_t startCount = other != nullptr ? At<std::uint32_t>(other, kCount) : 0;
	const std::uint32_t before = count;
	const std::uint64_t key = reinterpret_cast<UInt32>(self);
	if (g_mode == Mode::kRoute) {
		g_counts.Add(cd::Pass(MemoFor(key), key, hits, &count, start, startCount), before, startCount);
	} else if (before > kCheckAtMost) {
		reinterpret_cast<PassFn>(g_original)(self);
	} else {
		std::memcpy(g_copy, hits, before * sizeof(cd::Hit));
		std::uint32_t copyCount = before;
		g_counts.Add(cd::Pass(MemoFor(key), key, g_copy, &copyCount, start, startCount), before, startCount);
		reinterpret_cast<PassFn>(g_original)(self);
		++g_counts.compared;
		if (copyCount != count || std::memcmp(g_copy, hits, count * sizeof(cd::Hit)) != 0) {
			++g_counts.mismatches;
			if (g_mismatchLines != 0) {
				--g_mismatchLines;
				g_log("Route: contact pass 0x008CE770 MISMATCH - %u hits and %u start hits in, the original left %u, the rebuild %u", before, startCount, count,
				      copyCount);
			}
			MemoFor(key).key = 0;  // go on from the original's array
		}
	}
	InterlockedExchange(&g_busy, 0);
}

}  // namespace

void InstallContactDedup(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const UINT value = GetPrivateProfileIntA("Route", "ContactDedup", 0, iniPath);
	g_mode = value == 1 ? Mode::kRoute : value == 2 ? Mode::kCheck : Mode::kOff;
	if (g_mode == Mode::kOff) {
		return;
	}
	g_log = log;
	if (!InstallEntryDetour("contact pass 0x008CE770 (the character controller's collector)", kPass, kPassBytes, sizeof(kPassBytes),
	                        reinterpret_cast<const void*>(&Pass), &g_original, log, g_mode == Mode::kRoute ? DetourRole::Route : DetourRole::Shadow)) {
		g_mode = Mode::kOff;
		return;
	}
	log(g_mode == Mode::kRoute ? "Route: contact pass 0x008CE770 - the rebuild runs: the same removals in the same order, pairs an earlier pass answered not asked again"
	                           : "Route: contact pass 0x008CE770 - the check: the original runs, the rebuild on a copy, both compared bit for bit");
}

void LogContactDedupSummary() {
	if (g_mode == Mode::kOff) {
		return;
	}
	while (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
		YieldProcessor();
	}
	const cd::Counts counts = g_counts;
	g_counts = cd::Counts{};
	InterlockedExchange(&g_busy, 0);
	char line[700];
	cd::FormatSummary(counts, g_mode == Mode::kCheck, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
