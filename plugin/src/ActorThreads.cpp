#include "ActorThreads.h"

#include <windows.h>

#include <intrin.h>
#include <xmmintrin.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "ExeDataSection.h"
#include "FastSceneUpdate.h"
#include "engine/scene/ActorDownSplitLogic.h"
#include "engine/scene/ActorThreadsLogic.h"
#include "engine/scene/SceneUpdate.h"
#include "engine/scene/SceneUpdateLogic.h"

namespace obng::plugin {

namespace {

namespace at = obng::engine::scene::actor_threads;
namespace ab = obng::engine::scene::actor_batch;
namespace split = obng::engine::scene::actor_down_split;
namespace su = obng::engine::scene::scene_update;
namespace eng = obng::engine::scene::scene_update::engine;

constexpr UInt32 kDataStart = exe_data::kStart;
constexpr UInt32 kDataDwords = exe_data::kDwords;
constexpr UInt32 kProbeEvery = 16;
constexpr UInt32 kMaxRuns = 512;
constexpr UInt32 kMaxWorkers = 7;
constexpr UInt32 kParentDepth = 64;
constexpr UInt32 kControllerNext = 0x34;  // NiTimeController's next (read 0x0047C930)
constexpr UInt32 kSlotControllerUpdate = 0x54;
constexpr UInt32 kSlotPropertyUpdate = 0x50;
// The controller classes a worker runs (read 2026-11-27): NiTransformController's update 0x006C3C40 writes its
// target's local transform from its own interpolator; bhkBlendController's 0x008AAD60 only stores the time when its
// target has no collision object (0x00497420), and otherwise blends that object and may end the blend in the Havok
// world. Every other class, and every property's controllers, are held for the loop's thread.
constexpr UInt32 kTransformControllerVtable = 0x00A79634;
constexpr UInt32 kBlendControllerVtable = 0x00A97AC4;
constexpr UInt32 kControllerInterpolator = 0x3C;  // NiSingleInterpController's (read 0x006C3C40)

LogFn g_log = nullptr;
bool g_installed = false;
UInt32 g_threads = 0;
bool g_alternate = false;
bool g_check = false;
split::Counts g_counts;
std::uint64_t g_batchNumber = 0;
bool g_partsBatch = false;  // the batch in flight runs in two parts
volatile LONG g_summaryRequested = 0;
LARGE_INTEGER g_frequency{};

// The check's: the statics the workers' part writes, and what it runs.
at::WriteTally g_written;
at::WriteTally g_controllerClasses;    // by vtable: the controllers the workers' part runs
at::WriteTally g_interpolatorClasses;  // the interpolators of its transform controllers
at::WriteTally g_heldClasses;          // the controllers and properties held for the loop's thread
at::WriteTally g_otherClasses;         // the Other entries the sweep hands to the engine
std::uint64_t g_probes = 0;
bool g_probing = false;             // this batch is probed
std::vector<UInt32> g_metInBatch;   // a probed batch's controllers and interpolators the workers' part runs
std::uint64_t g_metTwice = 0;
// The check's sweep by its parts, in time stamp counter ticks (and the counter's ticks in the same stretch, to scale them).
std::uint64_t g_otherTsc = 0;
std::uint64_t g_collisionTsc = 0;
std::uint64_t g_heldTsc = 0;
std::uint64_t g_boundsTsc = 0;
std::uint64_t g_sweepTsc = 0;       // of them: met a second time in the same batch (shared between two controllers or skeletons)
std::unique_ptr<std::uint32_t[]> g_before;
std::unique_ptr<std::uint32_t[]> g_after;

// ---- The ports ---------------------------------------------------------------------

struct ControllerPort {
	ab::Handle Controller(ab::Handle object) { return eng::Dword(object, eng::kControllerOffset); }
	void SetController(ab::Handle object, ab::Handle controller) { *reinterpret_cast<UInt32*>(object + eng::kControllerOffset) = controller; }
};

using TimeFn = void(__thiscall*)(void* self, float time);

struct SplitPort : eng::Port {
	bool HasCollision(su::Handle h) const { return eng::Dword(h, eng::kCollisionObjectOffset) != 0; }
	su::Transform World(su::Handle h) const { return eng::TransformAt(h, eng::kWorldTransformOffset); }
	void WriteFlags(su::Handle h, std::uint16_t flags) { std::memcpy(eng::Pointer(h + eng::kFlagsOffset), &flags, 2); }
	bool WorkerMayRun(su::Handle h, su::Handle controller) const {
		const UInt32 vtable = eng::Dword(controller, 0);
		return vtable == kTransformControllerVtable || (vtable == kBlendControllerVtable && !HasCollision(h));
	}
	static bool HasPropertyControllers(su::Handle h) {
		for (su::Handle node = eng::Dword(h, eng::kPropertyListOffset); node != 0; node = eng::Dword(node, 0)) {
			const su::Handle item = eng::Dword(node, 8);
			if (item != 0 && eng::Dword(item, eng::kControllerOffset) != 0) {
				return true;
			}
		}
		return false;
	}
	// 0x0047C930's chain walk for the classes a worker runs; the next controller is read after the call, as there.
	bool WorkerControllers(su::Handle h, float time) {
		bool held = HasPropertyControllers(h);
		for (su::Handle c = eng::Dword(h, eng::kControllerOffset); c != 0; c = eng::Dword(c, kControllerNext)) {
			if (WorkerMayRun(h, c)) {
				reinterpret_cast<TimeFn>(eng::Slot(c, kSlotControllerUpdate))(eng::Pointer(c), time);
			} else {
				held = true;
			}
		}
		return held;
	}
	// The rest of 0x0047C930: the properties' controllers, then the chain's held ones.
	void HeldControllers(su::Handle h, float time) {
		su::Handle node = eng::Dword(h, eng::kPropertyListOffset);
		while (node != 0) {
			const su::Handle item = eng::Dword(node, 8);
			node = eng::Dword(node, 0);
			if (item != 0 && eng::Dword(item, eng::kControllerOffset) != 0) {
				reinterpret_cast<TimeFn>(eng::Slot(item, kSlotPropertyUpdate))(eng::Pointer(item), time);
			}
		}
		for (su::Handle c = eng::Dword(h, eng::kControllerOffset); c != 0; c = eng::Dword(c, kControllerNext)) {
			if (!WorkerMayRun(h, c)) {
				reinterpret_cast<TimeFn>(eng::Slot(c, kSlotControllerUpdate))(eng::Pointer(c), time);
			}
		}
	}
};

// The check's: the classes of what the two parts run, counted.
struct CensusPort : SplitPort {
	bool WorkerControllers(su::Handle h, float time) {
		for (su::Handle node = eng::Dword(h, eng::kPropertyListOffset); node != 0; node = eng::Dword(node, 0)) {
			const su::Handle item = eng::Dword(node, 8);
			if (item != 0 && eng::Dword(item, eng::kControllerOffset) != 0) {
				g_heldClasses.Note(eng::Dword(item, 0), 1);
			}
		}
		for (su::Handle c = eng::Dword(h, eng::kControllerOffset); c != 0; c = eng::Dword(c, kControllerNext)) {
			const bool runs = WorkerMayRun(h, c);
			(runs ? g_controllerClasses : g_heldClasses).Note(eng::Dword(c, 0), 1);
			const su::Handle interpolator = eng::Dword(c, kControllerInterpolator);
			const bool transform = runs && eng::Dword(c, 0) == kTransformControllerVtable;
			if (transform && interpolator != 0) {
				g_interpolatorClasses.Note(eng::Dword(interpolator, 0), 1);
			}
			if (runs && g_probing) {
				g_metInBatch.push_back(c);
				if (transform && interpolator != 0) {
					g_metInBatch.push_back(interpolator);
				}
			}
		}
		return SplitPort::WorkerControllers(h, time);
	}
	void UpdateDownwardOriginal(su::Handle h, float time, bool controllers) {
		g_otherClasses.Note(eng::Dword(h, 0), 1);
		const std::uint64_t t0 = __rdtsc();
		SplitPort::UpdateDownwardOriginal(h, time, controllers);
		g_otherTsc += __rdtsc() - t0;
	}
	void AfterWorldData(su::Handle h) {
		const std::uint64_t t0 = __rdtsc();
		SplitPort::AfterWorldData(h);
		g_collisionTsc += __rdtsc() - t0;
	}
	void HeldControllers(su::Handle h, float time) {
		const std::uint64_t t0 = __rdtsc();
		SplitPort::HeldControllers(h, time);
		g_heldTsc += __rdtsc() - t0;
	}
};

struct Parents {
	su::Handle Parent(su::Handle h) const { return eng::Dword(h, eng::kParentOffset); }
};

float TimeOf(UInt32 bits) {
	float t;
	std::memcpy(&t, &bits, sizeof(t));
	return t;
}

// ---- The batch's skeletons ---------------------------------------------------------

struct JobState {
	ab::Job job;
	const su::Order* order = nullptr;
	bool held = false;   // below another skeleton of the batch: whole on the loop's thread, in its place
	bool parts = false;  // the workers' part ran: the sweep waits
	bool flat = false;   // the flat run did all of it
	split::Pending pending;
};

std::vector<JobState> g_states;  // the first g_count are this batch's; the rest keep their vectors
std::size_t g_count = 0;
std::size_t g_cursor = 0;
std::vector<UInt32> g_roots;  // sorted

template <class Port>
void WorkersPart(Port& port, JobState& s) {
	ControllerPort controllers;
	const ab::Saved saved = ab::Detach(controllers, s.job);
	{
		eng::RoundingFromX87 rounding;
		split::RunWorker(*s.order, port, TimeOf(s.job.timeBits), true, s.pending);
	}
	ab::Attach(controllers, s.job, saved);
	s.parts = true;
}

void Flat(JobState& s) {
	ControllerPort controllers;
	const ab::Saved saved = ab::Detach(controllers, s.job);
	RunActorSubtreeDown(s.order, s.job.timeBits);
	ab::Attach(controllers, s.job, saved);
	s.flat = true;
}

// The check: a world that is not its parent's world times its local, on a
// node no collision object writes.
std::uint64_t Inconsistent(const su::Order& order, SplitPort& port) {
	std::uint64_t n = 0;
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		const su::Entry& e = order.entries[i];
		if (split::Left(order, i) || e.kind == su::Kind::Other || port.HasCollision(e.object)) {
			continue;
		}
		su::Transform world;
		su::UpdateWorld(port.ParentWorld(e.object), port.Local(e.object), world);
		const su::Transform held = port.World(e.object);
		n += std::memcmp(&world, &held, sizeof(world)) != 0 ? 1u : 0u;
	}
	return n;
}

template <class Port>
void Sweep(Port& port, JobState& s) {
	ControllerPort controllers;
	const ab::Saved saved = ab::Detach(controllers, s.job);
	split::SweepResult result;
	{
		eng::RoundingFromX87 rounding;
		const std::uint64_t t0 = g_check ? __rdtsc() : 0;
		result = split::RunSweep(*s.order, port, TimeOf(s.job.timeBits), true, s.pending);
		const std::uint64_t t1 = g_check ? __rdtsc() : 0;
		su::RunBounds(*s.order, port);
		if (g_check) {
			g_sweepTsc += t1 - t0;
			g_boundsTsc += __rdtsc() - t1;
			g_counts.inconsistent += Inconsistent(*s.order, port);
		}
	}
	ab::Attach(controllers, s.job, saved);
	split::Add(g_counts, s.pending, result);
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

void Drain() {
	for (;;) {
		const LONG i = InterlockedIncrement(&g_pool.next) - 1;
		if (i >= static_cast<LONG>(g_pool.work.size())) {
			return;
		}
		SplitPort port;
		WorkersPart(port, g_states[g_pool.work[static_cast<std::size_t>(i)]]);
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

// The workers' part of every free skeleton over the workers and this thread;
// back when every worker has left its drain (none still reads the round's list).
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
}

// ---- The check ---------------------------------------------------------------------

void RunCheck() {
	const bool probe = (g_batchNumber % kProbeEvery) == 0;
	g_probing = probe;
	g_metInBatch.clear();
	if (probe) {
		std::memcpy(g_before.get(), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kDataStart)), kDataDwords * 4);
	}
	for (std::size_t i = 0; i < g_count; ++i) {
		if (!g_states[i].held) {
			CensusPort port;
			WorkersPart(port, g_states[i]);
		}
	}
	if (probe) {
		std::memcpy(g_after.get(), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kDataStart)), kDataDwords * 4);
		at::WrittenRun runs[kMaxRuns];
		const UInt32 n = at::WrittenRuns(g_before.get(), g_after.get(), kDataDwords, runs, kMaxRuns);
		for (UInt32 i = 0; i < n; ++i) {
			g_written.Note(runs[i].first, runs[i].count);
		}
		++g_probes;
		std::sort(g_metInBatch.begin(), g_metInBatch.end());
		for (std::size_t i = 1; i < g_metInBatch.size(); ++i) {
			g_metTwice += g_metInBatch[i] == g_metInBatch[i - 1] ? 1u : 0u;
		}
	}
}

