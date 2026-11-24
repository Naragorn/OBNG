#include "Lighting30Kind.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "Memory.h"
#include "engine/render/Lighting30KindLogic.h"

namespace obng::plugin {

namespace {

namespace l30 = obng::engine::render::lighting30_kind;

using ChooserFn = int(__cdecl*)(UInt32 geometry, UInt32 a, UInt32 b);

LogFn g_log = nullptr;
bool g_on = false;
std::uint32_t g_bits = 0;
l30::Counts g_counts;  // written where geometry is prepared (the main thread and the loader's); a lost count is a count, not a fault

// In place of the object preparation's call of the chooser 0x007D1920.
int __cdecl ChooseKind(UInt32 geometry, UInt32 a, UInt32 b) {
	const int chosen = reinterpret_cast<ChooserFn>(static_cast<std::uintptr_t>(l30::kChooser))(geometry, a, b);
	const bool has30 = *reinterpret_cast<const UInt8*>(static_cast<std::uintptr_t>(l30::kHas30)) != 0;
	const int kind = l30::KindFor(chosen, has30, g_bits);
	l30::Count(g_counts, chosen, kind);
	return kind;
}

}  // namespace

void InstallLighting30Kind(const char* iniPath, LogFn log) {
	static bool s_done = false;
	if (s_done || iniPath == nullptr) {
		return;
	}
	s_done = true;
	g_bits = l30::BitsOf(static_cast<int>(GetPrivateProfileIntA("Probe", "Lighting30Kinds", l30::Shipped(), iniPath)));
	if (!l30::On(g_bits)) {
		return;
	}
	UInt8 expected[5] = {0xE8}, patch[5] = {0xE8};
	const UInt32 toOriginal = l30::kChooser - (l30::kCallSite + 5);
	const UInt32 toProbe = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(&ChooseKind)) - (l30::kCallSite + 5);
	std::memcpy(expected + 1, &toOriginal, 4);
	std::memcpy(patch + 1, &toProbe, 4);
	if (!Verify(l30::kCallSite, expected, sizeof(expected))) {
		log("Probe: Lighting30 kinds - the chooser's call at %08X is not the game's own; nothing moved", l30::kCallSite);
		return;
	}
	if (!SafeWrite(l30::kCallSite, patch, sizeof(patch))) {
		log("Probe: Lighting30 kinds - the chooser's call at %08X could not be written; nothing moved", l30::kCallSite);
		return;
	}
	g_log = log;
	g_on = true;
	log("Probe: Lighting30 kinds - on at %08X: geometry the game gives %s%s%s is prepared with its one-pass Lighting30Shader instead "
	    "(when the card's flag at %08X is set). The picture changes - no actor shadow falls on moved geometry: a measuring run only",
	    l30::kCallSite, (g_bits & l30::kMoveShadowLight) != 0 ? "ShadowLightShader " : "", (g_bits & l30::kMoveSkin) != 0 ? "SkinShader " : "",
	    (g_bits & l30::kMoveParallax) != 0 ? "ParallaxShader " : "", l30::kHas30);
}

void LogLighting30KindSummary() {
	if (!g_on) {
		return;
	}
	const bool has30 = *reinterpret_cast<const UInt8*>(static_cast<std::uintptr_t>(l30::kHas30)) != 0;
	g_log("Probe: Lighting30 kinds - since the start %llu geometries chosen for (the game's choice: ShadowLightShader %llu, SkinShader %llu, "
	      "ParallaxShader %llu, Lighting30Shader %llu, others %llu), %llu moved to Lighting30Shader; the card's flag is %s",
	      g_counts.asked, g_counts.shadowLight, g_counts.skin, g_counts.parallax, g_counts.lighting30, g_counts.other, g_counts.moved,
	      has30 ? "set" : "clear - the game makes no Lighting30Shader, nothing is moved");
}

}  // namespace obng::plugin
