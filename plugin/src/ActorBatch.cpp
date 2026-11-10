#include "ActorBatch.h"

#include "FastSceneUpdate.h"
#include "ShadowTimeInfo.h"
#include "engine/scene/ActorBatchLogic.h"

#include <windows.h>

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace obng::plugin {
namespace {

namespace ab = obng::engine::scene::actor_batch;

constexpr const char* kLoopName = "ProcessManager::UpdateHighActors";
constexpr const char* kApplyName = "ActorAnimData::ApplyAnimData";
constexpr UInt32 kLoopAddress = 0x00677EC0;   // __thiscall(float, bool), RET 8 (read 0x00678386)
constexpr UInt32 kApplyAddress = 0x00471F20;  // __thiscall(), RET (read 0x00472027)
constexpr UInt8 kLoopBytes[] = {0x83, 0xEC, 0x24, 0x55, 0x8B, 0xE9};         // sub esp,24h; push ebp; mov ebp,ecx (read)
constexpr UInt8 kApplyBytes[] = {0x6A, 0xFF, 0x68, 0xE0, 0x0D, 0x9D, 0x00};  // push -1; push 009D0DE0h (read)
constexpr UInt32 kPlayerPointer = 0x00B333C4;     // (read 0x00603CA0's compares; xOBSE g_thePlayer, OBVR kPlayerPointer)
constexpr UInt32 kSlotGetAnimData = 0x164;        // the actor's virtual the update asks for its anim data (read 0x00603CA0: the call's result is ApplyAnimData's this)
constexpr UInt32 kAnimRootOffset = 0x04;          // (read 0x00471F49)
constexpr UInt32 kAnimSecondOffset = 0x08;        // (read 0x00471F54)
constexpr UInt32 kAnimTimeOffset = 0x94;          // the float handed to the update (read 0x00471FAA)
constexpr UInt32 kControllerOffset = 0x0C;        // NiObjectNET (read 0x00471F5E, 0x00478304)
constexpr UInt32 kReferenceCountOffset = 0x04;    // NiRefObject (read 0x00471F6F)
constexpr std::size_t kCapacity = 512;

using LoopFn = void(__fastcall*)(void* self, void* edx, UInt32 timeBits, UInt32 flag);
using ApplyFn = void(__fastcall*)(void* self, void* edx);
using GetterFn = UInt32(__fastcall*)(void* self, void* edx);
using DeleteFn = void(__fastcall*)(void* self, void* edx, UInt32 flags);

void* g_loopOriginal = nullptr;
void* g_applyOriginal = nullptr;
LogFn g_log = nullptr;
bool g_installed = false;

// The loop's state. Written on the loop's thread only; ApplyAnimData on
// another thread reads g_batching and g_loopThread and runs in place.
volatile LONG g_batching = 0;
DWORD g_loopThread = 0;
UInt32 g_playerAnim = 0;
ab::Batch g_batch(kCapacity);
ab::KnownRoots g_known;
std::unordered_map<ab::Handle, const void*> g_orders;  // this run's orders by root

volatile LONG64 g_loops = 0;
volatile LONG64 g_otherLoops = 0;  // the loop's other callers and nested calls: not batched
volatile LONG64 g_deferred = 0;
volatile LONG64 g_inPlacePlayer = 0;
volatile LONG64 g_inPlaceNew = 0;
volatile LONG64 g_inPlaceAgain = 0;
volatile LONG64 g_inPlaceFull = 0;
volatile LONG64 g_inPlaceOutside = 0;
volatile LONG64 g_whole = 0;  // deferred bodies the flat run could not take
volatile LONG64 g_loopTicks = 0;
volatile LONG64 g_runTicks = 0;
volatile LONG64 g_downTicks = 0;
LARGE_INTEGER g_frequency{};

// [Route] ActorBatchTicks: every batched loop's three clock readings (before
// the loop, after it, after the run) kept and written with the summary, so
// that a sampler capture of the same run can be cut by them
// (tools/re/sampler-actor-loop.py). The loop's thread appends, the summary's
// thread takes them.
struct LoopTicks {
	LONGLONG start;
	LONGLONG middle;
	LONGLONG end;
	UInt32 deferred;
};
bool g_keepTicks = false;
SRWLOCK g_ticksLock = SRWLOCK_INIT;
std::vector<LoopTicks> g_ticks;

// With the ticks: the character controller's update 0x00896000 timed (the
// game's Havok character update: its own labels are "Character movement"
// and "update character state", read 0x00896000; __thiscall(float*), RET 4,
// read 0x0089701A), inside the loop and outside it.
constexpr const char* kControllerName = "CharacterController::Update";
constexpr UInt32 kControllerAddress = 0x00896000;
constexpr UInt8 kControllerBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};  // push ebp; mov ebp,esp; and esp,-10h (read)
using ControllerFn = void(__fastcall*)(void* self, void* edx, UInt32 argument);
void* g_controllerOriginal = nullptr;
volatile LONG64 g_controllerCallsInLoop = 0;
volatile LONG64 g_controllerTicksInLoop = 0;
volatile LONG64 g_controllerCallsOutside = 0;
volatile LONG64 g_controllerTicksOutside = 0;

void __fastcall ControllerReplacement(void* self, void* /*edx*/, UInt32 argument) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<ControllerFn>(g_controllerOriginal)(self, nullptr, argument);
	QueryPerformanceCounter(&end);
	if (g_batching != 0 && GetCurrentThreadId() == g_loopThread) {
		InterlockedIncrement64(&g_controllerCallsInLoop);
		InterlockedAdd64(&g_controllerTicksInLoop, end.QuadPart - start.QuadPart);
	} else {
		InterlockedIncrement64(&g_controllerCallsOutside);
		InterlockedAdd64(&g_controllerTicksOutside, end.QuadPart - start.QuadPart);
	}
}

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

