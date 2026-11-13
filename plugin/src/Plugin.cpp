// The DLL's exports and real side effects; the decisions live in PluginEntry
// and ShadowLogic.

#include "GrassFadeFix.h"
#include "GrassFadePatchLogic.h"
#include "OcclusionCull.h"
#include "OcclusionCullLogic.h"
#include "OcclusionProbeLogic.h"
#include "PluginEntry.h"
#include "Probes.h"
#include "FastSceneUpdate.h"
#include "FastSqrt.h"
#include "FastVolatilePack.h"
#include "ActorBatch.h"
#include "HeapProbe.h"
#include "LockProbe.h"
#include "LogKeepLogic.h"
#include "ActorCensus.h"
#include "ActorFarUpdate.h"
#include "DrawInstancing.h"
#include "GpuPassTimer.h"
#include "ReferenceAnimRate.h"
#include "ActorShadowRest.h"
#include "PassListKeep.h"
#include "ShadowReceiverFilter.h"
#include "ActorThreads.h"
#include "ProcessProbe.h"
#include "VoiceProbe.h"
#include "TimedCalls.h"
#include "FastConstantArrange.h"
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
#include <string>
#include <vector>

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

// The finished session's log goes to OBNG-logs under its last write's time
// (LogKeepLogic.h) before the new one starts; an empty or missing log is
// nothing to keep. Called under the log's lock.
void KeepLastLog() {
	namespace logkeep = obng::plugin::logkeep;
	WIN32_FILE_ATTRIBUTE_DATA data{};
	if (!GetFileAttributesExA(kLogFile, GetFileExInfoStandard, &data) || (data.nFileSizeLow == 0 && data.nFileSizeHigh == 0)) {
		return;
	}
	FILETIME local{};
	SYSTEMTIME t{};
	if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &t)) {
		return;
	}
	CreateDirectoryA(logkeep::kFolder, nullptr);
	const std::string folder = std::string(logkeep::kFolder) + "\\";
	const std::string kept = folder + logkeep::KeptName(logkeep::Stamp{t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond});
	if (!MoveFileExA(kLogFile, kept.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		return;
	}
	std::vector<std::string> names;
	WIN32_FIND_DATAA found{};
	const HANDLE find = FindFirstFileA((folder + "OBNG-*.log").c_str(), &found);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			names.emplace_back(found.cFileName);
		} while (FindNextFileA(find, &found));
		FindClose(find);
	}
	for (const std::string& name : logkeep::ToRemove(names, logkeep::kKeep)) {
		DeleteFileA((folder + name).c_str());
	}
}

