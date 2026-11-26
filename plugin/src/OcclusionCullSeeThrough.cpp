#include "OcclusionCullSeeThrough.h"

#include <cstring>

#include "OcclusionCullQuick.h"

namespace obng::plugin {

namespace {

namespace cull = occlusion_cull;
namespace oc = occlusion;
namespace st = occlusion_cull::see_through;

// NiGeometry's bound (+0x20) and property state (+0xAC); the state's property pointers by type after its reference
// count (+0x08, the alpha property is type 0); a property's flags (+0x18). The same readings as OcclusionCull.cpp's
// and DrawInstancing.cpp's (knowledge/shadow-light-list-remove.md, the property types).
constexpr UInt32 kGeometryBoundOffset = 0x20;
constexpr UInt32 kGeometryPropertyStateOffset = 0xAC;
constexpr UInt32 kPropertyStateSlotsOffset = 0x08;
constexpr UInt32 kPropertyFlagsOffset = 0x18;

st::Mode g_mode = st::Mode::kBeforeFirstBlended;
bool g_alternate = false;  // [Route] OcclusionCullBeforeBlendAlternate
st::Surfaces g_surfaces;
st::Counts g_counts;

template <class T>
T ReadAt(std::uintptr_t address) {
	return *reinterpret_cast<volatile const T*>(address);
}

bool Blends(UInt32 geometry) {
	const UInt32 state = ReadAt<UInt32>(geometry + kGeometryPropertyStateOffset);
	const UInt32 alpha = state != 0 ? ReadAt<UInt32>(state + kPropertyStateSlotsOffset) : 0;
	return st::Blends(alpha != 0, alpha != 0 ? ReadAt<UInt16>(alpha + kPropertyFlagsOffset) : 0u);
}

}  // namespace

void OcclusionCullSeeThroughInstall(int iniValue, bool alternate) {
	g_mode = st::ModeFromIni(iniValue);
	g_alternate = alternate;
}

st::Mode OcclusionCullSeeThroughMode() { return g_mode; }

void OcclusionCullSeeThroughBegin() { g_surfaces.Reset(); }

bool OcclusionCullSeeThroughEnds(UInt32 geometry, bool direct, UInt32 renderer, const oc::Viewport ports[2], const float secondEyeOffset[3],
                                 float margin, UInt32 eyes) {
	if (g_mode == st::Mode::kAtFinishEnd || (direct && g_mode != st::Mode::kSeeThrough) || !Blends(geometry)) {
		return false;
	}
	cull::EyeRects rects;
	oc::RectKind kinds[2] = {oc::RectKind::kNoBound, oc::RectKind::kNoBound};
	if (g_mode == st::Mode::kSeeThrough) {
		oc::Bound bound;
		std::memcpy(&bound, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(geometry + kGeometryBoundOffset)), sizeof(bound));
		OcclusionCullRects(renderer, bound, ports, secondEyeOffset, margin, eyes, &rects, kinds);
	}
	const bool full = g_surfaces.count >= st::kSurfacesAtMost;
	const st::Outcome outcome = st::OnRecord(g_mode, true, rects, kinds, eyes, &g_surfaces);
	if (g_mode == st::Mode::kSeeThrough) {
		g_counts.kept += outcome == st::Outcome::kKept ? 1 : 0;
		g_counts.offScreen += outcome == st::Outcome::kOffScreen ? 1 : 0;
		g_counts.endedFull += outcome == st::Outcome::kEnds && full ? 1 : 0;
		g_counts.endedNoRect += outcome == st::Outcome::kEnds && !full ? 1 : 0;
	}
	return outcome == st::Outcome::kEnds;
}

void OcclusionCullSeeThroughKeepTests(cull::Batch* batch) {
	if (g_mode != st::Mode::kSeeThrough || batch == nullptr) {
		return;
	}
	const std::uint32_t before = batch->count;
	batch->count = st::KeepTests(batch->tests, batch->count, g_surfaces, [batch](std::uint32_t from, std::uint32_t to) {
		if (!batch->traced || to >= cull::kTraceTests) {
			return;
		}
		if (from < cull::kTraceTests) {
			std::memcpy(batch->names[to], batch->names[from], sizeof(batch->names[to]));
			std::memcpy(batch->parents[to], batch->parents[from], sizeof(batch->parents[to]));
			batch->bounds[to] = batch->bounds[from];
		} else {
			batch->names[to][0] = '\0';  // a test beyond the named ones moved among them: it has no name
			batch->parents[to][0] = '\0';
			batch->bounds[to] = oc::Bound{};
		}
	});
	g_counts.tests += before;
	g_counts.testsDropped += before - batch->count;
}

void OcclusionCullSeeThroughSummary(LogFn log, std::uint64_t passes) {
	if (log == nullptr || g_mode == st::Mode::kAtFinishEnd) {
		return;
	}
	char line[480];
	st::FormatSummary(g_counts, g_mode, passes, line, sizeof(line));
	log("%s", line);
	g_counts = st::Counts{};
	g_mode = st::NextWindowMode(g_mode, g_alternate);  // the next window's mode; a pass starts with an empty list anyway
}

}  // namespace obng::plugin
