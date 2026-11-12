#include "ActorBatch.h"
#include "ActorThreads.h"

#include "FastSceneUpdate.h"
#include "Memory.h"
#include "ShadowTimeInfo.h"
#include "engine/scene/ActorBatchLogic.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
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

// Its parts, timed while a controller's update runs inside the loop: the
// support probes 0x00891780 (RET), the integrate wrapper 0x008902B0 (RET 4;
// it calls the proxy's integrate 0x008AD7D0), 0x00894C70 (RET 4) before
// them and 0x008939B0 (RET) after them (read: the calls in 0x00896000 and
// each function's return). All four begin as the update does.
constexpr unsigned kParts = 5;
constexpr UInt32 kPartAddress[kParts] = {0x00891780, 0x008902B0, 0x00894C70, 0x008939B0, 0x008AE890};
constexpr const char* kPartName[kParts] = {"support probes 0x00891780", "integrate 0x008902B0", "0x00894C70", "0x008939B0", "support check 0x008AE890"};  // the last: RET 8 (read 0x008AE91F), it calls 0x008AE100
void* g_partOriginal[kParts] = {};
volatile LONG64 g_partCalls[kParts] = {};
volatile LONG64 g_partTicks[kParts] = {};
volatile bool g_inController = false;  // written by the loop's thread only
using Part0Fn = void(__fastcall*)(void* self, void* edx);
using Part1Fn = void(__fastcall*)(void* self, void* edx, UInt32 argument);

template <unsigned Index>
void __fastcall Part0(void* self, void* /*edx*/) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Part0Fn>(g_partOriginal[Index])(self, nullptr);
	QueryPerformanceCounter(&end);
	if (g_inController && GetCurrentThreadId() == g_loopThread) {
		InterlockedIncrement64(&g_partCalls[Index]);
		InterlockedAdd64(&g_partTicks[Index], end.QuadPart - start.QuadPart);
	}
}
template <unsigned Index>
void __fastcall Part1(void* self, void* /*edx*/, UInt32 argument) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Part1Fn>(g_partOriginal[Index])(self, nullptr, argument);
	QueryPerformanceCounter(&end);
	if (g_inController && GetCurrentThreadId() == g_loopThread) {
		InterlockedIncrement64(&g_partCalls[Index]);
		InterlockedAdd64(&g_partTicks[Index], end.QuadPart - start.QuadPart);
	}
}

using Part2Fn = void(__fastcall*)(void* self, void* edx, UInt32 a, UInt32 b);
template <unsigned Index>
void __fastcall Part2(void* self, void* /*edx*/, UInt32 a, UInt32 b) {
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	reinterpret_cast<Part2Fn>(g_partOriginal[Index])(self, nullptr, a, b);
	QueryPerformanceCounter(&end);
	if (g_inController && GetCurrentThreadId() == g_loopThread) {
		InterlockedIncrement64(&g_partCalls[Index]);
		InterlockedAdd64(&g_partTicks[Index], end.QuadPart - start.QuadPart);
	}
}

// Havok's collision agents counted while a controller's update runs: a
// generated thunk a function (cmp byte [g_inController],0; je +7; lock inc
// dword [counter]; jmp [original]) - no knowledge of the arguments needed.
// The names are Havok's own profiling labels in the functions (read).
constexpr unsigned kCounted = 3;
constexpr UInt32 kCountedAddress[kCounted] = {0x00908DE0, 0x00902160, 0x008AD070};
constexpr const char* kCountedName[kCounted] = {"BvTree 0x00908DE0", "CvxList 0x00902160", "0x008AD070"};
void* g_countedOriginal[kCounted] = {};
volatile LONG g_counted[kCounted] = {};

void* BuildCountingThunk(unsigned index) {
	auto* code = static_cast<UInt8*>(AllocExecutable(32));
	if (code == nullptr) {
		return nullptr;
	}
	UInt8* at = code;
	auto dword = [&at](UInt32 value) {
		std::memcpy(at, &value, 4);
		at += 4;
	};
	*at++ = 0x80;
	*at++ = 0x3D;
	dword(reinterpret_cast<UInt32>(&g_inController));
	*at++ = 0x00;
	*at++ = 0x74;
	*at++ = 0x07;
	*at++ = 0xF0;
	*at++ = 0xFF;
	*at++ = 0x05;
	dword(reinterpret_cast<UInt32>(&g_counted[index]));
	*at++ = 0xFF;
	*at++ = 0x25;
	dword(reinterpret_cast<UInt32>(&g_countedOriginal[index]));
	return code;
}

