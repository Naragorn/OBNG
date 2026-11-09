// Every flow of Query and Load, with the side effects replaced by fakes.

#include "Check.h"
#include "PluginEntry.h"

#include <cmath>
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
	int stereoInstalls = 0;
	RouteSettings stereoInstalled;
	uint32_t stereoMade = 4;
	int timerStarts = 0;
	bool timerStarted = true;
	int fixInstalls = 0;
	obng::plugin::FixSettings fixInstalled;
	bool fixesSetUp = true;
	int sqrtShadowInstalls = 0;
	uint32_t sqrtShadowMade = 1;  // made when asked; the real installer makes none when the switch is off
	int sqrtRouteInstalls = 0;
	uint32_t sqrtRouteMade = 1;
	int actorShadowInstalls = 0;
	uint32_t actorShadowMade = 1;
	int actorRouteInstalls = 0;
	uint32_t actorRouteMade = 1;
	int actorTimingInstalls = 0;
	int volatileShadowInstalls = 0;
	int volatileRouteInstalls = 0;
	ShadowSettings sceneShadowSeen;
	RouteSettings sceneRouteSeen;
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

uint32_t FakeInstallStereo(const RouteSettings& settings) {
	g_fake->calls.emplace_back("stereo");
	++g_fake->stereoInstalls;
	g_fake->stereoInstalled = settings;
	return g_fake->stereoMade;
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

bool FakeInstallFixes(const obng::plugin::FixSettings& settings) {
	++g_fake->fixInstalls;
	g_fake->fixInstalled = settings;
	return g_fake->fixesSetUp;
}

uint32_t FakeInstallShadowSqrt(const ShadowSettings& settings) {
	++g_fake->sqrtShadowInstalls;
	return settings.sqrt ? g_fake->sqrtShadowMade : 0u;
}

uint32_t FakeInstallRouteSqrt(const RouteSettings& settings) {
	++g_fake->sqrtRouteInstalls;
	return settings.sqrt ? g_fake->sqrtRouteMade : 0u;
}

uint32_t FakeInstallShadowActorUpdate(const ShadowSettings& settings) {
	++g_fake->actorShadowInstalls;
	g_fake->sceneShadowSeen = settings;
	return (settings.actorUpdate ? g_fake->actorShadowMade : 0u) + (settings.gridUpdate ? 1u : 0u);
}

uint32_t FakeInstallRouteActorUpdate(const RouteSettings& settings) {
	++g_fake->actorRouteInstalls;
	g_fake->sceneRouteSeen = settings;
	return (settings.actorUpdate ? g_fake->actorRouteMade : 0u) + (settings.gridUpdate ? 1u : 0u);
}

uint32_t FakeInstallShadowVolatilePack(const ShadowSettings& settings) {
	++g_fake->volatileShadowInstalls;
	return settings.volatilePack ? 2u : 0u;
}

uint32_t FakeInstallRouteVolatilePack(const RouteSettings& settings) {
	++g_fake->volatileRouteInstalls;
	return settings.volatilePack ? 2u : 0u;
}

uint32_t FakeInstallActorUpdateTiming(const ShadowSettings& settings) {
	++g_fake->actorTimingInstalls;
	return settings.actorUpdateTiming ? 1u : 0u;
}

const Host kHost{&FakeReset,          &FakeLog,          &FakeReadSettings, &FakeInitRecorder,
                 &FakeInstall,        &FakeInstallMenu,  &FakeInstallRangeWalk, &FakeInstallRoute,
                 &FakeStartSummaryTimer, &FakeInstallProbes, &FakeInstallStereo, &FakeInstallFixes,
                 &FakeInstallShadowSqrt, &FakeInstallRouteSqrt, &FakeInstallShadowActorUpdate, &FakeInstallRouteActorUpdate,
                 &FakeInstallActorUpdateTiming, &FakeInstallShadowVolatilePack, &FakeInstallRouteVolatilePack};

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
	fake.probesMade = 9;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 1 && fake.probed.passSteps && !fake.probed.callCounters, "the pass steps reach the probe installer");
	Check(fake.Logged("Probes: 9 of 9 call counters made, memory on"), "nine asked without the counters");
	Fake both;
	both.iniSettings.shadow.passSteps = true;
	both.iniSettings.shadow.callCounters = true;
	both.probesMade = 13;
	g_fake = &both;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(both.Logged("Probes: 13 of 13 call counters made, memory on"), "thirteen asked with both: the shared SetupTransformations entry counted once");
}

