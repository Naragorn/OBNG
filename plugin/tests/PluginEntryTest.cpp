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
	ShadowSettings installed;
	uint32_t made = 4;
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

const Host kHost{&FakeReset, &FakeLog, &FakeReadSettings, &FakeInstall};

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
	          fake.installed.unfreeze && fake.installed.fpuControlWord && fake.installed.havokPlanner &&
	          fake.installed.mismatchLines == 20 && fake.installed.summaryEverySeconds == 10,
	      "every shadow and probe on by default, 20 lines, 10 s");
	Check(fake.Logged("Shadow: 4 of 4 TimeInfo detours made"), "the count of detours logged");
}

void TestLoadSwitches() {
	Section("Load with switches");
	Fake fake;
	fake.iniSettings.shadow.init = false;
	fake.iniSettings.shadow.unfreeze = false;
	fake.iniSettings.shadow.mismatchLines = 3;
	fake.made = 2;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.installs == 1 && fake.installed.update && !fake.installed.init && fake.installed.freeze &&
	          !fake.installed.unfreeze && fake.installed.mismatchLines == 3,
	      "the settings reach the installer as read");
	Check(fake.Logged("Shadow: 2 of 2 TimeInfo detours made"), "two asked, two made");
	Check(LoadedSettings().shadow.mismatchLines == 3, "LoadedSettings shows what Load read");
}

void TestLoadAllOff() {
	Section("Load with every shadow off");
	Fake fake;
	fake.iniSettings.shadow.update = false;
	fake.iniSettings.shadow.init = false;
	fake.iniSettings.shadow.freeze = false;
	fake.iniSettings.shadow.unfreeze = false;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads");
	Check(fake.installs == 0 && fake.Logged("nothing shadowed"), "the installer is not called, said so");
}

void TestLoadPartialFailure() {
	Section("Load when a detour cannot be made");
	Fake fake;
	fake.made = 3;
	g_fake = &fake;
	ResetForTest();
	Check(Load(Game(), kHost), "loads - a missed detour makes nothing safer");
	Check(fake.Logged("Shadow: 3 of 4 TimeInfo detours made"), "the shortfall is in the log");
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
	return Failures();
}