void LogTally(const char* what, at::WriteTally& tally, UInt32 base, UInt32 scale) {
	constexpr UInt32 kShown = 24;
	UInt32 first[kShown];
	UInt32 times[kShown];
	const UInt32 n = tally.Most(first, times, kShown);
	char places[1000] = {};
	int used = 0;
	for (UInt32 i = 0; i < n && used >= 0 && used < 960; ++i) {
		used += std::snprintf(places + used, sizeof(places) - static_cast<std::size_t>(used), " %08X x%u;", base + scale * first[i], times[i]);
	}
	g_log("Probe: actors' down passes in two parts - %s: %u in all (%llu not kept); the most often:%s", what, tally.Distinct(),
	      static_cast<unsigned long long>(tally.Dropped()), places);
	tally.Clear();
}

void WriteSummary() {
	char line[900];
	split::FormatSummary(g_counts, g_threads, g_check, static_cast<double>(g_frequency.QuadPart) / 1000.0, line, sizeof(line));
	g_log("%s", line);
	if (g_check) {
		g_log("Probe: actors' down passes in two parts - the workers' part probed in %llu batches; controllers and interpolators it met twice in one batch: %llu",
		      static_cast<unsigned long long>(g_probes), static_cast<unsigned long long>(g_metTwice));
		g_metTwice = 0;
		const double all = static_cast<double>(g_sweepTsc + g_boundsTsc) > 0.0 ? static_cast<double>(g_sweepTsc + g_boundsTsc) / 100.0 : 1.0;
		g_log("Probe: actors' down passes in two parts - the loop's thread's part by share: the Other entries %.1f %%, the collision updates %.1f %%, the held "
		      "controllers %.1f %%, the rest of the sweep (its walk, the worlds and bounds again) %.1f %%, the nodes' bounds %.1f %%",
		      static_cast<double>(g_otherTsc) / all, static_cast<double>(g_collisionTsc) / all, static_cast<double>(g_heldTsc) / all,
		      static_cast<double>(g_sweepTsc - g_otherTsc - g_collisionTsc - g_heldTsc) / all, static_cast<double>(g_boundsTsc) / all);
		g_otherTsc = g_collisionTsc = g_heldTsc = g_boundsTsc = g_sweepTsc = 0;
		LogTally("places of .data written during the workers' part", g_written, kDataStart, 4);
		LogTally("controller classes run by the workers' part (vtable)", g_controllerClasses, 0, 1);
		LogTally("interpolator classes of its transform controllers (vtable)", g_interpolatorClasses, 0, 1);
		LogTally("controller and property classes held for the loop's thread (vtable)", g_heldClasses, 0, 1);
		LogTally("Other entries left to the engine in the sweep (vtable)", g_otherClasses, 0, 1);
		g_probes = 0;
	}
	g_counts = split::Counts{};
}

