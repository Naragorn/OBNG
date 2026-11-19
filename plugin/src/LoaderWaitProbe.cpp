#include "LoaderWaitProbe.h"

#include <windows.h>

#include <intrin.h>
#include <atomic>
#include <cstring>

#include "engine/io/LoaderWaitLogic.h"

namespace obng::plugin {

namespace {

namespace lw = obng::engine::io::loader_wait;

// 0x004322B0: __thiscall(io manager) -> bool, no stack arguments, RET (read 2026-11-19).
constexpr UInt32 kPauseAll = 0x004322B0;
constexpr UInt8 kPauseAllBytes[] = {0x55, 0x8B, 0xE9, 0x8D, 0x45, 0x20};  // push ebp; mov ebp,ecx; lea eax,[ebp+20]
// 0x00431F50: __thiscall(loader thread), no stack arguments, RET (read).
constexpr UInt32 kWaitOne = 0x00431F50;
constexpr UInt8 kWaitOneBytes[] = {0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x0C};  // push esi; push edi; mov edi,ecx; mov eax,[edi+c]
// 0x00442890: four stack arguments, RET 0x10 (read); the second is the texture's path (read: [esp+0x30] after the
// prologue's 0x28 bytes of pushes, handed to the cache lookup at 0x004428D1).
constexpr UInt32 kCreateImage = 0x00442890;
constexpr UInt8 kCreateImageBytes[] = {0x6A, 0xFF, 0x68, 0x41, 0xD1, 0x9A, 0x00};  // push -1; push 0x009AD141

constexpr double kMinimumMs = 0.5;
constexpr UInt32 kCapacity = 512;

using ThisFn = UInt32(__thiscall*)(void*);
using CreateFn = void*(__stdcall*)(void*, const char*, UInt32, UInt32);

enum class Kind : UInt8 { PauseAll, WaitOne, CreateImage };

struct Record {
	Kind kind;
	UInt32 caller, thread;
	LONGLONG endTick;
	double ms;
	char path[96];
};

bool g_on = false;
LogFn g_log = nullptr;
void* g_pauseAll = nullptr;
void* g_waitOne = nullptr;
void* g_createImage = nullptr;
LARGE_INTEGER g_frequency{};
Record g_records[kCapacity];
std::atomic<UInt32> g_kept{0}, g_dropped{0};
std::atomic<UInt32> g_calls[3]{};

LONGLONG Now() {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

void Note(Kind kind, UInt32 caller, LONGLONG start, LONGLONG end, const char* path) {
	g_calls[static_cast<int>(kind)].fetch_add(1, std::memory_order_relaxed);
	const double ms = static_cast<double>(end - start) * 1000.0 / static_cast<double>(g_frequency.QuadPart);
	const UInt32 kept = g_kept.load(std::memory_order_relaxed);
	if (!lw::Keep(ms, kMinimumMs, kept, kCapacity)) {
		if (ms >= kMinimumMs) {
			g_dropped.fetch_add(1, std::memory_order_relaxed);
		}
		return;
	}
	const UInt32 slot = g_kept.fetch_add(1, std::memory_order_relaxed);
	if (slot >= kCapacity) {
		g_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	Record& r = g_records[slot];
	r.kind = kind;
	r.caller = caller;
	r.thread = GetCurrentThreadId();
	r.endTick = end;
	r.ms = ms;
	r.path[0] = 0;
	if (path != nullptr) {
		strncpy_s(r.path, path, _TRUNCATE);
	}
}

UInt32 __fastcall PauseAll(void* io, void*) {
	const UInt32* frame = static_cast<const UInt32*>(_AddressOfReturnAddress());
	const UInt32 caller = lw::CallerOf(frame[0], frame[1]);
	const LONGLONG start = Now();
	const UInt32 result = reinterpret_cast<ThisFn>(g_pauseAll)(io);
	Note(Kind::PauseAll, caller, start, Now(), nullptr);
	return result;
}

UInt32 __fastcall WaitOne(void* thread, void*) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const LONGLONG start = Now();
	const UInt32 result = reinterpret_cast<ThisFn>(g_waitOne)(thread);
	Note(Kind::WaitOne, caller, start, Now(), nullptr);
	return result;
}

void* __stdcall CreateImage(void* out, const char* path, UInt32 a3, UInt32 a4) {
	const UInt32 caller = reinterpret_cast<UInt32>(_ReturnAddress());
	const LONGLONG start = Now();
	void* result = reinterpret_cast<CreateFn>(g_createImage)(out, path, a3, a4);
	Note(Kind::CreateImage, caller, start, Now(), path);
	return result;
}

const char* NameOf(Kind kind) {
	switch (kind) {
	case Kind::PauseAll: return "pause all 0x004322B0";
	case Kind::WaitOne: return "wait IO 0x00431F50";
	case Kind::CreateImage: return "texture on this thread 0x00442890";
	}
	return "?";
}

}  // namespace

void InstallLoaderWaitProbe(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Probe", "LoaderWaits", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	const bool a = InstallEntryDetour("loader pause 0x004322B0", kPauseAll, kPauseAllBytes, sizeof(kPauseAllBytes),
	                                  reinterpret_cast<const void*>(&PauseAll), &g_pauseAll, log);
	const bool b = InstallEntryDetour("loader IO wait 0x00431F50", kWaitOne, kWaitOneBytes, sizeof(kWaitOneBytes),
	                                  reinterpret_cast<const void*>(&WaitOne), &g_waitOne, log);
	const bool c = InstallEntryDetour("texture image 0x00442890", kCreateImage, kCreateImageBytes, sizeof(kCreateImageBytes),
	                                  reinterpret_cast<const void*>(&CreateImage), &g_createImage, log);
	g_on = a || b || c;
	log("Probe: loader waits - pause %d, IO wait %d, texture image %d; waits of %.1f ms or more kept (at most %u a window)", a, b, c,
	    kMinimumMs, kCapacity);
}

void LogLoaderWaitSummary() {
	if (!g_on) {
		return;
	}
	const UInt32 kept = g_kept.load(std::memory_order_relaxed);
	const UInt32 n = kept < kCapacity ? kept : kCapacity;
	for (UInt32 i = 0; i < n; ++i) {
		const Record& r = g_records[i];
		g_log("Probe: loader wait - %s %.3f ms, caller %08X, thread %u, ending at tick %lld%s%s%s", NameOf(r.kind), r.ms, r.caller, r.thread,
		      r.endTick, r.path[0] ? " - \"" : "", r.path, r.path[0] ? "\"" : "");
	}
	g_log("Probe: loader waits - calls: pause %u, IO wait %u, texture image %u; %u kept, %u long ones not kept",
	      g_calls[0].exchange(0), g_calls[1].exchange(0), g_calls[2].exchange(0), n, g_dropped.exchange(0));
	g_kept.store(0, std::memory_order_relaxed);
}

}  // namespace obng::plugin
