// The DLL's exports and real side effects; the decisions live in PluginEntry
// and ShadowLogic.

#include "OcclusionProbeLogic.h"
#include "PluginEntry.h"
#include "Probes.h"
#include "RouteRangeWalk.h"
#include "RouteStereoDraw.h"
#include "ShadowRangeWalk.h"
#include "ShadowRecorder.h"
#include "ShadowTimeInfo.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char* kLogFile = "OBNG.log";
constexpr const char* kIniFile = "OBNG.ini";
// A harness run's settings, written next to the DLL for the run and removed
// after it, so the player's OBNG.ini is never touched (the shape of OBVR's
// OBVR-test.ini). When it exists it replaces OBNG.ini entirely.
constexpr const char* kTestIniFile = "OBNG-test.ini";

HMODULE g_module = nullptr;
SRWLOCK g_logLock = SRWLOCK_INIT;

void AppendLine(const char* format, va_list args) {
	AcquireSRWLockExclusive(&g_logLock);
	if (FILE* file = std::fopen(kLogFile, "a")) {
		std::vfprintf(file, format, args);
		std::fputc('\n', file);
		std::fclose(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

void Log(const char* format, ...) {
	va_list args;
	va_start(args, format);
	AppendLine(format, args);
	va_end(args);
}

void ResetLog() {
	AcquireSRWLockExclusive(&g_logLock);
	if (FILE* file = std::fopen(kLogFile, "w")) {
		std::fclose(file);
	}
	ReleaseSRWLockExclusive(&g_logLock);
}

bool IniPath(char* path, size_t length, const char* name) {
	char module[MAX_PATH] = {};
	if (GetModuleFileNameA(g_module, module, MAX_PATH) == 0) {
		return false;
	}
	char* slash = std::strrchr(module, '\\');
	if (slash == nullptr) {
		return false;
	}
	*(slash + 1) = '\0';
	std::snprintf(path, length, "%s%s", module, name);
	return true;
}

bool ReadSettings(obng::plugin::Settings* settings) {
	char path[MAX_PATH] = {};
	if (!IniPath(path, sizeof(path), kTestIniFile) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		if (!IniPath(path, sizeof(path), kIniFile) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
			return false;
		}
	}
	obng::plugin::ShadowSettings& s = settings->shadow;
	s.update = GetPrivateProfileIntA("Shadow", "TimeInfoUpdate", 1, path) != 0;
	s.init = GetPrivateProfileIntA("Shadow", "TimeInfoInit", 1, path) != 0;
	s.freeze = GetPrivateProfileIntA("Shadow", "TimeInfoFreeze", 1, path) != 0;
	s.unfreeze = GetPrivateProfileIntA("Shadow", "TimeInfoUnfreeze", 1, path) != 0;
	s.loadingMenuTick = GetPrivateProfileIntA("Shadow", "LoadingMenuTick", 1, path) != 0;
	s.loadingMenuCtor = GetPrivateProfileIntA("Shadow", "LoadingMenuCtor", 1, path) != 0;
	s.rangeWalk = GetPrivateProfileIntA("Shadow", "RangeWalk", 0, path) != 0;
	char order[32] = {};
	GetPrivateProfileStringA("Shadow", "RangeWalkOrder", "original-first", order, sizeof(order), path);
	bool orderKnown = false;
	s.rangeWalkOrder = obng::plugin::ParseRangeWalkOrder(order, &orderKnown);
	if (!orderKnown) {
		Log("Settings: [Shadow] RangeWalkOrder=%s is neither original-first nor rebuild-first - original-first used", order);
	}
	s.fpuControlWord = GetPrivateProfileIntA("Probe", "FpuControlWord", 1, path) != 0;
	s.havokPlanner = GetPrivateProfileIntA("Probe", "HavokPlanner", 1, path) != 0;
	s.callCounters = GetPrivateProfileIntA("Probe", "CallCounters", 0, path) != 0;
	s.passSteps = GetPrivateProfileIntA("Probe", "PassSteps", 0, path) != 0;
	s.packCache = GetPrivateProfileIntA("Probe", "PackCache", 0, path) != 0;
	s.constantUploads = GetPrivateProfileIntA("Probe", "ConstantUploads", 0, path) != 0;
	s.viewConstants = GetPrivateProfileIntA("Probe", "ViewConstants", 0, path) != 0;
	s.drawEntries = GetPrivateProfileIntA("Probe", "DrawEntries", 0, path) != 0;
	if (s.viewConstants && !s.constantUploads) {
		s.constantUploads = true;  // the view probe keys the uploads probe's verdicts
	}
	s.occlusion = GetPrivateProfileIntA("Probe", "Occlusion", 0, path) != 0;
	s.occlusionEvery = GetPrivateProfileIntA("Probe", "OcclusionEvery", 31, path);
	s.unroutedDraws = GetPrivateProfileIntA("Probe", "UnroutedDraws", 0, path) != 0;
	s.memoryProbe = GetPrivateProfileIntA("Probe", "Memory", 1, path) != 0;
	s.mismatchLines = GetPrivateProfileIntA("Log", "MismatchLines", 20, path);
	s.summaryEverySeconds = GetPrivateProfileIntA("Log", "SummaryEverySeconds", 10, path);
	s.summaryTimer = GetPrivateProfileIntA("Log", "SummaryTimer", 0, path) != 0;
	obng::plugin::RouteSettings& r = settings->route;
	r.rangeWalk = GetPrivateProfileIntA("Route", "RangeWalk", 0, path) != 0;
	r.stereoDraw = GetPrivateProfileIntA("Route", "StereoDraw", 0, path) != 0;
	bool eyesKnown = false;
	r.stereoEyes = obng::plugin::StereoEyesFromIni(GetPrivateProfileIntA("Route", "StereoEyes", 1, path), &eyesKnown);
	if (!eyesKnown) {
		Log("Settings: [Route] StereoEyes is neither 1 nor 2 - 1 used");
	}
	char shift[32] = {};
	GetPrivateProfileStringA("Route", "StereoTestEyeShift", "0", shift, sizeof(shift), path);
	r.stereoTestEyeShift = static_cast<float>(std::strtod(shift, nullptr));
	char reapply[32] = {};
	GetPrivateProfileStringA("Route", "StereoReapply", "view", reapply, sizeof(reapply), path);
	bool reapplyKnown = false;
	r.stereoReapply = obng::plugin::StereoReapplyFromIni(reapply, &reapplyKnown);
	if (!reapplyKnown) {
		Log("Settings: [Route] StereoReapply=%s is neither slot14, view nor none - view used", reapply);
	}
	if (const char* blocker = obng::plugin::occlusion::BlockedBy(s.passSteps, s.drawEntries, s.constantUploads, r.stereoDraw);
	    s.occlusion && blocker != nullptr) {
		Log("Settings: [Probe] Occlusion=1 cannot run beside %s (they share entries) - the occlusion probe is off", blocker);
		s.occlusion = false;
	}
	if (s.unroutedDraws && !r.stereoDraw) {
		Log("Settings: [Probe] UnroutedDraws=1 counts nothing without [Route] StereoDraw=1 (its hooks are put at the first routed draw)");
	}
	r.stereoFollowers = GetPrivateProfileIntA("Route", "StereoFollowers", 1, path) != 0;
	r.stereoRestoreDirect = GetPrivateProfileIntA("Route", "StereoRestoreDirect", 1, path) != 0;
	Log("Settings from %s: TimeInfoUpdate=%d TimeInfoInit=%d TimeInfoFreeze=%d TimeInfoUnfreeze=%d LoadingMenuTick=%d "
	    "LoadingMenuCtor=%d RangeWalk=%d RangeWalkOrder=%s FpuControlWord=%d HavokPlanner=%d CallCounters=%d PassSteps=%d PackCache=%d ConstantUploads=%d ViewConstants=%d DrawEntries=%d Occlusion=%d OcclusionEvery=%u UnroutedDraws=%d Memory=%d MismatchLines=%u SummaryEverySeconds=%u "
	    "SummaryTimer=%d Route RangeWalk=%d StereoDraw=%d StereoEyes=%u StereoTestEyeShift=%.3f StereoReapply=%s StereoFollowers=%d StereoRestoreDirect=%d",
	    path, s.update ? 1 : 0, s.init ? 1 : 0, s.freeze ? 1 : 0, s.unfreeze ? 1 : 0, s.loadingMenuTick ? 1 : 0,
	    s.loadingMenuCtor ? 1 : 0, s.rangeWalk ? 1 : 0, obng::plugin::RangeWalkOrderName(s.rangeWalkOrder), s.fpuControlWord ? 1 : 0, s.havokPlanner ? 1 : 0, s.callCounters ? 1 : 0,
	    s.passSteps ? 1 : 0, s.packCache ? 1 : 0, s.constantUploads ? 1 : 0, s.viewConstants ? 1 : 0, s.drawEntries ? 1 : 0, s.occlusion ? 1 : 0, s.occlusionEvery, s.unroutedDraws ? 1 : 0, s.memoryProbe ? 1 : 0, s.mismatchLines, s.summaryEverySeconds, s.summaryTimer ? 1 : 0, r.rangeWalk ? 1 : 0, r.stereoDraw ? 1 : 0, static_cast<unsigned>(r.stereoEyes), static_cast<double>(r.stereoTestEyeShift), obng::plugin::StereoReapplyName(r.stereoReapply), r.stereoFollowers ? 1 : 0, r.stereoRestoreDirect ? 1 : 0);
	return true;
}

void InitRecorderSideEffect(const obng::plugin::ShadowSettings& settings) {
	obng::plugin::InitRecorder(settings, &Log);
}

uint32_t InstallShadowSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallShadowTimeInfo(settings, &Log);
}

uint32_t InstallShadowLoadingMenuSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallShadowLoadingMenu(settings, &Log);
}

uint32_t InstallShadowRangeWalkSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallShadowRangeWalk(settings, &Log);
}

