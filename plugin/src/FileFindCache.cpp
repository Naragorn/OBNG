#include "FileFindCache.h"

#include <windows.h>

#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <memory>

#include "engine/io/FileFindCacheLogic.h"

namespace obng::plugin {

namespace {

namespace ffc = obng::engine::io::file_find_cache;

constexpr const char* kName = "FileFinder::FindFile 0x00431020 (file find cache)";
// __thiscall(path, out, flags, mask) -> result, RET 0x10 at 0x00431103 (read 2026-11-18); FileFinder's vtable
// 0x00A36414 slot 1 (by RTTI), the finder at 0x00B33A04.
constexpr UInt32 kAddress = 0x00431020;
constexpr UInt8 kBytes[] = {0x81, 0xEC, 0x08, 0x01, 0x00, 0x00};  // sub esp, 0x108 (read)
constexpr unsigned kMismatchLines = 20;
constexpr unsigned kKindSlots = 24;  // the passed kinds' census

using FindFn = UInt32(__thiscall*)(void* self, const char* path, char* out, UInt32 flags, UInt32 mask);

enum class Mode { kOff, kRoute, kCheck };

void* g_original = nullptr;
Mode g_mode = Mode::kOff;
LogFn g_log = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
std::unique_ptr<ffc::Table> g_table;
ffc::Counts g_counts;  // under g_lock (exclusive)
unsigned __int64 g_askedTicks = 0;
unsigned __int64 g_hitTicks = 0;
unsigned g_mismatchLines = 0;
struct KindCount {
	char kind[8];
	unsigned long long calls;
};
KindCount g_kinds[kKindSlots] = {};
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};

UInt32 Original(void* self, const char* path, char* out, UInt32 flags, UInt32 mask) {
	return reinterpret_cast<FindFn>(g_original)(self, path, out, flags, mask);
}

// Under g_lock (exclusive): one more call of a kind that is not kept, by its extension.
void NoteKind(const char* path) {
	const char* extension = ffc::ExtensionOf(path);
	char kind[8] = {};
	std::strncpy(kind, extension[0] != '\0' ? extension : "(none)", sizeof(kind) - 1);
	for (KindCount& k : g_kinds) {
		if (k.calls == 0) {
			std::memcpy(k.kind, kind, sizeof(kind));
			k.calls = 1;
			return;
		}
		if (_stricmp(k.kind, kind) == 0) {
			++k.calls;
			return;
		}
	}
}

// The original asked with a buffer of ours, so that its found path is known whether the caller passed one or not.
ffc::Answer Ask(void* self, const char* path, UInt32 flags, UInt32 mask, unsigned __int64* ticks) {
	char found[ffc::kPathMax];
	found[0] = '\0';
	const unsigned __int64 t0 = __rdtsc();
	ffc::Answer answer;
	answer.result = Original(self, path, found, flags, mask);
	*ticks = __rdtsc() - t0;
	answer.Keep(found);
	return answer;
}

void LogMismatch(const char* path, const ffc::Answer& kept, const ffc::Answer& now) {
	if (g_mismatchLines >= kMismatchLines) {
		return;
	}
	++g_mismatchLines;
	g_log("Route: file find cache - MISMATCH for \"%s\": kept %u \"%s\", the original now %u \"%s\"", path, kept.result, kept.found.c_str(), now.result,
	      now.found.c_str());
}

// The entry's replacement: __fastcall with an unused second register takes the __thiscall's this; four stack
// arguments, callee-cleaned like the original's RET 0x10.
UInt32 __fastcall Find(void* self, void*, const char* path, char* out, UInt32 flags, UInt32 mask) {
	const ffc::Pass pass = ffc::Classify(path);
	ffc::KeyBuffer key;
	const UInt32 finder = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self));
	if (pass != ffc::Pass::Keep || !key.Make(finder, flags, mask, path)) {
		AcquireSRWLockExclusive(&g_lock);
		++g_counts.calls;
		g_counts.Note(pass == ffc::Pass::Keep ? ffc::Pass::TooLong : pass);
		if (pass == ffc::Pass::Kind) {
			NoteKind(path);
		}
		ReleaseSRWLockExclusive(&g_lock);
		return Original(self, path, out, flags, mask);
	}
	if (g_mode == Mode::kRoute) {
		const unsigned __int64 t0 = __rdtsc();
		AcquireSRWLockShared(&g_lock);
		const ffc::Answer* kept = g_table->Find(key.View());
		if (kept != nullptr) {
			const UInt32 result = kept->result;
			kept->WriteTo(out);
			ReleaseSRWLockShared(&g_lock);
			const unsigned __int64 ticks = __rdtsc() - t0;
			AcquireSRWLockExclusive(&g_lock);
			++g_counts.calls;
			++(result != 0 ? g_counts.hitsFound : g_counts.hitsMissing);
			g_hitTicks += ticks;
			ReleaseSRWLockExclusive(&g_lock);
			return result;
		}
		ReleaseSRWLockShared(&g_lock);
	}
	unsigned __int64 ticks = 0;
	const ffc::Answer now = Ask(self, path, flags, mask, &ticks);
	AcquireSRWLockExclusive(&g_lock);
	++g_counts.calls;
	++g_counts.asked;
	g_askedTicks += ticks;
	const ffc::Answer* kept = g_table->Find(key.View());
	if (kept != nullptr) {
		// The check (or a second thread that asked the same path at once in the route): the kept answer must be the original's.
		if (!kept->Same(now.result, now.found.c_str())) {
			++g_counts.mismatches;
			LogMismatch(path, *kept, now);
		}
	} else if (g_table->Keep(key.View(), now) == ffc::Kept::Full) {
		++g_counts.full;
	}
	ReleaseSRWLockExclusive(&g_lock);
	now.WriteTo(out);
	return now.result;
}

}  // namespace

