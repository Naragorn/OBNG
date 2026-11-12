#include "PassListKeep.h"

#include <windows.h>

#include <intrin.h>
#include <cstring>
#include <memory>

#include "Memory.h"
#include "engine/render/PassListKeepLogic.h"

namespace obng::plugin {

namespace {

namespace keep = obng::engine::render::pass_list_keep;

// Read 2026-11-12 in the instructions (re/export/pass-lists/; knowledge/culling-and-draw-setup.md).
constexpr UInt32 kRelight = 0x007C6100;        // ShadowSceneNode::RelightGeometry: __cdecl(geometry, node), returns AL
constexpr UInt32 kRelightCalls[] = {0x007C70F0, 0x007C716E};  // its two calls, in RelightSubtree 0x007C7050
constexpr UInt32 kGetter = 0x007D85D0;         // GetRenderPasses: thiscall(property; geometry, flags, count out, mode), RET 0x10
constexpr UInt32 kGetterSlots[] = {0x00A913E0, 0x00A92AF0, 0x00A92BD0};  // slot +0x5C of the three vtables below
constexpr UInt32 kVtables[] = {0x00A91384, 0x00A92A94, 0x00A92B74};      // BSShaderPPLighting, SpeedTreeBranch, SpeedTreeShaderPPLighting
constexpr UInt32 kGetProperty = 0x00707530;    // NiAVObject::GetProperty: thiscall(object; type), RET 4
constexpr UInt32 kLightingProperty = 4;
constexpr UInt32 kClockMs = 0x00B33EA0;        // TimeInfo's clockMs: moves once a frame (knowledge/frame-clock.md)
// The property's fields.
constexpr UInt32 kPropFlags = 0x1C;            // its top nibble: the shadow scene node
constexpr UInt32 kPropKey = 0x24;
constexpr UInt32 kPassHead = 0x2C;             // the passes list (+0x28): head node
constexpr UInt32 kLightHead = 0x70;            // the lights list (+0x6C): head node
constexpr UInt32 kNodeNext = 0x00;
constexpr UInt32 kNodeData = 0x08;
// A render pass record.
constexpr UInt32 kPassId = 0x04;
constexpr UInt32 kPassFlags6 = 0x06;
constexpr UInt32 kPassFlags7 = 0x07;
constexpr UInt32 kPassLightCount = 0x08;
constexpr UInt32 kPassLights = 0x0C;
constexpr UInt32 kMismatchLines = 8;

using RelightFn = UInt32(__cdecl*)(UInt32 geometry, UInt32 node);
using GetterFn = UInt32(__fastcall*)(UInt32 property, UInt32 edx, UInt32 geometry, UInt32 flags, UInt32 countOut, UInt32 mode);
using GetPropertyFn = UInt32(__fastcall*)(UInt32 object, UInt32 edx, UInt32 type);

LogFn g_log = nullptr;
bool g_installed = false;
keep::Mode g_mode = keep::Mode::kOff;
bool g_alternate = false;
keep::Counts g_counts;
std::unique_ptr<keep::Pending> g_pending;
UInt32 g_lastClock = 0;
UInt32 g_frame = 0;
bool g_keepNow = true;
UInt32 g_mismatchesLogged = 0;
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};
volatile LONG g_summaryRequested = 0;

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }
UInt8 Byte(UInt32 address) { return *reinterpret_cast<const UInt8*>(address); }

bool OurClass(UInt32 property) {
	const UInt32 vtable = Dword(property);
	for (UInt32 v : kVtables) {
		if (vtable == v) {
			return true;
		}
	}
	return false;
}

keep::LightSet ReadLights(UInt32 property) {
	keep::LightSet s;
	for (UInt32 node = Dword(property + kLightHead); node != 0 && !s.overflow; node = Dword(node + kNodeNext)) {
		s.Add(Dword(node + kNodeData));
	}
	return s;
}

keep::PassList ReadPasses(UInt32 property) {
	keep::PassList list;
	for (UInt32 node = Dword(property + kPassHead); node != 0 && !list.overflow; node = Dword(node + kNodeNext)) {
		const UInt32 record = Dword(node + kNodeData);
		keep::Pass p;
		if (record != 0) {
			p.id = *reinterpret_cast<const UInt16*>(record + kPassId);
			p.flags6 = Byte(record + kPassFlags6);
			p.flags7 = Byte(record + kPassFlags7);
			p.lightCount = Byte(record + kPassLightCount);
			const UInt32 lights = Dword(record + kPassLights);
			for (UInt32 i = 0; i < p.lightCount && i < keep::Pass::kMaxLights && lights != 0; ++i) {
				p.lights[i] = Dword(lights + i * 4);
			}
		}
		list.Add(p);
	}
	return list;
}

