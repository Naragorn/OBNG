#pragma once

// The engine-facing shape of 0x005AD980 (proposed LoadingMenu_Tick): the
// layouts it reads and writes, and the calling conventions of it and its
// callees as called here. The decisions and the call sequence live in
// LoadingMenu_TickLogic.h.
//
// Evidence: re/functions/0x005AD980.md, re/export/frame-clock/0x005AD980/asm.s;
// the menu field offsets are read here and again in the constructor 0x005AD0D0
// (re/functions/0x005AD0D0.md, a second code path in the same binary). Names
// are proposals until the frame-clock slice's refutation round.
//
// LoadingMenuLayout duplicates the view in LoadingMenu_LoadingMenu_005AD0D0.h
// under the names re/functions/0x005AD980.md proposes; both belong in one
// shared LoadingMenu header that does not exist yet (layout request in the
// worker report). The offsets agree field for field (derived: same static
// asserts in both headers).
//
// Only addresses with two sources are constants here (AGENTS.md):
//   0x00582160 InterfaceManager_GetSingleton - called at 0x005ADAC6/0x005ADAE5
//              and xOBSE obse/GameAPI.cpp:114 (re/symbols/xobse.csv:327)
//   0x0058CEB0 Tile::UpdateFloat - called at 0x005ADA8E and xOBSE
//              obse/GameTiles.cpp:415 (xobse.csv:337), OBVR GameAddresses.h:1428
//              (re/symbols/obvr.csv:44)
//   0x00B33398 g_osGlobals - read at 0x005ADB15/0x005ADB2B and xOBSE
//              obse/GameOSDepend.cpp:3 (xobse.csv:1532)
//   0x00B33A10 g_ioManager - read at 0x005ADAD2/0x005ADB03 and xOBSE
//              obse/GameTasks.cpp:7 (xobse.csv:1549)
// The function itself, 0x00589B70, 0x005898F0 (xOBSE "inferred" only),
// 0x0047D170, 0x005AD780, 0x00403C00, 0x00583F40, 0x00579260, 0x005792B0,
// 0x00432860, 0x00432890, 0x0060D0A0, the settings 0x00B14158/60/68/70 and the
// constants 0x00A2FC78/0x00A2FC70/0x00A6C4C8 have one source so far (this
// body) and stay in comments until a second one agrees.

#include "engine/time/LoadingMenu_TickLogic.h"
#include "engine/time/TimeInfo.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::loading_menu_tick::engine {

inline constexpr std::uint32_t kInterfaceManagerGetSingletonAddress = 0x00582160;
inline constexpr std::uint32_t kTileUpdateFloatAddress = 0x0058CEB0;
inline constexpr std::uint32_t kOsGlobalsAddress = 0x00B33398;
inline constexpr std::uint32_t kIoManagerAddress = 0x00B33A10;

// The fields of the loading menu this function touches. The object size is
// not visible here; sizeof of this struct is not the object's size and is not
// asserted. +0x00..+0x27 is the Menu base per xOBSE GameMenus.h (unverified
// against this binary).
struct LoadingMenuLayout {
	std::uint8_t unknown00[0x3C];      // +0x00  not touched here
	std::int32_t slowStepCap;          // +0x3C  CMP EDX,[ESI+0x3C] 0x005AD9E8 (read)
	float screenTimeLeft;              // +0x40  FLD 0x005ADA0B, FST 0x005ADA2E, FSTP 0x005ADABF (read; unit unverified)
	std::uint8_t unknown44[0x10];      // +0x44  not touched here
	TimeInfo* clock;                   // +0x54  MOV EDI,[ESI+0x54] 0x005AD9B6, 0x005AD9C7 (read)
	std::uint8_t unknown58[0x08];      // +0x58  not touched here
	std::int32_t shownStep;            // +0x60  0x005ADA50, 0x005ADA75, FILD 0x005ADA78 (read)
	std::int32_t targetStep;           // +0x64  ADD 0x005AD9DF, MOV 0x005ADA06, read 0x005ADA4D (read)
	std::uint32_t stepChangedMs;       // +0x68  SUB 0x005AD9CF, MOV 0x005ADA72 (read)
	std::uint32_t lastTickMs;          // +0x6C  SUB 0x005AD9D4, MOV 0x005ADB12 (read)
	std::uint8_t fastStepEnabled;      // +0x70  CMP byte ptr [ESI+0x70],0 0x005AD9F5 (read)
};
static_assert(offsetof(LoadingMenuLayout, slowStepCap) == 0x3C, "+0x3C");
static_assert(offsetof(LoadingMenuLayout, screenTimeLeft) == 0x40, "+0x40");
static_assert(offsetof(LoadingMenuLayout, clock) == 0x54, "+0x54");
static_assert(offsetof(LoadingMenuLayout, shownStep) == 0x60, "+0x60");
static_assert(offsetof(LoadingMenuLayout, targetStep) == 0x64, "+0x64");
static_assert(offsetof(LoadingMenuLayout, stepChangedMs) == 0x68, "+0x68");
static_assert(offsetof(LoadingMenuLayout, lastTickMs) == 0x6C, "+0x6C");
static_assert(offsetof(LoadingMenuLayout, fastStepEnabled) == 0x70, "+0x70");
static_assert(sizeof(float) == 4 && sizeof(TimeInfo*) == 4, "a 32-bit build");

