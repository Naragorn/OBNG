#include "LockProbe.h"

#include "Memory.h"
#include "OcclusionCull.h"

#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <cstdio>

namespace obng::plugin {
namespace {

constexpr UInt32 kSlotEnter = 0x00A2806C;
constexpr UInt32 kSlotTryEnter = 0x00A28070;
constexpr UInt32 kSlotLeave = 0x00A28074;
constexpr UInt32 kSlotIncrement = 0x00A28078;
constexpr UInt32 kSlotDecrement = 0x00A2807C;

LogFn g_log = nullptr;
bool g_installed = false;
void(__stdcall* g_enter)(CRITICAL_SECTION*) = nullptr;
BOOL(__stdcall* g_tryEnter)(CRITICAL_SECTION*) = nullptr;
void(__stdcall* g_leave)(CRITICAL_SECTION*) = nullptr;
LONG(__stdcall* g_increment)(LONG volatile*) = nullptr;
LONG(__stdcall* g_decrement)(LONG volatile*) = nullptr;

// Open-addressed tables keyed by a pointer; a slot is claimed with a
// compare-exchange, the counts are interlocked. A full table drops the key
// (counted).
constexpr unsigned kSlots = 1024;
struct Entry {
	volatile LONG key;
	volatile LONG inPass;
	volatile LONG outside;
	volatile LONG contended;
	volatile LONG threads[4];  // the first four thread ids seen
	volatile LONG moreThreads;
	volatile LONG waits;      // contended enters timed
	volatile LONG waitUs;     // microseconds spent waiting after a failed try (contended enters only)
	volatile LONG longestUs;  // the longest such wait
	volatile LONG site;       // a section's last enter site (the holder's, when the next enter finds it held)
};
Entry g_sections[kSlots];
Entry g_enterSites[kSlots];
Entry g_interlockedSites[kSlots];
Entry g_waitSites[kSlots];    // the enter sites that waited, by the time waited
Entry g_holderSites[kSlots];  // the holders' enter sites, by the time others waited on them
LARGE_INTEGER g_frequency{};
volatile LONG g_dropped = 0;
volatile LONG g_interlockedInPass = 0;
volatile LONG g_interlockedOutside = 0;
UInt32 g_lastPasses = 0;

Entry* Find(Entry* table, UInt32 key) {
	UInt32 i = (key * 2654435761u) >> 22;
	for (unsigned probe = 0; probe < 32; ++probe, i = (i + 1) & (kSlots - 1)) {
		const LONG have = table[i].key;
		if (static_cast<UInt32>(have) == key) {
			return &table[i];
		}
		if (have == 0 && InterlockedCompareExchange(&table[i].key, static_cast<LONG>(key), 0) == 0) {
			return &table[i];
		}
		if (static_cast<UInt32>(table[i].key) == key) {
			return &table[i];
		}
	}
	InterlockedIncrement(&g_dropped);
	return nullptr;
}

void NoteThread(Entry* e) {
	const LONG id = static_cast<LONG>(GetCurrentThreadId());
	for (volatile LONG& slot : e->threads) {
		if (slot == id) {
			return;
		}
		if (slot == 0 && InterlockedCompareExchange(&slot, id, 0) == 0) {
			return;
		}
		if (slot == id) {
			return;
		}
	}
	InterlockedExchange(&e->moreThreads, 1);
}

void NoteWait(Entry* e, LONG us) {
	if (e == nullptr) {
		return;
	}
	InterlockedIncrement(&e->waits);
	InterlockedExchangeAdd(&e->waitUs, us);
	LONG longest = e->longestUs;
	while (us > longest && InterlockedCompareExchange(&e->longestUs, us, longest) != longest) {
		longest = e->longestUs;
	}
}

void Count(Entry* e, bool inPass) {
	if (e != nullptr) {
		InterlockedIncrement(inPass ? &e->inPass : &e->outside);
	}
}

void __stdcall EnterReplacement(CRITICAL_SECTION* section) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	const bool inPass = OcclusionCullInEyePass();
	Entry* e = Find(g_sections, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(section)));
	Count(e, inPass);
	Count(Find(g_enterSites, site), inPass);
	if (e != nullptr) {
		NoteThread(e);
	}
	if (g_tryEnter(section)) {
		if (e != nullptr) {
			InterlockedExchange(&e->site, static_cast<LONG>(site));
		}
		return;
	}
	const LONG holder = e != nullptr ? e->site : 0;
	LARGE_INTEGER t0{}, t1{};
	QueryPerformanceCounter(&t0);
	g_enter(section);
	QueryPerformanceCounter(&t1);
	const LONG us = g_frequency.QuadPart > 0 ? static_cast<LONG>((t1.QuadPart - t0.QuadPart) * 1000000 / g_frequency.QuadPart) : 0;
	if (e != nullptr) {
		InterlockedIncrement(&e->contended);
		NoteWait(e, us);
		InterlockedExchange(&e->site, static_cast<LONG>(site));
	}
	NoteWait(Find(g_waitSites, site), us);
	if (holder != 0) {
		NoteWait(Find(g_holderSites, static_cast<UInt32>(holder)), us);
	}
}

