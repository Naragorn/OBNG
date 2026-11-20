#include "AnimationPauseBatch.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "AnimationLazyAppend.h"
#include "AnimationNameArena.h"
#include "Memory.h"
#include "engine/io/AnimationPauseBatchLogic.h"

namespace obng::plugin {

namespace {

namespace apb = obng::engine::io::animation_pause_batch;

constexpr UInt32 kSetup = 0x00475D80;                                      // __thiscall(this, list, actor, ...), RET 0xC (read)
constexpr UInt8 kSetupBytes[] = {0x6A, 0xFF, 0x68, 0x8B, 0xEE, 0x9A, 0x00};  // push -1; push 0x009AEE8B
constexpr UInt32 kResumeSite = 0x0043A088;                                 // call 0x00432890 in the KF load (read; ECX = *0x00B33A10)
constexpr UInt8 kResumeBytes[] = {0xE8, 0x03, 0x88, 0xFF, 0xFF};
constexpr UInt32 kResume = 0x00432890;                                     // __thiscall(io manager) (read)

using SetupFn = char(__thiscall*)(void*, void*, void*, void*);
using ResumeFn = void(__thiscall*)(void*);

void* g_setup = nullptr;
LogFn g_log = nullptr;
bool g_on = false;
thread_local apb::Batch t_batch;
thread_local void* t_io = nullptr;
std::atomic<unsigned long long> g_setups{0}, g_held{0}, g_batched{0};

void __fastcall Resume(void* io, void*) {
	if (apb::HoldResume(t_batch)) {
		t_io = io;
		g_held.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	reinterpret_cast<ResumeFn>(kResume)(io);
}

char __fastcall Setup(void* self, void*, void* list, void* actor, void* third) {
	apb::Enter(t_batch);
	AnimationSetupBegins();  // [Route] AnimationLazyAppend keeps a list's end for the time of this set up
	const char result = reinterpret_cast<SetupFn>(g_setup)(self, list, actor, third);
	AnimationSetupEnds();
	AnimationNameArenaSetupEnds(list);  // [Route] AnimationNameArena: the list this set up was called with goes back to its block
	const UInt32 give = apb::Leave(t_batch);
	if (give > 1) {
		g_batched.fetch_add(1, std::memory_order_relaxed);
	}
	for (UInt32 i = 0; i < give; ++i) {
		reinterpret_cast<ResumeFn>(kResume)(t_io);
	}
	g_setups.fetch_add(1, std::memory_order_relaxed);
	return result;
}

}  // namespace

void InstallAnimationPauseBatch(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	if (GetPrivateProfileIntA("Route", "AnimationPauseBatch", 0, iniPath) != 1) {
		return;
	}
	g_log = log;
	if (!Verify(kResumeSite, kResumeBytes, sizeof(kResumeBytes))) {
		log("Route: animation pause batch - the resume's call at %08X is not the game's own; not routed", kResumeSite);
		return;
	}
	if (!InstallEntryDetour("animation setup 0x00475D80", kSetup, kSetupBytes, sizeof(kSetupBytes), reinterpret_cast<const void*>(&Setup), &g_setup,
	                        log, DetourRole::Route)) {
		return;
	}
	UInt8 patch[5] = {0xE8};
	const UInt32 rel = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&Resume)) - (kResumeSite + 5);
	std::memcpy(patch + 1, &rel, 4);
	if (!SafeWrite(kResumeSite, patch, sizeof(patch))) {
		log("Route: animation pause batch - the resume's call at %08X could not be written; the setup's detour stays, holding nothing", kResumeSite);
		return;
	}
	g_on = true;
	log("Route: animation pause batch - an actor's animation setup keeps the loader paused from its first KF miss to its end");
}

void LogAnimationPauseBatchSummary() {
	if (!g_on) {
		return;
	}
	g_log("Route: animation pause batch - %llu setups, %llu resumes held to a setup's end, %llu setups with more than one miss",
	      g_setups.exchange(0), g_held.exchange(0), g_batched.exchange(0));
}

}  // namespace obng::plugin
