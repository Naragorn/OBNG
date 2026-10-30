// Every flow of Query and Load, with the side effects replaced by fakes.

#include "Check.h"
#include "PluginEntry.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

using namespace obng::plugin;

namespace {

struct Fake {
	int resets = 0;
	bool iniExists = true;
	Settings iniSettings;
	int installs = 0;
	int menuInstalls = 0;
	int probeInstalls = 0;
	int rangeWalkInstalls = 0;
	ShadowSettings installed;
	ShadowSettings probed;
	uint32_t made = 4;
	uint32_t menuMade = 2;
	uint32_t probesMade = 5;
	uint32_t rangeWalkMade = 2;  // made when asked; the real installer makes none when the switch is off
	ShadowSettings rangeWalkInstalled;
	int recorderInits = 0;
	ShadowSettings recorderSettings;
	int routeInstalls = 0;
	RouteSettings routeInstalled;
	uint32_t routeMade = 1;
	int timerStarts = 0;
	bool timerStarted = true;
	std::vector<std::string> calls;  // the side effects in the order Load made them
	std::vector<std::string> lines;

	bool Logged(const char* text) const {
		for (const auto& line : lines) {
			if (line.find(text) != std::string::npos) {
				return true;
			}
		}
		return false;
	}
};
Fake* g_fake = nullptr;

void FakeReset() { ++g_fake->resets; }
void FakeLog(const char* format, ...) {
	char buffer[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	g_fake->lines.emplace_back(buffer);
}
bool FakeReadSettings(Settings* settings) {
	if (!g_fake->iniExists) {
		return false;
	}
	*settings = g_fake->iniSettings;
	return true;
}
void FakeInitRecorder(const ShadowSettings& settings) {
	++g_fake->recorderInits;
	g_fake->recorderSettings = settings;
	g_fake->calls.emplace_back("recorder");
}
uint32_t FakeInstall(const ShadowSettings& settings) {
	g_fake->calls.emplace_back("shadow");
	++g_fake->installs;
	g_fake->installed = settings;
	return g_fake->made;
}
uint32_t FakeInstallMenu(const ShadowSettings& settings) {
	++g_fake->menuInstalls;
	g_fake->installed = settings;
	return g_fake->menuMade;
}

uint32_t FakeInstallRangeWalk(const ShadowSettings& settings) {
	++g_fake->rangeWalkInstalls;
	g_fake->rangeWalkInstalled = settings;
	return settings.rangeWalk ? g_fake->rangeWalkMade : 0u;
}

uint32_t FakeInstallRoute(const RouteSettings& settings) {
	g_fake->calls.emplace_back("route");
	++g_fake->routeInstalls;
	g_fake->routeInstalled = settings;
	return g_fake->routeMade;
}

bool FakeStartSummaryTimer(const ShadowSettings&) {
	g_fake->calls.emplace_back("timer");
	++g_fake->timerStarts;
	return g_fake->timerStarted;
}

uint32_t FakeInstallProbes(const ShadowSettings& settings) {
	g_fake->calls.emplace_back("probes");
	++g_fake->probeInstalls;
	g_fake->probed = settings;
	return g_fake->probesMade;
}

const Host kHost{&FakeReset,          &FakeLog,          &FakeReadSettings, &FakeInitRecorder,
                 &FakeInstall,        &FakeInstallMenu,  &FakeInstallRangeWalk, &FakeInstallRoute,
                 &FakeStartSummaryTimer, &FakeInstallProbes};

// Every TimeInfo and LoadingMenu shadow off.
ShadowSettings NoShadow() {
	ShadowSettings s;
	s.update = false;
	s.init = false;
	s.freeze = false;
	s.unfreeze = false;
	s.loadingMenuTick = false;
	s.loadingMenuCtor = false;
	return s;
}

ExtenderInterfaceHead Game() {
	return ExtenderInterfaceHead{22, kOblivionVersion_1_2_416, 0, 0};
}

void TestQuery() {
	Section("Query");
	Fake fake;
	g_fake = &fake;
	PluginInfo info{};
	Check(Query(Game(), &info, kHost), "Query says yes");
	Check(info.infoVersion == kInfoVersion && std::string(info.name) == "OBNG" && info.version == kPluginVersion,
	      "the info names the plugin");
	Check(fake.resets == 1 && fake.Logged("OBNG 1 - xOBSE 22"), "the log is reset and the versions logged");
}

void TestLoadEditor() {
	Section("Load in the editor");
	Fake fake;
	g_fake = &fake;
	ResetForTest();
	ExtenderInterfaceHead editor = Game();
	editor.isEditor = 1;
	Check(Load(editor, kHost), "loads");
	Check(fake.installs == 0 && fake.Logged("Editor: nothing to do"), "nothing shadowed, said so");
	Check(fake.calls.empty(), "no recorder, no route, no timer, no probe");
}

void TestLoadWrongVersion() {
	Section("Load on another Oblivion version");
	Fake fake;
	g_fake = &fake;
	ResetForTest();
	ExtenderInterfaceHead other = Game();
	other.oblivionVersion = 0x010101FF;
	Check(Load(other, kHost), "loads anyway");
	Check(fake.installs == 0 && fake.Logged("is not 1.2.416"), "nothing shadowed, the version named");
	Check(fake.calls.empty(), "no recorder, no route, no timer, no probe");
}

void TestLoadDefaults() {
	Section("Load without an INI");
	Fake fake;
	fake.iniExists = false;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.Logged("No OBNG.ini"), "says the defaults stand");
	Check(fake.installs == 1 && fake.installed.update && fake.installed.init && fake.installed.freeze &&
	          fake.installed.unfreeze && fake.installed.loadingMenuTick && fake.installed.loadingMenuCtor &&
	          fake.installed.fpuControlWord && fake.installed.havokPlanner &&
	          fake.installed.mismatchLines == 20 && fake.installed.summaryEverySeconds == 10,
	      "every shadow and probe on by default, 20 lines, 10 s");
	Check(fake.menuInstalls == 1 && fake.Logged("Shadow: 6 of 6 detours made"), "both installers called, the count of detours logged");
	Check(fake.rangeWalkInstalls == 1 && !fake.rangeWalkInstalled.rangeWalk,
	      "the range-walk installer is called with its switch off by default (two detours per walk, off in play)");
	Check(fake.probeInstalls == 1 && !fake.probed.callCounters && fake.probed.memoryProbe &&
	          fake.Logged("Probes: 5 of 0 call counters made, memory on"),
	      "the probe installer is called for the memory probe; the counters are off by default (play)");
	Check(fake.recorderInits == 1 && fake.recorderSettings.summaryEverySeconds == 10 && fake.calls.front() == "recorder",
	      "the recorder is set up first, with the settings Load stands on");
	Check(fake.routeInstalls == 0 && fake.Logged("Route: every switch is 0, nothing routed"),
	      "the route is off by default (play), said so");
	Check(fake.timerStarts == 0 && !fake.Logged("summary timer") && !fake.installed.summaryTimer,
	      "the summary timer is off by default (play): no thread, no line");
}

void TestLoadProbes() {
	Section("Load with the call counters on");
	Fake fake;
	fake.iniSettings.shadow.callCounters = true;
	fake.probesMade = 4;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 1 && fake.probed.callCounters, "the counters reach the probe installer");
	Check(fake.Logged("Probes: 4 of 5 call counters made, memory on"), "five asked, the shortfall in the log");
}

