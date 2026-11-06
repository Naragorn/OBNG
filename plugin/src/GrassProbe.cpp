// The grass probe ([Probe] Grass). The decisions and the reasoning are
// GrassProbeLogic.h's; this file reads the game.
//
// Addresses: the Grass node the build hangs its patches under, [0x00B36094]
// (OBVR GrassCone.h, from its reading of the build 0x004EBDA0: the node's
// children are patch groups, their children the patches); a node's children
// at +0xB0 (the array) and +0xB6 (the count, a word) and the hidden bit at
// +0x18 bit 0 (OBVR GameAddresses.h; re/functions/0x0040C830.md reads both);
// the parent at +0x1C and the world bound at +0x20 (re/functions/
// 0x0055D4C0.md, 0x007693E0.md); the renderer [0x00B3F928] with its view
// matrix at +0x980 and the origin global 0x00B3F92C (re/functions/
// 0x00762640.md).
//
// Render thread only.

#include "GrassProbe.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "GrassProbeLogic.h"

namespace obng::plugin {

bool g_grassProbeOn = false;

namespace {

namespace gr = grass;

constexpr UInt32 kGrassNodeGlobal = 0x00B36094;
constexpr UInt32 kRendererGlobal = 0x00B3F928;
constexpr UInt32 kRendererViewOffset = 0x980;
constexpr UInt32 kOriginGlobal = 0x00B3F92C;
constexpr UInt32 kObjectFlagsOffset = 0x18;
constexpr UInt32 kObjectParentOffset = 0x1C;
constexpr UInt32 kObjectBoundOffset = 0x20;
constexpr UInt32 kNodeChildrenOffset = 0xB0;
constexpr UInt32 kNodeChildCountOffset = 0xB6;
constexpr UInt32 kMaxChildren = 4096;
constexpr UInt32 kAncestorsWalked = 4;  // a patch's geometry, the patch, its group, the Grass node
constexpr std::uint64_t kWindowMs = 100;

LogFn g_log = nullptr;
gr::Pass g_pass;       // the draws since the last boundary
// The world pass's camera, taken at a draw inside eye 0's viewport: at the
// boundary itself the renderer holds whatever pass came last (the interface's
// camera at the origin, run obng-grass-turn-single/20261105-205351).
bool g_cameraKnown = false;
float g_view[16] = {};
float g_origin[3] = {};
gr::Writer g_writer;
std::uint64_t g_lastBoundaryMs = 0;
std::uint64_t g_lastEyeBoundaryMs = 0;
bool g_noGrassNodeLogged = false;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

std::uint64_t NowMs() {
	return GetTickCount64();
}

// The distance of an object's world bound's centre from a camera position.
float DistanceOf(UInt32 object, const float camera[3]) {
	const float x = ReadAt<float>(object + kObjectBoundOffset) - camera[0];
	const float y = ReadAt<float>(object + kObjectBoundOffset + 4) - camera[1];
	const float z = ReadAt<float>(object + kObjectBoundOffset + 8) - camera[2];
	return std::sqrt(x * x + y * y + z * z);
}

// Keeps the camera of the pass that is being drawn (the renderer's view
// matrix and the origin global), once per pass.
void TakeCamera() {
	if (g_cameraKnown) {
		return;
	}
	const UInt32 renderer = ReadAt<UInt32>(kRendererGlobal);
	if (renderer == 0) {
		return;
	}
	std::memcpy(g_view, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(renderer + kRendererViewOffset)), sizeof(g_view));
	std::memcpy(g_origin, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kOriginGlobal)), sizeof(g_origin));
	g_cameraKnown = true;
}

