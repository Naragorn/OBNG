#include "ActorThreads.h"

#include <windows.h>

#include <intrin.h>
#include <xmmintrin.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "ExeDataSection.h"
#include "FastSceneUpdate.h"
#include "engine/scene/ActorThreadsLogic.h"
#include "engine/scene/SceneUpdate.h"
#include "engine/scene/SceneUpdateLogic.h"

namespace obng::plugin {

namespace {

namespace at = obng::engine::scene::actor_threads;
namespace ab = obng::engine::scene::actor_batch;
namespace su = obng::engine::scene::scene_update;
namespace eng = obng::engine::scene::scene_update::engine;

// Read 2026-11-12 (re/export/actor-threads/; docs/subsystems/actor-batch.md).
constexpr UInt32 kCollisionMode = 0x00BA7A8C;
constexpr UInt32 kCollisionUpdate = 0x0088EDC0;  // bhkNiCollisionObject::UpdateWorldData, slot +0x50 of the object at a node's +0xA8
constexpr UInt32 kBlendUpdate = 0x0088F3D0;      // the blend object's slot +0x64
constexpr UInt32 kSlotBlendUpdate = 0x64;
constexpr UInt32 kSlotAsNode = 0x08;             // NiObject's node cast (the range walk's vt[+0x08], re/functions/0x007D59E0.md)
constexpr UInt32 kDataStart = exe_data::kStart;
constexpr UInt32 kDataDwords = exe_data::kDwords;
constexpr UInt32 kProbeEvery = 16;
constexpr UInt32 kMaxRuns = 512;
constexpr UInt32 kMaxWorkers = 7;
constexpr UInt32 kOtherDepth = 16;

using NoArgFn = UInt32(__fastcall*)(UInt32 self, UInt32 edx);

LogFn g_log = nullptr;
bool g_installed = false;
UInt32 g_threads = 0;
bool g_alternate = false;
bool g_check = false;
at::Counts g_counts;
at::WriteTally g_tally;
std::uint64_t g_probes = 0;
std::uint64_t g_runsKept = 0;
std::unique_ptr<std::uint32_t[]> g_before;
std::unique_ptr<std::uint32_t[]> g_after;
volatile LONG g_summaryRequested = 0;
LARGE_INTEGER g_frequency{};

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }
float Float(UInt32 address) { return *reinterpret_cast<const float*>(address); }

// ---- The plan --------------------------------------------------------------------

struct JobState {
	ab::Job job;
	const su::Order* order = nullptr;
	bool safe = false;
	bool done = false;       // its down pass ran (on a thread or here)
	bool parallel = false;   // ran on the threads: its collision updates wait
	std::vector<UInt32> deferred;
};

std::vector<JobState> g_states;
std::unordered_map<UInt32, std::size_t> g_stateOf;

at::CollisionClass ClassOf(UInt32 node) {
	const UInt32 co = Dword(node + eng::kCollisionObjectOffset);
	if (co == 0) {
		return at::CollisionClass::kNothing;
	}
	if (eng::Slot(co, eng::kSlotCollisionUpdateWorldData) != kCollisionUpdate) {
		return at::CollisionClass::kOtherClass;
	}
	at::CollisionFields f;
	f.mode = Dword(kCollisionMode);
	f.flags = *reinterpret_cast<const UInt8*>(co + 0x0C);
	f.keepFlag = (*reinterpret_cast<const UInt8*>(co + 0x0D) & 1u) != 0;
	const UInt32 body = Dword(co + 0x10);
	const UInt32 rigid = body != 0 ? Dword(body + 8) : 0;
	f.inWorld = rigid != 0 && Dword(rigid + 8) != 0;  // 0x00607840
	f.blend = Float(co + 0x14);
	f.previous = Float(co + 0x18);
	f.motionType = Dword(co + 0x1C);
	f.blendClass = eng::Slot(co, kSlotBlendUpdate) == kBlendUpdate;
	return at::Classify(f);
}

// Whether a subtree the engine runs itself holds a collision object.
bool HoldsCollision(UInt32 object, UInt32 depth) {
	if (object == 0 || depth > kOtherDepth) {
		return depth > kOtherDepth;
	}
	if (Dword(object + eng::kCollisionObjectOffset) != 0) {
		return true;
	}
	const UInt32 node = reinterpret_cast<NoArgFn>(eng::Slot(object, kSlotAsNode))(object, 0);
	if (node == 0) {
		return false;
	}
	const UInt32 slots = *reinterpret_cast<const UInt16*>(node + 0xB6);
	const UInt32 children = Dword(node + eng::kChildrenOffset);
	for (UInt32 i = 0; i < slots && children != 0; ++i) {
		if (HoldsCollision(Dword(children + 4 * i), depth + 1)) {
			return true;
		}
	}
	return false;
}

bool PlanSafe(const su::Order& order) {
	bool safe = true;
	for (const su::Entry& e : order.entries) {
		if (e.kind == su::Kind::Other) {
			if (HoldsCollision(e.object, 0)) {
				++g_counts.otherWithCollision;
				safe = false;
			}
			continue;
		}
		const at::CollisionClass c = ClassOf(e.object);
		if (Dword(e.object + eng::kCollisionObjectOffset) != 0) {
			++g_counts.classes[static_cast<int>(c)];
		}
		safe = safe && at::Safe(c);
	}
	return safe;
}

// ---- The ports ---------------------------------------------------------------------

struct ControllerPort {
	ab::Handle Controller(ab::Handle object) { return Dword(object + 0x0C); }
	void SetController(ab::Handle object, ab::Handle controller) { *reinterpret_cast<UInt32*>(object + 0x0C) = controller; }
};

// A worker's: the collision updates noted, not run.
struct DeferPort : eng::Port {
	std::vector<UInt32>* deferred = nullptr;
	void AfterWorldData(su::Handle h) {
		if (Dword(h + eng::kCollisionObjectOffset) != 0) {
			deferred->push_back(h);
		}
	}
};

// The check's: every collision update classified and its node compared.
struct CheckPort : eng::Port {
	bool jobSafe = false;
	void AfterWorldData(su::Handle h) {
		if (Dword(h + eng::kCollisionObjectOffset) == 0) {
			return;
		}
		const at::CollisionClass c = ClassOf(h);
		constexpr UInt32 kBoth = eng::kWorldTransformOffset + sizeof(su::Transform) - eng::kLocalTransformOffset;
		UInt8 before[kBoth];
		const void* transforms = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + eng::kLocalTransformOffset));
		std::memcpy(before, transforms, kBoth);
		eng::Port::AfterWorldData(h);
		if (std::memcmp(before, transforms, kBoth) != 0) {
			++g_counts.classesWrote[static_cast<int>(c)];
			g_counts.mispredicted += jobSafe ? 1u : 0u;
		}
	}
};