BOOL __stdcall TryEnterReplacement(CRITICAL_SECTION* section) {
	const BOOL got = g_tryEnter(section);
	if (got) {
		Entry* e = Find(g_sections, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(section)));
		Count(e, OcclusionCullInEyePass());
		if (e != nullptr) {
			NoteThread(e);
		}
	}
	return got;
}

void __stdcall LeaveReplacement(CRITICAL_SECTION* section) { g_leave(section); }

LONG __stdcall IncrementReplacement(LONG volatile* value) {
	const bool inPass = OcclusionCullInEyePass();
	InterlockedIncrement(inPass ? &g_interlockedInPass : &g_interlockedOutside);
	if (inPass) {
		Count(Find(g_interlockedSites, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))), true);
	}
	return _InterlockedIncrement(value);
}

LONG __stdcall DecrementReplacement(LONG volatile* value) {
	const bool inPass = OcclusionCullInEyePass();
	InterlockedIncrement(inPass ? &g_interlockedInPass : &g_interlockedOutside);
	if (inPass) {
		Count(Find(g_interlockedSites, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()))), true);
	}
	return _InterlockedDecrement(value);
}

template <typename Fn>
bool Swap(UInt32 slot, Fn replacement, Fn* original) {
	*original = *reinterpret_cast<Fn*>(slot);
	const UInt32 value = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(replacement));
	return SafeWrite(slot, &value, sizeof(value));
}

unsigned Threads(const Entry& e) {
	unsigned n = 0;
	for (const volatile LONG& slot : e.threads) {
		n += slot != 0 ? 1u : 0u;
	}
	return n;
}

void LogTop(const char* what, Entry* table, double perFrame, bool sections) {
	struct Row {
		UInt32 key;
		LONG inPass;
		LONG outside;
		LONG contended;
		unsigned threads;
		bool more;
	};
	Row rows[kSlots];
	unsigned count = 0;
	for (unsigned i = 0; i < kSlots; ++i) {
		if (table[i].key == 0) {
			continue;
		}
		Row r{static_cast<UInt32>(table[i].key), InterlockedExchange(&table[i].inPass, 0), InterlockedExchange(&table[i].outside, 0),
		      InterlockedExchange(&table[i].contended, 0), Threads(table[i]), table[i].moreThreads != 0};
		if (r.inPass + r.outside > 0) {
			rows[count++] = r;
		}
	}
	std::sort(rows, rows + count, [](const Row& a, const Row& b) { return a.inPass + a.outside > b.inPass + b.outside; });
	char line[1500];
	int at = std::snprintf(line, sizeof(line), "Probe: lock calls - %s (%u with calls), a frame inside / outside the eye pass:", what, count);
	for (unsigned i = 0; i < count && i < 14 && at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 90; ++i) {
		at += std::snprintf(line + at, sizeof(line) - at, " %08X %.1f / %.1f", rows[i].key, rows[i].inPass * perFrame, rows[i].outside * perFrame);
		if (sections && at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 60) {
			at += std::snprintf(line + at, sizeof(line) - at, " (%u%s threads, held by another %ld times)", rows[i].threads, rows[i].more ? "+" : "",
			                    rows[i].contended);
		}
		if (at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 2) {
			at += std::snprintf(line + at, sizeof(line) - at, ";");
		}
	}
	g_log("%s", line);
}

