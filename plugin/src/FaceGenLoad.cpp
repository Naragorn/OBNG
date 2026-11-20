#include "FaceGenLoad.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HangWatch.h"
#include "Memory.h"
#include "engine/facegen/FaceGenLoadLogic.h"

namespace obng::plugin {

namespace {

namespace fl = obng::engine::facegen::load;

constexpr UInt32 kLoadEgm = 0x00559190;  // __thiscall(model): reads the .egm when the model has none (read)
constexpr UInt32 kLoadEgt = 0x00558520;  // the same for the .egt
constexpr UInt32 kLock = 0x00B39C00;     // the cache's critical section; its holder's thread at +0x78, the depth at +0x7C
constexpr UInt32 kOwner = 0x00B39C78, kDepth = 0x00B39C7C;
constexpr UInt32 kEgmObject = 0x8, kEgtObject = 0xC;  // a model's two data objects: path at [0], its length at +4, data at +8
constexpr UInt32 kLoader = 0x00B33A10, kLoaderThread = 0x30;  // the manager whose thread the engine takes for the loader's (read: 0x00442960)
constexpr double kLongMs = 2.0;

// The calls, each `E8 rel32`: the find 0x005515B0 (both kinds), 0x005516B0 (shapes), 0x005517F0 (textures).
struct Site {
	UInt32 address;
	UInt32 target;
};
constexpr Site kSites[] = {{0x0055163C, kLoadEgm}, {0x00551643, kLoadEgt}, {0x00551781, kLoadEgm}, {0x005518C1, kLoadEgt}};

using LoadFn = UInt32(__thiscall*)(void* model);

// A lock in the engine's shape, for the head's synthesis in place of the cache's (FaceGenLoadLogic.h).
struct EngineLock {
	CRITICAL_SECTION section;
	UInt8 pad[fl::kLockOwner - sizeof(CRITICAL_SECTION)];
	UInt32 owner;
	UInt32 depth;
};
static_assert(offsetof(EngineLock, owner) == fl::kLockOwner && offsetof(EngineLock, depth) == fl::kLockDepth && sizeof(EngineLock) == fl::kLockSize,
              "the shape the engine's enter 0x0043F2E0 and leave 0x0043F300 write to");

LogFn g_log = nullptr;
bool g_on = false, g_routed = false, g_synthesisMoved = false;
EngineLock g_synthesisLock;
double g_ticksPerMs = 0.0;
std::uint64_t g_longTicks = 0;
CRITICAL_SECTION g_stripes[fl::kStripes];
CRITICAL_SECTION g_statsLock;
fl::Totals g_totals;
fl::LongLoads g_long;

UInt32& At(UInt32 address) {
	return *reinterpret_cast<UInt32*>(static_cast<std::uintptr_t>(address));
}

CRITICAL_SECTION* CacheLock() {
	return reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::uintptr_t>(kLock));
}

// The engine's own leave and enter of the cache's lock, with its bookkeeping (read: 0x0055164C, 0x005515D7).
void LeaveCache() {
	if (--At(kDepth) == 0) {
		At(kOwner) = 0;
	}
	LeaveCriticalSection(CacheLock());
}

void EnterCache() {
	EnterCriticalSection(CacheLock());
	At(kOwner) = GetCurrentThreadId();
	++At(kDepth);
}

const char* PathOf(UInt32 object) {
	return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(At(object)));
}

fl::Part PartOf(UInt32 model, fl::Kind kind) {
	const UInt32 object = At(model + (kind == fl::Kind::Egm ? kEgmObject : kEgtObject));
	if (object == 0) {
		return fl::Part{};
	}
	const std::uint16_t stored = *reinterpret_cast<const std::uint16_t*>(static_cast<std::uintptr_t>(object + 4));
	const char* path = PathOf(object);
	const UInt32 counted = stored == 0xFFFF && path != nullptr ? static_cast<UInt32>(std::strlen(path)) : 0;
	return fl::Part{true, At(object + 8) != 0, fl::PathLength(stored, counted)};
}

std::uint64_t Between(const LARGE_INTEGER& from, const LARGE_INTEGER& to) {
	return static_cast<std::uint64_t>(to.QuadPart - from.QuadPart);
}

