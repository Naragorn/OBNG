#include "TextureLockSkip.h"

#include <windows.h>

#include <cstring>

#include "Memory.h"
#include "engine/render/TextureLockSkipLogic.h"

namespace obng::plugin {

namespace {

namespace tls = obng::engine::render::texture_lock_skip;

// The prepare's patch site (read 2026-11-19): `mov eax, [0x00B3F928]` (the renderer) and `push esi`, then
// `push edi` at 0x0077ACBE and `lea edi, [eax+0x180]` at 0x0077ACBF, the lock's enter at 0x0077ACC6; the data's use
// starts at 0x0077AD1C (`mov eax, [esi]`) after the lock's leave. EBX is the texture, EBP the texture manager; EAX,
// ECX and EDX are written before they are read on both ways.
constexpr UInt32 kSite = 0x0077ACB8;
constexpr UInt8 kSiteBytes[] = {0xA1, 0x28, 0xF9, 0xB3, 0x00, 0x56};
constexpr UInt32 kLockPath = 0x0077ACBF;
constexpr UInt8 kLockPathBytes[] = {0x8D, 0xB8, 0x80, 0x01, 0x00, 0x00};
constexpr UInt32 kUse = 0x0077AD1C;
constexpr UInt8 kUseBytes[] = {0x8B, 0x06, 0x8B, 0x50, 0x04};
constexpr UInt32 kRenderer = 0x00B3F928;
constexpr UInt32 kRendererLock = 0x180;  // a CRITICAL_SECTION at its start (the enter's argument, read)
constexpr UInt32 kRendererData = 0x24;   // the texture's renderer data (read)
constexpr unsigned kMismatchLines = 20;

enum class Mode { kOff, kRoute, kCheck };

Mode g_mode = Mode::kOff;
LogFn g_log = nullptr;
UInt32 g_lockPath = kLockPath;
UInt32 g_use = kUse;
volatile LONG g_unlocked = 0;
volatile LONG g_locked = 0;
volatile LONG g_compared = 0;
volatile LONG g_mismatches = 0;
volatile LONG g_mismatchLines = 0;

// The check, before the original's own lock: the pointer read without the lock, then under it.
void __stdcall Compare(UInt32 texture) {
	UInt32 unlocked = 0;
	std::memcpy(&unlocked, reinterpret_cast<const void*>(texture + kRendererData), sizeof(unlocked));
	if (tls::WayOf(unlocked) == tls::Way::Locked) {
		InterlockedIncrement(&g_locked);
		return;
	}
	InterlockedIncrement(&g_unlocked);
	UInt32 renderer = 0;
	std::memcpy(&renderer, reinterpret_cast<const void*>(kRenderer), sizeof(renderer));
	auto* lock = reinterpret_cast<CRITICAL_SECTION*>(renderer + kRendererLock);
	EnterCriticalSection(lock);
	UInt32 locked = 0;
	std::memcpy(&locked, reinterpret_cast<const void*>(texture + kRendererData), sizeof(locked));
	LeaveCriticalSection(lock);
	InterlockedIncrement(&g_compared);
	if (!tls::Agrees(unlocked, locked)) {
		InterlockedIncrement(&g_mismatches);
		if (InterlockedIncrement(&g_mismatchLines) <= static_cast<LONG>(kMismatchLines)) {
			g_log("Route: texture lock skip - MISMATCH for the texture %08X: %08X without the lock, %08X under it", texture, unlocked, locked);
		}
	}
}

// The route: the data there - on to its use; none - the original's way under the lock.
__declspec(naked) void RouteSite() {
	__asm {
		push esi
		push edi
		mov esi, dword ptr [ebx + 0x24]
		test esi, esi
		jz locked
		lock inc dword ptr [g_unlocked]
		jmp dword ptr [g_use]
	locked:
		lock inc dword ptr [g_locked]
		mov eax, dword ptr ds:[0x00B3F928]
		jmp dword ptr [g_lockPath]
	}
}

// The check: compared first, then always the original's way under the lock.
__declspec(naked) void CheckSite() {
	__asm {
		push esi
		push edi
		push ebx
		call Compare
		mov eax, dword ptr ds:[0x00B3F928]
		jmp dword ptr [g_lockPath]
	}
}

}  // namespace

void InstallTextureLockSkip(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	const UINT value = GetPrivateProfileIntA("Route", "TextureLockSkip", 0, iniPath);
	g_mode = value == 1 ? Mode::kRoute : value == 2 ? Mode::kCheck : Mode::kOff;
	if (g_mode == Mode::kOff) {
		return;
	}
	g_log = log;
	if (!Verify(kSite, kSiteBytes, sizeof(kSiteBytes)) || !Verify(kLockPath, kLockPathBytes, sizeof(kLockPathBytes)) ||
	    !Verify(kUse, kUseBytes, sizeof(kUseBytes))) {
		char have[32];
		DescribeBytes(kSite, have, sizeof(have));
		log("Route: texture lock skip - the prepare at %08X reads %s, not the game's own bytes; not routed", kSite, have);
		g_mode = Mode::kOff;
		return;
	}
	const void* to = g_mode == Mode::kRoute ? reinterpret_cast<const void*>(&RouteSite) : reinterpret_cast<const void*>(&CheckSite);
	UInt8 patch[6] = {0xE9, 0, 0, 0, 0, 0x90};
	const UInt32 relative = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(to)) - (kSite + 5);
	std::memcpy(&patch[1], &relative, sizeof(relative));
	if (!SafeWrite(kSite, patch, sizeof(patch))) {
		log("Route: texture lock skip - the prepare at %08X could not be written; not routed", kSite);
		g_mode = Mode::kOff;
		return;
	}
	log("Route: texture lock skip (%s) - the texture prepare 0x0077AC90 takes the renderer's lock only for a texture without its data",
	    g_mode == Mode::kRoute ? "route" : "check");
}

void LogTextureLockSkipSummary() {
	if (g_mode == Mode::kOff) {
		return;
	}
	g_log("Route: texture lock skip (%s) - prepares: %ld with their data (%s), %ld without (under the lock); compared %ld, mismatches %ld",
	      g_mode == Mode::kRoute ? "route" : "check", InterlockedExchange(&g_unlocked, 0), g_mode == Mode::kRoute ? "no lock" : "the lock taken anyway",
	      InterlockedExchange(&g_locked, 0), InterlockedExchange(&g_compared, 0), InterlockedExchange(&g_mismatches, 0));
}

}  // namespace obng::plugin
