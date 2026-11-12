#include "ShadowReceiverFilter.h"

#include <windows.h>

#include <cstdlib>

#include "RangeWalkGame.h"
#include "engine/render/ShadowReceiverFilterLogic.h"

namespace obng::plugin {

namespace {

namespace filter = obng::engine::render::shadow_receiver_filter;
namespace game = range_walk_game;

// Read 2026-11-12 (knowledge/actor-shadows.md, knowledge/culling-and-draw-setup.md).
constexpr UInt32 kLightCastsShadow = 0xF4;  // byte, set by 0x007D3370 (1 from the caster registration 0x007C6C30)
constexpr UInt32 kLightCaster = 0x130;      // the caster's node
constexpr UInt32 kBound = 0x20;             // NiAVObject's world bound: centre +0x20..+0x28, radius +0x2C
constexpr UInt32 kGeometrySkin = 0xB8;      // NiGeometry's skin instance (DrawInstancing.cpp)
constexpr UInt32 kClockMs = 0x00B33EA0;     // TimeInfo's clockMs: moves once a frame

LogFn g_log = nullptr;
bool g_installed = false;
filter::Settings g_settings;
filter::Counts g_counts;
void* g_original = nullptr;
UInt32 g_window = 0;
bool g_onNow = true;
UInt32 g_lastClock = 0;
std::uint64_t g_frames = 0;
volatile LONG g_summaryRequested = 0;

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

void WriteSummary() {
	char line[1024];
	filter::FormatSummary(g_counts, g_settings, g_onNow, g_frames, line, sizeof(line));
	g_log("%s", line);
	g_counts = filter::Counts{};
	g_frames = 0;
	++g_window;
	g_onNow = filter::OnInWindow(g_settings, g_window);
}

// AddGeometry's replacement: a shadow light's small receiver is left out.
UInt32 __fastcall AddGeometry(UInt32 light, void*, UInt32 geometry) {
	if (InterlockedExchange(&g_summaryRequested, 0) != 0) {
		WriteSummary();
	}
	const UInt32 clock = Dword(kClockMs);
	if (clock != g_lastClock) {
		g_lastClock = clock;
		++g_frames;
	}
	filter::Receiver r;
	if (light != 0 && geometry != 0) {
		const UInt32 caster = Dword(light + kLightCaster);
		r.castsShadow = *reinterpret_cast<const UInt8*>(light + kLightCastsShadow) != 0;
		r.hasCaster = caster != 0;
		if (r.castsShadow && r.hasCaster) {
			const float* receiverBound = reinterpret_cast<const float*>(geometry + kBound);
			r.radius = receiverBound[3];
			r.gap = filter::GapOf(reinterpret_cast<const float*>(caster + kBound), receiverBound, receiverBound[3]);
			r.skinned = Dword(geometry + kGeometrySkin) != 0;
		}
	}
	const filter::Action action = filter::Decide(g_settings, r, g_onNow);
	g_counts.Note(r, action);
	if (action == filter::Action::kSkip) {
		return 0;  // EAX: AddGeometry's own result is not read by its callers (after 0x007D5B01 and 0x007C6207, read)
	}
	return reinterpret_cast<game::AddGeometryFn>(g_original)(light, nullptr, geometry);
}

}  // namespace

void InstallShadowReceiverFilter(const char* iniPath, LogFn log) {
	if (g_installed) {
		return;
	}
	char radius[32] = {};
	char gap[32] = {};
	GetPrivateProfileStringA("Route", "ActorShadowReceiverMin", "0", radius, sizeof(radius), iniPath);
	GetPrivateProfileStringA("Route", "ActorShadowReceiverMaxGap", "0", gap, sizeof(gap), iniPath);
	g_settings = filter::SettingsFrom(std::strtof(radius, nullptr), std::strtof(gap, nullptr),
	                                  static_cast<int>(GetPrivateProfileIntA("Route", "ActorShadowReceiverAlternate", 0, iniPath)));
	if (!g_settings.On()) {
		return;
	}
	g_log = log;
	if (!InstallEntryDetour("ShadowSceneLight::AddGeometry (actor shadow receivers)", game::kAddGeometry, game::kAddGeometryBytes, sizeof(game::kAddGeometryBytes),
	                        reinterpret_cast<const void*>(&AddGeometry), &g_original, log, DetourRole::Route)) {
		return;
	}
	g_installed = true;
	g_onNow = filter::OnInWindow(g_settings, 0);
	log("Route: actor shadow receivers - a geometry under %.1f units of radius, or more than %.1f units from the caster, gets no actor shadow (0 = "
	    "that rule off)%s",
	    g_settings.minRadius, g_settings.maxGap, g_settings.alternate ? "; alternating by summary window" : "");
}

void LogShadowReceiverFilterSummary() {
	if (g_installed) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
