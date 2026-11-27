#include "ScaledTime.h"

#include <windows.h>

#include <xmmintrin.h>
#include <cstring>

#include "StepHooks.h"
#include "Types.h"
#include "engine/animation/ScaledTimeLogic.h"

namespace obng::plugin {
namespace {

namespace st = obng::engine::animation::scaled_time;

constexpr const char* kName = "NiTimeController::ComputeScaledTime";  // a proposal for 0x007155A0
constexpr UInt32 kAddress = 0x007155A0;                               // __thiscall(float), RET 4, the result in ST0 (read)
constexpr UInt8 kBytes[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1};     // sub esp,8; push esi; mov esi,ecx (read)
constexpr UInt32 kLock = 0x00B3FCA0;                                  // the memo's critical section (read 0x00715604)
constexpr UInt32 kMemoResult = 0x00B3FC88;
constexpr UInt32 kMemoTime = 0x00B3FC8C;
constexpr UInt32 kMemoLow = 0x00B3FC90;
constexpr UInt32 kMemoHigh = 0x00B3FC94;
constexpr UInt32 kMemoCycle = 0x00B27130;
using Fn = float(__fastcall*)(void* self, void* edx, float time);

void* g_original = nullptr;
LogFn g_log = nullptr;
bool g_installed = false;
bool g_shadow = false;
bool g_alternate = false;  // [Route] ScaledTimeAlternate: the rebuild in every second summary window, for timing the two in one run
bool g_on = true;
DWORD g_mainThread = 0;  // the thread of the game's update step (the plugin is loaded on another); 0 before the first step
st::Memo g_memo;
// The main thread's, written by it alone (no interlocked operation on this path: it would cost what the lock did).
std::uint64_t g_calls = 0;
std::uint64_t g_otherPrecision = 0;
std::uint64_t g_mismatches = 0;
volatile LONG64 g_otherThreads = 0;  // the shadow's: calls it did not compare
LONG g_mismatchLines = 0;

template <class T>
T At(UInt32 address) {
	T v;
	std::memcpy(&v, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(T));
	return v;
}

st::Controller Load(UInt32 self) {
	st::Controller c;
	c.flags = At<UInt8>(self + 0x08);
	c.frequency = At<float>(self + 0x0C);
	c.phase = At<float>(self + 0x10);
	c.low = At<float>(self + 0x14);
	c.high = At<float>(self + 0x18);
	c.start = At<float>(self + 0x1C);
	c.last = At<float>(self + 0x20);
	c.scaled = At<float>(self + 0x24);
	return c;
}

void Store(UInt32 self, const st::Controller& c) {
	const float written[3] = {c.start, c.last, c.scaled};
	std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(self + 0x1C)), written, sizeof(written));
}

st::Memo GameMemo() { return {At<float>(kMemoResult), At<float>(kMemoTime), At<float>(kMemoLow), At<float>(kMemoHigh), At<UInt32>(kMemoCycle)}; }

std::uint16_t ControlWord() {
	std::uint16_t word = 0;
	asm volatile("fnstcw %0" : "=m"(word));
	return word;
}

// The x87's rounding in the MXCSR for the rebuild's arithmetic, changed only when the two differ.
struct Rounding {
	std::uint32_t saved;
	bool changed;
	explicit Rounding(std::uint16_t word) {
		saved = _mm_getcsr();
		const std::uint32_t wanted = (saved & ~0x6000u) | (static_cast<std::uint32_t>((word >> 10) & 3u) << 13);
		changed = wanted != saved;
		if (changed) {
			_mm_setcsr(wanted);
		}
	}
	~Rounding() {
		if (changed) {
			_mm_setcsr(saved);
		}
	}
};

float Rebuilt(st::Controller& c, float time, st::Memo* memo, std::uint16_t word) {
	Rounding rounding(word);
	return st::Compute(c, time, memo);
}

