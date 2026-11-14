#include "TimedWrites.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "ExeDataSection.h"
#include "TimedWritesLogic.h"
#include "engine/scene/ActorThreadsLogic.h"

namespace obng::plugin::timed_writes {

namespace {

namespace at = obng::engine::scene::actor_threads;

constexpr UInt32 kMaxRuns = 512;
constexpr UInt32 kShown = 24;
constexpr std::size_t kNameSize = 96;

Probe g_probe;
bool g_on = false;
LogFn g_log = nullptr;
char g_name[kWatchedAtMost][kNameSize] = {};
UInt32 g_address[kWatchedAtMost] = {};
std::unique_ptr<at::WriteTally[]> g_tally;
std::uint64_t g_runs[kWatchedAtMost] = {};
std::uint64_t g_lastCalls[kWatchedAtMost] = {};
std::uint64_t g_lastProbes[kWatchedAtMost] = {};
std::uint64_t g_lastAbandoned = 0;
std::unique_ptr<std::uint32_t[]> g_before;
std::unique_ptr<std::uint32_t[]> g_after;

const void* Data() { return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(exe_data::kStart)); }

}  // namespace

UInt32 Install(const char* iniPath, const timed::Spec* specs, UInt32 count, LogFn log) {
	char list[400] = {};
	if (g_on || iniPath == nullptr || GetPrivateProfileStringA("Timed", "WritesOf", "", list, sizeof(list), iniPath) == 0) {
		return 0;
	}
	char names[kWatchedAtMost][kNameSize] = {};
	const std::size_t asked = SplitNames(list, names[0], kNameSize, kWatchedAtMost);
	std::uint32_t indices[kWatchedAtMost] = {};
	UInt32 found = 0;
	for (std::size_t n = 0; n < asked; ++n) {
		UInt32 at = count;
		for (UInt32 i = 0; i < count; ++i) {
			at = std::strcmp(specs[i].name, names[n]) == 0 ? i : at;
		}
		if (at == count) {
			log("Timed: WritesOf \"%s\" is not a timed function's name; not watched", names[n]);
			continue;
		}
		indices[found] = at;
		std::memcpy(g_name[found], names[n], kNameSize);
		g_address[found] = specs[at].address;
		++found;
	}
	if (found == 0) {
		return 0;
	}
	const UINT every = GetPrivateProfileIntA("Timed", "WritesEvery", 8, iniPath);
	g_probe.Set(indices, found, every);
	g_tally.reset(new at::WriteTally[kWatchedAtMost]);
	g_before.reset(new std::uint32_t[exe_data::kDwords]);
	g_after.reset(new std::uint32_t[exe_data::kDwords]);
	g_log = log;
	g_on = true;
	log("Timed: the statics written by %u functions' calls are probed (every %u. call; a probed call costs two copies of the EXE's .data - "
	    "this run's timings are not the game's)",
	    found, every == 0 ? 1u : every);
	return found;
}

void Entered(UInt32 index, std::size_t depthAfter, bool pushed) {
	if (g_on && g_probe.Entered(index, depthAfter, pushed)) {
		std::memcpy(g_before.get(), Data(), exe_data::kDwords * 4);
	}
}

void Exited(std::size_t depthAfter) {
	if (!g_on) {
		return;
	}
	const int watch = g_probe.Exited(depthAfter);
	if (watch == kNone) {
		return;
	}
	std::memcpy(g_after.get(), Data(), exe_data::kDwords * 4);
	static at::WrittenRun runs[kMaxRuns];  // the timed thread alone comes here
	const UInt32 n = at::WrittenRuns(g_before.get(), g_after.get(), exe_data::kDwords, runs, kMaxRuns);
	for (UInt32 i = 0; i < n; ++i) {
		g_tally[watch].Note(runs[i].first, runs[i].count);
	}
	g_runs[watch] += n;
}

void LogSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	// Read while the timed thread counts: a window's numbers may be one call apart.
	for (std::size_t w = 0; w < g_probe.Count(); ++w) {
		const std::uint64_t calls = g_probe.Calls(w);
		const std::uint64_t probes = g_probe.Probes(w);
		UInt32 first[kShown];
		UInt32 times[kShown];
		const UInt32 n = g_tally[w].Most(first, times, kShown);
		char places[700] = {};
		int used = 0;
		for (UInt32 i = 0; i < n && used >= 0 && used < 670; ++i) {
			used += std::snprintf(places + used, sizeof(places) - static_cast<std::size_t>(used), " %08X x%u;", exe_data::kStart + 4 * first[i], times[i]);
		}
		g_log("Probe: statics written by %s (0x%08X) - %llu calls, %llu probed since the start (%llu and %llu in this window): %u places "
		      "(%llu runs, %llu not kept); the most often:%s",
		      g_name[w], g_address[w], static_cast<unsigned long long>(calls), static_cast<unsigned long long>(probes),
		      static_cast<unsigned long long>(calls - g_lastCalls[w]), static_cast<unsigned long long>(probes - g_lastProbes[w]), g_tally[w].Distinct(),
		      static_cast<unsigned long long>(g_runs[w]), static_cast<unsigned long long>(g_tally[w].Dropped()), places);
		g_lastCalls[w] = calls;
		g_lastProbes[w] = probes;
	}
	const std::uint64_t abandoned = g_probe.Abandoned();
	if (abandoned != g_lastAbandoned) {
		g_log("Probe: statics written - %llu probed calls abandoned (their frame went without returning)",
		      static_cast<unsigned long long>(abandoned - g_lastAbandoned));
		g_lastAbandoned = abandoned;
	}
}

}  // namespace obng::plugin::timed_writes