void Record(const fl::Load& load, UInt32 model, fl::Kind kind, const LARGE_INTEGER& ended) {
	EnterCriticalSection(&g_statsLock);
	fl::Add(g_totals, load);
	if (g_routed && !load.outside) {
		fl::CountNested(g_totals);
	}
	if (fl::IsLong(load.ticks + load.waitTicks, g_longTicks)) {
		fl::LongLoad one;
		one.load = load;
		one.thread = GetCurrentThreadId();
		one.endedAt = static_cast<std::uint64_t>(ended.QuadPart);
		const UInt32 object = At(model + (kind == fl::Kind::Egm ? kEgmObject : kEgtObject));
		fl::Tail(object != 0 ? PathOf(object) : nullptr, one.name);
		fl::Keep(g_long, one);
	}
	LeaveCriticalSection(&g_statsLock);
}

// In place of the engine's load: the same call, outside the cache's lock when it reads a file and the lock is this
// call's alone. The find that called holds its own references to the entry and the model across it.
UInt32 Run(void* self, fl::Kind kind) {
	const LoadFn original = reinterpret_cast<LoadFn>(static_cast<std::uintptr_t>(kind == fl::Kind::Egm ? kLoadEgm : kLoadEgt));
	const UInt32 model = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self));
	if (!fl::Needs(PartOf(model, kind))) {
		return original(self);
	}
	const bool outside = fl::WhereOf(g_routed, true, At(kDepth), static_cast<UInt32>(CacheLock()->RecursionCount)) == fl::Where::Outside;
	CRITICAL_SECTION* stripe = &g_stripes[fl::StripeOf(model)];
	LARGE_INTEGER t0, t1, t2, t3;
	QueryPerformanceCounter(&t0);
	if (outside) {
		LeaveCache();
		EnterCriticalSection(stripe);
	}
	QueryPerformanceCounter(&t1);
	const UInt32 result = original(self);
	QueryPerformanceCounter(&t2);
	if (outside) {
		LeaveCriticalSection(stripe);
		EnterCache();
	}
	QueryPerformanceCounter(&t3);
	const UInt32 loader = At(kLoader);
	Record(fl::Load{kind, loader != 0 && GetCurrentThreadId() == At(loader + kLoaderThread), outside, Between(t1, t2), Between(t0, t1) + Between(t2, t3)},
	       model, kind, t3);
	return result;
}

UInt32 __fastcall LoadEgm(void* model) {
	return Run(model, fl::Kind::Egm);
}

UInt32 __fastcall LoadEgt(void* model) {
	return Run(model, fl::Kind::Egt);
}

void CallBytes(UInt32 site, UInt32 target, UInt8 out[5]) {
	out[0] = 0xE8;
	const UInt32 rel = target - (site + 5);
	std::memcpy(out + 1, &rel, 4);
}

bool Patch(LogFn log) {
	for (const Site& site : kSites) {
		UInt8 expected[5];
		CallBytes(site.address, site.target, expected);
		if (!Verify(site.address, expected, sizeof(expected))) {
			log("Route: FaceGen loads - the call at %08X is not the game's own; nothing is patched", site.address);
			return false;
		}
	}
	for (const Site& site : kSites) {
		UInt8 patch[5];
		const void* hook = site.target == kLoadEgm ? reinterpret_cast<const void*>(&LoadEgm) : reinterpret_cast<const void*>(&LoadEgt);
		CallBytes(site.address, static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(hook)), patch);
		if (!SafeWrite(site.address, patch, sizeof(patch))) {
			log("Route: FaceGen loads - the call at %08X could not be written; the calls before it stay patched and work as the game's", site.address);
			return false;
		}
	}
	return true;
}

// The head synthesis's two brackets from the cache's lock to OBNG's own: all four `mov ecx` or none.
bool MoveSynthesisLock(LogFn log) {
	UInt8 expected[5];
	fl::MovEcx(fl::kCacheLock, expected);
	for (const UInt32 site : fl::kSynthesisLockSites) {
		if (!Verify(site, expected, sizeof(expected))) {
			log("Route: FaceGen loads - the head synthesis's lock at %08X is not the game's own; its brackets stay on the cache's lock", site);
			return false;
		}
	}
	UInt8 patch[5];
	fl::MovEcx(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&g_synthesisLock)), patch);
	for (const UInt32 site : fl::kSynthesisLockSites) {
		if (!SafeWrite(site, patch, sizeof(patch))) {
			// A bracket half moved would leave a lock that was never entered: put the game's bytes back everywhere.
			for (const UInt32 undo : fl::kSynthesisLockSites) {
				SafeWrite(undo, expected, sizeof(expected));
			}
			log("Route: FaceGen loads - the head synthesis's lock at %08X could not be written; its brackets stay on the cache's lock", site);
			return false;
		}
	}
	return true;
}

}  // namespace

