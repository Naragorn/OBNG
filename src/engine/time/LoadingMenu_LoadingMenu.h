#pragma once

// The engine-facing shape of 0x005AD0D0 (proposed LoadingMenu::LoadingMenu):
// the layouts it writes and reads, and the calling conventions of it and its
// callees as called here. The decisions and the call sequence live in
// LoadingMenu_LoadingMenuLogic.h.
//
// Evidence: re/functions/0x005AD0D0.md, re/export/loading-menu/0x005AD0D0/
// (asm.s, data.json, meta.json). The menu field offsets are written here and
// read again by LoadingMenu::Tick 0x005AD980 (re/functions/0x005AD980.md,
// src/engine/time/LoadingMenu_Tick.h) - a second code path in the same
// binary for +0x3C, +0x40, +0x54, +0x60, +0x64, +0x68, +0x6C, +0x70. Names
// are proposals until the loading-menu slice's refutation round.
//
// LoadingMenuLayout duplicates the view in LoadingMenu_Tick.h
// (loading_menu_tick::engine::LoadingMenuLayout) for the offsets both
// functions touch; both belong in one shared LoadingMenu header that does not
// exist yet (layout request in the worker report). The two disagree on one
// type: Tick declares +0x68/+0x6C as uint32, this header int32 per the
// frame-clock refutation of this function (re/ledger.md, row 0x005AD0D0). The
// constructor itself only copies 32 bits there (0x005AD332/0x005AD335), so
// the signedness is its readers' to decide.
//
// Only addresses with two sources are constants here (AGENTS.md):
//   0x00B33398 g_osGlobals        - read at 0x005AD145 and xOBSE
//                                   obse/GameOSDepend.cpp:3 (re/symbols/xobse.csv:1532)
//   0x00401F00 FormHeap_Allocate  - called at 0x005AD2E9 and xOBSE
//                                   obse/GameAPI.cpp:111 (re/symbols/xobse.csv:4)
//   0x0047D150 / 0x0047D170       - TimeInfo::Init / Update, carried by the
//                                   shared TimeInfo.h (kTimeInfoInit, kTimeInfoUpdate)
// The function itself (one source: this body, plus the RTTI name
// RTTI_LoadingMenu 0x00B13270 at xobse.csv:956 whose link to the vtable
// 0x00A6C40C is unverified), the base constructor 0x00584640, 0x006A9B40,
// 0x004A7A60 (xOBSE "inferred" only, xobse.csv:157), _snprintf 0x009833BB,
// _mbsnbcat 0x009872B3, the table 0x00B14120, the copy 0x00B3B3DC, the
// setting 0x00B14158 and the IAT slot 0x00A280D0 have one source so far and
// stay in comments until a second one agrees.

#include "engine/time/LoadingMenu_LoadingMenuLogic.h"
#include "engine/time/TimeInfo.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace obng::engine::time::loading_menu_ctor::engine {

inline constexpr std::uint32_t kOsGlobalsAddress = 0x00B33398;
inline constexpr std::uint32_t kFormHeapAllocateAddress = 0x00401F00;

