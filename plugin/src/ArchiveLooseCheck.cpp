#include "ArchiveLooseCheck.h"

#include <windows.h>

#include <intrin.h>
#include <atomic>
#include <cstring>

#include "LooseListing.h"
#include "engine/io/ArchiveLooseCheckLogic.h"

namespace obng::plugin {

namespace {

namespace alc = obng::engine::io::archive_loose_check;
namespace lfl = obng::engine::io::loose_file_list;

// __thiscall(Archive* this, BSAEntry* record, char* path) -> bool in AL, RET 8 at 0x0042C2C1 (read 2026-11-19).
constexpr UInt32 kEntry = 0x0042C1D0;
constexpr UInt8 kEntryBytes[] = {0x81, 0xEC, 0x38, 0x01, 0x00, 0x00};  // sub esp, 0x138 (read)
constexpr UInt32 kEnabled = 0x00B04460;                                // byte: archive invalidation on (read at 0x0042C1E4)
constexpr UInt32 kStat = 0x00983754;                                   // the CRT's _stat64i32, called at 0x0042C276 (read)
constexpr UInt32 kRecordFlags = 0x08;                                  // the record's +8, bit 31 "checked" (read)
constexpr unsigned kMismatchLines = 20;

using CheckFn = bool(__thiscall*)(void* archive, UInt8* record, char* path);
using StatFn = int(__cdecl*)(const char* path, void* buffer);

enum class Mode { kOff, kRoute, kCheck };

void* g_original = nullptr;
Mode g_mode = Mode::kOff;
LogFn g_log = nullptr;

struct Counts {
	std::atomic<unsigned long long> calls{0}, notReady{0}, skip{0}, noLoose{0}, loose{0}, refused{0}, mismatches{0};
	std::atomic<unsigned long long> answeredTicks{0}, originalTicks{0};
};
Counts g_counts;
std::atomic<unsigned> g_mismatchLines{0};
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};

bool Original(void* archive, UInt8* record, char* path) {
	const unsigned __int64 t0 = __rdtsc();
	const bool result = reinterpret_cast<CheckFn>(g_original)(archive, record, path);
	g_counts.originalTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
	return result;
}

void Note(alc::Decision decision) {
	switch (decision) {
	case alc::Decision::Skip: g_counts.skip.fetch_add(1, std::memory_order_relaxed); break;
	case alc::Decision::NoLooseFile: g_counts.noLoose.fetch_add(1, std::memory_order_relaxed); break;
	case alc::Decision::Loose: g_counts.loose.fetch_add(1, std::memory_order_relaxed); break;
	case alc::Decision::Refused: g_counts.refused.fetch_add(1, std::memory_order_relaxed); break;
	}
}

// The check: where the listing said whether "Data\<path>" exists, a _stat of its own says too (the original's own
// call, the same string); then the original does the work.
bool Check(void* archive, UInt8* record, char* path, alc::Decision decision) {
	if (path != nullptr && (decision == alc::Decision::NoLooseFile || decision == alc::Decision::Loose)) {
		char joined[0x104];  // the original's local, 260 bytes
		const std::size_t prefix = sizeof(alc::kDataPrefix) - 1;
		const std::size_t length = std::strlen(path);
		if (prefix + length + 1 <= sizeof(joined)) {
			std::memcpy(joined, alc::kDataPrefix, prefix);
			std::memcpy(joined + prefix, path, length + 1);
			alignas(8) UInt8 buffer[64] = {};  // struct _stat64i32, 48 bytes
			const bool exists = reinterpret_cast<StatFn>(kStat)(joined, buffer) != -1;
			if (exists != (decision == alc::Decision::Loose)) {
				g_counts.mismatches.fetch_add(1, std::memory_order_relaxed);
				if (g_mismatchLines.fetch_add(1, std::memory_order_relaxed) < kMismatchLines) {
					g_log("Route: archive loose check - MISMATCH for \"%s\": the listing %s, _stat %s", joined,
					      decision == alc::Decision::Loose ? "a file" : "none", exists ? "found" : "nothing");
				}
			}
		}
	}
	return Original(archive, record, path);
}

// The entry's replacement: __fastcall with an unused second register takes the __thiscall's this; two stack
// arguments, callee-cleaned like the original's RET 8.
bool __fastcall Entry(void* archive, void*, UInt8* record, char* path) {
	g_counts.calls.fetch_add(1, std::memory_order_relaxed);
	const lfl::Listing* listing = LooseListing();
	if (listing == nullptr) {
		g_counts.notReady.fetch_add(1, std::memory_order_relaxed);
		return Original(archive, record, path);
	}
	const unsigned __int64 t0 = __rdtsc();
	const bool enabled = *reinterpret_cast<const volatile UInt8*>(kEnabled) != 0;
	UInt32 flags = 0;
	std::memcpy(&flags, record + kRecordFlags, sizeof(flags));
	const alc::Decision decision = alc::Decide(enabled, flags, path, *listing);
	Note(decision);
	if (g_mode == Mode::kCheck) {
		return Check(archive, record, path, decision);
	}
	const alc::Answer answer = alc::AnswerOf(decision);
	if (!answer.answered) {
		return Original(archive, record, path);
	}
	if (answer.mark) {
		*reinterpret_cast<UInt32*>(record + kRecordFlags) |= alc::kChecked;  // as 0x0042C2A0 does
	}
	g_counts.answeredTicks.fetch_add(__rdtsc() - t0, std::memory_order_relaxed);
	return false;
}

unsigned long long Take(std::atomic<unsigned long long>& counter) {
	return counter.exchange(0, std::memory_order_relaxed);
}

}  // namespace

void InstallArchiveLooseCheck(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const UINT value = GetPrivateProfileIntA("Route", "ArchiveLooseCheck", 0, iniPath);
	g_mode = value == 1 ? Mode::kRoute : value == 2 ? Mode::kCheck : Mode::kOff;
	if (g_mode == Mode::kOff) {
		return;
	}
	g_log = log;
	if (!StartLooseListing(log)) {
		log("Route: archive loose check - no listing; not routed");
		g_mode = Mode::kOff;
		return;
	}
	if (!InstallEntryDetour("archive loose file check 0x0042C1D0", kEntry, kEntryBytes, sizeof(kEntryBytes), reinterpret_cast<const void*>(&Entry),
	                        &g_original, log, DetourRole::Route)) {
		g_mode = Mode::kOff;
		return;
	}
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log("Route: archive loose check (%s) - a record's loose file looked up in the listing of \"%s\"; every call before it is ready goes to the "
	    "original",
	    g_mode == Mode::kRoute ? "route" : "check", LooseListingRoot());
}

void LogArchiveLooseCheckSummary() {
	if (g_mode == Mode::kOff) {
		return;
	}
	const unsigned long long calls = Take(g_counts.calls), notReady = Take(g_counts.notReady), skip = Take(g_counts.skip);
	const unsigned long long noLoose = Take(g_counts.noLoose), loose = Take(g_counts.loose), refused = Take(g_counts.refused);
	const unsigned long long mismatches = Take(g_counts.mismatches);
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
	g_log("Route: archive loose check (%s) - %llu calls; answered: checked before or off %llu, no loose file %llu (%.3f ms); to the original: "
	      "not ready %llu, a loose file %llu, refused %llu (%.3f ms); mismatches %llu",
	      g_mode == Mode::kRoute ? "route" : "check", calls, skip, noLoose, answeredMs, notReady, loose, refused, originalMs, mismatches);
}

}  // namespace obng::plugin