void InstallFileFindCache(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const UINT value = GetPrivateProfileIntA("Route", "FileFindCache", 0, iniPath);
	g_mode = value == 1 ? Mode::kRoute : value == 2 ? Mode::kCheck : Mode::kOff;
	if (g_mode == Mode::kOff) {
		return;
	}
	g_log = log;
	g_table = std::make_unique<ffc::Table>();
	if (!InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(&Find), &g_original, log,
	                        g_mode == Mode::kRoute ? DetourRole::Route : DetourRole::Shadow)) {
		g_mode = Mode::kOff;
		g_table.reset();
		return;
	}
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log(g_mode == Mode::kRoute ? "Route: file find cache - a relative asset path's answer kept and given again; INIs, text, absolute and .\\ paths asked every time"
	                           : "Route: file find cache - the check: the original answers every call, compared with the answer kept for the same call");
}

void LogFileFindCacheSummary() {
	if (g_mode == Mode::kOff) {
		return;
	}
	AcquireSRWLockExclusive(&g_lock);
	const ffc::Counts c = g_counts;
	const unsigned __int64 askedTicks = g_askedTicks;
	const unsigned __int64 hitTicks = g_hitTicks;
	const std::size_t entries = g_table->Size();
	KindCount kinds[kKindSlots];
	std::memcpy(kinds, g_kinds, sizeof(kinds));
	g_counts = ffc::Counts{};
	g_askedTicks = 0;
	g_hitTicks = 0;
	std::memset(g_kinds, 0, sizeof(g_kinds));
	ReleaseSRWLockExclusive(&g_lock);

	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 && g_summaryTsc != 0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	g_summaryTsc = tsc;
	g_summaryQpc = qpc;
	const double askedMs = ticksPerMs > 0.0 ? static_cast<double>(askedTicks) / ticksPerMs : 0.0;
	const double hitMs = ticksPerMs > 0.0 ? static_cast<double>(hitTicks) / ticksPerMs : 0.0;
	const auto passed = [&c](ffc::Pass p) { return c.passed[static_cast<int>(p)]; };
	g_log("Route: file find cache (%s) - %llu calls; from the cache %llu found + %llu not found (%.3f ms); asked of the original and kept %llu (%.3f ms, %.1f us "
	      "each); passed %llu (another kind %llu, absolute %llu, .\\ %llu, too long %llu, empty %llu); %zu kept, %llu not kept (full); mismatches %llu",
	      g_mode == Mode::kRoute ? "route" : "check", c.calls, c.hitsFound, c.hitsMissing, hitMs, c.asked, askedMs,
	      c.asked != 0 ? askedMs * 1000.0 / static_cast<double>(c.asked) : 0.0, c.PassedAll(), passed(ffc::Pass::Kind), passed(ffc::Pass::Absolute),
	      passed(ffc::Pass::DotRelative), passed(ffc::Pass::TooLong), passed(ffc::Pass::Empty), entries, c.full, c.mismatches);
	char line[600];
	int used = std::snprintf(line, sizeof(line), "Route: file find cache - the kinds passed by their extension:");
	for (const KindCount& k : kinds) {
		if (k.calls == 0 || used <= 0 || static_cast<std::size_t>(used) >= sizeof(line)) {
			break;
		}
		used += std::snprintf(line + used, sizeof(line) - static_cast<std::size_t>(used), " %s %llu;", k.kind, k.calls);
	}
	if (kinds[0].calls != 0) {
		g_log("%s", line);
	}
}

}  // namespace obng::plugin