// The fields of the loading menu this function writes. The object size is not
// visible here (at least 0x72, derived from the byte write at +0x71); sizeof
// of this struct is not the object's size. +0x04..+0x27 belong to the base
// 0x00584640 and are not touched here.
struct LoadingMenuLayout {
	std::uint32_t vtable;                       // +0x00  := 0x00A6C40C at 0x005AD11F (read)
	std::uint8_t base04[0x24];                  // +0x04  the base constructor's, not touched here
	std::int32_t unknown28;                     // +0x28  := -1 at 0x005AD134 (read; meaning unverified)
	std::array<std::uint32_t, 4> sectionValues;  // +0x2C  := 0 each, 0x005AD19E in the loop (read; meaning unverified)
	std::int32_t slowStepCap;                   // +0x3C  := -1 at 0x005AD131 (read; name from 0x005AD980)
	float screenTimeLeft;                       // +0x40  := +0.0f at 0x005AD12B, then [0x00B14158] at 0x005AD33E (read)
	std::uint8_t unknown44[0x08];               // +0x44  not touched here
	std::uint32_t unknown4C;                    // +0x4C  := 0 at 0x005AD125 (read)
	std::uint32_t unknown50;                    // +0x50  := 0 at 0x005AD128 (read)
	TimeInfo* clock;                            // +0x54  := the allocated TimeInfo or 0 at 0x005AD31F (read)
	std::uint8_t unknown58[0x08];               // +0x58  not touched here
	std::int32_t shownStep;                     // +0x60  := 0 at 0x005AD137 (read; name from 0x005AD980)
	std::int32_t targetStep;                    // +0x64  := 0 at 0x005AD13A (read; name from 0x005AD980)
	std::int32_t stepChangedMs;                 // +0x68  := clock->clockMs at 0x005AD332 (read)
	std::int32_t lastTickMs;                    // +0x6C  := clock->clockMs at 0x005AD335 (read)
	std::uint8_t fastStepEnabled;               // +0x70  := 0 at 0x005AD13D (read; name from 0x005AD980)
	std::uint8_t unknown71;                     // +0x71  := 1 at 0x005AD141 (read; meaning unverified)
};
static_assert(offsetof(LoadingMenuLayout, vtable) == 0x00, "+0x00");
static_assert(offsetof(LoadingMenuLayout, unknown28) == 0x28, "+0x28");
static_assert(offsetof(LoadingMenuLayout, sectionValues) == 0x2C, "+0x2C");
static_assert(sizeof(LoadingMenuLayout::sectionValues) == 0x10, "+0x2C..+0x3B, four dwords");
static_assert(offsetof(LoadingMenuLayout, slowStepCap) == 0x3C, "+0x3C");
static_assert(offsetof(LoadingMenuLayout, screenTimeLeft) == 0x40, "+0x40");
static_assert(offsetof(LoadingMenuLayout, unknown4C) == 0x4C, "+0x4C");
static_assert(offsetof(LoadingMenuLayout, unknown50) == 0x50, "+0x50");
static_assert(offsetof(LoadingMenuLayout, clock) == 0x54, "+0x54");
static_assert(offsetof(LoadingMenuLayout, shownStep) == 0x60, "+0x60");
static_assert(offsetof(LoadingMenuLayout, targetStep) == 0x64, "+0x64");
static_assert(offsetof(LoadingMenuLayout, stepChangedMs) == 0x68, "+0x68");
static_assert(offsetof(LoadingMenuLayout, lastTickMs) == 0x6C, "+0x6C");
static_assert(offsetof(LoadingMenuLayout, fastStepEnabled) == 0x70, "+0x70");
static_assert(offsetof(LoadingMenuLayout, unknown71) == 0x71, "+0x71");
static_assert(sizeof(float) == 4 && sizeof(TimeInfo*) == 4, "a 32-bit build");

// The clock is the shared TimeInfo; it is allocated with its size and only
// +0x10 is read here (0x005AD2E7 PUSH 0x18, 0x005AD32F MOV EAX,[EAX+0x10]).
static_assert(sizeof(TimeInfo) == kClockBytes, "FormHeap_Allocate(0x18) at 0x005AD2E9");
static_assert(offsetof(TimeInfo, clockMs) == 0x10, "clockMs at +0x10 (0x005AD32F)");

// A setting as the table 0x00B14120 points at it: the value at +0, the name
// at +4 (0x005AD18B/0x005AD206 FLD [EAX], 0x005AD208 MOV ECX,[EAX+4], read);
// the shape xOBSE calls SettingInfo, unverified against xOBSE here. Also the
// shape of 0x00B14158 (image 15.0f, name pointer 0x00A6C610 at +4, data.json).
struct SectionSettingLayout {
	float value;       // +0x00
	const char* name;  // +0x04  printed with %s (0x005AD211)
};
static_assert(sizeof(SectionSettingLayout) == 8, "8-byte setting");
static_assert(offsetof(SectionSettingLayout, name) == 4, "name at +4");