float TimeOf(UInt32 bits) {
	float t;
	std::memcpy(&t, &bits, sizeof(t));
	return t;
}

template <class Port>
void DownWith(Port& port, const JobState& s) {
	ControllerPort controllers;
	const ab::Saved saved = ab::Detach(controllers, s.job);
	{
		eng::RoundingFromX87 rounding;
		su::RunForward(*s.order, port, TimeOf(s.job.timeBits), true);
		su::RunBounds(*s.order, port);
	}
	ab::Attach(controllers, s.job, saved);
}

void DownRouted(const JobState& s) {
	ControllerPort controllers;
	const ab::Saved saved = ab::Detach(controllers, s.job);
	RunActorSubtreeDown(s.order, s.job.timeBits);
	ab::Attach(controllers, s.job, saved);
}

// ---- The workers -------------------------------------------------------------------

struct Pool {
	HANDLE start = nullptr;
	HANDLE threads[kMaxWorkers] = {};
	UInt32 workers = 0;
	std::vector<std::size_t> work;  // indices into g_states
	volatile LONG next = 0;
	volatile LONG finished = 0;     // workers out of their drain this round
	std::uint16_t fcw = 0;
	std::uint32_t mxcsr = 0;
};
Pool g_pool;

void RunOne(std::size_t index) {
	JobState& s = g_states[index];
	DeferPort port;
	port.deferred = &s.deferred;
	DownWith(port, s);
}

void Drain() {
	for (;;) {
		const LONG i = InterlockedIncrement(&g_pool.next) - 1;
		if (i >= static_cast<LONG>(g_pool.work.size())) {
			return;
		}
		RunOne(g_pool.work[static_cast<std::size_t>(i)]);
	}
}

