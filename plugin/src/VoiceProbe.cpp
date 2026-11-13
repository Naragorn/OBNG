#include "VoiceProbe.h"

#include <windows.h>

#include <cstdlib>

#include "ProbeLogic.h"
#include "VoiceProbeLogic.h"

namespace obng::plugin {

namespace {

constexpr const char* kName = "voice player 0x006A9E00 (timed)";
constexpr UInt32 kAddress = 0x006A9E00;  // __thiscall(out, const char* path, byte), RET 0Ch (read 0x006AA15E)
constexpr UInt8 kBytes[] = {0x6A, 0xFF, 0x68, 0xEC, 0x62, 0x9C, 0x00};  // push -1; push 009C62ECh (read)
// EAX is handed back as it comes: whether the function returns a value is not read.
using PlayFn = UInt32(__fastcall*)(void* self, void* edx, UInt32 out, const char* path, UInt32 flag);

void* g_original = nullptr;
bool g_installed = false;
LogFn g_log = nullptr;
LARGE_INTEGER g_frequency{};
SRWLOCK g_lock = SRWLOCK_INIT;
probe::SpanStats g_stats;
std::uint64_t g_thresholdTicks = 0;
double g_thresholdMs = 1.0;
std::uint32_t g_atMost = 40;
volatile LONG g_written = 0;

UInt32 __fastcall Replacement(void* self, void* /*edx*/, UInt32 out, const char* path, UInt32 flag) {
	// The path is copied first: the caller's buffer may be gone or rewritten when the function returns.
	char clean[voice::kPathAtMost + 1];
	voice::CleanPath(path, clean, sizeof(clean));
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	const UInt32 result = reinterpret_cast<PlayFn>(g_original)(self, nullptr, out, path, flag);
	QueryPerformanceCounter(&end);
	const std::uint64_t ticks = static_cast<std::uint64_t>(end.QuadPart - start.QuadPart);
	AcquireSRWLockExclusive(&g_lock);
	g_stats.Add(ticks, g_thresholdTicks);
	ReleaseSRWLockExclusive(&g_lock);
	if (voice::Writes(ticks, g_thresholdTicks, static_cast<std::uint32_t>(g_written), g_atMost)) {
		InterlockedIncrement(&g_written);
		char line[voice::kPathAtMost + 64];
		voice::FormatLine(clean, ticks, static_cast<std::uint64_t>(g_frequency.QuadPart), line, sizeof(line));
		g_log("%s", line);
	}
	return result;
}

}  // namespace

void InstallVoiceProbe(const char* iniPath, LogFn log) {
	if (g_installed || iniPath == nullptr || GetPrivateProfileIntA("Probe", "VoiceLines", 0, iniPath) == 0) {
		return;
	}
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	char value[32] = {};
	GetPrivateProfileStringA("Probe", "VoiceLinesMs", "1.0", value, sizeof(value), iniPath);
	const double ms = std::strtod(value, nullptr);
	g_thresholdMs = ms >= 0.0 && ms <= 1000.0 ? ms : 1.0;
	g_thresholdTicks = static_cast<std::uint64_t>(g_thresholdMs * static_cast<double>(g_frequency.QuadPart) / 1000.0);
	const UINT atMost = GetPrivateProfileIntA("Probe", "VoiceLinesAtMost", 40, iniPath);
	g_atMost = atMost > 1000 ? 1000 : atMost;
	g_installed = InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(&Replacement), &g_original, log);
}

void LogVoiceProbeSummary() {
	if (!g_installed) {
		return;
	}
	AcquireSRWLockExclusive(&g_lock);
	const probe::SpanStats taken = g_stats;
	g_stats = probe::SpanStats{};
	ReleaseSRWLockExclusive(&g_lock);
	InterlockedExchange(&g_written, 0);
	char line[200];
	probe::FormatSpan("voice player 0x006A9E00", taken, static_cast<std::uint64_t>(g_frequency.QuadPart), g_thresholdMs, line, sizeof(line));
	g_log("%s", line);
}

}  // namespace obng::plugin
