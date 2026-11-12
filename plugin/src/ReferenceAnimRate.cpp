#include "ReferenceAnimRate.h"

#include <windows.h>

#include <intrin.h>
#include <memory>

#include "EntryDetour.h"
#include "engine/scene/ReferenceAnimRateLogic.h"

namespace obng::plugin {

namespace {

namespace rate = obng::engine::scene::reference_anim_rate;

constexpr const char* kName = "TESObjectREFR::UpdateAnimation (references with nothing to animate)";
constexpr UInt32 kAddress = 0x004E3F30;  // slot +0x160 of a plain reference; __thiscall, no argument, RET at 0x004E407F (read)
constexpr UInt8 kBytes[] = {0x51, 0x53, 0x8B, 0xD9, 0x8B, 0x43, 0x08};  // push ecx; push ebx; mov ebx, ecx; mov eax, [ebx+8] (read)
// The one caller whose calls may be left out: the cell's loop over its
// references, 0x004CCC50..0x004CCCBC (read; re/export/rea-all 0x4ccc50.c).
constexpr UInt32 kCellLoopBegin = 0x004CCC50;
constexpr UInt32 kCellLoopEnd = 0x004CCCBD;
constexpr UInt32 kClockMs = 0x00B33EA0;  // TimeInfo 0x00B33E90 +0x10 clockMs: moves once a frame (knowledge/frame-clock.md)
// What 0x004E3F30 reads to find out whether there is anything to do (read in its instructions, 2026-11-12):
constexpr UInt32 kRefFlags = 0x08;             // bits 5 and 11: the function returns at once
constexpr UInt32 kRefBase = 0x1C;              // the base form; its type byte at +0x04
constexpr UInt32 kFormType = 0x04;
constexpr UInt32 kRefNode = 0x3C;              // the scene node; its parent at +0x1C
constexpr UInt32 kNodeParent = 0x1C;
constexpr UInt32 kSlotNode = 0x154;            // the reference's node, through the vtable
constexpr UInt32 kSlotAnimData = 0x164;        // its animation data, through the vtable
constexpr UInt32 kNodeChildren = 0xB0;         // the child array and its count (a word)
constexpr UInt32 kNodeChildCount = 0xB6;
constexpr UInt32 kObjectController = 0x0C;     // the first child's controller
constexpr UInt32 kRttiCast = 0x00560920;       // cdecl(rtti, object): the object as that class or null
constexpr UInt32 kControllerManagerRtti = 0x00B3CAC0;
constexpr UInt32 kManagerFlags = 0x08;         // bit 3: sequences to look after

using UpdateFn = void(__thiscall*)(void* self);
using GetterFn = UInt32(__thiscall*)(void* self);
using CastFn = UInt32(__cdecl*)(UInt32 rtti, UInt32 object);

void* g_original = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;
std::uint32_t g_every = 1;
DWORD g_thread = 0;  // the thread of the first call: the only one whose calls are left out
std::unique_ptr<rate::Table> g_table;
rate::Clock g_clock;
rate::Counts g_counts;
bool g_alternate = false;     // [Route] ReferenceAnimAlternate: the lower rate only in every second summary window (a measurement)
volatile LONG g_onNow = 1;

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

// Whether 0x004E3F30 does anything for this reference beyond finding out
// that there is nothing to do - its own gates, read again right after its
// call (the lines are warm then). A light, an NPC or a creature (base form
// types 0x1A, 0x23, 0x24) gets slot +0xFC called first in every call: always
// busy. The kind is for the summary's split of where the time goes.
enum Kind : unsigned { kIdle, kLight, kActorBase, kSequences, kAnimData, kKinds };
constexpr const char* kKindName[kKinds] = {"idle", "lights", "NPC or creature bases", "controller managers with sequences", "animation data"};

Kind KindOf(UInt32 reference) {
	const UInt32 flags = Dword(reference + kRefFlags);
	if (((flags >> 5) & 1u) != 0 || ((flags >> 11) & 1u) != 0) {
		return kIdle;
	}
	const UInt32 base = Dword(reference + kRefBase);
	if (base != 0) {
		const UInt8 type = *reinterpret_cast<const UInt8*>(base + kFormType);
		if (type == 0x1A) {
			return kLight;
		}
		if (type == 0x23 || type == 0x24) {
			return kActorBase;
		}
	}
	void* self = reinterpret_cast<void*>(reference);
	const UInt32 vtable = Dword(reference);
	const UInt32 node = reinterpret_cast<GetterFn>(Dword(vtable + kSlotNode))(self);
	if (node != 0 && *reinterpret_cast<const UInt16*>(node + kNodeChildCount) != 0) {
		const UInt32 child = Dword(Dword(node + kNodeChildren));
		const UInt32 controller = child != 0 ? Dword(child + kObjectController) : 0;
		if (controller != 0) {
			const UInt32 manager = reinterpret_cast<CastFn>(kRttiCast)(kControllerManagerRtti, controller);
			if (manager != 0 && ((*reinterpret_cast<const UInt8*>(manager + kManagerFlags) >> 3) & 1u) != 0) {
				return kSequences;
			}
		}
	}
	if (reinterpret_cast<GetterFn>(Dword(vtable + kSlotAnimData))(self) != 0) {
		const UInt32 own = Dword(reference + kRefNode);
		return own != 0 && Dword(own + kNodeParent) != 0 ? kAnimData : kIdle;
	}
	return kIdle;
}

// The calls that ran, by what the reference was found to be: how many and
// the time of the original's call (TSC ticks; the summary calibrates).
unsigned __int64 g_kindCalls[kKinds] = {};
unsigned __int64 g_kindTicks[kKinds] = {};
unsigned __int64 g_summaryTsc = 0;
LARGE_INTEGER g_summaryQpc{};

// The entry's replacement: __fastcall with an unused second register takes a
// __thiscall's this; no stack argument.
void __fastcall Update(void* self, void*) {
	const UInt32 site = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	const DWORD thread = GetCurrentThreadId();
	if (g_thread == 0) {
		g_thread = thread;
	}
	if (thread != g_thread || site < kCellLoopBegin || site >= kCellLoopEnd) {
		reinterpret_cast<UpdateFn>(g_original)(self);
		return;
	}
	g_clock.See(Dword(kClockMs));
	const UInt32 reference = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(self));
	if (g_table->Decide(reference, g_clock.frame, g_onNow != 0 ? g_every : 1, g_counts) == rate::Call::Skip) {
		return;
	}
	const unsigned __int64 t0 = __rdtsc();
	reinterpret_cast<UpdateFn>(g_original)(self);
	const unsigned __int64 ticks = __rdtsc() - t0;
	const Kind kind = KindOf(reference);
	++g_kindCalls[kind];
	g_kindTicks[kind] += ticks;
	g_table->Found(kind != kIdle, g_counts);
}

}  // namespace