// This batch's skeletons: their orders, and which hang below another.
void Plan(const std::vector<ab::Job>& jobs, const std::vector<std::size_t>& chosen, actor_threads::OrderOf orderOf) {
	g_count = chosen.size();
	g_cursor = 0;
	if (g_states.size() < g_count) {
		g_states.resize(g_count);
	}
	g_roots.clear();
	for (std::size_t i : chosen) {
		g_roots.push_back(jobs[i].root);
	}
	std::sort(g_roots.begin(), g_roots.end());
	const auto isRoot = [](su::Handle h) { return std::binary_search(g_roots.begin(), g_roots.end(), h); };
	for (std::size_t k = 0; k < g_count; ++k) {
		JobState& s = g_states[k];
		s.job = jobs[chosen[k]];
		s.order = static_cast<const su::Order*>(orderOf(s.job.root));
		s.parts = false;
		s.flat = false;
		s.held = s.order == nullptr || split::HangsBelow(Parents{}, s.job.root, isRoot, kParentDepth);
	}
}

JobState* Find(UInt32 root) {
	if (g_cursor < g_count && g_states[g_cursor].job.root == root) {
		return &g_states[g_cursor++];
	}
	for (std::size_t i = 0; i < g_count; ++i) {
		if (g_states[i].job.root == root) {
			return &g_states[i];
		}
	}
	return nullptr;
}

}  // namespace