DWORD WINAPI Worker(void*) {
	for (;;) {
		WaitForSingleObject(g_pool.start, INFINITE);
		const std::uint16_t fcw = g_pool.fcw;
		asm volatile("fldcw %0" : : "m"(fcw));
		_mm_setcsr(g_pool.mxcsr);
		Drain();
		InterlockedIncrement(&g_pool.finished);
	}
}

// The safe jobs over the workers and this thread; back when every worker has
// left its drain (so none still reads the round's list).
void RunParallel() {
	std::uint16_t fcw = 0;
	asm volatile("fnstcw %0" : "=m"(fcw));
	g_pool.fcw = fcw;
	g_pool.mxcsr = _mm_getcsr();
	g_pool.next = 0;
	g_pool.finished = 0;
	MemoryBarrier();
	ReleaseSemaphore(g_pool.start, static_cast<LONG>(g_pool.workers), nullptr);
	Drain();
	for (UInt32 spins = 0; g_pool.finished < static_cast<LONG>(g_pool.workers); ++spins) {
		if (spins < 4000) {
			_mm_pause();
		} else {
			SwitchToThread();
		}
	}
	for (std::size_t i : g_pool.work) {
		g_states[i].done = true;
		g_states[i].parallel = true;
	}
	g_counts.parallelJobs += g_pool.work.size();
}

// ---- The check ---------------------------------------------------------------------

void ProbeBefore() {
	std::memcpy(g_before.get(), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kDataStart)), kDataDwords * 4);
}

void ProbeAfter() {
	std::memcpy(g_after.get(), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kDataStart)), kDataDwords * 4);
	at::WrittenRun runs[kMaxRuns];
	const UInt32 n = at::WrittenRuns(g_before.get(), g_after.get(), kDataDwords, runs, kMaxRuns);
	for (UInt32 i = 0; i < n; ++i) {
		g_tally.Note(runs[i].first, runs[i].count);
	}
	g_runsKept += n;
	++g_probes;
}

void RunCheck() {
	const bool probe = (g_counts.batches % kProbeEvery) == 0;
	if (probe) {
		ProbeBefore();
	}
	for (JobState& s : g_states) {
		CheckPort port;
		port.jobSafe = s.safe;
		DownWith(port, s);
		s.done = true;
	}
	if (probe) {
		ProbeAfter();
	}
}

void WriteSummary() {
	char line[1400];
	at::FormatSummary(g_counts, g_threads, g_check, line, sizeof(line));
	const double perMs = static_cast<double>(g_frequency.QuadPart) / 1000.0;
	const double off = g_counts.downBatches[0] != 0 ? static_cast<double>(g_counts.downBatches[0]) : 1.0;
	const double on = g_counts.downBatches[1] != 0 ? static_cast<double>(g_counts.downBatches[1]) : 1.0;
	g_log("%s; the down phase and its waiting updates %.3f ms a batch on the threads (%llu), %.3f ms on one thread (%llu)", line,
	      static_cast<double>(g_counts.downTicks[1]) / perMs / on, static_cast<unsigned long long>(g_counts.downBatches[1]),
	      static_cast<double>(g_counts.downTicks[0]) / perMs / off, static_cast<unsigned long long>(g_counts.downBatches[0]));
	if (g_check && g_probes != 0) {
		constexpr UInt32 kShown = 16;
		UInt32 first[kShown];
		UInt32 times[kShown];
		const UInt32 n = g_tally.Most(first, times, kShown);
		char places[900] = {};
		int used = 0;
		for (UInt32 i = 0; i < n && used >= 0 && used < 860; ++i) {
			used += std::snprintf(places + used, sizeof(places) - static_cast<std::size_t>(used), " %08X x%u;", kDataStart + 4 * first[i], times[i]);
		}
		g_log("Probe: actors' down passes - .data written in %llu probed batches: %u places (%llu runs, %llu not kept); the most often:%s",
		      static_cast<unsigned long long>(g_probes), g_tally.Distinct(), static_cast<unsigned long long>(g_runsKept),
		      static_cast<unsigned long long>(g_tally.Dropped()), places);
		g_tally.Clear();
		g_probes = 0;
		g_runsKept = 0;
	}
	g_counts = at::Counts{};
}

}  // namespace

