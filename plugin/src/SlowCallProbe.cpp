#include "SlowCallProbe.h"

#include <windows.h>

#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "engine/io/LoaderWaitLogic.h"

namespace obng::plugin {

namespace {

namespace lw = obng::engine::io::loader_wait;

// 0x00701E00: a source texture made from a path, __cdecl(path, int*, byte) (read; CreateTextureImage's call).
constexpr UInt32 kSourceTexture = 0x00701E00;
constexpr UInt8 kSourceTextureBytes[] = {0x6A, 0xFF, 0x68, 0x2B, 0xA4, 0x9C, 0x00};

// 0x00439FF0: an animation file (KF) from the model loader's cache, __thiscall(loader, path), RET 4; on a miss off the
// loader's thread it pauses the loader and loads (read 2026-11-19; callers 0x00474C50 "Meshes\%s", 0x0041A610 the
// skeleton's "SpecialAnims").
constexpr UInt32 kAnimation = 0x00439FF0;
constexpr UInt8 kAnimationBytes[] = {0x6A, 0xFF, 0x68, 0x38, 0xC9, 0x9A, 0x00};

// 0x00439EB0: a model (NIF) from the model loader's cache, __thiscall(loader, path, a2, a3, a4), RET 0x10; the same
// pause on a miss (read: 0x00432860 at 0x00439F0C, the resume 0x00432890 after the load).
constexpr UInt32 kModel = 0x00439EB0;
constexpr UInt8 kModelBytes[] = {0x6A, 0xFF, 0x68, 0x08, 0xC9, 0x9A, 0x00};

// 0x006AC610: a sound made from a path, __thiscall(manager, out, path, a3, a4), RET 0x10 (read; it sends a path with
// "voice" in it to the voice player 0x006A9E00, knowledge/voice-lines.md).
constexpr UInt32 kSound = 0x006AC610;
constexpr UInt8 kSoundBytes[] = {0x6A, 0xFF, 0x68, 0xCE, 0x64, 0x9C, 0x00};

constexpr double kMinimumMs = 3.0;
constexpr UInt32 kCapacity = 256;

using TextureFn = void*(__cdecl*)(const char*, void*, UInt32);
using AnimationFn = UInt32(__thiscall*)(void*, const char*);
using ModelFn = UInt32(__thiscall*)(void*, const char*, UInt32, UInt32, UInt32);
using SoundFn = UInt32(__thiscall*)(void*, void*, const char*, UInt32, UInt32);

enum class Kind : UInt8 { Texture, Animation, Model, Sound };

struct Record {
	Kind kind;
	UInt32 caller, thread;
	LONGLONG endTick;
	double ms;
	char name[96];
};

bool g_on = false;
LogFn g_log = nullptr;
void* g_texture = nullptr;
void* g_animation = nullptr;
void* g_model = nullptr;
void* g_sound = nullptr;
double g_ticksPerMs = 0.0;
unsigned __int64 g_minimumTicks = 0;
Record g_records[kCapacity];
std::atomic<UInt32> g_kept{0}, g_dropped{0};

// A path argument copied for the log; an unreadable pointer leaves "?" (the argument's meaning is read, not settled for
// every caller).
void CopyName(char* out, std::size_t size, const char* path) {
	out[0] = 0;
	if (path == nullptr) {
		return;
	}
	__try {
		std::size_t i = 0;
		for (; i + 1 < size && path[i] != 0; ++i) {
			const unsigned char c = static_cast<unsigned char>(path[i]);
			out[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
		}
		out[i] = 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		out[0] = '?';
		out[1] = 0;
	}
}

void Note(Kind kind, UInt32 caller, unsigned __int64 ticks, const char* path) {
	if (ticks < g_minimumTicks) {
		return;
	}
	const double ms = static_cast<double>(ticks) / g_ticksPerMs;
	if (!lw::Keep(ms, kMinimumMs, g_kept.load(std::memory_order_relaxed), kCapacity)) {
		g_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	const UInt32 slot = g_kept.fetch_add(1, std::memory_order_relaxed);
	if (slot >= kCapacity) {
		return;
	}
	Record& r = g_records[slot];
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	r.kind = kind;
	r.caller = caller;
	r.endTick = now.QuadPart;
	r.ms = ms;
	r.thread = GetCurrentThreadId();
	CopyName(r.name, sizeof(r.name), path);
}

void* __cdecl SourceTexture(const char* path, void* a2, UInt32 a3) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const unsigned __int64 t0 = __rdtsc();
	void* result = reinterpret_cast<TextureFn>(g_texture)(path, a2, a3);
	Note(Kind::Texture, caller, __rdtsc() - t0, path);
	return result;
}

UInt32 __fastcall Animation(void* loader, void*, const char* path) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 result = reinterpret_cast<AnimationFn>(g_animation)(loader, path);
	Note(Kind::Animation, caller, __rdtsc() - t0, path);
	return result;
}

UInt32 __fastcall Model(void* loader, void*, const char* path, UInt32 a2, UInt32 a3, UInt32 a4) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 result = reinterpret_cast<ModelFn>(g_model)(loader, path, a2, a3, a4);
	Note(Kind::Model, caller, __rdtsc() - t0, path);
	return result;
}

UInt32 __fastcall Sound(void* manager, void*, void* out, const char* path, UInt32 a3, UInt32 a4) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 result = reinterpret_cast<SoundFn>(g_sound)(manager, out, path, a3, a4);
	Note(Kind::Sound, caller, __rdtsc() - t0, path);
	return result;
}