struct EnginePort {
	ab::Handle Controller(ab::Handle object) { return Dword(object + kControllerOffset); }
	void SetController(ab::Handle object, ab::Handle controller) { *reinterpret_cast<UInt32*>(object + kControllerOffset) = controller; }
	void Retain(ab::Handle object) { InterlockedIncrement(reinterpret_cast<LONG*>(object + kReferenceCountOffset)); }
	// As the engine lets a reference go (read 0x00471FDF-0x00471FF1): at zero
	// the object's first virtual with 1.
	void Release(ab::Handle object) {
		if (InterlockedDecrement(reinterpret_cast<LONG*>(object + kReferenceCountOffset)) == 0) {
			reinterpret_cast<DeleteFn>(Dword(Dword(object)))(reinterpret_cast<void*>(object), nullptr, 1);
		}
	}
	bool Prepare(const ab::Job& job) {
		const void* order = PrepareActorSubtree(job.root);
		if (order == nullptr) {
			InterlockedIncrement64(&g_whole);
			return false;
		}
		g_orders[job.root] = order;
		return true;
	}
	void Down(const ab::Job& job) { RunActorSubtreeDown(g_orders.find(job.root)->second, job.timeBits); }
	void Up(const ab::Job& job) { RunActorSubtreeUp(g_orders.find(job.root)->second); }
	void Whole(const ab::Job& job) { RunActorUpdateOriginal(job.root, job.timeBits); }
};

void __fastcall ApplyReplacement(void* self, void* /*edx*/) {
	const UInt32 anim = reinterpret_cast<UInt32>(self);
	ab::Job job;
	job.root = Dword(anim + kAnimRootOffset);
	job.second = Dword(anim + kAnimSecondOffset);
	job.timeBits = Dword(anim + kAnimTimeOffset);
	ab::Context c;
	c.batching = g_batching != 0;
	c.loopThread = c.batching && GetCurrentThreadId() == g_loopThread;
	if (c.batching && c.loopThread) {
		c.isPlayer = anim == g_playerAnim;
		c.knownLastFrame = g_known.Contains(job.root);
		c.seenThisFrame = g_known.SeenThisFrame(job.root);
		c.full = g_batch.Full();
		if (job.root != 0) {
			g_known.Saw(job.root);
		}
	}
	if (ab::Decide(c, job) == ab::Decision::Defer) {
		EnginePort port;
		g_batch.Add(port, job);
		InterlockedIncrement64(&g_deferred);
		return;
	}
	if (!c.batching || !c.loopThread) {
		InterlockedIncrement64(&g_inPlaceOutside);
	} else if (c.isPlayer) {
		InterlockedIncrement64(&g_inPlacePlayer);
	} else if (c.seenThisFrame) {
		InterlockedIncrement64(&g_inPlaceAgain);
	} else if (!c.knownLastFrame) {
		InterlockedIncrement64(&g_inPlaceNew);
	} else if (c.full) {
		InterlockedIncrement64(&g_inPlaceFull);
	}
	reinterpret_cast<ApplyFn>(g_applyOriginal)(self, nullptr);
}

