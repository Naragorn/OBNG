// The counting detours and the memory sample. Each detour counts the call
// and its return site, then calls the original with the same arguments; the
// game sees no difference. The counts are kept with interlocked operations,
// because NiBound::WhichSide runs for every plane of every object and must
// not take a lock. The memory sample is taken when the summary is due.

#include "Probes.h"

#include <windows.h>

#include <intrin.h>

#include <cstdio>

#include "ProbeLogic.h"
#include "ShadowRecorder.h"

namespace obng::plugin {
namespace {

enum class Probed : int {
	SetupTransformations = 0,  // 0x007C9230, shader vtable slot 13 (re/functions/0x007C9230.md)
	Cull,                      // 0x0070DFB0, NiCullingProcess::Cull (re/functions/0x0070DFB0.md)
	IsInRange,                 // 0x007D2EA0, ShadowSceneLight::IsInRange (re/functions/0x007D2EA0.md)
	CopyVertexStreamElements,  // 0x0076E610 (re/functions/0x0076E610.md)
	WhichSide,                 // 0x0047DA70, NiBound::WhichSide (re/functions/0x0047DA70.md)
	Count,
};

const char* const kNames[] = {"SetupTransformations", "NiCullingProcess::Cull", "ShadowSceneLight::IsInRange",
                              "CopyVertexStreamElements", "NiBound::WhichSide"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<int>(Probed::Count), "one name per probed function");

// The entries (asm.s of the exports): whole instructions, nothing relative.
constexpr UInt32 kSetup = 0x007C9230;
constexpr UInt8 kSetupBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-16
constexpr UInt32 kCull = 0x0070DFB0;
constexpr UInt8 kCullBytes[] = {0x51, 0x55, 0x8B, 0xE9, 0x8B, 0x85, 0x8C, 0x00, 0x00, 0x00};  // push ecx; push ebp; mov ebp,ecx; mov eax,[ebp+8Ch]
constexpr UInt32 kInRange = 0x007D2EA0;
constexpr UInt8 kInRangeBytes[] = {0x83, 0xEC, 0x1C, 0x53, 0x56};  // sub esp,1Ch; push ebx; push esi
constexpr UInt32 kCopy = 0x0076E610;
constexpr UInt8 kCopyBytes[] = {0x83, 0xEC, 0x0C, 0x53, 0x55};  // sub esp,0Ch; push ebx; push ebp
constexpr UInt32 kWhichSide = 0x0047DA70;
constexpr UInt8 kWhichSideBytes[] = {0x8B, 0x44, 0x24, 0x04, 0xD9, 0x40, 0x04};  // mov eax,[esp+4]; fld dword [eax+4]

// __fastcall with a dummy edx mirrors __thiscall: ECX is this, the rest on
// the stack, the callee cleans (RET N as the original does).
using SetupFn = UInt32(__fastcall*)(void* self, void* edx, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32, UInt32);
using CullFn = void(__fastcall*)(void* self, void* edx, UInt32 object);
using InRangeFn = UInt8(__fastcall*)(void* self, void* edx, UInt32 object, UInt32 light);
using CopyFn = UInt32(__cdecl*)(const void* desc);
using WhichSideFn = int(__fastcall*)(const void* sphere, void* edx, const void* plane);

SetupFn g_setup = nullptr;
CullFn g_cull = nullptr;
InRangeFn g_inRange = nullptr;
CopyFn g_copy = nullptr;
WhichSideFn g_whichSide = nullptr;

// The interlocked counterpart of probe::CallCounts: a slot is claimed with a
// compare-exchange on its address, the counts are incremented interlocked.
struct AtomicCounts {
	volatile LONG64 calls = 0;
	volatile LONG addresses[probe::ReturnSites::kSites] = {};
	volatile LONG64 counts[probe::ReturnSites::kSites] = {};
	volatile LONG64 others = 0;

	void Add(UInt32 returnAddress) {
		InterlockedIncrement64(&calls);
		for (unsigned i = 0; i < probe::ReturnSites::kSites; ++i) {
			const LONG seen = InterlockedCompareExchange(&addresses[i], static_cast<LONG>(returnAddress), 0);
			if (seen == 0 || static_cast<UInt32>(seen) == returnAddress) {
				InterlockedIncrement64(&counts[i]);
				return;
			}
		}
		InterlockedIncrement64(&others);
	}

