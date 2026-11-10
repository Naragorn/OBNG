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
}

void LogProcessProbeSummary() {
	if (!g_installed) {
		return;
	}
	AcquireSRWLockExclusive(&g_lock);
	const probe::SpanStats taken = g_stats;
	g_stats = probe::SpanStats{};
	ReleaseSRWLockExclusive(&g_lock);
	char line[200];
	probe::FormatSpan("process manager 0x00678510", taken, static_cast<std::uint64_t>(g_frequency.QuadPart), kLimitMs, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
