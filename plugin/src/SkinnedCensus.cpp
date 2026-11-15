// [Probe] SkinnedCensus (SkinnedCensusLogic.h): the stereo route's skinned
// draw entry hands every call here; calls inside the eye pass (the occlusion
// culler's bracket, so the probe needs [Route] OcclusionCull=1) are noted by
// the actor's fade node, the geometry and the shader's class - at the entry's
// end, with the textures (stages 0, 1, 6, 7) and the programs the device
// holds then (a draw in several partitions: its last one's) as the call's
// look, and the time the entry took. Render thread.

#include <windows.h>

#include <intrin.h>

#include "DrawInstancingDevice.h"
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
unsigned long long g_ticks = 0;
LARGE_INTEGER g_clock{};
unsigned long long g_stamp = 0;
constexpr UInt32 kRenderer = 0x00B3F928;       // g_renderer (xOBSE, OBVR GameAddresses.h)
constexpr UInt32 kRendererDeviceOffset = 0x280;

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

// A number for the textures and programs the device holds (FNV-1a over their identities); 0 is kept for "not told".
UInt32 LookNow() {
	const UInt32 renderer = ReadDword(kRenderer);
	const UInt32 device = renderer != 0 ? ReadDword(renderer + kRendererDeviceOffset) : 0;
	static draw_instancing::DeviceState state;  // the render thread alone comes here
	if (device == 0 || !draw_instancing::ReadDeviceState(device, &state)) {
		return 0;
	}
	const UInt32 words[6] = {state.textures[0], state.textures[1], state.textures[2], state.textures[3], state.vertexShader, state.pixelShader};
	UInt32 hash = 2166136261u;
	for (UInt32 word : words) {
		for (int byte = 0; byte < 4; ++byte) {
			hash = (hash ^ ((word >> (8 * byte)) & 0xFFu)) * 16777619u;
		}
	}
	return hash != 0 ? hash : 1u;
}

void SkinnedCensusNote(UInt32 geometry, UInt32 /*skin*/) { OcclusionCullNoteDraw(geometry); }

unsigned long long SkinnedCensusClock() { return g_on ? __rdtsc() | 1u : 0; }

void SkinnedCensusDrawn(UInt32 geometry, UInt32 skin, unsigned long long startTicks) {
	const unsigned long long took = __rdtsc() - startTicks;
	if (!g_on || geometry == 0 || skin == 0 || !OcclusionCullInEyePass()) {
		return;
	}
	g_ticks += took;
	const UInt32 pass = OcclusionCullEyePasses();
	if (pass != g_pass) {
		if (!g_frame.Empty()) {
			g_frame.End(&g_totals);
		}
		g_pass = pass;
	}
	const UInt32 shader = ReadDword(geometry + kGeometryShaderOffset);
	g_frame.Note(ActorOf(geometry), geometry, shader != 0 ? ReadDword(shader) : 0, LookNow());
}

void LogSkinnedCensusSummary() {
	if (!g_on || g_log == nullptr) {
		return;
	}
	char line[700];
	sc::Format(g_totals, line, sizeof(line));
	g_log("%s", line);
	// The window's ticks in milliseconds, by the performance counter over the same window.
	LARGE_INTEGER clock;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&clock);
	QueryPerformanceFrequency(&frequency);
	const unsigned long long stamp = __rdtsc();
	const double windowMs = g_stamp != 0 ? static_cast<double>(clock.QuadPart - g_clock.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
	const double ticksPerMs = windowMs > 0.0 ? static_cast<double>(stamp - g_stamp) / windowMs : 0.0;
	sc::FormatLooks(g_totals, ticksPerMs > 0.0 ? static_cast<double>(g_ticks) / ticksPerMs : -1.0, line, sizeof(line));
	g_log("%s", line);
	g_clock = clock;
	g_stamp = stamp;
	g_ticks = 0;
	g_totals = sc::Totals{};
}

}  // namespace obng::plugin