float __fastcall RouteReplacement(void* self, void* /*edx*/, float time) {
	const std::uint16_t word = ControlWord();
	const bool main = GetCurrentThreadId() == g_mainThread;
	if (!g_on) {
		return reinterpret_cast<Fn>(g_original)(self, nullptr, time);
	}
	if ((word & 0x0300u) != 0) {
		g_otherPrecision += main ? 1u : 0u;
		return reinterpret_cast<Fn>(g_original)(self, nullptr, time);
	}
	const UInt32 object = reinterpret_cast<UInt32>(self);
	st::Controller c = Load(object);
	const float result = Rebuilt(c, time, main ? &g_memo : nullptr, word);
	Store(object, c);
	g_calls += main ? 1u : 0u;
	return result;
}

bool g_bench = false;  // [Probe] ScaledTimeBench: both timed once on a controller of the probe's own

// The original against the rebuild on one controller, a million calls each with the time moving (no memo hit) and
// a million with it standing (every call a hit), at the first step.
void Bench() {
	alignas(4) UInt8 object[0x40] = {};
	const auto reset = [&object]() {
		st::Controller c;
		c.flags = 0x08;  // active, the cycle type 0
		c.high = 2.5f;
		std::memcpy(object + 0x08, &c.flags, 1);
		const float fields[7] = {c.frequency, c.phase, c.low, c.high, c.start, c.last, c.scaled};
		std::memcpy(object + 0x0C, fields, sizeof(fields));
	};
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	const Fn sides[2] = {reinterpret_cast<Fn>(g_original), &RouteReplacement};
	double ns[2][2];
	constexpr int kCalls = 1000000;
	for (int moving = 0; moving < 2; ++moving) {
		for (int side = 0; side < 2; ++side) {
			reset();
			LARGE_INTEGER t0;
			LARGE_INTEGER t1;
			QueryPerformanceCounter(&t0);
			float sum = 0.0f;
			for (int i = 0; i < kCalls; ++i) {
				sum += sides[side](object, nullptr, moving != 0 ? static_cast<float>(i) * 0.001f : 1.0f);
			}
			QueryPerformanceCounter(&t1);
			ns[moving][side] = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1.0e9 / static_cast<double>(frequency.QuadPart) / kCalls + (sum != sum ? 1.0 : 0.0);
		}
	}
	g_log("Probe: %s timed on one controller, ns a call - the time standing (memo hits): the original %.1f, the rebuild %.1f; the time moving: the original "
	      "%.1f, the rebuild %.1f",
	      kName, ns[0][0], ns[0][1], ns[1][0], ns[1][1]);
}

void AtStep() {
	g_mainThread = GetCurrentThreadId();
	if (g_bench) {
		g_bench = false;
		Bench();
	}
}

template <class T>
void Put(UInt32 address, T value) { std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), &value, sizeof(T)); }

// The other side for the next window, the memo carried over to it.
void Switch() {
	EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(kLock));
	if (g_on) {
		g_on = false;
		Put(kMemoResult, g_memo.result);
		Put(kMemoTime, g_memo.time);
		Put(kMemoLow, g_memo.low);
		Put(kMemoHigh, g_memo.high);
		Put(kMemoCycle, g_memo.cycle);
	} else {
		g_memo = GameMemo();
		g_on = true;
	}
	LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(kLock));
}

bool Same(const void* a, const void* b, std::size_t size) { return std::memcmp(a, b, size) == 0; }

