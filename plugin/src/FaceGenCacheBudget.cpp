#include "FaceGenCacheBudget.h"

#include <windows.h>

#include <cstdint>

#include "StepHooks.h"
#include "engine/facegen/FaceGenCacheBudgetLogic.h"

namespace obng::plugin {

namespace {

namespace fcb = obng::engine::facegen::cache_budget;

constexpr UInt32 kEgmSetting = 0x00B120EC;  // uiFaceGenMaxEGMDataSize:General, the value (read; its name pointer at +4)
constexpr UInt32 kEgtSetting = 0x00B120F4;  // uiFaceGenMaxEGTDataSize:General
constexpr UInt32 kFaceGen = 0x00B39B80;     // FaceGen's manager; its model cache at +0xDAC (read: 0x00553620)
constexpr UInt32 kCacheOffset = 0xDAC, kEgmLimit = 0x18, kEgtLimit = 0x1C;
constexpr UInt32 kDataSize = 0x0054F890;    // __thiscall(cache, kind): the kind's data in bytes, under the cache's lock (read)

using SizeFn = UInt32(__thiscall*)(void* cache, UInt32 kind);

LogFn g_log = nullptr;
bool g_on = false, g_set = false;
fcb::Budget g_egm, g_egt;
UInt32 g_gameEgm = 0, g_gameEgt = 0, g_peakEgm = 0, g_peakEgt = 0;

UInt32& At(UInt32 address) {
	return *reinterpret_cast<UInt32*>(static_cast<std::uintptr_t>(address));
}

UInt32 Cache() {
	const UInt32 faceGen = At(kFaceGen);
	return faceGen != 0 ? At(faceGen + kCacheOffset) : 0;
}

// At the first update step: the game has read its INI; a cache made later copies the settings, one made already
// gets its limits here (a 32-bit store; a purge on another thread reads the old or the new one).
void SetOnce() {
	if (g_set) {
		return;
	}
	g_set = true;
	g_gameEgm = At(kEgmSetting);
	g_gameEgt = At(kEgtSetting);
	const UInt32 egm = fcb::LimitOf(g_egm, g_gameEgm), egt = fcb::LimitOf(g_egt, g_gameEgt);
	At(kEgmSetting) = egm;
	At(kEgtSetting) = egt;
	const UInt32 cache = Cache();
	if (cache != 0) {
		At(cache + kEgmLimit) = egm;
		At(cache + kEgtLimit) = egt;
	}
	g_log("Route: FaceGen cache - the limits are %u MB for shapes (the game's %u) and %u MB for textures (the game's %u); the cache %s",
	      fcb::MbOf(egm), fcb::MbOf(g_gameEgm), fcb::MbOf(egt), fcb::MbOf(g_gameEgt), cache != 0 ? "was made already and has them" : "is not made yet and will copy them");
}

}  // namespace

void InstallFaceGenCacheBudget(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_egm = fcb::BudgetOf(static_cast<int>(GetPrivateProfileIntA("Route", "FaceGenCacheEgmMB", fcb::kShippedEgmMb, iniPath)));
	g_egt = fcb::BudgetOf(static_cast<int>(GetPrivateProfileIntA("Route", "FaceGenCacheEgtMB", fcb::kShippedEgtMb, iniPath)));
	if (!g_egm.routed && !g_egt.routed) {
		log("Route: FaceGen cache - off in OBNG.ini; the game's own limits (uiFaceGenMaxEGMDataSize, uiFaceGenMaxEGTDataSize)");
		return;
	}
	g_log = log;
	if (!AddStepHook(&SetOnce, "the FaceGen cache's limits (once)", log)) {
		log("Route: FaceGen cache - no step hook; the game's own limits");
		return;
	}
	g_on = true;
}

void LogFaceGenCacheSummary() {
	if (!g_on || !g_set) {
		return;
	}
	const UInt32 cache = Cache();
	if (cache == 0) {
		return;
	}
	const UInt32 egm = reinterpret_cast<SizeFn>(kDataSize)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(cache)), 0);
	const UInt32 egt = reinterpret_cast<SizeFn>(kDataSize)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(cache)), 1);
	g_peakEgm = egm > g_peakEgm ? egm : g_peakEgm;
	g_peakEgt = egt > g_peakEgt ? egt : g_peakEgt;
	g_log("Route: FaceGen cache - shapes %u of %u MB (most %u), textures %u of %u MB (most %u)", fcb::MbOf(egm), fcb::MbOf(At(cache + kEgmLimit)),
	      fcb::MbOf(g_peakEgm), fcb::MbOf(egt), fcb::MbOf(At(cache + kEgtLimit)), fcb::MbOf(g_peakEgt));
}

}  // namespace obng::plugin