void RunBatch() {
	EnginePort port;
	g_orders.clear();
	HoldActorOrders(true);
	g_batch.Flush(port, [&](const std::vector<std::size_t>& split) {
		LARGE_INTEGER start;
		LARGE_INTEGER end;
		QueryPerformanceCounter(&start);
		for (std::size_t i : split) {
			ab::RunDown(port, g_batch.Jobs()[i]);
		}
		QueryPerformanceCounter(&end);
		InterlockedAdd64(&g_downTicks, end.QuadPart - start.QuadPart);
	});
	HoldActorOrders(false);
}

void __fastcall LoopReplacement(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	const auto original = reinterpret_cast<LoopFn>(g_loopOriginal);
	if (g_batching != 0 || !ab::LoopBatches(timeBits, flag)) {
		InterlockedIncrement64(&g_otherLoops);
		original(self, nullptr, timeBits, flag);
		return;
	}
	const UInt32 player = Dword(kPlayerPointer);
	g_playerAnim = player != 0 ? reinterpret_cast<GetterFn>(Dword(Dword(player) + kSlotGetAnimData))(reinterpret_cast<void*>(player), nullptr) : 0;
	g_loopThread = GetCurrentThreadId();
	LARGE_INTEGER start;
	LARGE_INTEGER middle;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	InterlockedExchange(&g_batching, 1);
	original(self, nullptr, timeBits, flag);
	InterlockedExchange(&g_batching, 0);
	QueryPerformanceCounter(&middle);
	const UInt32 deferredNow = static_cast<UInt32>(g_batch.Size());
	RunBatch();
	g_known.EndFrame();
	QueryPerformanceCounter(&end);
	InterlockedIncrement64(&g_loops);
	InterlockedAdd64(&g_loopTicks, middle.QuadPart - start.QuadPart);
	InterlockedAdd64(&g_runTicks, end.QuadPart - middle.QuadPart);
	if (g_keepTicks) {
		AcquireSRWLockExclusive(&g_ticksLock);
		g_ticks.push_back(LoopTicks{start.QuadPart, middle.QuadPart, end.QuadPart, deferredNow});
		ReleaseSRWLockExclusive(&g_ticksLock);
	}
}

}  // namespace

UInt32 InstallRouteActorBatch(const RouteSettings& settings, LogFn log) {
	if (g_installed || !settings.actorBatch) {
		return 0;
	}
	if (!ActorUpdateRouted()) {
		log("Route %s: not installed - the actors' route (NiAVObject::Update) is not in place, and the batch runs through it", kLoopName);
		return 0;
	}
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	UInt32 made = 0;
	if (!InstallEntryDetour(kApplyName, kApplyAddress, kApplyBytes, sizeof(kApplyBytes), reinterpret_cast<const void*>(&ApplyReplacement), &g_applyOriginal, log,
	                        DetourRole::Route)) {
		return made;
	}
	++made;
	if (!InstallEntryDetour(kLoopName, kLoopAddress, kLoopBytes, sizeof(kLoopBytes), reinterpret_cast<const void*>(&LoopReplacement), &g_loopOriginal, log,
	                        DetourRole::Route)) {
		// ApplyAnimData's detour stays and runs the original for every call:
		// without the loop's nothing is ever batched.
		return made;
	}
	++made;
	g_installed = true;
	g_keepTicks = settings.actorBatchTicks;
	if (g_keepTicks) {
		InstallEntryDetour(kControllerName, kControllerAddress, kControllerBytes, sizeof(kControllerBytes), reinterpret_cast<const void*>(&ControllerReplacement),
		                   &g_controllerOriginal, log, DetourRole::Shadow);
	}
	log("Route %s: inside the frame's loop over the high actors ApplyAnimData's update is noted and run after the loop (down passes, then the "
	    "upward passes in order); the player, a root new this frame and every other caller run in place",
	    kLoopName);
	return made;
}

