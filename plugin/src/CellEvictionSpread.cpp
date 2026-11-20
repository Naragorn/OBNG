#include "CellEvictionSpread.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "StepHooks.h"
#include "engine/world/CellEvictionSpreadLogic.h"

namespace obng::plugin {

namespace {

namespace ces = obng::engine::world::cell_eviction_spread;

// All read 2026-11-20 in the disassembly (knowledge/background-loading.md).
constexpr UInt32 kEndSite = 0x00433632;  // the pump's call of a task's end: mov edx,[ecx]; mov eax,[edx+14h]; call eax
constexpr UInt8 kEndBytes[] = {0x8B, 0x11, 0x8B, 0x42, 0x14, 0xFF, 0xD0};
constexpr UInt32 kFramePumpSite = 0x0040D8D8;  // the update step's call of the pump 0x00433590
constexpr UInt8 kFramePumpBytes[] = {0xE8, 0xB3, 0x5C, 0x02, 0x00};
constexpr UInt32 kPump = 0x00433590;  // __thiscall(io), no stack arguments
constexpr UInt8 kPumpBytes[] = {0x6A, 0xFF, 0x68, 0x90, 0xBF, 0x9A, 0x00};
constexpr UInt32 kCellRequest = 0x004BE090;  // __thiscall(manager, worldspace, x, y)
constexpr UInt8 kCellRequestBytes[] = {0x6A, 0xFF, 0x68, 0x08, 0xC7, 0x9A, 0x00};
constexpr UInt32 kMoveEnds = 0x004BE4E0;  // __thiscall(manager): the grid move ends the tasks done
constexpr UInt8 kMoveEndsBytes[] = {0x6A, 0xFF, 0x68, 0xF0, 0x44, 0x9B, 0x00};
constexpr UInt32 kGridAttach = 0x00444340;  // __thiscall(TES)
constexpr UInt8 kGridAttachBytes[] = {0x83, 0xEC, 0x18, 0xA1, 0x2C, 0x6A, 0xB0, 0x00};

constexpr UInt32 kCellTaskVtable = 0x00A45BE8;  // its slot 5 is the end 0x004BDDF0
constexpr UInt32 kTaskRefs = 0x08;
constexpr UInt32 kTaskState = 0x0C;       // 6: cancelled
constexpr UInt32 kTaskRecord = 0x1C;      // {x, y, worldspace, cell +0xC, queued byte +0x10}
constexpr UInt32 kTes = 0x00B333A0;       // TES*; +0x3C the exterior buffer's array
constexpr UInt32 kBufferSize = 0x00B051DC;
constexpr UInt32 kPlayer = 0x00B333C4;
constexpr UInt32 kOwnerLock = 0x00B35C80;     // this of the object owner lock
constexpr UInt32 kOwnerTake = 0x00496EA0;     // __thiscall(lock, object), RET 4
constexpr UInt32 kOwnerGive = 0x00496F50;     // __thiscall(lock, object), RET 4
constexpr UInt32 kIsMenuMode = 0x00578F60;    // bool __cdecl()
constexpr UInt32 kCellNode = 0x54;
constexpr UInt32 kCellFlags = 0x24;           // bit 0: interior
constexpr UInt32 kCellReferences = 0x48;      // tList {reference, next}
constexpr UInt32 kSlotSet3D = 0x150;          // TESObjectREFR::Set3D(NiNode*) (xOBSE slot 0x54)
constexpr UInt32 kSlotGetNiNode = 0x154;

using ThisFn = void(__thiscall*)(void*);
using OwnerFn = void(__thiscall*)(void* lock, void* object);
using Set3DFn = void(__thiscall*)(void* reference, void* node);
using GetNodeFn = void*(__thiscall*)(void* reference);
using RequestFn = void(__thiscall*)(void* manager, UInt32 worldspace, UInt32 x, UInt32 y);
using MenuFn = bool(__cdecl*)();

LogFn g_log = nullptr;
bool g_on = false;
double g_ticksPerUs = 0.0;
std::uint64_t g_budgetUs = 2000;
void* g_pump = nullptr;
void* g_cellRequest = nullptr;
void* g_moveEnds = nullptr;
void* g_gridAttach = nullptr;

// The main thread's only. The main thread is the one that runs the update step (measured 2026-11-20: not the thread
// that installs the plugin's hooks); until the first step says which, nothing is held, and a hook reached on another
// thread passes through.
DWORD g_mainThread = 0;
bool g_framePump = false;
bool g_flushing = false;
void* g_held[ces::kMaxHeld] = {};
UInt32 g_heldCount = 0;
UInt32 g_heldFrames = 0;
bool g_victimStripped = false;
UInt32 g_victim = 0;  // the cell the strip state is about

struct Stats {
	unsigned long long holds = 0, ends = 0, slices = 0, stripped = 0, actors = 0, flushes = 0, flushedEnds = 0, timeouts = 0;
	double sliceMs = 0.0, longestSliceMs = 0.0, endMs = 0.0, longestEndMs = 0.0;
	UInt32 flushBy[6] = {};
	// What the ends looked like (the hold's conditions, counted): all ends, on the main thread, in the frame's pump,
	// cell tasks there, and of those: menu mode, cancelled, with a cell, queued, with a node, exterior, in the buffer
	// already, the last slot taken.
	unsigned long long seen = 0, onMain = 0, inFramePump = 0, cellTasks = 0, menu = 0, cancelled = 0, withCell = 0, queued = 0, withNode = 0,
	                   exterior = 0, inBuffer = 0, lastTaken = 0, framePumps = 0, otherPumps = 0;
};
Stats g_stats;
enum FlushReason : UInt32 { kByRequest, kByMoveEnds, kByAttach, kByOtherPump, kByMenu, kByFrames };

LONGLONG Now() {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

UInt32 At(UInt32 address) {
	return *reinterpret_cast<const UInt32*>(address);
}

// The exterior buffer's last slot: the cell the next push evicts, 0 when the slot is free.
UInt32 LastSlotCell() {
	const UInt32 tes = At(kTes), size = At(kBufferSize);
	const UInt32 array = tes != 0 ? At(tes + 0x3C) : 0;
	return array != 0 && size != 0 ? At(array + (size - 1) * 4) : 0;
}

bool InBuffer(UInt32 cell) {
	const UInt32 tes = At(kTes), size = At(kBufferSize);
	const UInt32 array = tes != 0 ? At(tes + 0x3C) : 0;
	for (UInt32 i = 0; array != 0 && i < size; ++i) {
		if (At(array + i * 4) == cell) {
			return true;
		}
	}
	return false;
}

void CallEnd(void* task) {
	const UInt32 vtable = At(reinterpret_cast<UInt32>(task));
	reinterpret_cast<ThisFn>(At(vtable + 0x14))(task);
}

void Release(void* task) {
	LONG* refs = reinterpret_cast<LONG*>(reinterpret_cast<UInt32>(task) + kTaskRefs);
	if (InterlockedDecrement(refs) == 0) {
		using DestroyFn = void(__thiscall*)(void*, UInt32);
		reinterpret_cast<DestroyFn>(At(At(reinterpret_cast<UInt32>(task))))(task, 1);  // as the pump does
	}
}

// The oldest held task's end, with the route's reference given back.
void RunOldest() {
	void* task = g_held[0];
	for (UInt32 i = 1; i < g_heldCount; ++i) {
		g_held[i - 1] = g_held[i];
	}
	--g_heldCount;
	g_heldFrames = 0;
	g_victimStripped = false;
	g_victim = 0;
	const LONGLONG t0 = Now();
	CallEnd(task);
	const double ms = static_cast<double>(Now() - t0) / g_ticksPerUs / 1000.0;
	g_stats.endMs += ms;
	g_stats.longestEndMs = ms > g_stats.longestEndMs ? ms : g_stats.longestEndMs;
	++g_stats.ends;
	Release(task);
}

void Flush(FlushReason reason) {
	if (g_heldCount == 0 || g_flushing || GetCurrentThreadId() != g_mainThread) {
		return;
	}
	g_flushing = true;
	++g_stats.flushes;
	++g_stats.flushBy[reason];
	while (g_heldCount != 0) {
		RunOldest();
		++g_stats.flushedEnds;
	}
	g_flushing = false;
}

// One slice: the last slot's cell's placed references let go of their 3D until the budget is used; true when none
// with 3D is left.
bool StripSlice(UInt32 cell) {
	const LONGLONG t0 = Now();
	const UInt32 player = At(kPlayer);
	bool done = true;
	UInt32 doneInSlice = 0;
	reinterpret_cast<OwnerFn>(kOwnerTake)(reinterpret_cast<void*>(kOwnerLock), reinterpret_cast<void*>(cell));
	UInt32 node = cell + kCellReferences;
	while (node != 0) {
		const UInt32 reference = At(node);
		node = At(node + 4);  // the next first, as the engine's loop reads it before the call
		if (reference == 0) {
			continue;
		}
		const UInt32 type = *reinterpret_cast<const UInt8*>(reference + 4);
		const UInt32 vtable = At(reference);
		const bool kind = type == ces::kPlacedReference || ces::IsActor(type);
		const bool has3D = kind && reinterpret_cast<GetNodeFn>(At(vtable + kSlotGetNiNode))(reinterpret_cast<void*>(reference)) != nullptr;
		if (!ces::Strippable(type, reference == player, has3D)) {
			continue;
		}
		const std::uint64_t elapsedUs = static_cast<std::uint64_t>(static_cast<double>(Now() - t0) / g_ticksPerUs);
		if (!ces::GoOn(elapsedUs, g_budgetUs, ces::IsActor(type), doneInSlice)) {
			done = false;
			break;
		}
		reinterpret_cast<Set3DFn>(At(vtable + kSlotSet3D))(reinterpret_cast<void*>(reference), nullptr);
		++doneInSlice;
		++g_stats.stripped;
		g_stats.actors += ces::IsActor(type) ? 1 : 0;
	}
	reinterpret_cast<OwnerFn>(kOwnerGive)(reinterpret_cast<void*>(kOwnerLock), reinterpret_cast<void*>(cell));
	const double ms = static_cast<double>(Now() - t0) / g_ticksPerUs / 1000.0;
	++g_stats.slices;
	g_stats.sliceMs += ms;
	g_stats.longestSliceMs = ms > g_stats.longestSliceMs ? ms : g_stats.longestSliceMs;
	return done;
}

// Before every update step: a slice, or the oldest end.
void BeforeStep() {
	g_mainThread = GetCurrentThreadId();
	if (g_heldCount == 0) {
		return;
	}
	++g_heldFrames;
	const bool menu = reinterpret_cast<MenuFn>(kIsMenuMode)();
	const UInt32 victim = LastSlotCell();
	if (victim != g_victim) {  // another cell in the last slot (or none): the strip state starts over
		g_victim = victim;
		g_victimStripped = false;
	}
	switch (ces::StepOf(g_heldCount, g_heldFrames, menu, victim != 0, g_victimStripped)) {
	case ces::StepWay::Flush:
		if (!menu) {
			++g_stats.timeouts;
		}
		Flush(menu ? kByMenu : kByFrames);
		break;
	case ces::StepWay::RunEnd:
		RunOldest();
		break;
	case ces::StepWay::Strip:
		g_victimStripped = StripSlice(victim);
		break;
	case ces::StepWay::None:
		break;
	}
}

// In place of the pump's call of a task's end.
void __fastcall End(void* task) {
	ces::EndFacts f;
	const bool onMain = GetCurrentThreadId() == g_mainThread;
	f.enabled = g_on && onMain;
	++g_stats.seen;
	g_stats.onMain += onMain ? 1 : 0;
	g_stats.inFramePump += g_framePump ? 1 : 0;
	f.framePump = g_framePump;
	f.flushing = g_flushing;
	f.held = g_heldCount;
	const UInt32 t = reinterpret_cast<UInt32>(task);
	f.cellTask = At(t) == kCellTaskVtable;
	if (f.enabled && f.framePump && f.cellTask) {
		f.menuMode = reinterpret_cast<MenuFn>(kIsMenuMode)();
		f.cancelled = At(t + kTaskState) == 6;
		const UInt32 record = At(t + kTaskRecord);
		const UInt32 cell = record != 0 ? At(record + 0x0C) : 0;
		f.hasCell = cell != 0;
		f.queued = record != 0 && *reinterpret_cast<const UInt8*>(record + 0x10) != 0;
		if (cell != 0) {
			f.cellHasNode = At(cell + kCellNode) != 0;
			f.exterior = (*reinterpret_cast<const UInt8*>(cell + kCellFlags) & 1) == 0;
			f.inBuffer = InBuffer(cell);
		}
		f.lastSlotTaken = LastSlotCell() != 0;
		++g_stats.cellTasks;
		g_stats.menu += f.menuMode;
		g_stats.cancelled += f.cancelled;
		g_stats.withCell += f.hasCell;
		g_stats.queued += f.queued;
		g_stats.withNode += f.cellHasNode;
		g_stats.exterior += f.exterior;
		g_stats.inBuffer += f.inBuffer;
		g_stats.lastTaken += f.lastSlotTaken;
	}
	if (ces::WayOf(f) == ces::EndWay::Run) {
		CallEnd(task);
		return;
	}
	InterlockedIncrement(reinterpret_cast<LONG*>(t + kTaskRefs));
	g_held[g_heldCount++] = task;
	++g_stats.holds;
}

// In place of the update step's call of the pump: the frame's own pump call, the only one that holds.
void __fastcall FramePump(void* io, void*) {
	g_framePump = true;
	++g_stats.framePumps;
	reinterpret_cast<ThisFn>(kPump)(io);  // the entry detour below sees the flag
	g_framePump = false;
}

void __fastcall Pump(void* io, void*) {
	if (!g_framePump) {
		++g_stats.otherPumps;
		Flush(kByOtherPump);  // a wait for the queue must find every end run
	}
	reinterpret_cast<ThisFn>(g_pump)(io);
}

void __fastcall CellRequest(void* manager, void*, UInt32 worldspace, UInt32 x, UInt32 y) {
	Flush(kByRequest);  // the request may move a buffered cell up: none may be stripped then
	reinterpret_cast<RequestFn>(g_cellRequest)(manager, worldspace, x, y);
}

void __fastcall MoveEnds(void* manager, void*) {
	Flush(kByMoveEnds);  // it ends every task done itself: none may be held then
	reinterpret_cast<ThisFn>(g_moveEnds)(manager);
}

void __fastcall GridAttach(void* tes, void*) {
	Flush(kByAttach);  // no stripped cell may be attached
	reinterpret_cast<ThisFn>(g_gridAttach)(tes);
}

bool PatchSite(UInt32 site, const UInt8* bytes, UInt32 length, const void* target, LogFn log) {
	if (!Verify(site, bytes, length)) {
		log("Route: cell eviction spread - the site at %08X is not the game's own; not routed", site);
		return false;
	}
	UInt8 patch[8];
	std::memset(patch, 0x90, sizeof(patch));
	patch[0] = 0xE8;
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(target)) - (site + 5);
	std::memcpy(patch + 1, &rel, 4);
	return SafeWrite(site, patch, length);
}

}  // namespace

void InstallCellEvictionSpread(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (ces::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "CellEvictionSpread", 0, iniPath))) == ces::Mode::Off) {
		return;
	}
	g_log = log;
	const UINT tenths = GetPrivateProfileIntA("Route", "CellEvictionSliceTenthsMs", 20, iniPath);
	g_budgetUs = (tenths == 0 ? 20 : tenths) * 100ull;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerUs = static_cast<double>(frequency.QuadPart) / 1000000.0;
	if (!Verify(kEndSite, kEndBytes, sizeof(kEndBytes)) || !Verify(kFramePumpSite, kFramePumpBytes, sizeof(kFramePumpBytes))) {
		log("Route: cell eviction spread - the pump's sites are not the game's own; not routed");
		return;
	}
	// The flush points first, then the step, then the hold itself: nothing is held before every way out is in.
	const bool entries =
	    InstallEntryDetour("task pump 0x00433590", kPump, kPumpBytes, sizeof(kPumpBytes), reinterpret_cast<const void*>(&Pump), &g_pump, log, DetourRole::Route) &&
	    InstallEntryDetour("cell request 0x004BE090", kCellRequest, kCellRequestBytes, sizeof(kCellRequestBytes), reinterpret_cast<const void*>(&CellRequest),
	                       &g_cellRequest, log, DetourRole::Route) &&
	    InstallEntryDetour("grid move's task ends 0x004BE4E0", kMoveEnds, kMoveEndsBytes, sizeof(kMoveEndsBytes), reinterpret_cast<const void*>(&MoveEnds),
	                       &g_moveEnds, log, DetourRole::Route) &&
	    InstallEntryDetour("grid attach 0x00444340", kGridAttach, kGridAttachBytes, sizeof(kGridAttachBytes), reinterpret_cast<const void*>(&GridAttach),
	                       &g_gridAttach, log, DetourRole::Route);
	if (!entries) {
		log("Route: cell eviction spread - a flush point could not be installed; nothing is held");
		return;
	}
	if (!AddStepHook(&BeforeStep, "the cell eviction's slices", log) ||
	    !PatchSite(kFramePumpSite, kFramePumpBytes, sizeof(kFramePumpBytes), reinterpret_cast<const void*>(&FramePump), log)) {
		log("Route: cell eviction spread - the step or the frame's pump call could not be hooked; nothing is held");
		return;
	}
	if (!PatchSite(kEndSite, kEndBytes, sizeof(kEndBytes), reinterpret_cast<const void*>(&End), log)) {
		log("Route: cell eviction spread - the pump's end call could not be written; nothing is held");
		return;
	}
	g_on = true;
	log("Route: cell eviction spread - a cell task's end that would evict is held, the last slot's cell stripped %.1f ms a frame, then the end runs",
	    static_cast<double>(g_budgetUs) / 1000.0);
}

