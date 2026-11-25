#include "FarGridDefer.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "StepHooks.h"
#include "engine/world/FarGridDeferLogic.h"

namespace obng::plugin {

namespace {

namespace fgd = obng::engine::world::far_grid_defer;

using ShiftFn = bool(__thiscall*)(UInt32 grid, std::int32_t x, std::int32_t y);
using UpdateFn = void(__thiscall*)(UInt32 grid, UInt32 world);

LogFn g_log = nullptr;
bool g_on = false;
fgd::Pending g_pending;  // the main thread's only (the crossing and the step both run on it)
fgd::Counts g_counts;
double g_ticksPerMs = 0.0;
double g_runMs = 0.0, g_longestMs = 0.0;

template <typename T>
T At(UInt32 base, std::uint32_t offset) {
	T value;
	std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base) + offset), sizeof(T));
	return value;
}

// In place of the move's `call [slot +0x10]` of the far grid: ECX the far grid, EDX its vtable, the centre pushed.
bool __fastcall KeepShift(UInt32 grid, UInt32, std::int32_t x, std::int32_t y) {
	fgd::KeepShift(g_pending, grid, x, y);
	return true;  // the move does not read it
}

// In place of the attach's call of the far grid's update: kept behind the crossing's kept shift, run in place
// otherwise (an attach after a jump or a load, whose own shift ran in place).
void __fastcall KeepUpdate(UInt32 grid, UInt32, UInt32 world) {
	const UInt32 tes = At<UInt32>(fgd::kTes, 0);
	const std::int32_t x = tes != 0 ? At<std::int32_t>(tes, fgd::kTesCenterX) : 0;
	const std::int32_t y = tes != 0 ? At<std::int32_t>(tes, fgd::kTesCenterY) : 0;
	if (tes != 0 && fgd::KeepsUpdate(g_pending, grid, x, y)) {
		fgd::KeepUpdate(g_pending, world);
		return;
	}
	++g_counts.passed;
	reinterpret_cast<UpdateFn>(fgd::kUpdate)(grid, world);
}

// At the next update step's start: the kept calls made, in the move's order.
void RunKept() {
	const fgd::Pending kept = g_pending;
	const UInt32 tes = At<UInt32>(fgd::kTes, 0);
	const UInt32 grid = tes != 0 ? At<UInt32>(tes, fgd::kTesFarGrid) : 0;
	const std::int32_t x = tes != 0 ? At<std::int32_t>(tes, fgd::kTesCenterX) : 0;
	const std::int32_t y = tes != 0 ? At<std::int32_t>(tes, fgd::kTesCenterY) : 0;
	const UInt32 world = tes != 0 ? At<UInt32>(tes, fgd::kTesWorld) : 0;
	const fgd::Taken taken = fgd::Take(g_pending, grid, x, y, world);
	fgd::Count(g_counts, taken);
	if (taken.shift != fgd::Outcome::Run && taken.update != fgd::Outcome::Run) {
		return;
	}
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	if (taken.shift == fgd::Outcome::Run) {
		reinterpret_cast<ShiftFn>(fgd::kShift)(kept.grid, kept.x, kept.y);
	}
	if (taken.update == fgd::Outcome::Run) {
		reinterpret_cast<UpdateFn>(fgd::kUpdate)(kept.grid, kept.world);
	}
	QueryPerformanceCounter(&t1);
	const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) / g_ticksPerMs;
	g_runMs += ms;
	g_longestMs = ms > g_longestMs ? ms : g_longestMs;
}

bool PatchShift() {
	UInt8 patch[sizeof(fgd::kShiftBytes)] = {0x55, 0x57, 0xE8};  // push ebp; push edi; call KeepShift
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&KeepShift)) - (fgd::kShiftSite + 2 + 5);
	std::memcpy(patch + 3, &rel, 4);
	return SafeWrite(fgd::kShiftSite, patch, sizeof(patch));
}

bool PatchUpdate() {
	UInt8 patch[5] = {0xE8};
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&KeepUpdate)) - (fgd::kUpdateSite + 5);
	std::memcpy(patch + 1, &rel, 4);
	return SafeWrite(fgd::kUpdateSite, patch, sizeof(patch));
}

}  // namespace

void InstallFarGridDefer(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (fgd::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "FarGridDefer", 0, iniPath))) == fgd::Mode::Off) {
		return;
	}
	g_log = log;
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_ticksPerMs = static_cast<double>(frequency.QuadPart) / 1000.0;
	UInt8 call[5] = {0xE8};
	const UInt32 toUpdate = fgd::kUpdate - (fgd::kUpdateSite + 5);
	std::memcpy(call + 1, &toUpdate, 4);
	if (!Verify(fgd::kShiftSite, fgd::kShiftBytes, sizeof(fgd::kShiftBytes)) || !Verify(fgd::kUpdateSite, call, sizeof(call))) {
		log("Route: far grid defer - the move's far grid calls at %08X / %08X are not the game's own; not routed", fgd::kShiftSite, fgd::kUpdateSite);
		return;
	}
	if (!AddStepHook(&RunKept, "the far grid's shift and update kept from a crossing", log)) {
		log("Route: far grid defer - no step hook; not routed");
		return;
	}
	// The update first: until the shift is taken, every update runs in place.
	if (!PatchUpdate() || !PatchShift()) {
		log("Route: far grid defer - the calls at %08X / %08X could not be written; not routed", fgd::kUpdateSite, fgd::kShiftSite);
		return;
	}
	g_on = true;
	log("Route: far grid defer - the far grid's shift and update at a cell crossing run at the next update step's start");
}

void LogFarGridDeferSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: far grid defer - %llu crossings' far grid a step later (%.3f ms in all, the longest %.3f ms), %llu dropped (a jump or a load "
	      "in between), %llu merged, %llu updates outside a crossing run in place",
	      g_counts.run, g_runMs, g_longestMs, g_counts.dropped, static_cast<unsigned long long>(g_pending.merged), g_counts.passed);
	g_counts = fgd::Counts{};
	g_runMs = g_longestMs = 0.0;
}

}  // namespace obng::plugin