void InstallReferenceAnimRate(const char* iniPath, LogFn log) {
	if (g_installed) {
		return;
	}
	const UINT every = GetPrivateProfileIntA("Route", "ReferenceAnimEvery", 1, iniPath);
	if (every <= 1) {
		return;
	}
	g_log = log;
	g_every = every > 64 ? 64 : every;
	g_table = std::make_unique<rate::Table>();
	g_alternate = GetPrivateProfileIntA("Route", "ReferenceAnimAlternate", 0, iniPath) != 0;
	if (!InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(&Update), &g_original, log, DetourRole::Route)) {
		g_table.reset();
		return;
	}
	g_installed = true;
	g_summaryTsc = __rdtsc();
	QueryPerformanceCounter(&g_summaryQpc);
	log("Route %s: a reference found with nothing to animate is asked again in one frame of %u%s", kName, g_every,
	    g_alternate ? "; alternating: only in every second summary window" : "");
}

void LogReferenceAnimRateSummary() {
	if (!g_installed) {
		return;
	}
	const rate::Counts c = g_counts;
	g_counts = rate::Counts{};
	const bool wasOn = g_onNow != 0;
	if (g_alternate) {
		InterlockedExchange(&g_onNow, wasOn ? 0 : 1);
	}
	g_log("Route: references' animation update (idle ones every %u. frame, %s in this window) - %llu calls from the cells' loop, %llu left out; run: "
	      "%llu busy, %llu on their turn, %llu new, %llu without a slot; found busy on a turn (noticed late) %llu, found idle again %llu",
	      g_every, wasOn ? "on" : "off", c.calls, c.skipped, c.runBusy, c.runTurn, c.runNew, c.runNoRoom, c.becameBusy, c.becameIdle);
	// The time of the calls that ran, by kind (in the windows with the route off every call runs: the full picture).
	const unsigned __int64 tsc = __rdtsc();
	LARGE_INTEGER qpc{};
	LARGE_INTEGER frequency{};
	QueryPerformanceCounter(&qpc);
	QueryPerformanceFrequency(&frequency);
	const double seconds = static_cast<double>(qpc.QuadPart - g_summaryQpc.QuadPart) / static_cast<double>(frequency.QuadPart);
	const double ticksPerMs = seconds > 0.0 && g_summaryTsc != 0 ? static_cast<double>(tsc - g_summaryTsc) / (seconds * 1000.0) : 0.0;
	g_summaryTsc = tsc;
	g_summaryQpc = qpc;
	for (unsigned kind = 0; kind < kKinds; ++kind) {
		const double ms = ticksPerMs > 0.0 ? static_cast<double>(g_kindTicks[kind]) / ticksPerMs : 0.0;
		g_log("Route: references' animation update - run for %s: %llu calls, %.3f ms in all (%.2f us each)", kKindName[kind], g_kindCalls[kind], ms,
		      g_kindCalls[kind] != 0 ? ms * 1000.0 / static_cast<double>(g_kindCalls[kind]) : 0.0);
		g_kindCalls[kind] = 0;
		g_kindTicks[kind] = 0;
	}
}

}  // namespace obng::plugin