void LogCellEvictionSpreadSummary() {
	if (!g_on) {
		return;
	}
	const Stats s = g_stats;
	g_stats = Stats{};
	g_log("Route: cell eviction spread - %llu ends held, %llu run later (%.2f ms in all, the longest %.2f); %llu slices (%.2f ms in all, the longest "
	      "%.2f), %llu references stripped ahead (%llu actors); %llu flushes ending %llu at once (request %u, move %u, attach %u, other pump %u, menu %u, frames %u); "
	      "held now %u",
	      s.holds, s.ends - s.flushedEnds, s.endMs, s.longestEndMs, s.slices, s.sliceMs, s.longestSliceMs, s.stripped, s.actors, s.flushes, s.flushedEnds,
	      s.flushBy[kByRequest], s.flushBy[kByMoveEnds], s.flushBy[kByAttach], s.flushBy[kByOtherPump], s.flushBy[kByMenu], s.flushBy[kByFrames],
	      g_heldCount);
	g_log("Route: cell eviction spread - ends seen %llu (main thread %llu, in the frame's pump %llu; pump calls: the frame's %llu, others %llu); cell "
	      "tasks there %llu: menu mode %llu, cancelled %llu, with a cell %llu, queued %llu, with a node %llu, exterior %llu, in the buffer already "
	      "%llu, the last slot taken %llu",
	      s.seen, s.onMain, s.inFramePump, s.framePumps, s.otherPumps, s.cellTasks, s.menu, s.cancelled, s.withCell, s.queued, s.withNode, s.exterior,
	      s.inBuffer, s.lastTaken);
}

}  // namespace obng::plugin