void TestLoadPackCache() {
	Section("Load with the pack cache on: two more detours asked");
	Fake fake;
	fake.iniSettings.shadow.packCache = true;
	fake.probesMade = 3;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 1 && fake.probed.packCache && !fake.probed.passSteps, "the pack cache reaches the probe installer");
	Check(fake.Logged("Probes: 3 of 3 call counters made, memory on"), "three asked");
}

void TestLoadConstantUploads() {
	Section("Load with the constant uploads probe on: three detours, two beside the pass steps");
	Fake fake;
	fake.iniSettings.shadow.constantUploads = true;
	fake.probesMade = 3;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 1 && fake.probed.constantUploads && fake.Logged("Probes: 3 of 3 call counters made, memory on"), "three asked alone");
	Fake both;
	both.iniSettings.shadow.constantUploads = true;
	both.iniSettings.shadow.passSteps = true;
	both.probesMade = 11;
	g_fake = &both;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(both.Logged("Probes: 11 of 11 call counters made, memory on"), "eleven asked with the pass steps: the finish counted once");
	Fake view;
	view.iniSettings.shadow.constantUploads = true;
	view.iniSettings.shadow.viewConstants = true;
	view.probesMade = 4;
	g_fake = &view;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(view.probeInstalls == 1 && view.probed.viewConstants && view.Logged("Probes: 4 of 4 call counters made, memory on"),
	      "the view constants add one detour to the uploads' three");
	Fake viewSteps;
	viewSteps.iniSettings.shadow.constantUploads = true;
	viewSteps.iniSettings.shadow.viewConstants = true;
	viewSteps.iniSettings.shadow.passSteps = true;
	viewSteps.probesMade = 11;
	g_fake = &viewSteps;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(viewSteps.Logged("Probes: 11 of 11 call counters made, memory on"), "with the pass steps the view probe asks for nothing (they hold its entry)");
	Fake entries;
	entries.iniSettings.shadow.drawEntries = true;
	entries.probesMade = 4;
	g_fake = &entries;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(entries.probed.drawEntries && entries.Logged("Probes: 4 of 4 call counters made, memory on"), "the draw entries ask for four");
	Fake occlusion;
	occlusion.iniSettings.shadow.occlusion = true;
	occlusion.probesMade = 4;
	g_fake = &occlusion;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(occlusion.probed.occlusion && occlusion.Logged("Probes: 4 of 4 call counters made, memory on"), "the occlusion probe asks for four");
	Fake unrouted;
	unrouted.iniSettings.shadow.unroutedDraws = true;
	unrouted.probesMade = 1;
	g_fake = &unrouted;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(unrouted.probed.unroutedDraws && unrouted.Logged("Probes: 1 of 1 call counters made, memory on"), "the unrouted-draw probe asks for one");
}

void TestLoadStereoDraw() {
	Section("Load with the stereo draw route on: four detours asked, the range walk untouched");
	Fake fake;
	fake.iniSettings.route.stereoDraw = true;
	fake.iniSettings.route.stereoEyes = 2;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.stereoInstalls == 1 && fake.stereoInstalled.stereoEyes == 2 && fake.routeInstalls == 0,
	      "the stereo installer is called with the plan, the range walk's is not");
	Check(fake.Logged("Route: 4 of 4 detours made"), "four asked, four made");
	Fake both;
	both.iniSettings.route.stereoDraw = true;
	both.iniSettings.route.rangeWalk = true;
	g_fake = &both;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(both.stereoInstalls == 1 && both.routeInstalls == 1 && both.Logged("Route: 5 of 5 detours made"), "both routes: five detours");
	Fake shadowed;
	shadowed.iniSettings.route.stereoDraw = true;
	shadowed.iniSettings.route.rangeWalk = true;
	shadowed.iniSettings.shadow.rangeWalk = true;
	g_fake = &shadowed;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(shadowed.stereoInstalls == 1 && shadowed.routeInstalls == 0 && shadowed.Logged("skipped") && shadowed.Logged("Route: 4 of 5 detours made"),
	      "the range walk's route yields to its shadow, the stereo draw still installs");
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