void TestLoadPassSteps() {
	Section("Load with the pass steps on: four more detours asked");
	Fake fake;
	fake.iniSettings.shadow.passSteps = true;
	fake.probesMade = 8;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 1 && fake.probed.passSteps && !fake.probed.callCounters, "the pass steps reach the probe installer");
	Check(fake.Logged("Probes: 8 of 8 call counters made, memory on"), "eight asked without the counters");
	Fake both;
	both.iniSettings.shadow.passSteps = true;
	both.iniSettings.shadow.callCounters = true;
	both.probesMade = 12;
	g_fake = &both;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(both.Logged("Probes: 12 of 12 call counters made, memory on"), "twelve asked with both: the shared SetupTransformations entry counted once");
}

void TestLoadProbesOff() {
	Section("Load with every probe off");
	Fake fake;
	fake.iniSettings.shadow.callCounters = false;
	fake.iniSettings.shadow.memoryProbe = false;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 0 && fake.Logged("Probes: off"), "no probe installer call, said so");

	Fake timer;
	timer.iniSettings.shadow.callCounters = false;
	timer.iniSettings.shadow.memoryProbe = false;
	timer.iniSettings.shadow.summaryTimer = true;
	g_fake = &timer;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(timer.timerStarts == 1 && timer.Logged("Probes: off"), "every probe off does not end Load before the timer");
}