// *g_osGlobals: only +0x24 is read here (0x005AD14A, read); what it holds is
// unverified. LoadingMenu_Tick.h has the same object's +0x10.
struct OsGlobalsView {
	std::uint8_t unknown00[0x24];  // +0x00  not touched here
	void* hookObject;              // +0x24  ECX for 0x006A9B40 when nonzero
};
static_assert(offsetof(OsGlobalsView, hookObject) == 0x24, "+0x24");

// 0x005AD0D0 itself: thiscall, this in ECX (MOV ESI,ECX at 0x005AD110), no
// stack parameters (nothing at [EBP+8] or above is read), RET without
// immediate at 0x005AD366, returns this in EAX (MOV EAX,ESI at 0x005AD341)
// (read). The /GS check and the C++ EH frame are not part of the result.
using ConstructFn = LoadingMenuLayout*(__thiscall*)(LoadingMenuLayout* self);

// 0x00584640, the base constructor (name unverified): ECX = this, nothing
// pushed (0x005AD116), no caller cleanup; its EAX is not used (read).
using BaseConstructFn = void(__thiscall*)(LoadingMenuLayout* self);

// 0x006A9B40: ECX = [*g_osGlobals+0x24], nothing pushed (0x005AD158); no
// stack arguments, as the ESP-relative locals after it are unchanged
// (derived: [ESP+0x2C] is the sum slot before and after).
using OsGlobalsHookFn = void(__thiscall*)(void* object);

// 0x004A7A60, the warning sink (name unverified): called with (format,
// index) and cleaned with ADD ESP,8 (0x005AD181/0x005AD186), and with the
// finished buffer alone (0x005AD2DF, cleaned together with _mbsnbcat's three
// arguments by ADD ESP,0x10 at 0x005AD2E4) (read): caller cleanup with a
// varying count, cdecl variadic (derived). Whether it formats is unverified.
using WarnFn = void(__cdecl*)(const char* format, ...);

// 0x009833BB _snprintf (Ghidra's CRT name): (buffer, size, format, ...),
// caller cleanup (ADD ESP,0xC / 0x18 / 0x10 / 0x14, read).
using SnprintfFn = int(__cdecl*)(char* buffer, std::size_t size, const char* format, ...);

// 0x009872B3 _mbsnbcat (Ghidra's CRT name): (message, line, count), caller
// cleanup (ADD ESP,0xC at 0x005AD278, read); its result is not used.
using MbsnbcatFn = unsigned char*(__cdecl*)(unsigned char* destination, const unsigned char* source, std::size_t count);

// 0x00401F00 FormHeap_Allocate: one pushed size, caller cleanup (ADD ESP,4 at
// 0x005AD2F0, read). Whether it can return 0 is unverified (C14b).
using FormHeapAllocateFn = void*(__cdecl*)(std::uint32_t size);

// GetTickCount via the IAT slot 0x00A280D0, loaded into EBX at 0x005AD2F9 and
// called twice (0x005AD309, 0x005AD322) (read).
using GetTickCountFn = std::uint32_t(__stdcall*)();

// 0x0047D150 TimeInfo::Init: ECX = block, the tick pushed, RET 4; returns the
// tick, unused here (TimeInfo.h; 0x005AD30B-0x005AD30E, read).
using TimeInfoInitFn = std::uint32_t(__thiscall*)(TimeInfo* self, std::uint32_t tick);

// 0x0047D170 TimeInfo::Update: ECX = block (0 after a failed allocation), the
// tick pushed, RET 4 (TimeInfo.h; 0x005AD324-0x005AD327, read).
using TimeInfoUpdateFn = void(__thiscall*)(TimeInfo* self, std::uint32_t tick);

// No binding exists yet (read: nothing under src/ includes this header). A
// binding implements the ports of RunConstructor with the functions above, a
// Fields(menu) returning the LoadingMenuLayout, and passes each
// FormatString's address (the image's string) to the CRT and the sink.

} // namespace obng::engine::time::loading_menu_ctor::engine