namespace actor_threads {

bool Active() { return g_installed; }

void RunDowns(const std::vector<ab::Job>& jobs, const std::vector<std::size_t>& chosen, OrderOf orderOf) {
	if (InterlockedExchange(&g_summaryRequested, 0) != 0) {
		WriteSummary();
	}
	LARGE_INTEGER t0;
	QueryPerformanceCounter(&t0);
	Plan(jobs, chosen, orderOf);
	g_partsBatch = g_check || !g_alternate || (g_batchNumber % 2) == 0;
	if (!g_partsBatch) {
		for (std::size_t i = 0; i < g_count; ++i) {
			if (g_states[i].order != nullptr) {
				Flat(g_states[i]);
			}
		}
	} else if (g_check) {
		RunCheck();
	} else {
		g_pool.work.clear();
		for (std::size_t i = 0; i < g_count; ++i) {
			if (!g_states[i].held) {
				g_pool.work.push_back(i);
			}
		}
		RunParallel();
	}
	++g_batchNumber;
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	const std::uint64_t ticks = static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
	g_counts.ticks[g_partsBatch ? 1 : 0] += ticks;
	g_counts.workerTicks += g_partsBatch ? ticks : 0u;
	++g_counts.batches[g_partsBatch ? 1 : 0];
}

void BeforeUp(const ab::Job& job, const void* order) {
	JobState* s = Find(job.root);
	if (s == nullptr || s->flat) {
		return;
	}
	LARGE_INTEGER t0;
	QueryPerformanceCounter(&t0);
	if (s->parts) {
		if (g_check) {
			CensusPort port;
			Sweep(port, *s);
		} else {
			SplitPort port;
			Sweep(port, *s);
		}
	} else {
		s->order = static_cast<const su::Order*>(order);
		Flat(*s);
		++g_counts.held;
	}
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	g_counts.ticks[g_partsBatch ? 1 : 0] += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
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
		log("Route: actors' down passes in two parts - not installed: needs [Route] ActorBatch=1 (and ActorUpdate=1) in place");
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
			log("Route: actors' down passes in two parts - no worker could be made; not installed");
			return;
		}
		g_threads = g_pool.workers;
	}
	g_installed = true;
	if (g_check) {
		log("Route: actors' down passes in two parts - the check: both parts on the loop's thread, the workers' part's classes and .data writes counted");
	} else {
		log("Route: actors' down passes in two parts - %u workers beside the loop's thread%s", g_threads, g_alternate ? ", every second batch flat on one thread" : "");
	}
}

void LogActorThreadsSummary() {
	if (g_installed) {
		InterlockedExchange(&g_summaryRequested, 1);
	}
}

}  // namespace obng::plugin
