#pragma once

// The engine-facing shape of 0x005AD0D0 (proposed LoadingMenu::LoadingMenu):
// the layouts it writes and reads, and the calling conventions of it and its
// callees. The decisions live in LoadingMenu_LoadingMenu_005AD0D0Logic.h.
//
// Evidence: re/functions/0x005AD0D0.md, re/export/frame-clock/0x005AD0D0/asm.s;
// the field offsets are read in this body and again in the loading menu's
// update 0x005AD980 (a second code path in the same binary, per the function
// file's struct table). Names are proposals until the frame-clock slice's
// refutation round.
//
// No callee or global address is a constant here: each has one source so far
// (this body) and stays a comment until a second one agrees (AGENTS.md).

#include "engine/time/LoadingMenu_LoadingMenu_005AD0D0Logic.h"
#include "engine/time/TimeInfo.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::loading_menu_ctor::engine {

// A Setting as read through the table 0x00B14120 (0x005AD18B value,
// 0x005AD208 name).
struct SettingLayout {
	float value;       // +0x00
	const char* name;  // +0x04, printed with %s
};
static_assert(sizeof(SettingLayout) == 8, "value and name, 4 bytes each on a 32-bit build");
static_assert(offsetof(SettingLayout, value) == 0x00, "value at +0x00");
static_assert(offsetof(SettingLayout, name) == 0x04, "name at +0x04");

// The fields of the loading menu this body touches. The object size is not
// visible here (at least 0x72, derived from the byte write at +0x71); sizeof of
// this struct is not the object's size and is not asserted. Whether
// +0x04..+0x27 all belong to the base 0x00584640 is unverified.
struct LoadingMenuLayout {
	const void* vtable;              // +0x00  0x00A6C40C (0x005AD11F)
	std::uint8_t menuBase[0x24];     // +0x04  not touched here
	std::int32_t unknown28;          // +0x28  -1 (0x005AD134)
	float sectionValues[4];          // +0x2C  0 per pass (0x005AD19E)
	std::int32_t stepLimit;          // +0x3C  -1 (0x005AD131); compared in 0x005AD980
	float countdown;                 // +0x40  0.0, then [0x00B14158] (0x005AD12B, 0x005AD33E)
	std::uint8_t unknown44[8];       // +0x44  not touched here
	std::uint32_t unknown4C;         // +0x4C  0 (0x005AD125)
	std::uint32_t unknown50;         // +0x50  0 (0x005AD128)
	TimeInfo* clock;                 // +0x54  the private clock (0x005AD31F)
	std::uint8_t unknown58[8];       // +0x58  not touched here
	std::int32_t shownPercent;       // +0x60  0 (0x005AD137)
	std::int32_t step;               // +0x64  0 (0x005AD13A)
	std::uint32_t lastStepMs;        // +0x68  clock+0x10 (0x005AD332)
	std::uint32_t startMs;           // +0x6C  clock+0x10 (0x005AD335)
	bool flag70;                     // +0x70  0 (0x005AD13D)
	bool flag71;                     // +0x71  1 (0x005AD141)
};
static_assert(offsetof(LoadingMenuLayout, unknown28) == 0x28, "+0x28");
static_assert(offsetof(LoadingMenuLayout, sectionValues) == 0x2C, "+0x2C");
static_assert(offsetof(LoadingMenuLayout, stepLimit) == 0x3C, "+0x3C");
static_assert(offsetof(LoadingMenuLayout, countdown) == 0x40, "+0x40");
static_assert(offsetof(LoadingMenuLayout, unknown4C) == 0x4C, "+0x4C");
static_assert(offsetof(LoadingMenuLayout, unknown50) == 0x50, "+0x50");
static_assert(offsetof(LoadingMenuLayout, clock) == 0x54, "+0x54");
static_assert(offsetof(LoadingMenuLayout, shownPercent) == 0x60, "+0x60");
static_assert(offsetof(LoadingMenuLayout, step) == 0x64, "+0x64");
static_assert(offsetof(LoadingMenuLayout, lastStepMs) == 0x68, "+0x68");
static_assert(offsetof(LoadingMenuLayout, startMs) == 0x6C, "+0x6C");
static_assert(offsetof(LoadingMenuLayout, flag70) == 0x70, "+0x70");
static_assert(offsetof(LoadingMenuLayout, flag71) == 0x71, "+0x71");

// The allocation is sized for the shared TimeInfo (0x005AD2E7 PUSH 0x18).
static_assert(sizeof(TimeInfo) == kClockSize, "the private clock is a TimeInfo");
static_assert(offsetof(TimeInfo, lastMs) == 0x10, "the baselines copy +0x10 (0x005AD32F)");

// 0x005AD0D0 itself: thiscall, no stack parameters, plain RET, returns this
// in EAX (read: 0x005AD110, 0x005AD341, 0x005AD366).
using ConstructorFn = LoadingMenuLayout*(__thiscall*)(LoadingMenuLayout* self);

// 0x00584640 (proposed Menu::Menu): ECX = this only (0x005AD116, read); its
// return value is not used here.
using BaseConstructorFn = void(__thiscall*)(LoadingMenuLayout* self);

// 0x006A9B40: ECX = [[0x00B33398]+0x24] (0x005AD14A, read); no stack
// arguments, result not used.
using OsGlobalsHookFn = void(__thiscall*)(void* object);

// 0x004A7A60 (proposed formatted log): caller cleans (ADD ESP,8 at 0x005AD186,
// read); called with (format, index) and with (buffer), so cdecl varargs
// (derived). The buffer is passed as the format: a '%' in a setting name would
// be read as a conversion (derived, unverified in the callee).
using LogFn = void(__cdecl*)(const char* format, ...);

// FormHeap_Allocate 0x00401F00: caller cleans (ADD ESP,4 at 0x005AD2F0, read).
using AllocateFn = void*(__cdecl*)(std::uint32_t size);

// 0x0047D150 (proposed TimeInfo::TimeInfo): ECX = block, one pushed tick, no
// caller cleanup (read), so ret 4 (derived). Return value not used here.
using TimeInfoConstructorFn = void(__thiscall*)(TimeInfo* self, std::uint32_t tick);

// 0x0047D170, the TimeInfo update: ECX = clock, one pushed tick, RET 4 at
// 0x0047D253 (read).
using TimeInfoUpdateFn = void(__thiscall*)(TimeInfo* self, std::uint32_t tick);

// GetTickCount through the IAT slot 0x00A280D0 (read).
using GetTickCountFn = std::uint32_t(__stdcall*)();

} // namespace obng::engine::time::loading_menu_ctor::engine