// The clock is the shared TimeInfo; only +0x10 is read here (C10, read).
static_assert(offsetof(TimeInfo, lastMs) == 0x10, "lastMs at +0x10 (0x005AD9CA, 0x005ADA6F)");

// g_osGlobals: only +0x10 is read here (0x005ADB1B, read); the name is
// xOBSE's (GameOSDepend.h:351).
struct OsGlobalsView {
	std::uint8_t unknown00[0x10];  // +0x00  not touched here
	std::uint32_t mainThreadId;    // +0x10
};
static_assert(offsetof(OsGlobalsView, mainThreadId) == 0x10, "mainThreadID at +0x10");

// 0x005AD980 itself: cdecl, one stack argument read as a byte at
// [entry ESP+4] (0x005ADA3C), RET without immediate at 0x005ADB3E (read); EAX
// at exit is not a defined result (re/functions/0x005AD980.md, calling
// convention), so void.
//
// The original's countdown (0x005ADA18-0x005ADA26) rounds to the x87
// precision control of the thread that calls it, which the rebuild takes as
// Inputs::precision (LoadingMenu_TickLogic.h). A binding has to read the
// caller's control word at the call (FNSTCW) and pass
// x87::SupportedPrecision of it; when that gives none (another rounding
// control, an unmasked exception, the reserved precision 01B) the rebuild does
// not model the call and the original 0x005AD980 has to run. No binding
// exists yet (read: nothing under src/ includes this header).
using TickFn = void(__cdecl*)(std::uint8_t forceRefresh);

// 0x00589B70: one pushed argument, caller cleans (ADD ESP,4 at 0x005AD990,
// read); returns the tile menu (C3, derived).
using TileMenuForTypeFn = void*(__cdecl*)(std::uint32_t menuType);

// 0x005898F0 Tile::GetContainingMenu: ECX = tile, no stack arguments (read:
// nothing pushed at 0x005AD99B/0x005AD9AD).
using GetContainingMenuFn = LoadingMenuLayout*(__thiscall*)(void* tile);

// GetTickCount via the IAT slot 0x00A280D0, GetCurrentThreadId via 0x00A2808C (read).
using GetTickCountFn = std::uint32_t(__stdcall*)();
using GetCurrentThreadIdFn = std::uint32_t(__stdcall*)();

// 0x0047D170, the TimeInfo update: ECX = clock, one pushed tick, callee cleans
// (no ADD ESP after 0x005AD9C2, read; RET 4 per OBVR FrameClock.cpp:13).
using TimeInfoUpdateFn = void(__thiscall*)(TimeInfo* self, std::uint32_t tick);

// 0x0058CEB0 Tile::UpdateFloat: ECX = tile, (trait, value) pushed, callee
// cleans (C5, derived).
using TileUpdateFloatFn = void(__thiscall*)(void* tile, std::uint32_t trait, float value);

// 0x005AD780: ECX = menu, the tile menu pushed, callee cleans (C6, derived).
using RefreshScreenFn = void(__thiscall*)(LoadingMenuLayout* self, void* tileMenu);

// 0x00403C00: ECX = the setting 0x00B14158, returns a pointer to its float
// value (FLD [EAX] at 0x005ADABD; C7, read + OBVR GameAddresses.h).
using SettingValueFn = float*(__thiscall*)(void* setting);

// 0x00582160 InterfaceManager_GetSingleton: PUSH 1; PUSH 0, caller cleans
// (ADD ESP,8 at 0x005ADACB/0x005ADAEA, read). The width of each argument is
// unverified beyond the 4-byte push.
using InterfaceManagerGetSingletonFn = void*(__cdecl*)(std::uint32_t first, std::uint32_t second);

// 0x00583F40 (on the InterfaceManager), 0x00432860 and 0x00432890 (on
// g_ioManager), 0x0060D0A0 (on g_osGlobals, entered by JMP): ECX = this, no
// stack arguments (nothing pushed before them and the stack balances at the
// epilogue, derived).
using ThisOnlyFn = void(__thiscall*)(void* self);

// 0x00579260: PUSH 0, caller cleans (ADD ESP,4 at 0x005ADAFB, read).
using InterfacePassFn = void(__cdecl*)(std::uint32_t argument);

// 0x005792B0: nothing pushed and ECX not set before the call (0x005ADAFE,
// read); declared cdecl with no arguments (derived).
using InterfacePassEndFn = void(__cdecl*)();

} // namespace obng::engine::time::loading_menu_tick::engine
