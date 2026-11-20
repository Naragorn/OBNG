#include "BodyTexturePreload.h"

#include <windows.h>
#include <psapi.h>

#include <intrin.h>
#include <cstdio>
#include <cstring>

#include "Memory.h"
#include "StepHooks.h"
#include "engine/render/BodyTexturePreloadLogic.h"

namespace obng::plugin {

namespace {

namespace btp = obng::engine::render::body_texture_preload;

constexpr UInt32 kLoadingSite = 0x005ADADC;                          // call 0x00432860 in LoadingMenu::Tick (read)
constexpr UInt8 kLoadingBytes[] = {0xE8, 0x7F, 0x4D, 0xE8, 0xFF};    // call rel32 -> 0x00432860
constexpr UInt32 kPauseLoader = 0x00432860;                          // __thiscall(io manager) (read)
constexpr UInt32 kCreateImage = 0x00442890;                          // (out, path, 0, 0), RET 0x10 (read; the body build's call at 0x0052D4E8)
constexpr UInt32 kShaderTexture = 0x007B8200;                       // __cdecl(out, path, load, check mips) (read; the body's normal maps at 0x007B82C3)
constexpr UInt32 kDataHandler = 0x00B33A98;                          // DataHandler* (xOBSE g_dataHandler; read at 0x005C7BA0)
constexpr UInt32 kRaces = 0x44;                                      // tList<TESRace> {data, next}
constexpr UInt32 kFormId = 0x0C;                                     // TESForm's refID
constexpr UInt32 kMaxPaths = 512;
constexpr UInt32 kPathSize = 260;
constexpr UInt32 kMaxRaces = 128;
constexpr UInt32 kMaxPointers = kMaxRaces * btp::kCount;

using PauseFn = void(__thiscall*)(void* io);
using CreateFn = void*(__stdcall*)(void** out, const char* path, UInt32, UInt32);
using ShaderTextureFn = void(__cdecl*)(void** out, const char* path, UInt32 load, UInt32 checkMips);

btp::Mode g_mode = btp::Mode::Off;
LogFn g_log = nullptr;
char g_paths[kMaxPaths][kPathSize];  // the held textures' names
void* g_held[kMaxPaths];             // the references CreateTextureImage handed out, never given back
UInt32 g_count = 0;
UInt32 g_seen[kMaxPointers];         // the race paths' pointers at the last preload
UInt32 g_seenCount = 0;
bool g_preloaded = false;
UInt32 g_passes = 0;
unsigned __int64 g_checkTicks = 0, g_checks = 0;

UInt64 PrivateBytes() {
	PROCESS_MEMORY_COUNTERS_EX counters{};
	counters.cb = sizeof(counters);
	return K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))
	           ? counters.PrivateUsage
	           : 0;
}

bool Known(const char* path) {
	for (UInt32 i = 0; i < g_count; ++i) {
		if (btp::SameFile(g_paths[i], path)) {
			return true;
		}
	}
	return false;
}

const char* EntryPath(UInt32 race, UInt32 index, UInt16* length) {
	const UInt32 entry = race + btp::EntryOffset(index);
	const char* texture = *reinterpret_cast<const char* const*>(entry + btp::kPathPointer);
	*length = *reinterpret_cast<const UInt16*>(entry + btp::kPathLength);
	if (*length == btp::kLengthUnknown) {
		*length = texture != nullptr ? static_cast<UInt16>(std::strlen(texture)) : 0;
	}
	return texture;
}

// The race paths' pointers now, in list order; 0 when the races are not loaded.
UInt32 Snapshot(UInt32* out) {
	const UInt32 handler = *reinterpret_cast<const UInt32*>(kDataHandler);
	if (handler == 0 || *reinterpret_cast<const UInt32*>(handler + kRaces) == 0) {
		return 0;
	}
	UInt32 n = 0;
	for (UInt32 node = handler + kRaces; node != 0 && n + btp::kCount <= kMaxPointers; node = *reinterpret_cast<const UInt32*>(node + 4)) {
		const UInt32 race = *reinterpret_cast<const UInt32*>(node);
		for (UInt32 i = 0; race != 0 && i < btp::kCount; ++i) {
			out[n++] = *reinterpret_cast<const UInt32*>(race + btp::EntryOffset(i) + btp::kPathPointer);
		}
	}
	return n;
}

struct Tally {
	UInt32 races = 0, held = 0, failed = 0, known = 0, empty = 0, full = 0, normals = 0, noNormal = 0;
};

// The normal map beside a skin, through the shader's texture cache 0x007B8200 the body's own load asks (held as the
// skins are; a skin without one counts as no normal).
void Normal(const char* skin, Tally& tally) {
	if (g_count >= kMaxPaths) {
		++tally.full;
		return;
	}
	char* path = g_paths[g_count];
	if (!btp::NormalPathOf(skin, path, kPathSize) || Known(path)) {
		return;
	}
	void* texturePointer = nullptr;
	reinterpret_cast<ShaderTextureFn>(kShaderTexture)(&texturePointer, path, 1, 0);
	g_held[g_count++] = texturePointer;
	texturePointer != nullptr ? ++tally.normals : ++tally.noNormal;
}

