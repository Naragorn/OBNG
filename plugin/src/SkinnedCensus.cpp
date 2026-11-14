// [Probe] SkinnedCensus (SkinnedCensusLogic.h): the stereo route's skinned
// draw entry hands every call here; calls inside the eye pass (the occlusion
// culler's bracket, so the probe needs [Route] OcclusionCull=1) are noted by
// the actor's fade node, the geometry and the shader's class. Render thread.

#include <windows.h>

#include "OcclusionCull.h"
#include "Probes.h"
#include "SkinnedCensusLogic.h"

namespace obng::plugin {
namespace {

namespace sc = skinned_census;

// The actor a part belongs to: the nearest BSFadeNode above the geometry (an actor's 3D root is one; vtable
// 0x00A3F944, knowledge of the occlusion culler's guard), the parent at NiAVObject +0x1C. The skin instance's root
// parent +0x10 is not it: in the gate horde it gave 468 "actors" for 733 calls (run 20261114-213129) - a node per part.
constexpr UInt32 kObjectParentOffset = 0x1C;
constexpr UInt32 kBSFadeNodeVtable = 0x00A3F944;
constexpr unsigned kLevels = 12;
constexpr UInt32 kGeometryShaderOffset = 0xBC;  // NiGeometry's shader, the draw functions' candidate (re/functions/0x00767520.md)

bool g_on = false;
LogFn g_log = nullptr;
sc::Frame g_frame;
sc::Totals g_totals;
UInt32 g_pass = 0;

UInt32 ReadDword(UInt32 address) {
	return *reinterpret_cast<volatile const UInt32*>(static_cast<std::uintptr_t>(address));
}

// The nearest fade node above the geometry; the highest ancestor reached when there is none within kLevels.
UInt32 ActorOf(UInt32 geometry) {
	UInt32 node = geometry;
	for (unsigned i = 0; i < kLevels; ++i) {
		const UInt32 parent = ReadDword(node + kObjectParentOffset);
		if (parent == 0) {
			break;
		}
		node = parent;
		if (ReadDword(node) == kBSFadeNodeVtable) {
			break;
		}
	}
	return node;
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
	g_frame.Note(ActorOf(geometry), geometry, shader != 0 ? ReadDword(shader) : 0);
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