// Whether the object's class name (its vtable's RTTI) ends in "Node" - the
// test OBVR's FirstPersonHide makes. False when anything on the way cannot
// be read.
bool IsNode(UInt32 object) {
	__try {
		const UInt32 vtable = ReadAt<UInt32>(object);
		const UInt32 locator = ReadAt<UInt32>(vtable - 4);
		const UInt32 type = ReadAt<UInt32>(locator + 12);
		const char* name = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(type + 8));
		std::size_t length = 0;
		while (length < 96 && name[length] != '\0') {
			++length;
		}
		while (length > 0 && name[length - 1] == '@') {
			--length;
		}
		return length >= 4 && std::memcmp(name + length - 4, "Node", 4) == 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Calls visit(child) for the node's children that are there; returns their number.
template <class Visit>
UInt32 ForChildren(UInt32 node, Visit&& visit) {
	const UInt32 children = ReadAt<UInt32>(node + kNodeChildrenOffset);
	const UInt32 count = ReadAt<UInt16>(node + kNodeChildCountOffset);
	if (children == 0 || count > kMaxChildren) {
		return 0;
	}
	UInt32 present = 0;
	for (UInt32 i = 0; i < count; ++i) {
		const UInt32 child = ReadAt<UInt32>(children + i * 4);
		if (child != 0) {
			++present;
			visit(child);
		}
	}
	return present;
}

void CountPatch(UInt32 patch, bool groupHidden, gr::Pass& pass) {
	const float distance = DistanceOf(patch, pass.camera);
	pass.existing.Add(distance);
	if (groupHidden || (ReadAt<UInt16>(patch + kObjectFlagsOffset) & 1u) != 0) {
		pass.hidden.Add(distance);
	}
}

// Ends the pass: the camera it had, what stands under the Grass node now,
// and a line when the writer wants one.
void Boundary(bool perEyeData) {
	const std::uint64_t now = NowMs();
	g_lastBoundaryMs = now;
	const UInt32 grassNode = ReadAt<UInt32>(kGrassNodeGlobal);
	gr::Pass pass = g_pass;
	const bool cameraKnown = g_cameraKnown;
	g_pass = gr::Pass{};
	g_cameraKnown = false;
	if (grassNode == 0) {
		if (!g_noGrassNodeLogged) {
			g_noGrassNodeLogged = true;
			g_log("Probe: grass - no Grass node yet; lines follow when there is one");
		}
		return;
	}
	if (!cameraKnown) {
		return;  // no world draw since the last boundary: nothing to say about a pass
	}
	pass.perEyeData = perEyeData;
	pass.yaw = gr::YawDegrees(g_view);
	pass.pitch = gr::PitchDegrees(g_view);
	std::memcpy(pass.camera, g_origin, sizeof(pass.camera));
	pass.groups = ForChildren(grassNode, [&pass](UInt32 child) {
		const bool hidden = (ReadAt<UInt16>(child + kObjectFlagsOffset) & 1u) != 0;
		if (IsNode(child)) {
			ForChildren(child, [&pass, hidden](UInt32 patch) { CountPatch(patch, hidden, pass); });
		} else {
			CountPatch(child, hidden, pass);
		}
	});
	if (!g_writer.Due(pass, now)) {
		return;
	}
	g_writer.Wrote(pass, now);
	SYSTEMTIME local{};
	GetLocalTime(&local);
	char clock[32];
	std::snprintf(clock, sizeof(clock), "%02u:%02u:%02u.%03u", local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
	LARGE_INTEGER qpc{};
	QueryPerformanceCounter(&qpc);
	char line[1100];
	gr::Format(pass, static_cast<std::uint64_t>(qpc.QuadPart), clock, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace

void InstallGrassProbe(LogFn log) {
	g_log = log;
	g_grassProbeOn = true;
	log("Probe: grass on - a line per world pass that differs from the last written one (at most ten a second, at least one every two "
	    "seconds): the patches under the Grass node, those with the hidden bit and those drawn, in bands of 1000 units from the camera");
}

void GrassProbeNoteDraw(UInt32 geometry, bool worldViewport) {
	if (worldViewport) {
		TakeCamera();
	}
	const UInt32 grassNode = ReadAt<UInt32>(kGrassNodeGlobal);
	if (worldViewport && grassNode != 0 && geometry != 0) {
		UInt32 object = geometry;
		for (UInt32 step = 0; step < kAncestorsWalked && object != 0; ++step) {
			if (object == grassNode) {
				g_pass.drawn.Add(DistanceOf(geometry, g_origin));
				break;
			}
			object = ReadAt<UInt32>(object + kObjectParentOffset);
		}
	}
	// Without eye data nothing marks the passes: the draws of a time window stand in.
	const std::uint64_t now = NowMs();
	if (now - g_lastEyeBoundaryMs > 4 * kWindowMs && now - g_lastBoundaryMs >= kWindowMs) {
		Boundary(false);
	}
}

void GrassProbeNoteFade(const float alphaParam[4], const float clipRows[16], const float* instanceRegisters, int instances, float p11, float p22) {
	for (int i = 0; i < instances; ++i) {
		const float* p = instanceRegisters + i * 4;
		if (p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f) {
			continue;
		}
		g_pass.fade.Add(gr::ShaderFade(alphaParam, clipRows, p, p11, p22));
	}
}

void GrassProbePassBoundary() {
	g_lastEyeBoundaryMs = NowMs();
	Boundary(true);
}

}  // namespace obng::plugin
