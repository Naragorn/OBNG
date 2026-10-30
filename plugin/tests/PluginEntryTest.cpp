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
uint32_t FakeInstall(const ShadowSettings& settings) {
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

uint32_t FakeInstallProbes(const ShadowSettings& settings) {
	++g_fake->probeInstalls;
	g_fake->probed = settings;
	return g_fake->probesMade;
}

const Host kHost{&FakeReset, &FakeLog, &FakeReadSettings, &FakeInstall, &FakeInstallMenu, &FakeInstallRangeWalk,
                 &FakeInstallProbes};

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

void TestLoadProbesOff() {
	Section("Load with every probe off");
	Fake fake;
	fake.iniSettings.shadow.callCounters = false;
	fake.iniSettings.shadow.memoryProbe = false;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.probeInstalls == 0 && fake.Logged("Probes: off"), "no probe installer call, said so");
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
}

void TestLoadRangeWalk() {
	Section("Load with the range walk on");
	Fake fake;
	fake.iniSettings.shadow.rangeWalk = true;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.rangeWalkInstalls == 1 && fake.rangeWalkInstalled.rangeWalk, "the switch reaches the range-walk installer");
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
	TestLoadProbesOff();
	return Failures();
}