void InstallFaceGenLoad(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const bool routed = fl::OnOf(static_cast<int>(GetPrivateProfileIntA("Route", "FaceGenLoadOutside", fl::Shipped(), iniPath)));
	const bool probed = GetPrivateProfileIntA("Probe", "FaceGenLoads", 0, iniPath) == 1;
	if (!routed && !probed) {
		log("Route: FaceGen loads - off in OBNG.ini; a model's files are read under the cache's lock, as the game does it");
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	g_longTicks = static_cast<std::uint64_t>(kLongMs * g_ticksPerMs);
	static const char* const kStripeNames[fl::kStripes] = {"OBNG's FaceGen load 0", "OBNG's FaceGen load 1", "OBNG's FaceGen load 2", "OBNG's FaceGen load 3",
	                                                       "OBNG's FaceGen load 4", "OBNG's FaceGen load 5", "OBNG's FaceGen load 6", "OBNG's FaceGen load 7",
	                                                       "OBNG's FaceGen load 8", "OBNG's FaceGen load 9", "OBNG's FaceGen load 10", "OBNG's FaceGen load 11",
	                                                       "OBNG's FaceGen load 12", "OBNG's FaceGen load 13", "OBNG's FaceGen load 14", "OBNG's FaceGen load 15"};
	for (UInt32 i = 0; i < fl::kStripes; ++i) {
		InitializeCriticalSection(&g_stripes[i]);
		HangWatchLock(kStripeNames[i], &g_stripes[i]);
	}
	InitializeCriticalSection(&g_statsLock);
	g_routed = routed;  // before the patch: a load on another thread may run through it at once
	if (!Patch(log)) {
		g_routed = false;
		return;
	}
	g_on = true;
	if (routed) {
		std::memset(&g_synthesisLock, 0, sizeof(g_synthesisLock));
		InitializeCriticalSection(&g_synthesisLock.section);
		HangWatchLock("OBNG's head synthesis", &g_synthesisLock.section);
		g_synthesisMoved = MoveSynthesisLock(log);
	}
	log("Route: FaceGen loads - a find's reads of a model's .egm and .egt files %s; the head's texture synthesis takes turns on %s; loads of %.0f ms or more "
	    "are listed",
	    routed ? "run outside the cache's lock (the lock is given up for the read)" : "are timed only and stay under the cache's lock",
	    g_synthesisMoved ? "a lock of its own" : "the cache's lock, as in the game", kLongMs);
}

void LogFaceGenLoadSummary() {
	if (!g_on) {
		return;
	}
	EnterCriticalSection(&g_statsLock);
	const fl::Totals totals = g_totals;
	const fl::LongLoads longs = g_long;
	g_long = fl::LongLoads{};
	LeaveCriticalSection(&g_statsLock);
	const auto ms = [](std::uint64_t ticks) { return static_cast<double>(ticks) / g_ticksPerMs; };
	const fl::Cell& shapesLoader = totals.of[0][0];
	const fl::Cell& shapesOther = totals.of[0][1];
	const fl::Cell& texturesLoader = totals.of[1][0];
	const fl::Cell& texturesOther = totals.of[1][1];
	g_log("Route: FaceGen loads - since the start, loads / ms / longest ms: shapes on the loader's thread %llu / %.1f / %.1f, on others %llu / %.1f / %.1f; "
	      "textures on the loader's thread %llu / %.1f / %.1f, on others %llu / %.1f / %.1f; outside the cache's lock %llu (their waits %.1f ms, the longest "
	      "%.1f), kept under it for a lock held twice %llu",
	      shapesLoader.loads, ms(shapesLoader.ticks), ms(shapesLoader.longest), shapesOther.loads, ms(shapesOther.ticks), ms(shapesOther.longest),
	      texturesLoader.loads, ms(texturesLoader.ticks), ms(texturesLoader.longest), texturesOther.loads, ms(texturesOther.ticks), ms(texturesOther.longest),
	      totals.outside, ms(totals.waitTicks), ms(totals.longestWait), totals.nested);
	for (unsigned i = 0; i < longs.count; ++i) {
		const fl::LongLoad& one = longs.loads[i];
		g_log("Route: FaceGen load - %s %.3f ms on %s thread %u, %s, waited %.3f ms, ending at tick %lld: %s",
		      one.load.kind == fl::Kind::Egm ? "shapes" : "textures", ms(one.load.ticks), one.load.loaderThread ? "the loader's" : "another", one.thread,
		      one.load.outside ? "outside the cache's lock" : "under the cache's lock", ms(one.load.waitTicks), static_cast<long long>(one.endedAt), one.name);
	}
	if (longs.dropped != 0) {
		g_log("Route: FaceGen loads - %u more long loads not listed", longs.dropped);
	}
}

}  // namespace obng::plugin
