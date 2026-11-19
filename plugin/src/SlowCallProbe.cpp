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

constexpr double kMinimumMs = 3.0;
constexpr UInt32 kCapacity = 256;

using AnimationFn = UInt32(__thiscall*)(void*, const char*);
using TextureFn = void*(__cdecl*)(const char*, void*, UInt32);

struct Record {
	bool animation;
	UInt32 caller, thread;
	LONGLONG endTick;
	double ms;
	char name[96];
};

bool g_on = false;
LogFn g_log = nullptr;
void* g_texture = nullptr;
void* g_animation = nullptr;
double g_ticksPerMs = 0.0;
unsigned __int64 g_minimumTicks = 0;
Record g_records[kCapacity];
std::atomic<UInt32> g_kept{0}, g_dropped{0};

Record* Slot(unsigned __int64 ticks) {
	if (ticks < g_minimumTicks) {
		return nullptr;
	}
	const double ms = static_cast<double>(ticks) / g_ticksPerMs;
	if (!lw::Keep(ms, kMinimumMs, g_kept.load(std::memory_order_relaxed), kCapacity)) {
		g_dropped.fetch_add(1, std::memory_order_relaxed);
		return nullptr;
	}
	const UInt32 slot = g_kept.fetch_add(1, std::memory_order_relaxed);
	if (slot >= kCapacity) {
		return nullptr;
	}
	Record& r = g_records[slot];
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	r.endTick = now.QuadPart;
	r.ms = ms;
	r.thread = GetCurrentThreadId();
	r.name[0] = 0;
	return &r;
}

void* __cdecl SourceTexture(const char* path, void* a2, UInt32 a3) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const unsigned __int64 t0 = __rdtsc();
	void* result = reinterpret_cast<TextureFn>(g_texture)(path, a2, a3);
	if (Record* r = Slot(__rdtsc() - t0)) {
		r->animation = false;
		r->caller = caller;
		if (path != nullptr) {
			strncpy_s(r->name, path, _TRUNCATE);
		}
	}
	return result;
}

UInt32 __fastcall Animation(void* loader, void*, const char* path) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const unsigned __int64 t0 = __rdtsc();
	const UInt32 result = reinterpret_cast<AnimationFn>(g_animation)(loader, path);
	if (Record* r = Slot(__rdtsc() - t0)) {
		r->animation = true;
		r->caller = caller;
		if (path != nullptr) {
			strncpy_s(r->name, path, _TRUNCATE);
		}
	}
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
	g_on = InstallEntryDetour("source texture 0x00701E00", kSourceTexture, kSourceTextureBytes, sizeof(kSourceTextureBytes),
	                                  reinterpret_cast<const void*>(&SourceTexture), &g_texture, log);
	const bool animation = InstallEntryDetour("animation file 0x00439FF0", kAnimation, kAnimationBytes, sizeof(kAnimationBytes),
	                                          reinterpret_cast<const void*>(&Animation), &g_animation, log);
	g_on = g_on || animation;
	log("Probe: slow calls - animation file %d;", animation);
	log("Probe: slow calls - source texture %d; calls of %.1f ms or more kept (%.0f TSC ticks a ms)", g_on, kMinimumMs, g_ticksPerMs);
}

void LogSlowCallSummary() {
	if (!g_on) {
		return;
	}
	const UInt32 kept = g_kept.load(std::memory_order_relaxed);
	const UInt32 n = kept < kCapacity ? kept : kCapacity;
	for (UInt32 i = 0; i < n; ++i) {
		const Record& r = g_records[i];
		g_log("Probe: slow call - %s %.3f ms \"%s\", caller %08X, thread %u, ending at tick %lld", r.animation ? "animation file" : "source texture", r.ms, r.name,
		      r.caller, r.thread, r.endTick);
	}
	g_log("Probe: slow calls - %u kept, %u long ones not kept", n, g_dropped.exchange(0));
	g_kept.store(0, std::memory_order_relaxed);
}

}  // namespace obng::plugin