double TicksPerMs() {
	LARGE_INTEGER q0, q1, f;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&q0);
	const unsigned __int64 t0 = __rdtsc();
	Sleep(30);
	QueryPerformanceCounter(&q1);
	const unsigned __int64 t1 = __rdtsc();
	const double ms = static_cast<double>(q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
	return static_cast<double>(t1 - t0) / ms;
}

const char* NameOf(Kind kind) {
	switch (kind) {
	case Kind::Texture: return "source texture";
	case Kind::Animation: return "animation file";
	case Kind::Model: return "model file";
	case Kind::Sound: return "sound";
	}
	return "?";
}

bool Detour(const char* name, UInt32 address, const UInt8* bytes, UInt32 length, const void* replacement, void** original, LogFn log) {
	return InstallEntryDetour(name, address, bytes, length, replacement, original, log);
}

}  // namespace

void InstallSlowCallProbe(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Probe", "SlowCalls", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	g_ticksPerMs = TicksPerMs();
	g_minimumTicks = static_cast<unsigned __int64>(g_ticksPerMs * kMinimumMs);
	const bool texture = Detour("source texture 0x00701E00", kSourceTexture, kSourceTextureBytes, sizeof(kSourceTextureBytes),
	                            reinterpret_cast<const void*>(&SourceTexture), &g_texture, log);
	const bool animation = Detour("animation file 0x00439FF0", kAnimation, kAnimationBytes, sizeof(kAnimationBytes),
	                              reinterpret_cast<const void*>(&Animation), &g_animation, log);
	const bool model = Detour("model file 0x00439EB0", kModel, kModelBytes, sizeof(kModelBytes), reinterpret_cast<const void*>(&Model), &g_model, log);
	const bool sound = Detour("sound 0x006AC610", kSound, kSoundBytes, sizeof(kSoundBytes), reinterpret_cast<const void*>(&Sound), &g_sound, log);
	g_on = texture || animation || model || sound;
	log("Probe: slow calls - source texture %d, animation file %d, model file %d, sound %d; calls of %.1f ms or more kept (%.0f TSC ticks a ms)",
	    texture, animation, model, sound, kMinimumMs, g_ticksPerMs);
}

void LogSlowCallSummary() {
	if (!g_on) {
		return;
	}
	const UInt32 kept = g_kept.load(std::memory_order_relaxed);
	const UInt32 n = kept < kCapacity ? kept : kCapacity;
	for (UInt32 i = 0; i < n; ++i) {
		const Record& r = g_records[i];
		g_log("Probe: slow call - %s %.3f ms \"%s\", caller %08X, thread %u, ending at tick %lld", NameOf(r.kind), r.ms, r.name, r.caller, r.thread,
		      r.endTick);
	}
	g_log("Probe: slow calls - %u kept, %u long ones not kept", n, g_dropped.exchange(0));
	g_kept.store(0, std::memory_order_relaxed);
}

}  // namespace obng::plugin