namespace actor_threads {

bool Active() { return g_installed; }

void RunDowns(const std::vector<ab::Job>& jobs, const std::vector<std::size_t>& split, OrderOf orderOf) {
	if (InterlockedExchange(&g_summaryRequested, 0) != 0) {
		WriteSummary();
	}
	LARGE_INTEGER t0;
	QueryPerformanceCounter(&t0);
	g_states.clear();
	g_stateOf.clear();
	g_pool.work.clear();
	for (std::size_t i : split) {
		JobState s;
		s.job = jobs[i];
		s.order = static_cast<const su::Order*>(orderOf(s.job.root));
		s.safe = s.order != nullptr && PlanSafe(*s.order);
		g_counts.safeJobs += s.safe ? 1u : 0u;
		g_stateOf[s.job.root] = g_states.size();
		g_states.push_back(std::move(s));
	}
	g_counts.jobs += split.size();
	const bool threadsNow = !g_check && g_threads != 0 && (!g_alternate || (g_counts.batches % 2) == 0);
	++g_counts.batches;
	if (g_check) {
		RunCheck();
	} else if (threadsNow) {
		for (std::size_t i = 0; i < g_states.size(); ++i) {
			if (g_states[i].safe) {
				g_states[i].deferred.reserve(24);
				g_pool.work.push_back(i);
			}
		}
		RunParallel();
	} else {
		for (JobState& s : g_states) {
			DownRouted(s);
			s.done = true;
		}
	}
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	g_counts.downTicks[threadsNow ? 1 : 0] += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
	++g_counts.downBatches[threadsNow ? 1 : 0];
}

void BeforeUp(const ab::Job& job, const void* order) {
	auto it = g_stateOf.find(job.root);
	if (it == g_stateOf.end()) {
		return;
	}
	JobState& s = g_states[it->second];
	if (s.done && !s.parallel) {
		return;
	}
	LARGE_INTEGER t0;
	QueryPerformanceCounter(&t0);
	if (s.parallel) {
		eng::Port port;
		for (UInt32 h : s.deferred) {
			port.AfterWorldData(h);
		}
	} else {
		s.order = static_cast<const su::Order*>(order);
		DownRouted(s);
		s.done = true;
	}
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	g_counts.downTicks[1] += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
}

}  // namespace actor_threads

bool ActorBatchInstalled();

void InstallActorThreads(const char* iniPath, LogFn log) {
	if (g_installed) {
		return;
	}
	const UINT threads = GetPrivateProfileIntA("Route", "ActorThreads", 0, iniPath);
	g_check = GetPrivateProfileIntA("Probe", "ActorThreadsCheck", 0, iniPath) != 0;
	if (threads == 0 && !g_check) {
		return;
	}
	g_log = log;
	if (!ActorBatchInstalled()) {
		log("Route: actors' down passes on threads - not installed: needs [Route] ActorBatch=1 (and ActorUpdate=1) in place");
		return;
	}
	g_threads = g_check ? 0 : (threads > kMaxWorkers ? kMaxWorkers : threads);
	g_alternate = GetPrivateProfileIntA("Route", "ActorThreadsAlternate", 0, iniPath) != 0;
	QueryPerformanceFrequency(&g_frequency);
	if (g_check) {
		g_before.reset(new std::uint32_t[kDataDwords]);
		g_after.reset(new std::uint32_t[kDataDwords]);
	}
	if (g_threads != 0) {
		g_pool.start = CreateSemaphoreA(nullptr, 0, static_cast<LONG>(kMaxWorkers * 64), nullptr);
		for (UInt32 i = 0; i < g_threads && g_pool.start != nullptr; ++i) {
			g_pool.threads[i] = CreateThread(nullptr, 1 << 20, &Worker, nullptr, 0, nullptr);
			g_pool.workers += g_pool.threads[i] != nullptr ? 1u : 0u;
		}
		if (g_pool.workers == 0) {
			log("Route: actors' down passes on threads - no worker could be made; not installed");
			return;
		}
		g_threads = g_pool.workers;
	}
	g_installed = true;
	log("Route: actors' down passes on threads - %s%s", g_check ? "the check (one thread, the collision classes compared, .data probed)" : "on",
	    g_alternate ? ", alternating by batch" : "");
	if (!g_check) {
		log("Route: actors' down passes on threads - %u workers beside the loop's thread", g_threads);
	}
}

void LogActorThreadsSummary() {
	if (g_installed) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
