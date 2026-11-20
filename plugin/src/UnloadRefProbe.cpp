#include "UnloadRefProbe.h"

#include <windows.h>

#include <intrin.h>
#include <cstdio>

namespace obng::plugin {

namespace {

// 0x004CB4D0, read 2026-11-20: __thiscall(cell); 0x00496EA0(0x00B35C80, cell); for each reference of the list at
// cell +0x48 that is not the player (*0x00B333C4): vtable +0x150 with 0; 0x00496F50(0x00B35C80, cell).
constexpr UInt32 kLoop = 0x004CB4D0;
constexpr UInt8 kLoopBytes[] = {0x56, 0x57, 0x8B, 0xF9, 0x57};
constexpr UInt32 kOwnerLock = 0x00B35C80, kOwnerTake = 0x00496EA0, kOwnerGive = 0x00496F50, kPlayer = 0x00B333C4;

using OwnerFn = void(__thiscall*)(void* lock, void* object);
using Set3DFn = void(__thiscall*)(void* reference, void* node);
using GetNodeFn = void*(__thiscall*)(void* reference);

struct Row {
	unsigned long long calls = 0, with3D = 0, ticks = 0, longest = 0;
};

void* g_original = nullptr;
LogFn g_log = nullptr;
bool g_on = false;
double g_ticksPerMs = 0.0;
Row g_rows[256];  // by form type; the main thread's (the unload runs there)
unsigned long long g_cells = 0, g_cellTicks = 0, g_longestCell = 0;

UInt32 At(UInt32 address) {
	return *reinterpret_cast<const UInt32*>(address);
}

void __fastcall Loop(void* cellPointer, void*) {
	const UInt32 cell = reinterpret_cast<UInt32>(cellPointer);
	const unsigned __int64 start = __rdtsc();
	reinterpret_cast<OwnerFn>(kOwnerTake)(reinterpret_cast<void*>(kOwnerLock), cellPointer);
	UInt32 node = cell + 0x48;
	while (node != 0) {
		const UInt32 reference = At(node);
		node = At(node + 4);  // the next first, as the original reads it before the call
		if (reference == 0 || reference == At(kPlayer)) {
			continue;
		}
		const UInt32 vtable = At(reference);
		Row& row = g_rows[*reinterpret_cast<const UInt8*>(reference + 4)];
		const bool has3D = reinterpret_cast<GetNodeFn>(At(vtable + 0x154))(reinterpret_cast<void*>(reference)) != nullptr;
		const unsigned __int64 t0 = __rdtsc();
		reinterpret_cast<Set3DFn>(At(vtable + 0x150))(reinterpret_cast<void*>(reference), nullptr);
		const unsigned __int64 ticks = __rdtsc() - t0;
		++row.calls;
		row.with3D += has3D ? 1 : 0;
		row.ticks += ticks;
		row.longest = ticks > row.longest ? ticks : row.longest;
	}
	reinterpret_cast<OwnerFn>(kOwnerGive)(reinterpret_cast<void*>(kOwnerLock), cellPointer);
	const unsigned __int64 all = __rdtsc() - start;
	++g_cells;
	g_cellTicks += all;
	g_longestCell = all > g_longestCell ? all : g_longestCell;
}

double TicksPerMs() {
	LARGE_INTEGER q0, q1, f;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&q0);
	const unsigned __int64 t0 = __rdtsc();
	Sleep(30);
	QueryPerformanceCounter(&q1);
	return static_cast<double>(__rdtsc() - t0) / (static_cast<double>(q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart));
}

}  // namespace

void InstallUnloadRefProbe(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Probe", "UnloadRefs", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	g_ticksPerMs = TicksPerMs();
	g_on = InstallEntryDetour("cell unload's reference loop 0x004CB4D0", kLoop, kLoopBytes, sizeof(kLoopBytes), reinterpret_cast<const void*>(&Loop),
	                          &g_original, log, DetourRole::Route);
	log("Probe: unload references - %s", g_on ? "the loop 0x004CB4D0 runs in OBNG, each Set3D(null) timed by form type" : "not installed");
}

void LogUnloadRefSummary() {
	if (!g_on || g_cells == 0) {
		return;
	}
	g_log("Probe: unload references - %llu cells, %.2f ms in all, the longest %.2f ms", g_cells, static_cast<double>(g_cellTicks) / g_ticksPerMs,
	      static_cast<double>(g_longestCell) / g_ticksPerMs);
	for (unsigned type = 0; type < 256; ++type) {
		Row& row = g_rows[type];
		if (row.calls == 0) {
			continue;
		}
		g_log("Probe: unload references - type %02X: %llu references (%llu with 3D), %.2f ms in all, %.1f us each, the longest %.2f ms", type, row.calls,
		      row.with3D, static_cast<double>(row.ticks) / g_ticksPerMs, static_cast<double>(row.ticks) / g_ticksPerMs * 1000.0 / static_cast<double>(row.calls),
		      static_cast<double>(row.longest) / g_ticksPerMs);
		row = Row{};
	}
	g_cells = g_cellTicks = g_longestCell = 0;
}

}  // namespace obng::plugin