void TestLoadFixes() {
	Section("Load: the grass fade fix is on by default, with the scale Load stands on");
	{
		Fake fake;
		fake.iniExists = false;
		g_fake = &fake;
		ResetForTest();
		Check(Load(Game(), kHost), "loads");
		Check(fake.fixInstalls == 1 && fake.fixInstalled.grassFade && !fake.fixInstalled.grassFadeIniReach,
		      "the installer is called once with the defaults");
		Check(fake.Logged("Fix: the grass fades by its distance from the camera, reaching as far as the game has it straight ahead"),
		      "said so, with the reach");
	}
	Section("Load: [Fix] GrassFade=0 leaves the game's shaders");
	{
		Fake fake;
		fake.iniSettings.fix.grassFade = false;
		g_fake = &fake;
		ResetForTest();
		Check(Load(Game(), kHost), "loads");
		Check(fake.fixInstalls == 0 && fake.Logged("Fix: every switch is 0, nothing fixed"), "no installer call, said so");
	}
	Section("Load: the reach from the INI reaches the installer");
	{
		Fake fake;
		fake.iniSettings.fix.grassFadeIniReach = true;
		g_fake = &fake;
		ResetForTest();
		Check(Load(Game(), kHost), "loads");
		Check(fake.fixInstalls == 1 && fake.fixInstalled.grassFadeIniReach && fake.Logged("reaching as far as the INI's distances say"),
		      "the INI's reach");
	}
	Section("Load: fixes that could not be set up are said so");
	{
		Fake fake;
		fake.fixesSetUp = false;
		g_fake = &fake;
		ResetForTest();
		Check(Load(Game(), kHost), "loads anyway");
		Check(fake.fixInstalls == 1 && fake.Logged("Fix: could not be set up - the game's grass shaders stay"), "the failure in the log");
	}
	Section("Load: the editor and another game version get no fix");
	{
		Fake fake;
		g_fake = &fake;
		ResetForTest();
		ExtenderInterfaceHead editor = Game();
		editor.isEditor = 1;
		Load(editor, kHost);
		ExtenderInterfaceHead other = Game();
		other.oblivionVersion = 0x010101FF;
		Load(other, kHost);
		Check(fake.fixInstalls == 0, "no installer call");
	}
}

void TestLoadSqrt() {
	Section("Load with the square root shadowed: one detour");
	Fake fake;
	fake.iniSettings.shadow.sqrt = true;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.sqrtShadowInstalls == 1 && fake.sqrtRouteInstalls == 0 && fake.Logged("Shadow: 7 of 7 detours made"),
	      "the shadow installer is called, the route's is not: six TimeInfo and LoadingMenu detours plus the square root's");
	Fake alone;
	alone.iniSettings.shadow = NoShadow();
	alone.iniSettings.shadow.sqrt = true;
	alone.made = 0;
	alone.menuMade = 0;
	g_fake = &alone;
	ResetForTest();
	Check(Load(Game(), kHost) && alone.Logged("Shadow: 1 of 1 detours made"), "alone: one of one");
	Check(fake.Logged("Route: every switch is 0, nothing routed"), "nothing routed");

	Section("Load with the square root routed: one detour, the shadow untouched");
	Fake routed;
	routed.iniSettings.shadow = NoShadow();
	routed.iniSettings.route.sqrt = true;
	g_fake = &routed;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(routed.sqrtRouteInstalls == 1 && routed.Logged("Route: 1 of 1 detours made"), "the route installer is called, one of one");
	Check(routed.Logged("Shadow: every switch is 0, nothing shadowed"), "nothing shadowed");
	Fake failing;
	failing.iniSettings.shadow = NoShadow();
	failing.iniSettings.route.sqrt = true;
	failing.sqrtRouteMade = 0;
	g_fake = &failing;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(failing.Logged("Route: 0 of 1 detours made"), "the shortfall is in the log");

	Section("Load with the square root both shadowed and routed: the shadow wins");
	Fake both;
	both.iniSettings.shadow = NoShadow();
	both.iniSettings.shadow.sqrt = true;
	both.iniSettings.route.sqrt = true;
	both.made = 0;
	both.menuMade = 0;
	g_fake = &both;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(both.sqrtShadowInstalls == 1 && both.sqrtRouteInstalls == 0, "the shadow is installed, the route is not");
	Check(both.Logged("Route: _CIsqrt skipped - the Sqrt shadow takes the same entry") && both.Logged("Route: 0 of 1 detours made"),
	      "said and counted: one asked, none made");

	Section("Load with the square root route beside the stereo draw: five detours");
	Fake beside;
	beside.iniSettings.shadow = NoShadow();
	beside.iniSettings.route.sqrt = true;
	beside.iniSettings.route.stereoDraw = true;
	g_fake = &beside;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(beside.sqrtRouteInstalls == 1 && beside.stereoInstalls == 1 && beside.Logged("Route: 5 of 5 detours made"), "both routes counted");
}

