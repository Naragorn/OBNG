#include "FastConstantArrange.h"

#include "engine/render/ConstantMapArrangeLogic.h"

#include <windows.h>

#include <cstring>

namespace obng::plugin {
namespace {

namespace cma = obng::engine::render::constant_map_arrange;

constexpr const char* kName = "NiD3DShaderConstantMap::ArrangeEntries";  // a proposal for 0x009A97B0
constexpr UInt32 kAddress = 0x009A97B0;                                   // __thiscall(), RET (read 0x009A98FF)
constexpr UInt8 kBytes[] = {0x83, 0xEC, 0x0C, 0x53, 0x8B, 0xD9};         // sub esp,0Ch; push ebx; mov ebx,ecx (read)
constexpr UInt32 kUsedOffset = 0x18;                                      // the array's count of non-null entries, u16 (read 0x009A9670: +0x0C of the array at +0x0C)
constexpr UInt32 kSnapshotMost = 256;
using Fn = void(__fastcall*)(void* self, void* edx);

void* g_original = nullptr;
LogFn g_log = nullptr;
bool g_shadow = false;
bool g_installed = false;
volatile LONG64 g_calls = 0;
volatile LONG64 g_arranged = 0;
volatile LONG64 g_entries = 0;        // entries of the calls left out (two interlocked calls each, saved)
volatile LONG64 g_changedAsSaid = 0;  // shadow: not arranged, and the original changed the array
volatile LONG64 g_unchangedNotArranged = 0;  // shadow: not arranged, the array the same afterwards
volatile LONG64 g_tooLarge = 0;       // shadow: more entries than the snapshot holds, not compared
volatile LONG64 g_mismatches = 0;
volatile LONG g_mismatchLines = 0;

struct EnginePort {
	UInt32 map;
	cma::Handle Entry(std::uint32_t index) const {
		return *reinterpret_cast<const UInt32*>(*reinterpret_cast<const UInt32*>(map + cma::kEntriesOffset) + 4 * index);
	}
	std::uint8_t Kind(cma::Handle entry) const { return *reinterpret_cast<const UInt8*>(entry + cma::kEntryKindOffset); }
};

UInt32 SizeOf(UInt32 map) { return *reinterpret_cast<const UInt16*>(map + cma::kSizeOffset); }

void __fastcall RouteReplacement(void* self, void* /*edx*/) {
	const UInt32 map = reinterpret_cast<UInt32>(self);
	const UInt32 size = SizeOf(map);
	InterlockedIncrement64(&g_calls);
	if (cma::Arranged(EnginePort{map}, size)) {
		InterlockedIncrement64(&g_arranged);
		InterlockedAdd64(&g_entries, size > 1 ? size : 0);
		return;
	}
	reinterpret_cast<Fn>(g_original)(self, nullptr);
}

void __fastcall ShadowReplacement(void* self, void* /*edx*/) {
	const UInt32 map = reinterpret_cast<UInt32>(self);
	const UInt32 size = SizeOf(map);
	InterlockedIncrement64(&g_calls);
	const bool arranged = cma::Arranged(EnginePort{map}, size);
	if (size > kSnapshotMost) {
		InterlockedIncrement64(&g_tooLarge);
		reinterpret_cast<Fn>(g_original)(self, nullptr);
		return;
	}
	UInt32 before[kSnapshotMost];
	const UInt32 data = *reinterpret_cast<const UInt32*>(map + cma::kEntriesOffset);
	if (size != 0) {
		std::memcpy(before, reinterpret_cast<const void*>(data), 4 * size);
	}
	const UInt16 usedBefore = *reinterpret_cast<const UInt16*>(map + kUsedOffset);
	reinterpret_cast<Fn>(g_original)(self, nullptr);
	const bool same = SizeOf(map) == size && *reinterpret_cast<const UInt32*>(map + cma::kEntriesOffset) == data &&
	                  *reinterpret_cast<const UInt16*>(map + kUsedOffset) == usedBefore &&
	                  (size == 0 || std::memcmp(before, reinterpret_cast<const void*>(data), 4 * size) == 0);
	if (arranged) {
		InterlockedIncrement64(&g_arranged);
		if (!same) {
			InterlockedIncrement64(&g_mismatches);
			if (InterlockedIncrement(&g_mismatchLines) <= 20) {
				g_log("Shadow %s: MISMATCH - the map %08X with %u entries was taken as arranged, the original changed it", kName, map, size);
			}
		}
	} else if (same) {
		InterlockedIncrement64(&g_unchangedNotArranged);
	} else {
		InterlockedIncrement64(&g_changedAsSaid);
	}
}

}  // namespace

void InstallConstantArrange(const ShadowSettings& shadow, const RouteSettings& route, LogFn log) {
	if (g_installed || (!shadow.constantArrange && !route.constantArrange)) {
		return;
	}
	g_log = log;
	g_shadow = shadow.constantArrange;
	if (shadow.constantArrange && route.constantArrange) {
		log("Route: %s skipped - its shadow takes the same entry and wins", kName);
	}
	g_installed = InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes),
	                                 reinterpret_cast<const void*>(g_shadow ? &ShadowReplacement : &RouteReplacement), &g_original, log,
	                                 g_shadow ? DetourRole::Shadow : DetourRole::Route);
}

void LogConstantArrangeSummary() {
	if (!g_installed || g_log == nullptr) {
		return;
	}
	const LONG64 calls = InterlockedExchange64(&g_calls, 0);
	const LONG64 arranged = InterlockedExchange64(&g_arranged, 0);
	if (calls == 0) {
		return;
	}
	if (g_shadow) {
		g_log("Shadow %s: %lld calls, %lld taken as arranged, %lld mismatches (the original changed an arranged map); not arranged: %lld changed "
		      "by the original, %lld left as they were; %lld too large to compare",
		      kName, calls, arranged, InterlockedExchange64(&g_mismatches, 0), InterlockedExchange64(&g_changedAsSaid, 0),
		      InterlockedExchange64(&g_unchangedNotArranged, 0), InterlockedExchange64(&g_tooLarge, 0));
	} else {
		g_log("Route %s: %lld calls, %lld left out as arranged (%lld entries not walked), %lld run in the original", kName, calls, arranged,
		      InterlockedExchange64(&g_entries, 0), calls - arranged);
	}
}

}  // namespace obng::plugin