// The Gsk agent 0x0093F250 looked into while a controller's update runs: it
// is cdecl with the two bodies first (read 0x0093F2AD: [ebp+0xC] and
// [ebp+8]; a body's shape at +0, its parent at +0xC as 0x0093F2B3-BB and
// Havok's hkCdBody have it). For each call the two bodies' roots (the parent
// chain's end: the collidable) are described: a histogram of the dwords at
// +0x10..+0x2C of the root (where the broad phase handle and the filter
// info lie; which offset is which is what this is to show), the shape type
// (the shape's virtual +8, as the agent itself asks it at 0x0093F2D0), and
// the first roots dumped raw.
using GskFn = void(__cdecl*)(UInt32 a, UInt32 b, UInt32 c, UInt32 d, UInt32 e);
void* g_gskOriginal = nullptr;
volatile LONG g_gskCalls = 0;
struct ValueCounts {
	static constexpr unsigned kSlots = 48;
	UInt32 value[kSlots];
	UInt32 count[kSlots];
	UInt32 used;
	UInt32 other;
	void Add(UInt32 v) {
		for (unsigned i = 0; i < used; ++i) {
			if (value[i] == v) {
				++count[i];
				return;
			}
		}
		if (used < kSlots) {
			value[used] = v;
			count[used++] = 1;
		} else {
			++other;
		}
	}
};
constexpr unsigned kRootOffsets = 8;  // +0x10, +0x14, ... +0x2C
ValueCounts g_rootValues[2][kRootOffsets];  // the loop's thread writes; the summary reads at the end of a window (a probe: torn reads accepted)
ValueCounts g_shapeTypes[2];
ValueCounts g_depths[2];
constexpr unsigned kRootDumps = 24;
UInt32 g_rootDump[kRootDumps][2][12];
volatile LONG g_rootDumped = 0;

UInt32 RootOf(UInt32 body, UInt32* depth) {
	*depth = 0;
	for (unsigned i = 0; i < 8; ++i) {
		const UInt32 parent = *reinterpret_cast<const UInt32*>(body + 0x0C);
		if (parent == 0) {
			break;
		}
		body = parent;
		++*depth;
	}
	return body;
}

void __cdecl GskReplacement(UInt32 a, UInt32 b, UInt32 c, UInt32 d, UInt32 e) {
	if (g_inController && GetCurrentThreadId() == g_loopThread && a != 0 && b != 0) {
		InterlockedIncrement(&g_gskCalls);
		const UInt32 bodies[2] = {a, b};
		UInt32 roots[2];
		for (unsigned k = 0; k < 2; ++k) {
			UInt32 depth;
			roots[k] = RootOf(bodies[k], &depth);
			g_depths[k].Add(depth);
			for (unsigned o = 0; o < kRootOffsets; ++o) {
				g_rootValues[k][o].Add(*reinterpret_cast<const UInt32*>(roots[k] + 0x10 + 4 * o));
			}
			const UInt32 shape = *reinterpret_cast<const UInt32*>(bodies[k]);
			if (shape != 0) {
				g_shapeTypes[k].Add(reinterpret_cast<UInt32(__fastcall*)(void*, void*)>(*reinterpret_cast<const UInt32*>(*reinterpret_cast<const UInt32*>(shape) + 8))(reinterpret_cast<void*>(shape), nullptr));
			}
		}
		const LONG slot = g_rootDumped;
		if (slot < static_cast<LONG>(kRootDumps) && (g_gskCalls % 997) == 1) {
			for (unsigned k = 0; k < 2; ++k) {
				std::memcpy(g_rootDump[slot][k], reinterpret_cast<const void*>(roots[k]), sizeof(g_rootDump[slot][k]));
			}
			InterlockedIncrement(&g_rootDumped);
		}
	}
	reinterpret_cast<GskFn>(g_gskOriginal)(a, b, c, d, e);
}

void LogValueCounts(const char* what, ValueCounts& counts) {
	char line[1300];
	int at = std::snprintf(line, sizeof(line), "Probe: gsk %s:", what);
	// The largest first, twelve of them.
	for (unsigned n = 0; n < 12 && at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 40; ++n) {
		unsigned best = counts.used;
		for (unsigned i = 0; i < counts.used; ++i) {
			if (counts.count[i] != 0 && (best == counts.used || counts.count[i] > counts.count[best])) {
				best = i;
			}
		}
		if (best == counts.used) {
			break;
		}
		at += std::snprintf(line + at, sizeof(line) - at, " %08X x%u", counts.value[best], counts.count[best]);
		counts.count[best] = 0;
	}
	if (at > 0 && static_cast<std::size_t>(at) < sizeof(line) - 40) {
		std::snprintf(line + at, sizeof(line) - at, " (%u distinct kept, %u calls beyond them)", counts.used, counts.other);
	}
	g_log("%s", line);
	counts = ValueCounts{};
}

