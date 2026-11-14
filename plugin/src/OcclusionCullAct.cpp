#include "OcclusionCullAct.h"

#include "OcclusionCullActLogic.h"

namespace obng::plugin {
namespace {

namespace cull = occlusion_cull;

cull::act::State g_act;

}  // namespace

void OcclusionCullActInit(LogFn log, int iniValue) {
	g_act.Set(cull::act::RuleFromIni(iniValue));
	if (g_act.rule != cull::act::Rule::kAct) {
		log("Route: occlusion cull - the verdicts are %s ([Route] OcclusionCullAct): every draw is tested and decided; a test-only window's "
		    "eye pass ms is the pass without the skips",
		    cull::act::RuleName(g_act.rule));
	}
}

cull::Decided OcclusionCullActDecide(cull::Table& table, std::uint32_t geometry, std::uint32_t frame, cull::TestKind kind, const cull::Settings& settings,
                                     const cull::Cameras& cameras, cull::Stats* stats, std::uint32_t identity, cull::LodFn isLod) {
	return cull::act::Decide(g_act, table, geometry, frame, kind, settings, cameras, stats, identity, isLod);
}

int OcclusionCullActSummary(const cull::Stats& window, const cull::Settings& settings, double ticksPerMillisecond, std::uint64_t qpc, char* out,
                            std::size_t capacity) {
	int at = cull::FormatSummary(window, settings, ticksPerMillisecond, qpc, out, capacity);
	if (g_act.rule != cull::act::Rule::kAct) {
		at = cull::act::AppendSummary(g_act, window.eyePasses, out, capacity, at);
	}
	g_act.NextWindow();
	return at;
}

}  // namespace obng::plugin