float __fastcall ShadowReplacement(void* self, void* /*edx*/, float time) {
	if (GetCurrentThreadId() != g_mainThread) {
		InterlockedIncrement64(&g_otherThreads);
		return reinterpret_cast<Fn>(g_original)(self, nullptr, time);
	}
	const UInt32 object = reinterpret_cast<UInt32>(self);
	const std::uint16_t word = ControlWord();
	EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(kLock));  // no other thread at the memo while both run; the original enters again
	st::Controller mine = Load(object);
	st::Memo memo = GameMemo();
	const st::Controller before = mine;
	const float theirs = reinterpret_cast<Fn>(g_original)(self, nullptr, time);
	const st::Controller after = Load(object);
	const st::Memo memoAfter = GameMemo();
	LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(kLock));
	++g_calls;
	if ((word & 0x0300u) != 0) {
		++g_otherPrecision;
		return theirs;
	}
	const float ours = Rebuilt(mine, time, &memo, word);
	const bool same = Same(&ours, &theirs, 4) && Same(&mine.start, &after.start, 4) && Same(&mine.last, &after.last, 4) && Same(&mine.scaled, &after.scaled, 4) &&
	                  Same(&memo, &memoAfter, sizeof(memo));
	if (!same) {
		++g_mismatches;
		if (++g_mismatchLines <= 20) {
			g_log("Shadow %s: MISMATCH - time %.9g flags %02X frequency %.9g phase %.9g low %.9g high %.9g start %.9g last %.9g scaled %.9g control word %04X: "
			      "the original %.9g (last %.9g scaled %.9g), the rebuild %.9g (last %.9g scaled %.9g)",
			      kName, static_cast<double>(time), before.flags, static_cast<double>(before.frequency), static_cast<double>(before.phase),
			      static_cast<double>(before.low), static_cast<double>(before.high), static_cast<double>(before.start), static_cast<double>(before.last),
			      static_cast<double>(before.scaled), word, static_cast<double>(theirs), static_cast<double>(after.last), static_cast<double>(after.scaled),
			      static_cast<double>(ours), static_cast<double>(mine.last), static_cast<double>(mine.scaled));
		}
	}
	return theirs;
}

}  // namespace

void InstallScaledTime(const char* iniPath, LogFn log) {
	const bool shadow = GetPrivateProfileIntA("Shadow", "ScaledTime", 0, iniPath) != 0;
	const bool route = GetPrivateProfileIntA("Route", "ScaledTime", 0, iniPath) != 0;
	if (g_installed || (!shadow && !route)) {
		return;
	}
	g_log = log;
	g_shadow = shadow;
	g_memo = GameMemo();
	g_bench = !shadow && GetPrivateProfileIntA("Probe", "ScaledTimeBench", 0, iniPath) != 0;
	g_alternate = !shadow && GetPrivateProfileIntA("Route", "ScaledTimeAlternate", 0, iniPath) != 0;
	if (!AddStepHook(&AtStep, "the scaled time's main thread", log)) {
		log("%s: not installed - no step hook to learn the main thread by", kName);
		return;
	}
	if (shadow && route) {
		log("Route: %s skipped - its shadow takes the same entry and wins", kName);
	}
	g_installed = InstallEntryDetour(kName, kAddress, kBytes, sizeof(kBytes), reinterpret_cast<const void*>(g_shadow ? &ShadowReplacement : &RouteReplacement),
	                                 &g_original, log, g_shadow ? DetourRole::Shadow : DetourRole::Route);
}

void LogScaledTimeSummary() {
	if (!g_installed || g_log == nullptr) {
		return;
	}
	const std::uint64_t calls = g_calls;
	const std::uint64_t otherPrecision = g_otherPrecision;
	g_calls = 0;
	g_otherPrecision = 0;
	if (g_shadow) {
		const std::uint64_t mismatches = g_mismatches;
		g_mismatches = 0;
		g_log("Shadow %s: %llu calls on the main thread compared, %llu mismatches; %llu under another precision control (not compared), %lld calls on other "
		      "threads (not compared)",
		      kName, static_cast<unsigned long long>(calls - otherPrecision), static_cast<unsigned long long>(mismatches),
		      static_cast<unsigned long long>(otherPrecision), InterlockedExchange64(&g_otherThreads, 0));
	} else {
		g_log("Route %s (%s in this window): %llu calls on the main thread without the lock, %llu left to the original for their precision control", kName,
		      g_on ? "on" : "off", static_cast<unsigned long long>(calls), static_cast<unsigned long long>(otherPrecision));
		if (g_alternate) {
			Switch();
		}
	}
}

}  // namespace obng::plugin