void TestLoadRoute() {
	Section("Load with the range walk routed");
	Fake fake;
	fake.iniSettings.shadow = NoShadow();
	fake.iniSettings.route.rangeWalk = true;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.routeInstalls == 1 && fake.routeInstalled.rangeWalk && fake.Logged("Route: 1 of 1 detours made"),
	      "the route's installer is called with its switch, one detour asked and made");
	Check(fake.Logged("nothing shadowed") && fake.installs == 0, "no shadow beside it");
	Check(LoadedSettings().route.rangeWalk, "LoadedSettings shows the route");

	Fake failing;
	failing.iniSettings.shadow = NoShadow();
	failing.iniSettings.route.rangeWalk = true;
	failing.routeMade = 0;
	g_fake = &failing;
	ResetForTest();
	Check(Load(Game(), kHost), "loads when the route's detour cannot be made - the game keeps its original");
	Check(failing.Logged("Route: 0 of 1 detours made"), "the shortfall is in the log");

	Fake beside;
	beside.iniSettings.route.rangeWalk = true;  // with the default TimeInfo and LoadingMenu shadows
	g_fake = &beside;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(beside.routeInstalls == 1 && beside.Logged("Shadow: 6 of 6 detours made") && beside.Logged("Route: 1 of 1 detours made"),
	      "other shadows do not stand in the route's way");
}

void TestLoadRouteAndShadow() {
	Section("Load with the range walk both shadowed and routed: the shadow wins");
	Fake fake;
	fake.iniSettings.shadow.rangeWalk = true;
	fake.iniSettings.route.rangeWalk = true;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.routeInstalls == 0, "the route's installer is not called");
	Check(fake.rangeWalkInstalls == 1 && fake.rangeWalkInstalled.rangeWalk && fake.Logged("Shadow: 8 of 8 detours made"),
	      "the shadow takes the entry as asked");
	Check(fake.Logged("Route: ShadowSceneLight::CollectGeometryInRange skipped - the RangeWalk shadow takes the same entry"),
	      "the skip is logged with its reason");
	Check(fake.Logged("Route: 0 of 1 detours made"), "and counted: one asked, none made");
}

void TestLoadSummaryTimer() {
	Section("Load with the summary timer");
	Fake fake;
	fake.iniSettings.shadow = NoShadow();
	fake.iniSettings.shadow.summaryTimer = true;
	fake.iniSettings.shadow.summaryEverySeconds = 7;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.timerStarts == 1 && fake.Logged("Log: a summary every 7 s from the summary timer"),
	      "the timer is started and its period logged");
	Check(fake.calls.size() == 3 && fake.calls[0] == "recorder" && fake.calls[1] == "probes" && fake.calls[2] == "timer",
	      "after the recorder and the probes, so its first summary finds both");

	Fake failing;
	failing.iniSettings.shadow.summaryTimer = true;
	failing.timerStarted = false;
	g_fake = &failing;
	ResetForTest();
	Check(Load(Game(), kHost), "loads when the timer cannot be started");
	Check(failing.Logged("Log: the summary timer could not be started"), "said so");
}

void TestLoadSwitches() {
	Section("Load with switches");
	Fake fake;
	fake.iniSettings.shadow.init = false;
	fake.iniSettings.shadow.unfreeze = false;
	fake.iniSettings.shadow.mismatchLines = 3;
	fake.made = 2;
	fake.menuMade = 2;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.installs == 1 && fake.installed.update && !fake.installed.init && fake.installed.freeze &&
	          !fake.installed.unfreeze && fake.installed.mismatchLines == 3,
	      "the settings reach the installer as read");
	Check(fake.Logged("Shadow: 4 of 4 detours made"), "four asked (two TimeInfo, two LoadingMenu), four made");
	Check(LoadedSettings().shadow.mismatchLines == 3, "LoadedSettings shows what Load read");
}

