#include "StepHooks.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"

namespace obng::plugin {

namespace {

constexpr UInt32 kStepSite = 0x0040F270;                        // call 0x0040D800 in the main loop (read)
constexpr UInt8 kStepBytes[] = {0xE8, 0x8B, 0xE5, 0xFF, 0xFF};  // call rel32 -> 0x0040D800
constexpr UInt32 kUpdateStep = 0x0040D800;                      // __thiscall(main), ECX = *0x00B33398 at 0x0040F26A (read)
constexpr unsigned kMaxHooks = 8;

using StepFn = void(__thiscall*)(void* main);

StepHook g_hooks[kMaxHooks] = {};
unsigned g_count = 0;
bool g_patched = false;

void __fastcall Step(void* main, void*) {
	for (unsigned i = 0; i < g_count; ++i) {
		g_hooks[i]();
	}
	reinterpret_cast<StepFn>(kUpdateStep)(main);
}

bool Patch(LogFn log) {
	if (!Verify(kStepSite, kStepBytes, sizeof(kStepBytes))) {
		log("Step hooks: the call at %08X is not the game's own; no per-step work", kStepSite);
		return false;
	}
	UInt8 patch[sizeof(kStepBytes)] = {0xE8};
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Step)) - (kStepSite + 5);
	std::memcpy(patch + 1, &rel, 4);
	if (!SafeWrite(kStepSite, patch, sizeof(patch))) {
		log("Step hooks: the call at %08X could not be written; no per-step work", kStepSite);
		return false;
	}
	return true;
}

}  // namespace

bool AddStepHook(StepHook hook, const char* name, LogFn log) {
	if (hook == nullptr || g_count >= kMaxHooks) {
		return false;
	}
	if (!g_patched) {
		if (!Patch(log)) {
			return false;
		}
		g_patched = true;
	}
	g_hooks[g_count++] = hook;  // installs run before the game's first step, on the loading thread: no step reads this yet
	log("Step hooks: %s runs before every update step (hook %u)", name, g_count);
	return true;
}

}  // namespace obng::plugin