void Race(UInt32 race, Tally& tally) {
	for (UInt32 i = 0; i < btp::kCount; ++i) {
		UInt16 length = 0;
		const char* texture = EntryPath(race, i, &length);
		if (g_count >= kMaxPaths) {
			++tally.full;
			continue;
		}
		char* path = g_paths[g_count];
		if (!btp::PathOf(texture, length, path, kPathSize)) {
			++tally.empty;
			continue;
		}
		if (Known(path)) {
			++tally.known;
			continue;
		}
		void* texturePointer = nullptr;
		reinterpret_cast<CreateFn>(kCreateImage)(&texturePointer, path, 0, 0);
		g_held[g_count++] = texturePointer;
		texturePointer != nullptr ? ++tally.held : ++tally.failed;
		Normal(path, tally);
		if (g_passes > 0) {  // the later passes are few: name what they load
			g_log("Route: body texture preload - pass %u: race %08X [%u] \"%s\" %s", g_passes + 1, *reinterpret_cast<const UInt32*>(race + kFormId), i,
			      path, texturePointer != nullptr ? "held" : "failed");
		}
	}
}

// Every race path not held yet loaded and held; the snapshot taken. Main thread only.
void Pass(const char* where) {
	const UInt32 handler = *reinterpret_cast<const UInt32*>(kDataHandler);
	LARGE_INTEGER from, to, frequency;
	QueryPerformanceCounter(&from);
	const UInt64 before = PrivateBytes();
	Tally tally;
	for (UInt32 node = handler + kRaces; node != 0; node = *reinterpret_cast<const UInt32*>(node + 4)) {
		const UInt32 race = *reinterpret_cast<const UInt32*>(node);
		if (race != 0) {
			++tally.races;
			Race(race, tally);
		}
	}
	g_seenCount = Snapshot(g_seen);
	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&frequency);
	const double ms = static_cast<double>(to.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	const double mb = static_cast<double>(static_cast<long long>(PrivateBytes() - before)) / (1024.0 * 1024.0);
	++g_passes;
	g_log("Route: body texture preload (%s) - pass %u %s: %u races, %u skins and %u normal maps newly held (%u entries in all), %u skins "
	      "failed, %u without a normal map, %u held already, %u without a path, %u past the list's end; %.1f ms, private memory %+.1f MB",
	      g_mode == btp::Mode::Async ? "async not built yet, as on" : "on", g_passes, where, tally.races, tally.held, tally.normals, g_count,
	      tally.failed, tally.noNormal, tally.known, tally.empty, tally.full, ms, mb);
}

// A pass when the races are there and their paths are not the ones of the last pass.
void CheckAndPass(const char* where) {
	static UInt32 s_now[kMaxPointers];
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 n = Snapshot(s_now);
	const bool changed = n > 0 && btp::Changed(g_seen, g_seenCount, s_now, n);
	g_checkTicks += __rdtsc() - t0;
	++g_checks;
	if (changed) {
		Pass(where);
		g_preloaded = true;
	}
}

// In place of the loading screen's call of the loader pause: the pause, then a pass while the loader rests.
void __fastcall PauseThenPass(void* io, void*) {
	reinterpret_cast<PauseFn>(kPauseLoader)(io);
	CheckAndPass("at a loading screen");
}

// Before every update step (StepHooks.h): after the first pass, a pass when a script changed a race's paths
// (measured: a body mod does so in the first frames after a load).
void CheckBeforeStep() {
	if (g_preloaded) {
		CheckAndPass("after a race's paths changed");
	}
}

bool PatchCall(UInt32 site, const UInt8* bytes, const void* replacement, LogFn log) {
	if (!Verify(site, bytes, 5)) {
		log("Route: body texture preload - the call at %08X is not the game's own; not routed", site);
		return false;
	}
	UInt8 patch[5] = {0xE8};
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(replacement)) - (site + 5);
	std::memcpy(patch + 1, &rel, 4);
	if (!SafeWrite(site, patch, sizeof(patch))) {
		log("Route: body texture preload - the call at %08X could not be written; not routed", site);
		return false;
	}
	return true;
}

}  // namespace

void InstallBodyTexturePreload(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_mode = btp::ModeOf(static_cast<int>(GetPrivateProfileIntA("Route", "BodyTexturePreload", 1, iniPath)));
	if (btp::WayOf(g_mode) == btp::Way::None) {
		return;
	}
	g_log = log;
	if (g_mode == btp::Mode::Async) {
		log("Route: body texture preload - BodyTexturePreload=2 (async) is not built yet; running as 1 (on)");
	}
	if (!PatchCall(kLoadingSite, kLoadingBytes, reinterpret_cast<const void*>(&PauseThenPass), log)) {
		return;
	}
	const bool step = AddStepHook(&CheckBeforeStep, "the body texture preload's check", log);
	log("Route: body texture preload (%s) - every race's body textures loaded and held at a loading screen (the call at %08X)%s",
	    g_mode == btp::Mode::Async ? "async not built yet, as on" : "on", kLoadingSite,
	    step ? ", again when a script changes a race's paths (checked every step at 0x0040F270)" : "; the step's check is not in");
}

void LogBodyTexturePreloadSummary() {
	if (!g_preloaded || g_log == nullptr) {
		return;
	}
	const unsigned __int64 checks = g_checks, ticks = g_checkTicks;
	g_checks = 0;
	g_checkTicks = 0;
	g_log("Route: body texture preload - %u textures held after %u passes; %llu checks, %.0f ticks each", g_count, g_passes, checks,
	      checks != 0 ? static_cast<double>(ticks) / static_cast<double>(checks) : 0.0);
}

}  // namespace obng::plugin