void ResetLog() {
	AcquireSRWLockExclusive(&g_logLock);
	KeepLastLog();
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

// The route settings as last read, for the installers that are not among the host's (the constant map's
// arranging, which wants the shadow's and the route's switch together).
obng::plugin::RouteSettings g_routeAsRead;

// The INI in use: a harness run's OBNG-test.ini when it is there, else OBNG.ini.
bool ActiveIniPath(char* path, size_t length) {
	if (!IniPath(path, length, kTestIniFile) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
		if (!IniPath(path, length, kIniFile) || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
			return false;
		}
	}
	return true;
}

bool ReadSettings(obng::plugin::Settings* settings) {
	char path[MAX_PATH] = {};
	if (!ActiveIniPath(path, sizeof(path))) {
		return false;
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
	s.sqrt = GetPrivateProfileIntA("Shadow", "Sqrt", 0, path) != 0;
	s.actorUpdate = GetPrivateProfileIntA("Shadow", "ActorUpdate", 0, path) != 0;
	s.gridUpdate = GetPrivateProfileIntA("Shadow", "GridUpdate", 0, path) != 0;
	s.volatilePack = GetPrivateProfileIntA("Shadow", "VolatilePack", 0, path) != 0;
	s.fpuControlWord = GetPrivateProfileIntA("Probe", "FpuControlWord", 1, path) != 0;
	s.havokPlanner = GetPrivateProfileIntA("Probe", "HavokPlanner", 1, path) != 0;
	s.callCounters = GetPrivateProfileIntA("Probe", "CallCounters", 0, path) != 0;
	s.passSteps = GetPrivateProfileIntA("Probe", "PassSteps", 0, path) != 0;
	s.packCache = GetPrivateProfileIntA("Probe", "PackCache", 0, path) != 0;
	s.constantUploads = GetPrivateProfileIntA("Probe", "ConstantUploads", 0, path) != 0;
	s.viewConstants = GetPrivateProfileIntA("Probe", "ViewConstants", 0, path) != 0;
	s.drawEntries = GetPrivateProfileIntA("Probe", "DrawEntries", 0, path) != 0;
	s.sceneUpdate = GetPrivateProfileIntA("Probe", "SceneUpdate", 0, path) != 0;
	s.actorUpdateTiming = GetPrivateProfileIntA("Probe", "ActorUpdateTiming", 0, path) != 0;
	if (s.viewConstants && !s.constantUploads) {
		s.constantUploads = true;  // the view probe keys the uploads probe's verdicts
	}
	s.occlusion = GetPrivateProfileIntA("Probe", "Occlusion", 0, path) != 0;
	s.occlusionEvery = GetPrivateProfileIntA("Probe", "OcclusionEvery", 31, path);
	s.unroutedDraws = GetPrivateProfileIntA("Probe", "UnroutedDraws", 0, path) != 0;
	s.grass = GetPrivateProfileIntA("Probe", "Grass", 0, path) != 0;
	s.memoryProbe = GetPrivateProfileIntA("Probe", "Memory", 1, path) != 0;
	s.heapCalls = GetPrivateProfileIntA("Probe", "HeapCalls", 0, path) != 0;
	s.lockCalls = GetPrivateProfileIntA("Probe", "LockCalls", 0, path) != 0;
	s.processManager = GetPrivateProfileIntA("Probe", "ProcessManager", 0, path) != 0;
	s.actorCensus = GetPrivateProfileIntA("Probe", "ActorCensus", 0, path) != 0;
	s.constantArrange = GetPrivateProfileIntA("Shadow", "ConstantArrange", 0, path) != 0;
	s.mismatchLines = GetPrivateProfileIntA("Log", "MismatchLines", 20, path);
	s.summaryEverySeconds = GetPrivateProfileIntA("Log", "SummaryEverySeconds", 10, path);
	s.summaryTimer = GetPrivateProfileIntA("Log", "SummaryTimer", 0, path) != 0;
	obng::plugin::RouteSettings& r = settings->route;
	r.rangeWalk = GetPrivateProfileIntA("Route", "RangeWalk", 0, path) != 0;
	r.sqrt = GetPrivateProfileIntA("Route", "Sqrt", 0, path) != 0;
	r.actorUpdate = GetPrivateProfileIntA("Route", "ActorUpdate", 0, path) != 0;
	r.gridUpdate = GetPrivateProfileIntA("Route", "GridUpdate", 0, path) != 0;
	r.volatilePack = GetPrivateProfileIntA("Route", "VolatilePack", 0, path) != 0;
	r.actorBatch = GetPrivateProfileIntA("Route", "ActorBatch", 0, path) != 0;
	r.constantArrange = GetPrivateProfileIntA("Route", "ConstantArrange", 0, path) != 0;
	r.actorBatchTicks = GetPrivateProfileIntA("Route", "ActorBatchTicks", 0, path) != 0;
	r.collisionWrites = GetPrivateProfileIntA("Probe", "CollisionWrites", 0, path) != 0;
	r.actorUnusedBones = GetPrivateProfileIntA("Route", "ActorUnusedBones", 0, path) != 0;
	r.actorFarEvery = GetPrivateProfileIntA("Route", "ActorFarEvery", 1, path);
	if (GetPrivateProfileIntA("Route", "ActorFarWhole", 0, path) != 0) {
		r.actorFarEvery = 1;  // the whole update is left out instead (ActorFarUpdate.cpp): not the skeleton's a second time
	}
	r.actorFarDistance = static_cast<float>(GetPrivateProfileIntA("Route", "ActorFarDistance", 6000, path));
	r.actorFarAlternate = GetPrivateProfileIntA("Route", "ActorFarAlternate", 0, path) != 0;
	r.unusedBonesProbe = GetPrivateProfileIntA("Probe", "UnusedBones", 0, path) != 0;
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
	if (s.grass && !r.stereoDraw) {
		Log("Settings: [Probe] Grass=1 counts nothing without [Route] StereoDraw=1 (it counts through the route's detours)");
	}
	if (s.unroutedDraws && !r.stereoDraw) {
		Log("Settings: [Probe] UnroutedDraws=1 counts nothing without [Route] StereoDraw=1 (its hooks are put at the first routed draw)");
	}
	r.stereoFollowers = GetPrivateProfileIntA("Route", "StereoFollowers", 1, path) != 0;
	r.stereoRestoreDirect = GetPrivateProfileIntA("Route", "StereoRestoreDirect", 1, path) != 0;
	r.stereoParticles = GetPrivateProfileIntA("Route", "StereoParticles", 1, path) != 0;
	r.stereoCarryConstants = GetPrivateProfileIntA("Route", "StereoCarryConstants", 1, path) != 0;
	r.stereoDrawTimer = GetPrivateProfileIntA("Route", "StereoDrawTimer", 0, path) != 0;
	r.stereoTraceConstants = GetPrivateProfileIntA("Route", "StereoTraceConstants", 0, path) != 0;
	r.occlusionCull = GetPrivateProfileIntA("Route", "OcclusionCull", 0, path) != 0;
	r.occlusionCullMaxAge = obng::plugin::occlusion_cull::MaxAgeFromIni(GetPrivateProfileIntA("Route", "OcclusionCullMaxAge", 2, path));
	r.occlusionCullMargin = obng::plugin::occlusion_cull::MarginFromIni(GetPrivateProfileIntA("Route", "OcclusionCullMargin", 2, path));
	{
		r.occlusionCullBeforeBlend = GetPrivateProfileIntA("Route", "OcclusionCullBeforeBlend", 1, path) != 0;
		const UINT every = GetPrivateProfileIntA("Route", "OcclusionCullVisibleEvery", 1, path);
		r.occlusionCullVisibleEvery = every < 1 ? 1 : every > 16 ? 16 : every;
	}
	r.occlusionCullVisibleAlternate = GetPrivateProfileIntA("Route", "OcclusionCullVisibleAlternate", 0, path) != 0;
	char cullMove[32] = {};
	GetPrivateProfileStringA("Route", "OcclusionCullMaxMove", "100", cullMove, sizeof(cullMove), path);
	r.occlusionCullMaxMove = static_cast<float>(std::strtod(cullMove, nullptr));
	char cullTurn[32] = {};
	GetPrivateProfileStringA("Route", "OcclusionCullMaxTurn", "10", cullTurn, sizeof(cullTurn), path);
	r.occlusionCullMaxTurn = static_cast<float>(std::strtod(cullTurn, nullptr));
	r.occlusionCullFlat = GetPrivateProfileIntA("Route", "OcclusionCullFlat", 0, path) != 0;
	r.occlusionCullStats = GetPrivateProfileIntA("Route", "OcclusionCullStats", 0, path) != 0;
	r.occlusionCullNodes = GetPrivateProfileIntA("Route", "OcclusionCullNodes", 0, path) != 0;
	r.occlusionCullNodeLog = static_cast<UInt32>(GetPrivateProfileIntA("Route", "OcclusionCullNodeLog", 0, path));
	r.occlusionCullEyeTarget = GetPrivateProfileIntA("Route", "OcclusionCullEyeTarget", 1, path) != 0;
	r.occlusionCullDriverLog = GetPrivateProfileIntA("Route", "OcclusionCullDriverLog", 0, path) != 0;
	r.occlusionCullTrace = static_cast<UInt32>(GetPrivateProfileIntA("Route", "OcclusionCullTrace", 0, path));
	char traceDepth[32] = {};
	GetPrivateProfileStringA("Route", "OcclusionCullTraceDepth", "-1", traceDepth, sizeof(traceDepth), path);
	r.occlusionCullTraceDepth = static_cast<float>(std::strtod(traceDepth, nullptr));
	if (const char* blocker = obng::plugin::occlusion_cull::BlockedBy(r.stereoDraw, s.passSteps, s.constantUploads, s.occlusion);
	    r.occlusionCull && blocker != nullptr) {
		Log("Settings: [Route] OcclusionCull=1 cannot run beside %s - the culler is off", blocker);
		r.occlusionCull = false;
	}
	Log("Settings: [Route] OcclusionCull=%d OcclusionCullFlat=%d OcclusionCullMaxAge=%u OcclusionCullMargin=%u OcclusionCullMaxMove=%.0f "
	    "OcclusionCullMaxTurn=%.0f OcclusionCullTrace=%u",
	    r.occlusionCull ? 1 : 0, r.occlusionCullFlat ? 1 : 0, r.occlusionCullMaxAge, r.occlusionCullMargin, static_cast<double>(r.occlusionCullMaxMove),
	    static_cast<double>(r.occlusionCullMaxTurn), r.occlusionCullTrace);
	g_routeAsRead = r;
	obng::plugin::FixSettings& f = settings->fix;
	f.grassFade = GetPrivateProfileIntA("Fix", "GrassFade", 1, path) != 0;
	char fadeReach[32] = {};
	GetPrivateProfileStringA("Fix", "GrassFadeReach", "game", fadeReach, sizeof(fadeReach), path);
	bool fadeReachKnown = false;
	f.grassFadeIniReach = obng::plugin::grass_fade::ReachFromIni(fadeReach, &fadeReachKnown) == obng::plugin::grass_fade::Reach::kIni;
	if (!fadeReachKnown) {
		Log("Settings: [Fix] GrassFadeReach=%s is neither game nor ini - game used", fadeReach);
	}
	Log("Settings: [Fix] GrassFade=%d GrassFadeReach=%s", f.grassFade ? 1 : 0, f.grassFadeIniReach ? "ini" : "game");
	Log("Settings from %s: TimeInfoUpdate=%d TimeInfoInit=%d TimeInfoFreeze=%d TimeInfoUnfreeze=%d LoadingMenuTick=%d "
	    "LoadingMenuCtor=%d RangeWalk=%d RangeWalkOrder=%s Sqrt=%d ActorUpdate=%d FpuControlWord=%d HavokPlanner=%d CallCounters=%d PassSteps=%d PackCache=%d ConstantUploads=%d ViewConstants=%d DrawEntries=%d SceneUpdate=%d Occlusion=%d OcclusionEvery=%u UnroutedDraws=%d Grass=%d Memory=%d MismatchLines=%u SummaryEverySeconds=%u "
	    "SummaryTimer=%d Route RangeWalk=%d Sqrt=%d ActorUpdate=%d StereoDraw=%d StereoEyes=%u StereoTestEyeShift=%.3f StereoReapply=%s StereoFollowers=%d StereoRestoreDirect=%d StereoParticles=%d StereoCarryConstants=%d StereoTraceConstants=%d",
	    path, s.update ? 1 : 0, s.init ? 1 : 0, s.freeze ? 1 : 0, s.unfreeze ? 1 : 0, s.loadingMenuTick ? 1 : 0,
	    s.loadingMenuCtor ? 1 : 0, s.rangeWalk ? 1 : 0, obng::plugin::RangeWalkOrderName(s.rangeWalkOrder), s.sqrt ? 1 : 0, s.actorUpdate ? 1 : 0, s.fpuControlWord ? 1 : 0, s.havokPlanner ? 1 : 0, s.callCounters ? 1 : 0,
	    s.passSteps ? 1 : 0, s.packCache ? 1 : 0, s.constantUploads ? 1 : 0, s.viewConstants ? 1 : 0, s.drawEntries ? 1 : 0, s.sceneUpdate ? 1 : 0, s.occlusion ? 1 : 0, s.occlusionEvery, s.unroutedDraws ? 1 : 0, s.grass ? 1 : 0, s.memoryProbe ? 1 : 0, s.mismatchLines, s.summaryEverySeconds, s.summaryTimer ? 1 : 0, r.rangeWalk ? 1 : 0, r.sqrt ? 1 : 0, r.actorUpdate ? 1 : 0, r.stereoDraw ? 1 : 0, static_cast<unsigned>(r.stereoEyes), static_cast<double>(r.stereoTestEyeShift), obng::plugin::StereoReapplyName(r.stereoReapply), r.stereoFollowers ? 1 : 0, r.stereoRestoreDirect ? 1 : 0, r.stereoParticles ? 1 : 0, r.stereoCarryConstants ? 1 : 0, r.stereoTraceConstants ? 1 : 0);
	return true;
}

void InitRecorderSideEffect(const obng::plugin::ShadowSettings& settings) {
	obng::plugin::InitRecorder(settings, &Log);
	obng::plugin::InstallConstantArrange(settings, g_routeAsRead, &Log);
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
	// The culler sits above the route (Do_RenderShape, the finish) and asks it
	// for the eye data; Load calls this only with the route on.
	const uint32_t made = obng::plugin::InstallRouteStereoDraw(settings, &Log) + obng::plugin::InstallOcclusionCull(settings, &Log);
	char path[MAX_PATH] = {};
	if (ActiveIniPath(path, sizeof(path))) {
		obng::plugin::InstallGpuPassTimer(path, &Log);  // before the instancing: its pass list hook serves the timer too
		obng::plugin::InstallDrawInstancing(path, settings, &Log);  // below the culler: its two patches are not among the counted ones
	}
	return made;
}

bool StartSummaryTimerSideEffect(const obng::plugin::ShadowSettings&) {
	return obng::plugin::StartSummaryTimer();
}

uint32_t InstallProbesSideEffect(const obng::plugin::ShadowSettings& settings) {
	obng::plugin::InstallHeapProbe(settings, &Log);
	obng::plugin::InstallProcessProbe(settings, &Log);
	char path[MAX_PATH] = {};
	const bool havePath = ActiveIniPath(path, sizeof(path));
	if (havePath) {
		obng::plugin::InstallVoiceProbe(path, &Log);
		obng::plugin::InstallActorFarUpdate(path, &Log);  // before the census: the route has the entry first
		obng::plugin::InstallReferenceAnimRate(path, &Log);  // before the [Timed] lines: the route has the entry first
		obng::plugin::InstallActorShadowRest(path, &Log);   // call sites and two patches inside the actor shadow step, no entry
		obng::plugin::InstallPassListKeep(path, &Log);      // the relight's two call sites and three vtable slots, no entry
		obng::plugin::InstallShadowReceiverFilter(path, &Log);  // AddGeometry's entry (not with [Shadow] RangeWalk)
		obng::plugin::InstallActorThreads(path, &Log);           // inside the actors' batch (needs it installed: the routes come first)
	}
	obng::plugin::InstallActorCensus(settings, &Log);
	obng::plugin::InstallLockProbe(settings, &Log);  // its two detours are not among the counted ones
	const uint32_t made = obng::plugin::InstallProbes(settings, &Log);
	// Last of all: a timed function must not take an entry a route or another probe wants.
	if (havePath) {
		obng::plugin::InstallTimedCalls(path, &Log);
	}
	return made;
}

bool InstallFixesSideEffect(const obng::plugin::FixSettings& settings) {
	return obng::plugin::InstallGrassFadeFix(settings, &Log);
}

uint32_t InstallShadowSqrtSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallShadowFastSqrt(settings, &Log);
}

uint32_t InstallRouteSqrtSideEffect(const obng::plugin::RouteSettings& settings) {
	return obng::plugin::InstallRouteFastSqrt(settings, &Log);
}

uint32_t InstallShadowActorUpdateSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallShadowActorUpdate(settings, &Log);
}

