#include "OcclusionCullQuick.h"

#include <cstring>

#include "OcclusionCullEngine.h"
#include "OcclusionCullQuickLogic.h"

namespace obng::plugin {
namespace {

namespace cull = occlusion_cull;
namespace oc = occlusion;

cull::quick::State g_state;
cull::quick::Counts g_counts;
bool g_applied = false;  // the check: the quick decision would have applied to the draw being decided

}  // namespace

void OcclusionCullQuickInit(LogFn log, int iniValue) {
	g_state.Set(cull::quick::RuleFromIni(iniValue));
	log("Route: occlusion cull - a draw with a visible verdict and no test due is decided without its rectangles: %s ([Route] OcclusionCullQuick)",
	    cull::quick::RuleName(g_state.rule));
}

bool OcclusionCullQuick(cull::Verdict* seen, UInt32 frame, std::uint32_t identity, std::uint32_t every, const cull::Settings& settings,
                        cull::Stats* stats, cull::Decided* out) {
	if ((g_state.on || g_state.Checks()) && cull::quick::Applies(seen, frame, identity, every, settings.lodRule)) {
		if (g_state.on) {
			*out = cull::quick::Decide(*seen, frame, stats);
			++g_counts.quick;
			return true;
		}
		g_applied = true;
	}
	++g_counts.full;
	return false;
}

cull::TestKind OcclusionCullRects(UInt32 renderer, const oc::Bound& bound, const oc::Viewport ports[2], const float secondEyeOffset[3], float margin,
                                  UInt32 eyes, cull::EyeRects* rects, oc::RectKind kinds[2]) {
	float origin[3];
	float view0[16];
	float view1[16];
	float proj[16];
	std::memcpy(origin, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(cull::engine::kOriginGlobal)), sizeof(origin));
	std::memcpy(view0, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(renderer + cull::engine::kRendererViewOffset)), sizeof(view0));
	std::memcpy(proj, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(renderer + cull::engine::kRendererProjOffset)), sizeof(proj));
	if (eyes == 2) {
		cull::SecondEyeView(view0, secondEyeOffset, view1);
	}
	return cull::RectsFor(bound, origin, view0, view1, proj, ports, margin, rects, kinds, eyes);
}

void OcclusionCullQuickCompare(const cull::Decided& full, cull::TestKind kind) {
	if (g_applied) {
		g_applied = false;
		cull::quick::Compare(&g_counts, full, kind);
	}
}

void OcclusionCullQuickSummary(LogFn log, std::uint64_t passes) {
	char line[500];
	cull::quick::FormatSummary(g_counts, g_state, passes, line, sizeof(line));
	log("%s", line);
	g_counts = cull::quick::Counts{};
	g_state.NextWindow();
}

}  // namespace obng::plugin