// The waits since the last summary, the longest total first: count, ms in all, the longest in ms. Read and reset
// while other threads add (a measurement: a wait landing between the two reads may be lost).
void LogWaits(const char* what, Entry* table) {
	struct Row {
		UInt32 key;
		LONG waits;
		LONG us;
		LONG longest;
	};
	Row rows[kSlots];
	unsigned count = 0;
	for (unsigned i = 0; i < kSlots; ++i) {
		if (table[i].key == 0 || table[i].waitUs == 0) {
			continue;
		}
		rows[count++] = Row{static_cast<UInt32>(table[i].key), InterlockedExchange(&table[i].waits, 0), InterlockedExchange(&table[i].waitUs, 0),
		                    InterlockedExchange(&table[i].longestUs, 0)};
	}
	if (count == 0) {
		return;
	}
	std::sort(rows, rows + count, [](const Row& a, const Row& b) { return a.us > b.us; });
	char line[1500];
	int at = std::snprintf(line, sizeof(line), "Probe: lock waits - %s (%u), waits / ms in all / longest ms:", what, count);
	for (unsigned i = 0; i < count && i < 12 && at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 60; ++i) {
		at += std::snprintf(line + at, sizeof(line) - at, " %08X %ld / %.2f / %.2f;", rows[i].key, rows[i].waits, rows[i].us / 1000.0, rows[i].longest / 1000.0);
	}
	g_log("%s", line);
}

}  // namespace

void InstallLockProbe(const ShadowSettings& settings, LogFn log) {
	if (g_installed || !settings.lockCalls) {
		return;
	}
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	const bool ok = Swap(kSlotEnter, &EnterReplacement, &g_enter) && Swap(kSlotTryEnter, &TryEnterReplacement, &g_tryEnter) &&
	                Swap(kSlotLeave, &LeaveReplacement, &g_leave) && Swap(kSlotIncrement, &IncrementReplacement, &g_increment) &&
	                Swap(kSlotDecrement, &DecrementReplacement, &g_decrement);
	g_installed = ok;
	log(ok ? "Probe: lock calls - the EXE's import slots for the critical section and interlocked calls are OBNG's counters"
	       : "Probe: lock calls - an import slot could not be written, the count is incomplete");
}

void LogLockProbeSummary() {
	if (!g_installed || g_log == nullptr) {
		return;
	}
	const UInt32 passes = OcclusionCullEyePasses();
	const UInt32 frames = passes - g_lastPasses;
	g_lastPasses = passes;
	LogWaits("critical sections", g_sections);
	LogWaits("waiting enter sites", g_waitSites);
	LogWaits("the holders' enter sites", g_holderSites);
	const LONG in = InterlockedExchange(&g_interlockedInPass, 0);
	const LONG out = InterlockedExchange(&g_interlockedOutside, 0);
	if (frames == 0) {
		return;
	}
	const double per = 1.0 / static_cast<double>(frames);
	g_log("Probe: lock calls - %u frames; interlocked increments and decrements a frame: %.0f inside the eye pass, %.0f outside (all threads); "
	      "%ld keys dropped",
	      frames, in * per, out * per, InterlockedExchange(&g_dropped, 0));
	LogTop("critical sections", g_sections, per, true);
	LogTop("enter sites", g_enterSites, per, false);
	LogTop("interlocked sites inside the pass", g_interlockedSites, per, false);
}

}  // namespace obng::plugin