uint32_t InstallRouteRangeWalkSideEffect(const obng::plugin::RouteSettings& settings) {
	return obng::plugin::InstallRouteRangeWalk(settings, &Log);
}

uint32_t InstallRouteStereoDrawSideEffect(const obng::plugin::RouteSettings& settings) {
	return obng::plugin::InstallRouteStereoDraw(settings, &Log);
}

bool StartSummaryTimerSideEffect(const obng::plugin::ShadowSettings&) {
	return obng::plugin::StartSummaryTimer();
}

uint32_t InstallProbesSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallProbes(settings, &Log);
}

const obng::plugin::Host kHost{&ResetLog,
                               &Log,
                               &ReadSettings,
                               &InitRecorderSideEffect,
                               &InstallShadowSideEffect,
                               &InstallShadowLoadingMenuSideEffect,
                               &InstallShadowRangeWalkSideEffect,
                               &InstallRouteRangeWalkSideEffect,
                               &StartSummaryTimerSideEffect,
                               &InstallProbesSideEffect,
                               &InstallRouteStereoDrawSideEffect};

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

extern "C" {

__declspec(dllexport) bool OBSEPlugin_Query(const obng::plugin::ExtenderInterfaceHead* xse,
                                            obng::plugin::PluginInfo* info) {
	return obng::plugin::Query(*xse, info, kHost);
}

__declspec(dllexport) bool OBSEPlugin_Load(const obng::plugin::ExtenderInterfaceHead* xse) {
	return obng::plugin::Load(*xse, kHost);
}

// The stereo eye interface for OBVR (StereoEyesApi.h).
uint32_t ObngStereoPlan(void) {
	return obng::plugin::StereoPlan();
}

uint32_t ObngStereoSetEyes(const ObngStereoEyes* eyes) {
	return obng::plugin::SetStereoEyes(eyes);
}

void ObngStereoClearEyes(void) {
	obng::plugin::ClearStereoEyes();
}

}  // extern "C"