void TestLoadActorUpdate() {
	Section("Load with the actor update shadowed: one detour");
	Fake fake;
	fake.iniSettings.shadow = NoShadow();
	fake.iniSettings.shadow.actorUpdate = true;
	fake.made = 0;
	fake.menuMade = 0;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.actorShadowInstalls == 1 && fake.actorRouteInstalls == 0 && fake.Logged("Shadow: 1 of 1 detours made"), "the shadow installer, one of one");

	Section("Load with the actor update routed: one detour");
	Fake routed;
	routed.iniSettings.shadow = NoShadow();
	routed.iniSettings.route.actorUpdate = true;
	g_fake = &routed;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(routed.actorRouteInstalls == 1 && routed.actorShadowInstalls == 0 && routed.Logged("Route: 1 of 1 detours made"), "the route installer, one of one");

	Section("Load with the actor update both shadowed and routed: the shadow wins");
	Fake both;
	both.iniSettings.shadow = NoShadow();
	both.iniSettings.shadow.actorUpdate = true;
	both.iniSettings.route.actorUpdate = true;
	both.made = 0;
	both.menuMade = 0;
	g_fake = &both;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(both.actorShadowInstalls == 1 && both.actorRouteInstalls == 0 && both.Logged("Route: NiAVObject::Update skipped - the ActorUpdate shadow takes the same entry") &&
	          both.Logged("Route: 0 of 1 detours made"),
	      "the shadow is installed, the route is said and counted as not made");

	Section("Load with the actor update beside the SceneUpdate probe: neither takes the entry");
	Fake probed;
	probed.iniSettings.shadow = NoShadow();
	probed.iniSettings.shadow.sceneUpdate = true;
	probed.iniSettings.shadow.actorUpdate = true;
	probed.iniSettings.route.actorUpdate = true;
	probed.made = 0;
	probed.menuMade = 0;
	g_fake = &probed;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(probed.actorShadowInstalls == 0 && probed.actorRouteInstalls == 0 && probed.Logged("Shadow: NiAVObject::Update skipped - the SceneUpdate probe takes the same entry") &&
	          probed.Logged("Route: NiAVObject::Update skipped - the SceneUpdate probe takes the same entry") && probed.Logged("Shadow: 0 of 1 detours made") &&
	          probed.Logged("Route: 0 of 1 detours made"),
	      "both skipped, said and counted");

	Section("Load with the cells' selected update: routed beside the actors', shadowed alone, the shadow wins per switch");
	Fake grid;
	grid.iniSettings.shadow = NoShadow();
	grid.iniSettings.route.actorUpdate = true;
	grid.iniSettings.route.gridUpdate = true;
	g_fake = &grid;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(grid.actorRouteInstalls == 1 && grid.sceneRouteSeen.actorUpdate && grid.sceneRouteSeen.gridUpdate && grid.Logged("Route: 2 of 2 detours made"),
	      "one installer call with both switches, two of two");
	Fake gridShadow;
	gridShadow.iniSettings.shadow = NoShadow();
	gridShadow.iniSettings.shadow.gridUpdate = true;
	gridShadow.iniSettings.route.actorUpdate = true;
	gridShadow.iniSettings.route.gridUpdate = true;
	gridShadow.made = 0;
	gridShadow.menuMade = 0;
	g_fake = &gridShadow;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(gridShadow.actorShadowInstalls == 1 && gridShadow.sceneShadowSeen.gridUpdate && !gridShadow.sceneShadowSeen.actorUpdate &&
	          gridShadow.Logged("Shadow: 1 of 1 detours made"),
	      "the grid's shadow alone: one of one");
	Check(gridShadow.sceneRouteSeen.actorUpdate && !gridShadow.sceneRouteSeen.gridUpdate &&
	          gridShadow.Logged("Route: the cells' selected update skipped - the GridUpdate shadow takes the same entry and wins") &&
	          gridShadow.Logged("Route: 1 of 2 detours made"),
	      "the route keeps the actors' update and loses the cells' to the shadow: one of two, said");
	Fake gridProbe;
	gridProbe.iniSettings.shadow = NoShadow();
	gridProbe.iniSettings.shadow.sceneUpdate = true;
	gridProbe.iniSettings.route.actorUpdate = true;
	gridProbe.iniSettings.route.gridUpdate = true;
	g_fake = &gridProbe;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(gridProbe.actorRouteInstalls == 1 && !gridProbe.sceneRouteSeen.actorUpdate && gridProbe.sceneRouteSeen.gridUpdate &&
	          gridProbe.Logged("Route: 1 of 2 detours made"),
	      "beside the SceneUpdate probe the actors' route is skipped, the cells' stays");

	Section("Load with the volatile pack: routed two detours, the shadow wins, neither beside the PackCache probe");
	Fake vpRoute;
	vpRoute.iniSettings.shadow = NoShadow();
	vpRoute.iniSettings.route.volatilePack = true;
	g_fake = &vpRoute;
	ResetForTest();
	Check(Load(Game(), kHost) && vpRoute.volatileRouteInstalls == 1 && vpRoute.Logged("Route: 2 of 2 detours made"), "routed: two of two");
	Fake vpBoth;
	vpBoth.iniSettings.shadow = NoShadow();
	vpBoth.iniSettings.shadow.volatilePack = true;
	vpBoth.iniSettings.route.volatilePack = true;
	vpBoth.made = 0;
	vpBoth.menuMade = 0;
	g_fake = &vpBoth;
	ResetForTest();
	Check(Load(Game(), kHost) && vpBoth.volatileShadowInstalls == 1 && vpBoth.volatileRouteInstalls == 0 && vpBoth.Logged("Shadow: 2 of 2 detours made") &&
	          vpBoth.Logged("Route: the volatile pack skipped - its shadow takes the same entries and wins") && vpBoth.Logged("Route: 0 of 2 detours made"),
	      "both asked: the shadow installs, the route is said and counted as not made");
	Fake vpProbe;
	vpProbe.iniSettings.shadow = NoShadow();
	vpProbe.iniSettings.shadow.packCache = true;
	vpProbe.iniSettings.shadow.volatilePack = true;
	vpProbe.iniSettings.route.volatilePack = true;
	vpProbe.made = 0;
	vpProbe.menuMade = 0;
	g_fake = &vpProbe;
	ResetForTest();
	Check(Load(Game(), kHost) && vpProbe.volatileShadowInstalls == 0 && vpProbe.volatileRouteInstalls == 0 &&
	          vpProbe.Logged("Shadow: the volatile pack skipped - the PackCache probe takes the same entries") &&
	          vpProbe.Logged("Route: the volatile pack skipped - the PackCache probe takes the same entries"),
	      "beside the probe: neither, both said");

	Section("Load with the actor update timing probe: alone it takes the entry, beside the route it does not");
	Fake timing;
	timing.iniSettings.shadow = NoShadow();
	timing.iniSettings.shadow.actorUpdateTiming = true;
	g_fake = &timing;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(timing.actorTimingInstalls == 1 && timing.Logged("Probe: NiAVObject::Update timing - 1 of 1 detours made"), "installed and said");
	Fake timingBesideRoute;
	timingBesideRoute.iniSettings.shadow = NoShadow();
	timingBesideRoute.iniSettings.shadow.actorUpdateTiming = true;
	timingBesideRoute.iniSettings.route.actorUpdate = true;
	g_fake = &timingBesideRoute;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(timingBesideRoute.actorTimingInstalls == 0 && timingBesideRoute.actorRouteInstalls == 1 &&
	          timingBesideRoute.Logged("Probe: NiAVObject::Update timing skipped"),
	      "the route keeps the entry, the timing is skipped and said");
}

int main() {
	TestLoadActorUpdate();
	TestLoadSqrt();
	TestLoadFixes();
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
	TestLoadPackCache();
	TestLoadConstantUploads();
	TestLoadStereoDraw();
	TestLoadProbesOff();
	TestLoadRoute();
	TestLoadRouteAndShadow();
	TestLoadSummaryTimer();
	return Failures();
}