void LogActorBatchSummary() {
	if (!g_installed || g_log == nullptr) {
		return;
	}
	if (g_keepTicks) {
		std::vector<LoopTicks> taken;
		AcquireSRWLockExclusive(&g_ticksLock);
		taken.swap(g_ticks);
		ReleaseSRWLockExclusive(&g_ticksLock);
		// Sixteen loops a line: start, then the two lengths and the bodies deferred.
		char line[1200];
		for (std::size_t i = 0; i < taken.size();) {
			int at = std::snprintf(line, sizeof(line), "Probe: actor loop ticks");
			for (unsigned n = 0; n < 16 && i < taken.size() && at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 64; ++n, ++i) {
				at += std::snprintf(line + at, sizeof(line) - at, " %lld+%lld+%lld:%u", taken[i].start, taken[i].middle - taken[i].start,
				                    taken[i].end - taken[i].middle, taken[i].deferred);
			}
			g_log("%s", line);
		}
	}
	const LONG64 loops = InterlockedExchange64(&g_loops, 0);
	const LONG64 otherLoops = InterlockedExchange64(&g_otherLoops, 0);
	const LONG64 deferred = InterlockedExchange64(&g_deferred, 0);
	const LONG64 player = InterlockedExchange64(&g_inPlacePlayer, 0);
	const LONG64 fresh = InterlockedExchange64(&g_inPlaceNew, 0);
	const LONG64 again = InterlockedExchange64(&g_inPlaceAgain, 0);
	const LONG64 full = InterlockedExchange64(&g_inPlaceFull, 0);
	const LONG64 outside = InterlockedExchange64(&g_inPlaceOutside, 0);
	const LONG64 whole = InterlockedExchange64(&g_whole, 0);
	const LONG64 loopTicks = InterlockedExchange64(&g_loopTicks, 0);
	const LONG64 runTicks = InterlockedExchange64(&g_runTicks, 0);
	const LONG64 downTicks = InterlockedExchange64(&g_downTicks, 0);
	if (loops == 0 && otherLoops == 0 && outside == 0) {
		return;
	}
	if (g_controllerOriginal != nullptr && loops > 0 && g_frequency.QuadPart > 0) {
		const LONG64 inCalls = InterlockedExchange64(&g_controllerCallsInLoop, 0);
		const LONG64 inTicks = InterlockedExchange64(&g_controllerTicksInLoop, 0);
		const LONG64 outCalls = InterlockedExchange64(&g_controllerCallsOutside, 0);
		const LONG64 outTicks = InterlockedExchange64(&g_controllerTicksOutside, 0);
		const double toMs = 1000.0 / static_cast<double>(g_frequency.QuadPart);
		g_log("Probe: character controller update 0x00896000 - inside the loop %.1f calls and %.3f ms a loop (%.1f us each); outside it %.1f calls "
		      "and %.3f ms a loop",
		      static_cast<double>(inCalls) / static_cast<double>(loops), static_cast<double>(inTicks) * toMs / static_cast<double>(loops),
		      inCalls > 0 ? static_cast<double>(inTicks) * toMs * 1000.0 / static_cast<double>(inCalls) : 0.0,
		      static_cast<double>(outCalls) / static_cast<double>(loops), static_cast<double>(outTicks) * toMs / static_cast<double>(loops));
	}
	const double perMs = g_frequency.QuadPart > 0 && loops > 0 ? 1000.0 / static_cast<double>(g_frequency.QuadPart) / static_cast<double>(loops) : 0.0;
	const double perLoop = loops > 0 ? 1.0 / static_cast<double>(loops) : 0.0;
	g_log("Route: actor batch - %lld loops (%lld not batched): %.1f bodies deferred a loop (%.2f of them whole in the original), in place %.2f the "
	      "player's, %.2f new roots, %.2f a root again, %.2f batch full; %lld bodies outside the loop; the loop %.3f ms, the run after it %.3f ms "
	      "(of it the down passes %.3f ms)",
	      loops, otherLoops, static_cast<double>(deferred) * perLoop, static_cast<double>(whole) * perLoop, static_cast<double>(player) * perLoop,
	      static_cast<double>(fresh) * perLoop, static_cast<double>(again) * perLoop, static_cast<double>(full) * perLoop, outside,
	      static_cast<double>(loopTicks) * perMs, static_cast<double>(runTicks) * perMs, static_cast<double>(downTicks) * perMs);
}

}  // namespace obng::plugin