// What the update left, per call inside the loop: whether it counted the
// actor as moving (bit 3 of the flags at +0x1F4, read 0x00896000: set when
// the displacement or the velocity is not zero within 0.01 / 0.001), the
// proxy's manifold size ([[self+8]+0x74] is the array, its size at +4, read
// 0x00896ACD and 0x008ABDB0) and the cached manifold's size (+0x3C0).
constexpr unsigned kManifoldBuckets = 5;  // 0, 1-5, 6-10, 11-20, more
volatile LONG64 g_moving = 0;
volatile LONG64 g_manifold[kManifoldBuckets] = {};
volatile LONG64 g_manifoldSum = 0;
volatile LONG64 g_cachedSum = 0;
volatile LONG64 g_noProxy = 0;

void __fastcall ControllerReplacement(void* self, void* /*edx*/, UInt32 argument) {
	const bool inLoop = g_batching != 0 && GetCurrentThreadId() == g_loopThread;
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	if (inLoop) {
		g_inController = true;
	}
	QueryPerformanceCounter(&start);
	reinterpret_cast<ControllerFn>(g_controllerOriginal)(self, nullptr, argument);
	QueryPerformanceCounter(&end);
	if (inLoop) {
		g_inController = false;
		const UInt32 controller = reinterpret_cast<UInt32>(self);
		if ((*reinterpret_cast<const UInt32*>(controller + 0x1F4) & 8u) != 0) {
			InterlockedIncrement64(&g_moving);
		}
		const UInt32 proxy = *reinterpret_cast<const UInt32*>(controller + 8);
		if (proxy == 0) {
			InterlockedIncrement64(&g_noProxy);
		} else {
			const UInt32 size = *reinterpret_cast<const UInt32*>(proxy + 0x78);
			InterlockedIncrement64(&g_manifold[size == 0 ? 0 : size <= 5 ? 1 : size <= 10 ? 2 : size <= 20 ? 3 : 4]);
			InterlockedAdd64(&g_manifoldSum, size);
			InterlockedAdd64(&g_cachedSum, *reinterpret_cast<const UInt32*>(controller + 0x3C0));
		}
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
	void Up(const ab::Job& job) {
		const void* order = g_orders.find(job.root)->second;
		if (actor_threads::Active()) {
			actor_threads::BeforeUp(job, order);  // the waiting collision updates, or the down pass the threads left
		}
		RunActorSubtreeUp(order);
	}
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

const void* OrderOf(std::uint32_t root) {
	auto it = g_orders.find(root);
	return it != g_orders.end() ? it->second : nullptr;
}

void RunBatch() {
	EnginePort port;
	g_orders.clear();
	HoldActorOrders(true);
	g_batch.Flush(port, [&](const std::vector<std::size_t>& split) {
		if (actor_threads::Active()) {
			actor_threads::RunDowns(g_batch.Jobs(), split, &OrderOf);
			return;
		}
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
		const void* const replacements[kParts] = {reinterpret_cast<const void*>(&Part0<0>), reinterpret_cast<const void*>(&Part1<1>),
		                                          reinterpret_cast<const void*>(&Part1<2>), reinterpret_cast<const void*>(&Part0<3>),
		                                          reinterpret_cast<const void*>(&Part2<4>)};
		for (unsigned i = 0; i < kParts; ++i) {
			InstallEntryDetour(kPartName[i], kPartAddress[i], kControllerBytes, sizeof(kControllerBytes), replacements[i], &g_partOriginal[i], log,
			                   DetourRole::Shadow);
		}
		static const UInt8 kGskBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};
		InstallEntryDetour("Gsk agent", 0x0093F250, kGskBytes, sizeof(kGskBytes), reinterpret_cast<const void*>(&GskReplacement), &g_gskOriginal, log,
		                   DetourRole::Shadow);
		for (unsigned i = 0; i < kCounted; ++i) {
			if (void* thunk = BuildCountingThunk(i)) {
				InstallEntryDetour(kCountedName[i], kCountedAddress[i], kControllerBytes, sizeof(kControllerBytes), thunk, &g_countedOriginal[i], log,
				                   DetourRole::Shadow);
			}
		}
	}
	log("Route %s: inside the frame's loop over the high actors ApplyAnimData's update is noted and run after the loop (down passes, then the "
	    "upward passes in order); the player, a root new this frame and every other caller run in place",
	    kLoopName);
	return made;
}

bool ActorBatchInstalled() { return g_installed; }

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
			static const char* const kWho[2] = {"A", "B"};
			for (unsigned k = 0; k < 2; ++k) {
				char what[48];
				std::snprintf(what, sizeof(what), "%s shape type", kWho[k]);
				LogValueCounts(what, g_shapeTypes[k]);
				std::snprintf(what, sizeof(what), "%s parent depth", kWho[k]);
				LogValueCounts(what, g_depths[k]);
				for (unsigned o = 0; o < kRootOffsets; ++o) {
					std::snprintf(what, sizeof(what), "%s root +0x%02X", kWho[k], 0x10 + 4 * o);
					LogValueCounts(what, g_rootValues[k][o]);
				}
			}
			static LONG s_dumpsLogged = 0;
			for (; s_dumpsLogged < g_rootDumped && s_dumpsLogged < static_cast<LONG>(kRootDumps); ++s_dumpsLogged) {
				for (unsigned k = 0; k < 2; ++k) {
					const UInt32* w = g_rootDump[s_dumpsLogged][k];
					g_log("Probe: gsk root %s raw: %08X %08X %08X %08X | %08X %08X %08X %08X | %08X %08X %08X %08X", kWho[k], w[0], w[1], w[2], w[3], w[4], w[5],
					      w[6], w[7], w[8], w[9], w[10], w[11]);
				}
			}
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
		if (inCalls > 0) {
			const double perCall = 1.0 / static_cast<double>(inCalls);
			char line[1400];
			int at = std::snprintf(line, sizeof(line), "Probe: character controller parts, per update inside the loop:");
			for (unsigned i = 0; i < kParts && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++i) {
				const LONG64 calls = InterlockedExchange64(&g_partCalls[i], 0);
				const LONG64 ticks = InterlockedExchange64(&g_partTicks[i], 0);
				at += std::snprintf(line + at, sizeof(line) - at, " %s %.2f calls %.1f us;", kPartName[i], static_cast<double>(calls) * perCall,
				                    static_cast<double>(ticks) * toMs * 1000.0 * perCall);
			}
			for (unsigned i = 0; i < kCounted && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++i) {
				at += std::snprintf(line + at, sizeof(line) - at, " %s %.1f calls;", kCountedName[i],
				                    static_cast<double>(InterlockedExchange(&g_counted[i], 0)) * perCall);
			}
			if (at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
				at += std::snprintf(line + at, sizeof(line) - at, " Gsk 0x0093F250 %.1f calls;", static_cast<double>(InterlockedExchange(&g_gskCalls, 0)) * perCall);
			}
			const LONG64 moving = InterlockedExchange64(&g_moving, 0);
			const LONG64 noProxy = InterlockedExchange64(&g_noProxy, 0);
			const LONG64 manifoldSum = InterlockedExchange64(&g_manifoldSum, 0);
			const LONG64 cachedSum = InterlockedExchange64(&g_cachedSum, 0);
			LONG64 m[kManifoldBuckets];
			for (unsigned i = 0; i < kManifoldBuckets; ++i) {
				m[i] = InterlockedExchange64(&g_manifold[i], 0);
			}
			if (at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
				std::snprintf(line + at, sizeof(line) - at,
				              " counted as moving %.1f %%, no proxy %.1f %%; manifold size mean %.1f (cached %.1f): 0: %.1f %%, 1-5: %.1f %%, 6-10: %.1f %%, "
				              "11-20: %.1f %%, more: %.1f %%",
				              100.0 * static_cast<double>(moving) * perCall, 100.0 * static_cast<double>(noProxy) * perCall,
				              static_cast<double>(manifoldSum) * perCall, static_cast<double>(cachedSum) * perCall, 100.0 * static_cast<double>(m[0]) * perCall,
				              100.0 * static_cast<double>(m[1]) * perCall, 100.0 * static_cast<double>(m[2]) * perCall,
				              100.0 * static_cast<double>(m[3]) * perCall, 100.0 * static_cast<double>(m[4]) * perCall);
			}
			g_log("%s", line);
		}
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