	// Reads the counts into the plain shape and starts the next window.
	probe::CallCounts TakeSnapshot() {
		probe::CallCounts out;
		out.calls = static_cast<std::uint64_t>(InterlockedExchange64(&calls, 0));
		for (unsigned i = 0; i < probe::ReturnSites::kSites; ++i) {
			const LONG address = addresses[i];
			if (address == 0) {
				break;
			}
			const LONG64 count = InterlockedExchange64(&counts[i], 0);
			if (count != 0) {
				out.sites.sites[out.sites.used++] = probe::ReturnSites::Site{static_cast<std::uint32_t>(address),
				                                                            static_cast<std::uint64_t>(count)};
			}
		}
		out.sites.others = static_cast<std::uint64_t>(InterlockedExchange64(&others, 0));
		return out;
	}
};

AtomicCounts g_counts[static_cast<int>(Probed::Count)];
// IsInRange split by the light's path: [this+0x14C] != 0 selects the six-plane
// test, 0 the distance test (re/functions/0x007D2EA0.md B3). The split by
// return site says which caller's lights take the plane path - the question
// behind the unrestored mask under 0x007C6100 (knowledge/culling-and-draw-setup.md).
constexpr UInt32 kPlaneSourceOffset = 0x14C;
AtomicCounts g_inRangePlanePath;
AtomicCounts g_inRangeDistancePath;
bool g_installed[static_cast<int>(Probed::Count)] = {};
bool g_memory = false;
probe::MemoryPeak g_memoryPeak;
LogFn g_log = nullptr;

void Count(Probed which, void* returnAddress) {
	g_counts[static_cast<int>(which)].Add(static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(returnAddress)));
}

UInt32 __fastcall ProbeSetup(void* self, void* /*edx*/, UInt32 a1, UInt32 a2, UInt32 a3, UInt32 a4, UInt32 a5, UInt32 a6,
                             UInt32 a7, UInt32 a8) {
	Count(Probed::SetupTransformations, _ReturnAddress());
	// Once per draw: cheap enough to drive the summary clock from here, so
	// the probes report even when no shadow runs every frame.
	SummaryIfDue(GetTickCount(), nullptr, false);
	return g_setup(self, nullptr, a1, a2, a3, a4, a5, a6, a7, a8);
}

void __fastcall ProbeCull(void* self, void* /*edx*/, UInt32 object) {
	Count(Probed::Cull, _ReturnAddress());
	g_cull(self, nullptr, object);
}

UInt8 __fastcall ProbeInRange(void* self, void* /*edx*/, UInt32 object, UInt32 light) {
	Count(Probed::IsInRange, _ReturnAddress());
	const UInt32 returnAddress = static_cast<UInt32>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
	const UInt32 planeSource = *reinterpret_cast<volatile UInt32*>(reinterpret_cast<std::uintptr_t>(self) + kPlaneSourceOffset);
	(planeSource != 0 ? g_inRangePlanePath : g_inRangeDistancePath).Add(returnAddress);
	return g_inRange(self, nullptr, object, light);
}

UInt32 __cdecl ProbeCopy(const void* desc) {
	Count(Probed::CopyVertexStreamElements, _ReturnAddress());
	return g_copy(desc);
}

int __fastcall ProbeWhichSide(const void* sphere, void* /*edx*/, const void* plane) {
	Count(Probed::WhichSide, _ReturnAddress());
	return g_whichSide(sphere, nullptr, plane);
}

struct ProcessMemoryCounters {
	DWORD cb;
	DWORD pageFaultCount;
	SIZE_T peakWorkingSetSize;
	SIZE_T workingSetSize;
	SIZE_T quotaPeakPagedPoolUsage;
	SIZE_T quotaPagedPoolUsage;
	SIZE_T quotaPeakNonPagedPoolUsage;
	SIZE_T quotaNonPagedPoolUsage;
	SIZE_T pagefileUsage;
	SIZE_T peakPagefileUsage;
};
using GetProcessMemoryInfoFn = BOOL(WINAPI*)(HANDLE process, ProcessMemoryCounters* counters, DWORD cb);

bool SampleMemory(probe::MemorySample* out) {
	static GetProcessMemoryInfoFn s_getProcessMemoryInfo = reinterpret_cast<GetProcessMemoryInfoFn>(
	    GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo"));
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (!GlobalMemoryStatusEx(&status)) {
		return false;
	}
	out->virtualTotalBytes = status.ullTotalVirtual;
	out->virtualAvailableBytes = status.ullAvailVirtual;
	ProcessMemoryCounters counters{};
	counters.cb = sizeof(counters);
	if (s_getProcessMemoryInfo != nullptr && s_getProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
		out->commitBytes = counters.pagefileUsage;
		out->workingSetBytes = counters.workingSetSize;
		out->peakWorkingSetBytes = counters.peakWorkingSetSize;
	}
	return true;
}

}  // namespace

UInt32 ProbesAsked(const ShadowSettings& settings) {
	return settings.callCounters ? static_cast<UInt32>(Probed::Count) : 0u;
}

UInt32 InstallProbes(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	g_log = log;
	g_memory = settings.memoryProbe;
	if (!settings.callCounters) {
		return 0;
	}
	struct Entry {
		Probed which;
		UInt32 address;
		const UInt8* bytes;
		UInt32 length;
		const void* replacement;
		void** original;
	};
	const Entry entries[] = {
	    {Probed::SetupTransformations, kSetup, kSetupBytes, sizeof(kSetupBytes), reinterpret_cast<const void*>(&ProbeSetup), reinterpret_cast<void**>(&g_setup)},
	    {Probed::Cull, kCull, kCullBytes, sizeof(kCullBytes), reinterpret_cast<const void*>(&ProbeCull), reinterpret_cast<void**>(&g_cull)},
	    {Probed::IsInRange, kInRange, kInRangeBytes, sizeof(kInRangeBytes), reinterpret_cast<const void*>(&ProbeInRange), reinterpret_cast<void**>(&g_inRange)},
	    {Probed::CopyVertexStreamElements, kCopy, kCopyBytes, sizeof(kCopyBytes), reinterpret_cast<const void*>(&ProbeCopy), reinterpret_cast<void**>(&g_copy)},
	    {Probed::WhichSide, kWhichSide, kWhichSideBytes, sizeof(kWhichSideBytes), reinterpret_cast<const void*>(&ProbeWhichSide), reinterpret_cast<void**>(&g_whichSide)},
	};
	UInt32 made = 0;
	for (const Entry& e : entries) {
		const bool ok = InstallEntryDetour(kNames[static_cast<int>(e.which)], e.address, e.bytes, e.length, e.replacement, e.original, log);
		g_installed[static_cast<int>(e.which)] = ok;
		made += ok ? 1 : 0;
	}
	return made;
}

void LogProbeSummary() {
	if (g_log == nullptr) {
		return;
	}
	bool anyCounter = false;
	char line[900];
	int at = std::snprintf(line, sizeof(line), "Probe: calls");
	for (int i = 0; i < static_cast<int>(Probed::Count) && at < 800; ++i) {
		if (!g_installed[i]) {
			continue;
		}
		anyCounter = true;
		const probe::CallCounts counts = g_counts[i].TakeSnapshot();
		at += probe::FormatCallCounts(kNames[i], counts, line + at, sizeof(line) - at);
	}
	if (anyCounter) {
		g_log("%s", line);
		if (g_installed[static_cast<int>(Probed::IsInRange)]) {
			at = std::snprintf(line, sizeof(line), "Probe: IsInRange by path -");
			at += probe::FormatCallCounts("plane path (+0x14C != 0)", g_inRangePlanePath.TakeSnapshot(), line + at, sizeof(line) - at);
			at += probe::FormatCallCounts("distance path", g_inRangeDistancePath.TakeSnapshot(), line + at, sizeof(line) - at);
			g_log("%s", line);
		}
	}
	if (g_memory) {
		probe::MemorySample sample;
		if (SampleMemory(&sample)) {
			g_memoryPeak.Add(sample);
			probe::FormatMemory(sample, g_memoryPeak, line, sizeof(line));
			g_log("%s", line);
		}
	}
}

}  // namespace obng::plugin
