#include "ProcessProbe.h"

#include <windows.h>

#include "ProbeLogic.h"

namespace obng::plugin {

namespace {

constexpr const char* kName = "ProcessManager::Update (timed)";
constexpr UInt32 kAddress = 0x00678510;                              // __thiscall(), RET (read 0x0067868A and 0x0067869B)
constexpr UInt8 kBytes[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1};     // sub esp,8; push esi; mov esi,ecx (read)
constexpr double kLimitMs = 3.0;
using UpdateFn = void(__fastcall*)(void* self, void* edx);

void* g_original = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;
LARGE_INTEGER g_frequency{};
SRWLOCK g_lock = SRWLOCK_INIT;
probe::SpanStats g_stats;

// The character controller's update 0x00896000 (the game's Havok character
// update; __thiscall(float*), RET 4, read 0x0089701A) and its parts, as
// ActorBatch.cpp reads them: the support probes 0x00891780 (RET), the
// integrate wrapper 0x008902B0 (RET 4), 0x00894C70 (RET 4), 0x008939B0
// (RET), the support check 0x008AE890 (RET 8). All six begin with the same
// six bytes. The parts count only inside a controller's update on its
// thread. The moving bit: controller +0x1F4 bit 3 (as ActorBatch.cpp counts
// it).
constexpr UInt8 kControllerBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-10h (read)
constexpr UInt32 kControllerAddress = 0x00896000;
constexpr double kControllerLimitMs = 0.1;
constexpr unsigned kParts = 5;
constexpr UInt32 kPartAddress[kParts] = {0x00891780, 0x008902B0, 0x00894C70, 0x008939B0, 0x008AE890};
constexpr const char* kPartName[kParts] = {"controller: support probes 0x00891780", "controller: integrate 0x008902B0", "controller: 0x00894C70",
                                           "controller: 0x008939B0", "controller: support check 0x008AE890"};
using Call0Fn = void(__fastcall*)(void* self, void* edx);
using Call1Fn = void(__fastcall*)(void* self, void* edx, UInt32 a);
using Call2Fn = void(__fastcall*)(void* self, void* edx, UInt32 a, UInt32 b);

void* g_controllerOriginal = nullptr;
void* g_partOriginal[kParts] = {};
probe::SpanStats g_controller;
probe::SpanStats g_part[kParts];
std::uint64_t g_moving = 0;
volatile LONG g_controllerThread = 0;  // the thread inside a controller's update, 0 when none

void Account(probe::SpanStats* stats, const LARGE_INTEGER& start, const LARGE_INTEGER& end, double limitMs) {
	const std::uint64_t limit = static_cast<std::uint64_t>(limitMs * static_cast<double>(g_frequency.QuadPart) / 1000.0);
	AcquireSRWLockExclusive(&g_lock);
	stats->Add(static_cast<std::uint64_t>(end.QuadPart - start.QuadPart), limit);
	ReleaseSRWLockExclusive(&g_lock);
}

void __fastcall ControllerReplacement(void* self, void* /*edx*/, UInt32 argument) {
	const LONG outer = InterlockedExchange(&g_controllerThread, static_cast<LONG>(GetCurrentThreadId()));
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Call1Fn>(g_controllerOriginal)(self, nullptr, argument);
	QueryPerformanceCounter(&end);
	InterlockedExchange(&g_controllerThread, outer);
	const bool moving = (*reinterpret_cast<const UInt32*>(reinterpret_cast<UInt32>(self) + 0x1F4) & 8u) != 0;
	Account(&g_controller, start, end, kControllerLimitMs);
	if (moving) {
		AcquireSRWLockExclusive(&g_lock);
		++g_moving;
		ReleaseSRWLockExclusive(&g_lock);
	}
}

bool InController() { return g_controllerThread == static_cast<LONG>(GetCurrentThreadId()); }

template <unsigned Index>
void __fastcall Part0(void* self, void* /*edx*/) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Call0Fn>(g_partOriginal[Index])(self, nullptr);
	QueryPerformanceCounter(&end);
	if (InController()) {
		Account(&g_part[Index], start, end, kControllerLimitMs);
	}
}
template <unsigned Index>
void __fastcall Part1(void* self, void* /*edx*/, UInt32 a) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Call1Fn>(g_partOriginal[Index])(self, nullptr, a);
	QueryPerformanceCounter(&end);
	if (InController()) {
		Account(&g_part[Index], start, end, kControllerLimitMs);
	}
}
template <unsigned Index>
void __fastcall Part2(void* self, void* /*edx*/, UInt32 a, UInt32 b) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Call2Fn>(g_partOriginal[Index])(self, nullptr, a, b);
	QueryPerformanceCounter(&end);
	if (InController()) {
		Account(&g_part[Index], start, end, kControllerLimitMs);
	}
}

