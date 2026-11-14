#include "OcclusionCullStandDown.h"

#include "OcclusionCullStandDownLogic.h"

namespace obng::plugin {

bool g_occlusionCullRests = false;

namespace {

namespace cull = occlusion_cull;
namespace sd = occlusion_cull::stand_down;

sd::Rule g_rule = sd::Rule::kOff;
sd::Settings g_settings;
sd::State g_state;
sd::Totals g_before;  // the windows before the last summary: the Stats are reset there
sd::Totals g_now;
std::uint64_t g_nodeDraws = 0;

sd::Totals Of(const cull::Stats& s) {
	sd::Totals t = g_before;
	t.draws += s.Draws();
	t.skippedDraws += s.Skipped();
	t.nodeDraws = g_nodeDraws;
	t.cullTicks += s.decideTicks + s.endTestsTicks + s.collectTicks;
	t.passTicks += s.passTicks;
	return t;
}

}  // namespace

void OcclusionCullStandDownInit(LogFn log, int iniValue) {
	g_rule = sd::RuleFromIni(iniValue);
	if (g_rule != sd::Rule::kOff) {
		log("Route: occlusion cull - it rests while its skips save less than it costs: %s ([Route] OcclusionCullStandDown; windows of %u passes, "
		    "rests of %u to %u)",
		    sd::RuleName(g_rule), g_settings.windowPasses, g_settings.restPasses, g_settings.maxRestPasses);
	}
}

void OcclusionCullStandDownNode(std::uint32_t drawsBelow) {
	g_nodeDraws += drawsBelow;
}

void OcclusionCullStandDownPass(const cull::Stats& live) {
	g_now = Of(live);
	g_occlusionCullRests = sd::Pass(g_state, g_settings, g_rule, g_now);
}

void OcclusionCullStandDownSummary(LogFn log, const cull::Stats& window, double ticksPerMillisecond) {
	g_before = Of(window);
	if (g_rule == sd::Rule::kOff) {
		return;
	}
	char line[500];
	sd::FormatSummary(g_state, g_settings, g_rule, ticksPerMillisecond, line, sizeof(line));
	log("%s", line);
}

}  // namespace obng::plugin