// The frame from the clock; with the alternation, keeping only in even frames.
void SeeFrame() {
	const UInt32 clock = Dword(kClockMs);
	if (clock != g_lastClock) {
		g_lastClock = clock;
		++g_frame;
		g_keepNow = !g_alternate || (g_frame & 1u) == 0;
		++g_counts.frames[g_keepNow ? 1 : 0];
	}
}

void WriteSummary() {
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	g_summaryTsc = tsc;
	g_summaryQpc = qpc;
	char line[1024];
	keep::FormatSummary(g_counts, g_mode, g_alternate, ticksPerMs, line, sizeof(line));
	g_log("%s", line);
	g_counts = keep::Counts{};
}

void LogMismatch(UInt32 property, keep::Diff diff, UInt32 at, const keep::PassList& kept, const keep::PassList& rebuilt) {
	if (g_mismatchesLogged >= kMismatchLines) {
		return;
	}
	++g_mismatchesLogged;
	char a[160] = {};
	char b[160] = {};
	int ua = 0;
	int ub = 0;
	for (UInt32 i = 0; i < kept.count && ua >= 0 && ua < 150; ++i) {
		ua += std::snprintf(a + ua, sizeof(a) - ua, " %X/%u", kept.pass[i].id, kept.pass[i].lightCount);
	}
	for (UInt32 i = 0; i < rebuilt.count && ub >= 0 && ub < 150; ++i) {
		ub += std::snprintf(b + ub, sizeof(b) - ub, " %X/%u", rebuilt.pass[i].id, rebuilt.pass[i].lightCount);
	}
	g_log("Route: pass lists kept - MISMATCH at property %08X: %s at pass %u; cached (id/lights):%s; rebuilt:%s", property,
	      keep::kDiffName[static_cast<int>(diff)], at, a, b);
}

// ---- The sites ----------------------------------------------------------------

// RelightSubtree's call of RelightGeometry.
UInt32 __cdecl Relight(UInt32 geometry, UInt32 node) {
	const unsigned __int64 t0 = __rdtsc();
	SeeFrame();
	const UInt32 property = geometry != 0 ? reinterpret_cast<GetPropertyFn>(kGetProperty)(geometry, 0, kLightingProperty) : 0;
	keep::Before before;
	if (property != 0) {
		before.ourClass = OurClass(property);
		if (before.ourClass) {
			before.key = Dword(property + kPropKey);
			before.nibble = Dword(property + kPropFlags) >> 28;
			before.lights = ReadLights(property);
		}
	}
	const unsigned __int64 t1 = __rdtsc();
	const UInt32 result = reinterpret_cast<RelightFn>(kRelight)(geometry, node);
	const unsigned __int64 t2 = __rdtsc();
	++g_counts.relights;
	keep::Why why = keep::Why::kNoProperty;
	if (property != 0) {
		const UInt32 keyAfter = Dword(property + kPropKey);
		why = before.ourClass ? keep::Decide(before, keyAfter, Dword(property + kPropFlags) >> 28, ReadLights(property)) : keep::Why::kNotOurClass;
		if (why == keep::Why::kKept && g_keepNow) {
			if (g_mode == keep::Mode::kVerify && before.key != keep::kSentinel) {
				g_pending->Put(property, before.key, ReadPasses(property));
			}
			*reinterpret_cast<UInt32*>(property + kPropKey) = keep::KeyToWrite(g_mode, before);
		}
	}
	++g_counts.why[static_cast<int>(why)];
	g_counts.relightTicks += (t1 - t0) + (__rdtsc() - t2);
	if (InterlockedExchange(&g_summaryRequested, 0) != 0) {
		WriteSummary();
	}
	return result;
}