void __fastcall Replacement(void* self, void* /*edx*/) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<UpdateFn>(g_original)(self, nullptr);
	QueryPerformanceCounter(&end);
	const std::uint64_t limit = static_cast<std::uint64_t>(kLimitMs * static_cast<double>(g_frequency.QuadPart) / 1000.0);
	AcquireSRWLockExclusive(&g_lock);
	g_stats.Add(static_cast<std::uint64_t>(end.QuadPart - start.QuadPart), limit);
	ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace

void InstallProcessProbe(const ShadowSettings& settings, LogFn log) {
	if (g_installed || !settings.processManager) {
		return;
	}
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	g_installed = InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(&Replacement), &g_original, log);
	if (!g_installed) {
		return;
	}
	// The controller and its parts; each is counted only when its own detour
	// went in (another probe may hold an entry: [Route] ActorBatchTicks).
	InstallEntryDetour("CharacterController::Update (timed)", kControllerAddress, kControllerBytes, sizeof(kControllerBytes),
	                   reinterpret_cast<const void*>(&ControllerReplacement), &g_controllerOriginal, log);
	const void* const parts[kParts] = {reinterpret_cast<const void*>(&Part0<0>), reinterpret_cast<const void*>(&Part1<1>),
	                                   reinterpret_cast<const void*>(&Part1<2>), reinterpret_cast<const void*>(&Part0<3>),
	                                   reinterpret_cast<const void*>(&Part2<4>)};
	for (unsigned i = 0; i < kParts; ++i) {
		InstallEntryDetour(kPartName[i], kPartAddress[i], kControllerBytes, sizeof(kControllerBytes), parts[i], &g_partOriginal[i], log);
	}
}

void LogProcessProbeSummary() {
	if (!g_installed) {
		return;
	}
	AcquireSRWLockExclusive(&g_lock);
	const probe::SpanStats taken = g_stats;
	g_stats = probe::SpanStats{};
	const probe::SpanStats controller = g_controller;
	g_controller = probe::SpanStats{};
	const std::uint64_t moving = g_moving;
	g_moving = 0;
	probe::SpanStats part[kParts];
	for (unsigned i = 0; i < kParts; ++i) {
		part[i] = g_part[i];
		g_part[i] = probe::SpanStats{};
	}
	ReleaseSRWLockExclusive(&g_lock);
	char line[200];
	const std::uint64_t frequency = static_cast<std::uint64_t>(g_frequency.QuadPart);
	probe::FormatSpan("process manager 0x00678510", taken, frequency, kLimitMs, line, sizeof(line));
	g_log("%s", line);
	if (g_controllerOriginal != nullptr) {
		probe::FormatSpan("character controller 0x00896000", controller, frequency, kControllerLimitMs, line, sizeof(line));
		g_log("%s", line);
		probe::FormatMoving(moving, controller.calls, line, sizeof(line));
		g_log("%s", line);
	}
	for (unsigned i = 0; i < kParts; ++i) {
		if (g_partOriginal[i] != nullptr) {
			probe::FormatSpan(kPartName[i], part[i], frequency, kControllerLimitMs, line, sizeof(line));
			g_log("%s", line);
		}
	}
}

}  // namespace obng::plugin
