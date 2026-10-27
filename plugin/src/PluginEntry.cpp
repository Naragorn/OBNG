#include "PluginEntry.h"

namespace obng::plugin {

namespace {

Settings g_settings;

}  // namespace

bool Query(const ExtenderInterfaceHead& xse, PluginInfo* info, const Host& host) {
	info->infoVersion = kInfoVersion;
	info->name = kPluginName;
	info->version = kPluginVersion;
	host.resetLog();
	host.log("%s %u - xOBSE %u, Oblivion %08X, editor=%u", kPluginName, kPluginVersion, xse.obseVersion,
	         xse.oblivionVersion, xse.isEditor);
	return true;
}

bool Load(const ExtenderInterfaceHead& xse, const Host& host) {
	if (xse.isEditor != 0) {
		host.log("Editor: nothing to do");
		return true;
	}
	if (xse.oblivionVersion != kOblivionVersion_1_2_416) {
		host.log("Oblivion %08X is not 1.2.416 (%08X): the shadowed addresses are for 1.2.416 only, nothing done",
		         xse.oblivionVersion, kOblivionVersion_1_2_416);
		return true;
	}
	g_settings = Settings{};
	if (!host.readSettings(&g_settings)) {
		host.log("No OBNG.ini next to the DLL - defaults: every TimeInfo shadow on, both probes on, 20 mismatch "
		         "lines a function, a summary every 10 seconds");
	}
	const ShadowSettings& s = g_settings.shadow;
	if (!s.update && !s.init && !s.freeze && !s.unfreeze) {
		host.log("Shadow: every TimeInfo switch is 0, nothing shadowed");
		return true;
	}
	const uint32_t made = host.installShadowTimeInfo(s);
	host.log("Shadow: %u of %u TimeInfo detours made", made,
	         (s.update ? 1u : 0u) + (s.init ? 1u : 0u) + (s.freeze ? 1u : 0u) + (s.unfreeze ? 1u : 0u));
	return true;
}

const Settings& LoadedSettings() {
	return g_settings;
}

void ResetForTest() {
	g_settings = Settings{};
}

}  // namespace obng::plugin