// The check after a rebuild of a property this route marked.
void Check(UInt32 property, UInt32 keyIn) {
	const keep::Pending::Entry* e = g_pending->Find(property);
	if (e == nullptr) {
		return;
	}
	if (keyIn != keep::kSentinel) {
		++g_counts.otherwiseCleared;
	} else if (Dword(property + kPropKey) != e->key) {
		++g_counts.otherKey;
	} else {
		const keep::PassList rebuilt = ReadPasses(property);
		UInt32 at = 0;
		const keep::Diff diff = keep::Compare(e->passes, rebuilt, &at);
		++g_counts.checks[static_cast<int>(diff)];
		if (diff != keep::Diff::kSame && diff != keep::Diff::kOverflow) {
			LogMismatch(property, diff, at, e->passes, rebuilt);
		}
	}
	g_pending->Drop(property);
}

// The pass getter of the three classes: timed, builds counted; with the
// check, a marked property's rebuild compared.
UInt32 __fastcall Passes(UInt32 property, UInt32, UInt32 geometry, UInt32 flags, UInt32 countOut, UInt32 mode) {
	SeeFrame();
	const UInt32 keyIn = Dword(property + kPropKey);
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 list = reinterpret_cast<GetterFn>(kGetter)(property, 0, geometry, flags, countOut, mode);
	const unsigned __int64 ticks = __rdtsc() - t0;
	++g_counts.getterCalls;
	g_counts.getterTicks[g_keepNow ? 1 : 0] += ticks;
	const bool built = static_cast<UInt8>(mode) == 1 && Dword(property + kPropKey) != keyIn;
	if (built) {
		++g_counts.builds;
		g_counts.buildTicks += ticks;
	}
	if (g_mode == keep::Mode::kVerify && built) {  // a count-only call (mode 0) leaves a marked property marked
		Check(property, keyIn);
	}
	return list;
}

// ---- Installing ----------------------------------------------------------------

bool VerifyCall(UInt32 site, UInt32 target) {
	UInt8 expected[5] = {0xE8};
	const UInt32 relative = target - (site + 5);
	std::memcpy(&expected[1], &relative, sizeof(relative));
	return Verify(site, expected, sizeof(expected));
}

bool WriteCall(UInt32 site, const void* to) {
	UInt8 patch[5] = {0xE8};
	const UInt32 relative = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(to)) - (site + 5);
	std::memcpy(&patch[1], &relative, sizeof(relative));
	return SafeWrite(site, patch, sizeof(patch));
}

bool VerifyAll(LogFn log) {
	for (UInt32 site : kRelightCalls) {
		if (!VerifyCall(site, kRelight)) {
			char have[32];
			DescribeBytes(site, have, sizeof(have));
			log("Route: pass lists kept - the call of RelightGeometry at %08X reads %s, not the game's own bytes; nothing installed", site, have);
			return false;
		}
	}
	for (UInt32 slot : kGetterSlots) {
		if (Dword(slot) != kGetter) {
			log("Route: pass lists kept - the vtable slot %08X holds %08X, not GetRenderPasses %08X; nothing installed", slot, Dword(slot), kGetter);
			return false;
		}
	}
	return true;
}

}  // namespace

void InstallPassListKeep(const char* iniPath, LogFn log) {
	if (g_installed) {
		return;
	}
	g_mode = keep::ModeFromIni(static_cast<int>(GetPrivateProfileIntA("Route", "PassListKeep", 0, iniPath)));
	if (g_mode == keep::Mode::kOff) {
		return;
	}
	g_log = log;
	g_alternate = GetPrivateProfileIntA("Route", "PassListKeepAlternate", 0, iniPath) != 0;
	if (!VerifyAll(log)) {
		return;
	}
	g_pending = std::make_unique<keep::Pending>();
	const void* getter = reinterpret_cast<const void*>(&Passes);
	for (UInt32 slot : kGetterSlots) {
		if (!SafeWrite(slot, &getter, sizeof(getter))) {
			log("Route: pass lists kept - the vtable slot %08X could not be written; the getter is timed only where written, nothing is kept", slot);
			return;
		}
	}
	for (UInt32 site : kRelightCalls) {  // last: nothing is kept until a relight is routed
		if (!WriteCall(site, reinterpret_cast<const void*>(&Relight))) {
			log("Route: pass lists kept - the call at %08X could not be written", site);
			return;
		}
	}
	g_installed = true;
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log("Route: pass lists kept (%s%s) - a relight that leaves a property's lights as they were keeps its render passes; two calls and three vtable slots "
	    "patched",
	    g_mode == keep::Mode::kKeep ? "keep" : "verify: the rebuild compared with the cached passes", g_alternate ? ", alternating by frame" : "");
}

void LogPassListKeepSummary() {
	if (g_installed) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
