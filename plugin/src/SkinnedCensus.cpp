// [Probe] SkinnedCensus (SkinnedCensusLogic.h): the stereo route's skinned
// draw entry hands every call here; calls inside the eye pass (the occlusion
// culler's bracket, so the probe needs [Route] OcclusionCull=1) are noted by
// the skin's root, the geometry and the shader's class. Render thread.

#include <windows.h>

#include "OcclusionCull.h"
#include "Probes.h"
#include "SkinnedCensusLogic.h"

namespace obng::plugin {
namespace {

namespace sc = skinned_census;

constexpr UInt32 kSkinRootParentOffset = 0x10;  // NiSkinInstance's root parent (read 0x0072BB9C, src/engine/scene/SceneUpdate.h)
constexpr UInt32 kGeometryShaderOffset = 0xBC;  // NiGeometry's shader, the draw functions' candidate (re/functions/0x00767520.md)

bool g_on = false;
LogFn g_log = nullptr;
sc::Frame g_frame;
sc::Totals g_totals;
UInt32 g_pass = 0;

UInt32 ReadDword(UInt32 address) {
	return *reinterpret_cast<volatile const UInt32*>(static_cast<std::uintptr_t>(address));
}

}  // namespace

void InstallSkinnedCensus(const char* iniPath, LogFn log) {
	g_on = GetPrivateProfileIntA("Probe", "SkinnedCensus", 0, iniPath) != 0;
	g_log = log;
	if (g_on) {
		log("Probe: skinned census on ([Probe] SkinnedCensus) - every skinned draw call inside the eye pass by its skin's root, its geometry and "
		    "its shader's class");
	}
}

void SkinnedCensusNote(UInt32 geometry, UInt32 skin) {
	OcclusionCullNoteDraw(geometry);
	if (!g_on || geometry == 0 || skin == 0 || !OcclusionCullInEyePass()) {
		return;
	}
	const UInt32 pass = OcclusionCullEyePasses();
	if (pass != g_pass) {
		if (!g_frame.Empty()) {
			g_frame.End(&g_totals);
		}
		g_pass = pass;
	}
	const UInt32 shader = ReadDword(geometry + kGeometryShaderOffset);
	g_frame.Note(ReadDword(skin + kSkinRootParentOffset), geometry, shader != 0 ? ReadDword(shader) : 0);
}

void LogSkinnedCensusSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	char line[700];
	sc::Format(g_totals, line, sizeof(line));
	g_log("%s", line);
	g_totals = sc::Totals{};
}

}  // namespace obng::plugin