uint32_t InstallRouteActorUpdateSideEffect(const obng::plugin::RouteSettings& settings) {
	return obng::plugin::InstallRouteActorUpdate(settings, &Log);
}

uint32_t InstallActorUpdateTimingSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallActorUpdateTiming(settings, &Log);
}

uint32_t InstallShadowVolatilePackSideEffect(const obng::plugin::ShadowSettings& settings) {
	return obng::plugin::InstallShadowVolatilePack(settings, &Log);
}

uint32_t InstallRouteVolatilePackSideEffect(const obng::plugin::RouteSettings& settings) {
	return obng::plugin::InstallRouteVolatilePack(settings, &Log);
}

uint32_t InstallRouteActorBatchSideEffect(const obng::plugin::RouteSettings& settings) {
	return obng::plugin::InstallRouteActorBatch(settings, &Log);
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
                               &InstallRouteStereoDrawSideEffect,
                               &InstallFixesSideEffect,
                               &InstallShadowSqrtSideEffect,
                               &InstallRouteSqrtSideEffect,
                               &InstallShadowActorUpdateSideEffect,
                               &InstallRouteActorUpdateSideEffect,
                               &InstallActorUpdateTimingSideEffect,
                               &InstallShadowVolatilePackSideEffect,
                               &InstallRouteVolatilePackSideEffect,
                               &InstallRouteActorBatchSideEffect};

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
