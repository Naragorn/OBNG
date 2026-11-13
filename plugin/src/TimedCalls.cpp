#include "TimedCalls.h"

#include <intrin.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Memory.h"
#include "ShadowTimeInfo.h"
#include "TimedCallLogic.h"

namespace obng::plugin {

namespace {

timed::Calls g_calls;
timed::Spec g_spec[timed::kFunctionsAtMost];
bool g_on[timed::kFunctionsAtMost] = {};
void* g_original[timed::kFunctionsAtMost] = {};
timed::Totals g_last[timed::kFunctionsAtMost];
UInt32 g_count = 0;
volatile LONG g_thread = 0;  // the timed thread: the first one that calls F1 (the plugin is not loaded on the game's main thread: run 20261111-085559 timed nothing with the loading thread)
UInt32 g_stub = 0;
LogFn g_log = nullptr;
unsigned long long g_lastStamp = 0;
LARGE_INTEGER g_lastClock{};
LARGE_INTEGER g_frequency{};
// The spike watch ([Timed] SpikeName, SpikeMs, SpikeLines): a long call of one listed function, written at once by
// the timed thread with what the other listed functions did inside it.
bool g_spikes = false;
double g_ticksPerMs = 0.0;       // measured once at install
volatile LONG g_spikeLines = 0;  // lines written in this summary window
LONG g_spikeLinesAtMost = 20;
constexpr double kSpikeLeastShare = 0.03;

void WriteSpike(const timed::Spike& spike) {
	if (InterlockedIncrement(&g_spikeLines) > g_spikeLinesAtMost) {
		return;
	}
	const char* names[timed::kFunctionsAtMost] = {};
	for (UInt32 i = 0; i < g_count; ++i) {
		names[i] = g_spec[i].name;
	}
	char line[900];
	timed::FormatSpike(spike, names, g_count, g_ticksPerMs, kSpikeLeastShare, line, sizeof(line));
	g_log("%s", line);
}

// The time stamp counter's ticks in a millisecond, over five milliseconds of the performance counter.
double MeasureTicksPerMs() {
	LARGE_INTEGER frequency;
	LARGE_INTEGER from;
	LARGE_INTEGER now;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&from);
	const unsigned long long stamp = __rdtsc();
	const LONGLONG span = frequency.QuadPart / 200;
	do {
		QueryPerformanceCounter(&now);
	} while (now.QuadPart - from.QuadPart < span);
	const double ms = static_cast<double>(now.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	return ms > 0.0 ? static_cast<double>(__rdtsc() - stamp) / ms : 0.0;
}

// [Timed] SpikeName=<a listed function's name> SpikeMs=<its call longer than this> SpikeLines=<at most a window>.
void InstallSpikeWatch(const char* iniPath, LogFn log) {
	char name[96] = {};
	if (GetPrivateProfileStringA("Timed", "SpikeName", "", name, sizeof(name), iniPath) == 0) {
		return;
	}
	UInt32 root = g_count;
	for (UInt32 i = 0; i < g_count; ++i) {
		if (std::strcmp(g_spec[i].name, name) == 0) {
			root = i;
		}
	}
	if (root == g_count) {
		log("Timed: SpikeName \"%s\" is not a timed function's name; no spike watch", name);
		return;
	}
	char value[32] = {};
	GetPrivateProfileStringA("Timed", "SpikeMs", "8", value, sizeof(value), iniPath);
	const double ms = std::strtod(value, nullptr);
	g_ticksPerMs = MeasureTicksPerMs();
	if (!(ms > 0.0) || !(g_ticksPerMs > 0.0)) {
		log("Timed: SpikeMs \"%s\" is not a time above 0 (or the counter could not be measured); no spike watch", value);
		return;
	}
	const UINT lines = GetPrivateProfileIntA("Timed", "SpikeLines", 20, iniPath);
	g_spikeLinesAtMost = static_cast<LONG>(lines > 1000 ? 1000 : lines);
	g_calls.Watch(root, static_cast<unsigned long long>(ms * g_ticksPerMs));
	g_spikes = true;
	log("Timed: spike watch - a call of %s over %.1f ms is written with what the timed functions did inside it, at most %ld lines a window",
	    name, ms, g_spikeLinesAtMost);
}

// The thread's id from its TEB (FS:[0x24]) - no call, nothing clobbered.
UInt32 ThisThread() { return __readfsdword(0x24); }

// Called by every entry thunk (TimedCallLogic.h BuildEntryThunk). Integer
// code only: the registers a convention may pass in are saved by the thunk,
// the x87 stack is empty at a call.
void __cdecl Enter(UInt32 index, UInt32* slot) {
	const LONG thread = static_cast<LONG>(ThisThread());
	if (g_thread != thread) {
		if (index != 0 || InterlockedCompareExchange(&g_thread, thread, 0) != 0) {
			return;
		}
	}
	if (g_spikes) {
		// A spike that ended since the last entry is written here, not in Exit: the line needs floating point, and
		// at a call the x87 stack is empty and the vector registers are the callee's to use - at a return a float
		// result lies there. Static: the record is over a kilobyte, and the timed thread alone comes here.
		static timed::Spike spike;
		if (g_calls.TakeSpike(&spike)) {
			WriteSpike(spike);
		}
	}
	if (g_calls.Enter(index, reinterpret_cast<UInt32>(slot), *slot, g_stub, __rdtsc())) {
		*slot = g_stub;
	}
}

// Called by the exit stub with the room for the real return address, which
// lies right below where the stack pointer stood after the function's RET.
// Integer code only: a float result is on the x87 stack.
void __cdecl Exit(UInt32* room) {
	*room = g_calls.Exit(reinterpret_cast<UInt32>(room) + 4, __rdtsc());
}

}  // namespace

UInt32 InstallTimedCalls(const char* iniPath, LogFn log) {
	if (g_stub != 0 || iniPath == nullptr) {
		return g_count;
	}
	g_log = log;
	timed::Spec specs[timed::kFunctionsAtMost];
	UInt32 asked = 0;
	for (unsigned i = 1; i <= timed::kFunctionsAtMost; ++i) {
		char key[8];
		char value[160];
		std::snprintf(key, sizeof(key), "F%u", i);
		if (GetPrivateProfileStringA("Timed", key, "", value, sizeof(value), iniPath) == 0) {
			continue;
		}
		if (!timed::ParseSpec(value, &specs[asked])) {
			log("Timed %s: \"%s\" is not <address> <entry bytes, five or more> <name>; skipped", key, value);
			continue;
		}
		++asked;
	}
	if (asked == 0) {
		return 0;
	}
	auto* stub = static_cast<UInt8*>(AllocExecutable(timed::kExitStubSize));
	if (stub == nullptr ||
	    timed::BuildExitStub(stub, timed::kExitStubSize, reinterpret_cast<UInt32>(stub), reinterpret_cast<UInt32>(&Exit)) == 0) {
		log("Timed: no executable memory for the exit stub; nothing timed");
		return 0;
	}
	g_stub = reinterpret_cast<UInt32>(stub);
	QueryPerformanceFrequency(&g_frequency);
	QueryPerformanceCounter(&g_lastClock);
	g_lastStamp = __rdtsc();
	for (UInt32 i = 0; i < asked; ++i) {
		const timed::Spec& spec = specs[i];
		const UInt32 index = g_count;
		auto* thunk = static_cast<UInt8*>(AllocExecutable(timed::kEntryThunkSize));
		if (thunk == nullptr) {
			log("Timed %s: no executable memory; skipped", spec.name);
			continue;
		}
		// The detour needs its replacement's address before it hands out the
		// way into the original, and the thunk needs that way: so the entry
		// first goes to a hop that jumps through g_original[index], and the
		// hop is pointed at the thunk once the thunk can be built.
		g_spec[index] = spec;
		void* original = nullptr;
		auto* hop = static_cast<UInt8*>(AllocExecutable(8));
		if (hop == nullptr) {
			log("Timed %s: no executable memory; skipped", spec.name);
			continue;
		}
		// jmp dword ptr [&g_original[index]]
		hop[0] = 0xFF;
		hop[1] = 0x25;
		const UInt32 pointerAt = reinterpret_cast<UInt32>(&g_original[index]);
		std::memcpy(hop + 2, &pointerAt, 4);
		if (!InstallEntryDetour(spec.name, spec.address, spec.bytes, spec.length, hop, &original, log)) {
			continue;
		}
		g_original[index] = original;
		if (timed::BuildEntryThunk(thunk, timed::kEntryThunkSize, reinterpret_cast<UInt32>(thunk), index, reinterpret_cast<UInt32>(&Enter),
		                           reinterpret_cast<UInt32>(original)) == 0) {
			continue;  // the hop stays: the function runs untimed
		}
		// The hop now goes to the thunk: jmp rel32, written whole over the
		// indirect jump (the plugin loads before the game's threads run).
		UInt8 jump[5];
		jump[0] = 0xE9;
		const UInt32 rel = reinterpret_cast<UInt32>(thunk) - (reinterpret_cast<UInt32>(hop) + 5);
		std::memcpy(jump + 1, &rel, 4);
		std::memcpy(hop, jump, 5);
		FlushInstructionCache(GetCurrentProcess(), hop, 8);
		g_on[index] = true;
		++g_count;
	}
	log("Timed: %u of %u functions timed at entry and return, on the thread that first calls the first of them", g_count, asked);
	InstallSpikeWatch(iniPath, log);
	return g_count;
}

void LogTimedCallsSummary() {
	if (g_count == 0 || g_log == nullptr) {
		return;
	}
	LARGE_INTEGER clock;
	QueryPerformanceCounter(&clock);
	const unsigned long long stamp = __rdtsc();
	const double ms = g_frequency.QuadPart > 0 ? static_cast<double>(clock.QuadPart - g_lastClock.QuadPart) * 1000.0 / static_cast<double>(g_frequency.QuadPart) : 0.0;
	const double ticksPerMs = ms > 0.0 ? static_cast<double>(stamp - g_lastStamp) / ms : 0.0;
	g_lastClock = clock;
	g_lastStamp = stamp;
	if (g_spikes) {
		const LONG lines = InterlockedExchange(&g_spikeLines, 0);
		if (lines > g_spikeLinesAtMost) {
			g_log("Timed: %ld more spikes in this window not written", lines - g_spikeLinesAtMost);
		}
	}
	char line[320];
	for (UInt32 i = 0; i < g_count; ++i) {
		// Read while the main thread counts: a torn 64-bit read is caught by
		// reading until two readings agree.
		timed::Totals now = g_calls.Of(i);
		for (int tries = 0; tries < 4; ++tries) {
			const timed::Totals again = g_calls.Of(i);
			if (again.calls == now.calls && again.inclusiveTicks == now.inclusiveTicks && again.exclusiveTicks == now.exclusiveTicks) {
				break;
			}
			now = again;
		}
		const timed::Totals window = timed::Window(now, g_last[i]);
		g_last[i] = now;
		timed::FormatTimed(g_spec[i].name, g_spec[i].address, window, ticksPerMs, line, sizeof(line));
		g_log("%s", line);
	}
}

}  // namespace obng::plugin
