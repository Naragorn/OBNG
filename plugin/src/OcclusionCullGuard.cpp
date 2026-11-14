// The occlusion culler's guards: what OcclusionCullGuardLogic.h decides, read
// from the game. See OcclusionCullGuard.h.

#include "OcclusionCullGuard.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "OcclusionCullGuardLogic.h"

namespace obng::plugin {

namespace {

namespace cull = occlusion_cull;
namespace oc = occlusion;

constexpr UInt32 kObjectNameOffset = 0x08;    // NiObjectNET's name (the probe's)
constexpr UInt32 kObjectParentOffset = 0x1C;  // NiAVObject's parent (the probe's)
constexpr UInt32 kObjectBoundOffset = 0x20;   // its world bound (re/functions/0x007693E0.md)
constexpr UInt32 kGeometryModelDataOffset = 0xB4;  // NiGeometry's data (read 0x00722AE5, SceneUpdate.h)
// "LODRoot", made by 0x00406950 and kept in this global; "LandLOD" and
// "DistantRefLOD" (with "LOD Trees") are its children (read 0x00406950, the
// names it gives through 0x006FF420; not in xOBSE or OBVR's GameAddresses.h -
// the second source is the run: the land's pieces are counted under it).
constexpr UInt32 kLodRootGlobal = 0x00B333A8;
constexpr UInt32 kPlayerPointer = 0x00B333C4;  // (read 0x00603CA0's compares; xOBSE g_thePlayer, OBVR kPlayerPointer)
constexpr UInt32 kRefParentCellOffset = 0x40;  // TESObjectREFR::parentCell (xOBSE; OBVR GameAddresses.h kRefParentCellOffset)
// The layout as GrassProbe.cpp reads it: the child array at +0xB0, its count at +0xB6.
constexpr UInt32 kNodeChildrenOffset = 0xB0;
constexpr UInt32 kNodeChildCountOffset = 0xB6;
constexpr UInt32 kNodeLogChildren = 8;

LogFn g_log = nullptr;
cull::guard::Totals g_totals;  // the windows summed so far
cull::guard::CellWatch g_watch;
UInt32 g_windowLines = cull::guard::kWindowLines;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

}  // namespace

void OcclusionCullReadName(UInt32 object, char out[oc::kNameWidth]) {
	char raw[oc::kNameWidth] = {};
	__try {
		const char* name = *reinterpret_cast<const char* const*>(static_cast<std::uintptr_t>(object + kObjectNameOffset));
		for (std::size_t i = 0; name != nullptr && i + 1 < oc::kNameWidth && name[i] != '\0'; ++i) {
			raw[i] = name[i];
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		raw[0] = '\0';
	}
	oc::CopyName(raw, out);
}

std::uint32_t OcclusionCullIdentity(UInt32 object, bool node, const float centre[3]) {
	return cull::guard::Identity(ReadAt<UInt32>(object + kObjectNameOffset), ReadAt<UInt32>(object + kObjectParentOffset),
	                             node ? 0 : ReadAt<UInt32>(object + kGeometryModelDataOffset), cull::guard::Place(centre[0], centre[1]));
}

bool OcclusionCullIsLod(std::uint32_t object) {
	return cull::guard::UnderRoot(object, ReadAt<UInt32>(kLodRootGlobal), [](std::uint32_t o) { return ReadAt<UInt32>(o + kObjectParentOffset); });
}

void OcclusionCullGuardPass(UInt32 frame, const cull::Stats& live) {
	const UInt32 player = ReadAt<UInt32>(kPlayerPointer);
	const UInt32 cell = player != 0 ? ReadAt<UInt32>(player + kRefParentCellOffset) : 0;
	cull::guard::Totals now = g_totals;
	now += cull::guard::TotalsOf(live);
	cull::guard::CellWatch::Window window;
	if (g_watch.Pass(cell, frame, now, &window) && g_windowLines != 0 && g_log != nullptr) {
		--g_windowLines;
		char line[600];
		cull::guard::FormatCellWindow(window, line, sizeof(line));
		g_log("%s", line);
	}
}

void OcclusionCullGuardSummary(const cull::Stats& window, cull::LodRule rule) {
	if (g_log != nullptr) {
		char line[600];
		cull::guard::FormatGuards(window, rule, line, sizeof(line));
		g_log("%s", line);
	}
	g_totals += cull::guard::TotalsOf(window);
}

void OcclusionCullLogNode(UInt32 node, UInt32 frame, LogFn log) {
	char line[768];
	char name[oc::kNameWidth];
	OcclusionCullReadName(node, name);
	oc::Bound bound;
	std::memcpy(&bound, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(node + kObjectBoundOffset)), sizeof(bound));
	int at = std::snprintf(line, sizeof(line), "Route: occlusion cull node left out - frame %u node 0x%08X '%s' vtable 0x%08X bound (%.0f, %.0f, %.0f) r %.0f; parents",
	                       frame, node, name, ReadAt<UInt32>(node), static_cast<double>(bound.centre[0]), static_cast<double>(bound.centre[1]),
	                       static_cast<double>(bound.centre[2]), static_cast<double>(bound.radius));
	UInt32 parent = node;
	for (unsigned level = 0; level < 3 && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++level) {
		parent = ReadAt<UInt32>(parent + kObjectParentOffset);
		if (parent == 0) {
			break;
		}
		OcclusionCullReadName(parent, name);
		at += std::snprintf(line + at, sizeof(line) - at, " '%s'", name);
	}
	const UInt32 children = ReadAt<UInt32>(node + kNodeChildrenOffset);
	const UInt32 count = ReadAt<UInt16>(node + kNodeChildCountOffset);
	if (at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
		at += std::snprintf(line + at, sizeof(line) - at, "; %u children:", count);
	}
	for (UInt32 i = 0; children != 0 && i < count && i < kNodeLogChildren && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++i) {
		const UInt32 child = ReadAt<UInt32>(children + i * 4);
		if (child == 0) {
			continue;
		}
		OcclusionCullReadName(child, name);
		at += std::snprintf(line + at, sizeof(line) - at, " '%s' 0x%08X", name, ReadAt<UInt32>(child));
	}
	log("%s", line);
}

void OcclusionCullGuardInit(LogFn log) {
	g_log = log;
}

}  // namespace obng::plugin
