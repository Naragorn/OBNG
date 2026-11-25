#pragma once
// [Route] FarGridDefer: the far grid's part of a cell crossing one frame later - its shift and its update run at the
// next update step's start instead of inside the grid's move - the decisions over plain values (the plugin's
// FarGridDefer.cpp calls).
//
// Read 2026-11-25 in Oblivion.exe 1.2.0.416:
// - The grid's move 0x00444FB0, at a crossing, shifts the near grid (TES +8, GridCellArray, its slot +0x10 =
//   0x00482090) and then the far grid (TES +4, GridDistantArray, its slot +0x10 = 0x00483B60) to the new centre
//   (`mov ecx,[esi+4]; mov edx,[ecx]; mov eax,[edx+0x10]; push ebp; push edi; call eax` at 0x0044544F - 0x0044545A,
//   the centre's x in EDI, y in EBP - the values the move has just written to TES +0x20 / +0x24), then attaches the
//   cells (0x00444340), whose first call is the far grid's update 0x00483D60 (at 0x00444396, ECX the far grid, the
//   worldspace TES +0x74 pushed; `ret 4`) when the far LOD is on (0x004E9F40).
// - The far grid's shift moves its slots' entries and gives the empty ones their coordinates (0x00482050,
//   0x00483750); its update hides the LOD of the slots inside the near grid, shows or queues it for the others
//   (0x004BD430, loaded on the IO thread) and drops cells leaving from the LOD map (0x007B3A40). Nothing else in the
//   move reads the far grid; the LOD nodes are in world space, so the render draws what the slots held before.
// - 0x00444340 is called from two more places, 0x00444D3C and 0x00444E42 in 0x00444C70 (a jump or a load), each
//   behind a shift of the far grid at a site of that function's own (not redirected), as is 0x00438540's: there the
//   shift runs in place and so must the update, even when a crossing's shift is still kept - which is why an update
//   is kept only when TES's centre is the kept shift's.
// - Measured (2026-11-25, two rides, obng-profile-ride-anvil-late/20261125-143623 and -144020): the far grid's
//   update 0.87 - 1.43 ms and its shift 0.55 - 0.98 ms of a 3.7 - 8.5 ms move, the LOD map's scan 0x007B3A40 inside
//   them.
//
// Kept: the shift (grid, x, y) and the update (grid, worldspace). At the next step's start both run in the move's
// order while the far grid is the one they were kept for and TES's centre and worldspace are still theirs; after a
// jump or a load in between (the centre or the worldspace changed, that path shifted and updated by itself) they are
// dropped. One frame long the far LOD stays as it was: the row that entered the near grid keeps its LOD a frame
// longer, the row that left it gets its LOD a frame later - two cells from the player. Class B: the same calls one
// frame later.
//
// The two sources for each address: the move's instructions and the two grids' vtables (0x00A3D79C, 0x00A3D7E0,
// found through RTTI); the update's other callers and their arguments in the same instructions.
//
// 64-bit clean: the grid and the worldspace as 32-bit handles.

#include <cstdint>

namespace obng::engine::world::far_grid_defer {

inline constexpr std::uint32_t kShiftSite = 0x00445454;  // mov eax,[edx+0x10]; push ebp; push edi; call eax
inline constexpr std::uint8_t kShiftBytes[7] = {0x8B, 0x42, 0x10, 0x55, 0x57, 0xFF, 0xD0};
inline constexpr std::uint32_t kUpdateSite = 0x00444396;  // call 0x00483D60
inline constexpr std::uint32_t kShift = 0x00483B60;       // bool __thiscall (far grid, x, y), ret 8
inline constexpr std::uint32_t kUpdate = 0x00483D60;      // void __thiscall (far grid, worldspace), ret 4
inline constexpr std::uint32_t kTes = 0x00B333A0;         // TES*
inline constexpr std::uint32_t kTesFarGrid = 0x04, kTesCenterX = 0x20, kTesCenterY = 0x24, kTesWorld = 0x74;

enum class Mode : std::uint8_t { Off, On };

constexpr Mode ModeOf(int value) {
	return value == 1 ? Mode::On : Mode::Off;
}

struct Pending {
	bool shift = false;
	bool update = false;
	std::uint32_t grid = 0;
	std::int32_t x = 0, y = 0;
	std::uint32_t world = 0;
	std::uint64_t merged = 0;  // a second crossing before the step's start: the later one's state is kept
};

constexpr void KeepShift(Pending& pending, std::uint32_t grid, std::int32_t x, std::int32_t y) {
	if (pending.shift) {
		++pending.merged;
	}
	pending.shift = true;
	pending.grid = grid;
	pending.x = x;
	pending.y = y;
}

// The update is kept only behind a kept shift of the same grid to the centre TES has now (the crossing's own
// attach); an attach after a jump or a load, whose own shift ran in place, runs its update as before.
constexpr bool KeepsUpdate(const Pending& pending, std::uint32_t grid, std::int32_t xNow, std::int32_t yNow) {
	return pending.shift && pending.grid == grid && pending.x == xNow && pending.y == yNow;
}

constexpr void KeepUpdate(Pending& pending, std::uint32_t world) {
	pending.update = true;
	pending.world = world;
}

enum class Outcome : std::uint8_t { None, Run, Drop };

struct Taken {
	Outcome shift = Outcome::None;
	Outcome update = Outcome::None;
};

// At the step's start: what was kept runs while TES's far grid, centre and worldspace are still the ones it was
// kept for, else it is dropped. Either way nothing is kept afterwards.
constexpr Taken Take(Pending& pending, std::uint32_t gridNow, std::int32_t xNow, std::int32_t yNow, std::uint32_t worldNow) {
	Taken taken;
	if (!pending.shift && !pending.update) {
		return taken;
	}
	const bool same = pending.grid != 0 && pending.grid == gridNow && pending.x == xNow && pending.y == yNow;
	if (pending.shift) {
		taken.shift = same ? Outcome::Run : Outcome::Drop;
	}
	if (pending.update) {
		taken.update = same && pending.world != 0 && pending.world == worldNow ? Outcome::Run : Outcome::Drop;
	}
	pending.shift = false;
	pending.update = false;
	return taken;
}

struct Counts {
	std::uint64_t kept = 0;     // crossings whose far grid waited for the next step
	std::uint64_t run = 0;      // of them, run there
	std::uint64_t dropped = 0;  // of them, dropped (a jump or a load in between)
	std::uint64_t passed = 0;   // updates outside a crossing, run in place
};

constexpr void Count(Counts& counts, Taken taken) {
	if (taken.shift == Outcome::None && taken.update == Outcome::None) {
		return;
	}
	++counts.kept;
	++(taken.shift == Outcome::Drop || taken.update == Outcome::Drop ? counts.dropped : counts.run);
}

}  // namespace obng::engine::world::far_grid_defer