void TestLoadAllOff() {
	Section("Load with every shadow off");
	Fake fake;
	fake.iniSettings.shadow.update = false;
	fake.iniSettings.shadow.init = false;
	fake.iniSettings.shadow.freeze = false;
	fake.iniSettings.shadow.unfreeze = false;
	fake.iniSettings.shadow.loadingMenuTick = false;
	fake.iniSettings.shadow.loadingMenuCtor = false;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.installs == 0 && fake.menuInstalls == 0 && fake.rangeWalkInstalls == 0 && fake.Logged("nothing shadowed"),
	      "no installer is called, said so");
	Check(fake.probeInstalls == 1 && fake.Logged("memory on"), "the probes still run with every shadow off");
	Check(fake.recorderInits == 1, "the recorder is set up with every shadow off (the probes and the route report to it)");
}

void TestLoadRangeWalk() {
	Section("Load with the range walk on");
	Fake fake;
	fake.iniSettings.shadow.rangeWalk = true;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.rangeWalkInstalls == 1 && fake.rangeWalkInstalled.rangeWalk &&
	          fake.rangeWalkInstalled.rangeWalkOrder == RangeWalkOrder::OriginalFirst,
	      "the switch reaches the range-walk installer, original-first by default");

	Fake ordered;
	ordered.iniSettings.shadow.rangeWalk = true;
	ordered.iniSettings.shadow.rangeWalkOrder = RangeWalkOrder::RebuildFirst;
	g_fake = &ordered;
	ResetForTest();
	Check(Load(Game(), kHost) && ordered.rangeWalkInstalled.rangeWalkOrder == RangeWalkOrder::RebuildFirst,
	      "rebuild-first reaches the installer as read");
	Check(fake.Logged("Shadow: 8 of 8 detours made"), "six TimeInfo and LoadingMenu detours plus the walk's two");

	Fake failing;
	failing.iniSettings.shadow.rangeWalk = true;
	failing.rangeWalkMade = 1;
	g_fake = &failing;
	ResetForTest();
	Check(Load(Game(), kHost), "loads when one of the walk's detours is missing");
	Check(failing.Logged("Shadow: 7 of 8 detours made"), "the shortfall is in the log");

	Fake alone;
	alone.iniSettings.shadow = ShadowSettings{};
	alone.iniSettings.shadow.update = false;
	alone.iniSettings.shadow.init = false;
	alone.iniSettings.shadow.freeze = false;
	alone.iniSettings.shadow.unfreeze = false;
	alone.iniSettings.shadow.loadingMenuTick = false;
	alone.iniSettings.shadow.loadingMenuCtor = false;
	alone.iniSettings.shadow.rangeWalk = true;
	alone.made = 0;
	alone.menuMade = 0;
	g_fake = &alone;
	ResetForTest();
	Check(Load(Game(), kHost), "loads with the range walk as the only shadow");
	Check(alone.rangeWalkInstalls == 1 && alone.Logged("Shadow: 2 of 2 detours made"), "it alone counts as asked");
}

void TestLoadPartialFailure() {
	Section("Load when a detour cannot be made");
	Fake fake;
	fake.made = 3;
	fake.menuMade = 1;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads - a missed detour makes nothing safer");
	Check(fake.Logged("Shadow: 4 of 6 detours made"), "the shortfall is in the log");
}

}  // namespace

int main() {
	TestQuery();
	TestLoadEditor();
	TestLoadWrongVersion();
	TestLoadDefaults();
	TestLoadSwitches();
	TestLoadAllOff();
	TestLoadPartialFailure();
	TestLoadRangeWalk();
	TestLoadProbes();
	TestLoadPassSteps();
	TestLoadProbesOff();
	TestLoadRoute();
	TestLoadRouteAndShadow();
	TestLoadSummaryTimer();
	return Failures();
}
