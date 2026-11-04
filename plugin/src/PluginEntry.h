#pragma once

#include <cstdint>

#include "RouteSettings.h"
#include "ShadowTimeInfo.h"

// What the plugin does when xOBSE queries and loads it - with the side
// effects behind a Host so every flow runs in a test. The shape is
// OblivionFrameFixes' (the same author); the content is OBNG's shadow mode.

namespace obng::plugin {

// First fields of xOBSE's OBSEInterface (obse/obse/PluginAPI.h); nothing
// past isEditor is read.
struct ExtenderInterfaceHead {
	uint32_t obseVersion;
	uint32_t oblivionVersion;
	uint32_t editorVersion;
	uint32_t isEditor;
};

// PluginAPI.h PluginInfo; kInfoVersion is 3 in xOBSE 22.
struct PluginInfo {
	uint32_t infoVersion;
	const char* name;
	uint32_t version;
};

constexpr const char* kPluginName = "OBNG";
constexpr uint32_t kPluginVersion = 1;
constexpr uint32_t kInfoVersion = 3;

// obse_common/obse_version.h: MAKE_OBLIVION_VERSION(1, 2, 416). Every
// address here is of this build and of no other.
constexpr uint32_t kOblivionVersion_1_2_416 = 0x010201A0;

struct Settings {
	ShadowSettings shadow;  // [Shadow], [Probe] and [Log]
	RouteSettings route;    // [Route]
};

struct Host {
	void (*resetLog)();
	void (*log)(const char* format, ...);
	// Reads the INI next to the DLL into *settings; false if there is no INI
	// (the defaults then stand).
	bool (*readSettings)(Settings* settings);
	// The summary's bookkeeping (ShadowRecorder) every shadow, probe and
	// route reports to; set up before any of them.
	void (*initRecorder)(const ShadowSettings& settings);
	// The shadow detours; each returns how many were made.
	uint32_t (*installShadowTimeInfo)(const ShadowSettings& settings);
	uint32_t (*installShadowLoadingMenu)(const ShadowSettings& settings);
	uint32_t (*installShadowRangeWalk)(const ShadowSettings& settings);
	// The range walk's route (RouteRangeWalk.cpp); returns how many detours were made.
	uint32_t (*installRouteRangeWalk)(const RouteSettings& settings);
	// A thread that drives the summary every second, for runs where no
	// per-frame detour does (ShadowRecorder); false if it could not start.
	bool (*startSummaryTimer)(const ShadowSettings& settings);
	// The counting probes (Probes.cpp); returns how many detours were made.
	uint32_t (*installProbes)(const ShadowSettings& settings);
};

// How many counting detours the settings ask for: five hot functions of the
// scene pass for the call counters, nine timed steps of the pass for the
// pass steps - seven when the counters are on too, as SetupTransformations
// 0x007C9230 is in both sets and the counter keeps its entry.
constexpr uint32_t kProbeDetours = 5;
constexpr uint32_t kPassStepDetours = 9;
constexpr uint32_t kPackCacheDetours = 3;  // the packer's entry, PackStream and the unlock
constexpr uint32_t kConstantUploadDetours = 3;  // the two upload slots and the finish (the pass counter); two when the pass steps hold the finish
inline uint32_t ProbesAskedFor(const ShadowSettings& s) {
	return (s.callCounters ? kProbeDetours : 0u) + (s.passSteps ? (s.callCounters ? kPassStepDetours - 1 : kPassStepDetours) : 0u) +
	       (s.packCache ? kPackCacheDetours : 0u) +
	       (s.constantUploads ? (s.passSteps ? kConstantUploadDetours - 1 : kConstantUploadDetours) : 0u);
}

bool Query(const ExtenderInterfaceHead& xse, PluginInfo* info, const Host& host);
// Puts the shadows the settings ask for. Always true: refusing to load would
// make nothing safer, and the log says what was skipped.
bool Load(const ExtenderInterfaceHead& xse, const Host& host);

// The state Load left; exposed for tests.
const Settings& LoadedSettings();
void ResetForTest();

}  // namespace obng::plugin
