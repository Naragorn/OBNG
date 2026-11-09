// The actors' skeleton update in the game: see FastSceneUpdate.h.

#include "FastSceneUpdate.h"

#include <intrin.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "ShadowRecorder.h"
#include "engine/scene/SceneUpdate.h"
#include "engine/scene/SceneUpdateLogic.h"

namespace obng::plugin {
namespace {

namespace su = obng::engine::scene::scene_update;
namespace eng = obng::engine::scene::scene_update::engine;

constexpr const char* kName = "NiAVObject::Update";
constexpr UInt32 kApplyAnimDataSite = 0x00471FBE;  // the return into ActorAnimData::ApplyAnimData 0x00471F20 (read)
constexpr UInt8 kUpdateEntryBytes[] = {0xD9, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1};  // fld [esp+4]; push esi; mov esi,ecx (read)

// __fastcall with a dead edx is the same call byte for byte as the
// original's __thiscall(float, bool) RET 8: self in ecx, the float and the
// flag on the stack, callee clean-up (Probes.cpp does the same).
using UpdateFn = void(__fastcall*)(void* self, void* edx, UInt32 timeBits, UInt32 flag);

enum class Mode { Off, Route, Shadow };
Mode g_mode = Mode::Off;
void* g_original = nullptr;  // the trampoline
LogFn g_log = nullptr;

// The orders by skeleton root, under a lock (the actor update runs on one
// thread as far as read; the lock costs a few nanoseconds and makes that a
// non-assumption).
SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<su::Handle, su::Order> g_orders;
constexpr std::size_t kOrdersKeptAtMost = 2048;  // beyond this every order is dropped and rebuilt as needed

// Counters for the summary (interlocked: the summary thread reads them).
volatile LONG64 g_calls = 0;        // calls from ApplyAnimData
volatile LONG64 g_routed = 0;       // of them run flat
volatile LONG64 g_built = 0;        // orders built for a new root
volatile LONG64 g_rebuilt = 0;      // orders rebuilt after the shape changed
volatile LONG64 g_fallback = 0;     // to the original: the root is not a plain node
volatile LONG64 g_routedTicks = 0;  // QPC ticks inside the routed calls
LARGE_INTEGER g_frequency{};

void Original(void* self, UInt32 timeBits, UInt32 flag) { reinterpret_cast<UpdateFn>(g_original)(self, nullptr, timeBits, flag); }

// The order for this root, current against the tree; nullptr when the root
// is not a plain node (the flat run has nothing to say about it).
su::Order* OrderFor(su::Handle root) {
	eng::Tree tree;
	AcquireSRWLockExclusive(&g_lock);
	auto it = g_orders.find(root);
	if (it != g_orders.end() && su::Matches(it->second, tree)) {
		su::Order* order = &it->second;
		ReleaseSRWLockExclusive(&g_lock);
		return order;
	}
	if (tree.KindOf(root) != su::Kind::Node) {
		if (it != g_orders.end()) {
			g_orders.erase(it);
		}
		ReleaseSRWLockExclusive(&g_lock);
		InterlockedIncrement64(&g_fallback);
		return nullptr;
	}
	if (it != g_orders.end()) {
		InterlockedIncrement64(&g_rebuilt);
		it->second = su::Build(tree, root);
		su::Order* order = &it->second;
		ReleaseSRWLockExclusive(&g_lock);
		return order;
	}
	if (g_orders.size() >= kOrdersKeptAtMost) {
		g_orders.clear();
	}
	InterlockedIncrement64(&g_built);
	auto inserted = g_orders.emplace(root, su::Build(tree, root));
	su::Order* order = &inserted.first->second;
	ReleaseSRWLockExclusive(&g_lock);
	return order;
}

float TimeOf(UInt32 bits) {
	float f;
	std::memcpy(&f, &bits, 4);
	return f;
}

// ---- The route -----------------------------------------------------------------------------

void __fastcall RouteReplacement(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kApplyAnimDataSite) {
		Original(self, timeBits, flag);
		return;
	}
	InterlockedIncrement64(&g_calls);
	const su::Handle root = reinterpret_cast<UInt32>(self);
	// The order is held across the run: the map may move under a rebuild of
	// another root on another thread, so the pointer is used under the lock's
	// guarantee that this root's node stays (unordered_map keeps node
	// addresses stable across inserts; only clear() drops them, and clear()
	// runs under the lock before any run that follows it).
	su::Order* order = OrderFor(root);
	if (order == nullptr) {
		Original(self, timeBits, flag);
		return;
	}
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	eng::UpdateSubtree(*order, TimeOf(timeBits), (flag & 0xFF) != 0);
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_routedTicks, end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_routed);
}

// ---- The shadow ------------------------------------------------------------------------------

struct State {
	std::vector<su::Transform> worlds;
	std::vector<su::Bound> bounds;
};

void Take(const su::Order& order, State& s) {
	s.worlds.resize(order.entries.size());
	s.bounds.resize(order.entries.size());
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		const su::Handle h = order.entries[i].object;
		std::memcpy(&s.worlds[i], eng::Pointer(h + eng::kWorldTransformOffset), sizeof(su::Transform));
		std::memcpy(&s.bounds[i], eng::Pointer(h + eng::kWorldBoundOffset), sizeof(su::Bound));
	}
}

void Put(const su::Order& order, const State& s) {
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		const su::Handle h = order.entries[i].object;
		std::memcpy(eng::Pointer(h + eng::kWorldTransformOffset), &s.worlds[i], sizeof(su::Transform));
		std::memcpy(eng::Pointer(h + eng::kWorldBoundOffset), &s.bounds[i], sizeof(su::Bound));
	}
}

void Hex(const void* bytes, UInt32 count, char* out) {
	const UInt8* b = static_cast<const UInt8*>(bytes);
	for (UInt32 i = 0; i < count; ++i) {
		std::snprintf(out + 2 * i, 3, "%02X", b[i]);
	}
}

void __fastcall ShadowReplacement(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kApplyAnimDataSite) {
		Original(self, timeBits, flag);
		return;
	}
	InterlockedIncrement64(&g_calls);
	const su::Handle root = reinterpret_cast<UInt32>(self);
	su::Order* order = OrderFor(root);
	if (order == nullptr) {
		RecordSkipped(Shadowed::ActorUpdate, "the root is not a plain NiNode");
		Original(self, timeBits, flag);
		return;
	}
	// A copy of the order: the original's run below may change the tree
	// (an Other subtree may attach nodes), and the comparison must walk the
	// objects the run was built for.
	const su::Order kept = *order;
	State before;
	Take(kept, before);
	Original(self, timeBits, flag);
	State original;
	Take(kept, original);
	Put(kept, before);
	{
		eng::RoundingFromX87 rounding;
		eng::Port port;
		su::Run(kept, port, TimeOf(timeBits), false);  // the controllers ran in the original's pass
	}
	State ours;
	Take(kept, ours);
	Put(kept, original);
	InterlockedIncrement64(&g_routed);
	char inputs[96];
	std::snprintf(inputs, sizeof(inputs), "root=%08X entries=%u time=%g flag=%u", root, static_cast<unsigned>(kept.entries.size()),
	              TimeOf(timeBits), flag & 0xFF);
	for (std::size_t i = 0; i < kept.entries.size(); ++i) {
		const bool world = std::memcmp(&original.worlds[i], &ours.worlds[i], sizeof(su::Transform)) != 0;
		const bool bound = std::memcmp(&original.bounds[i], &ours.bounds[i], sizeof(su::Bound)) != 0;
		if (!world && !bound) {
			continue;
		}
		char a[sizeof(su::Transform) * 2 + 1];
		char b[sizeof(su::Transform) * 2 + 1];
		if (world) {
			Hex(&original.worlds[i], sizeof(su::Transform), a);
			Hex(&ours.worlds[i], sizeof(su::Transform), b);
		} else {
			Hex(&original.bounds[i], sizeof(su::Bound), a);
			Hex(&ours.bounds[i], sizeof(su::Bound), b);
		}
		char detail[320];
		std::snprintf(detail, sizeof(detail), " entry %u object %08X kind %d %s: original %s; rebuild %s", static_cast<unsigned>(i),
		              kept.entries[i].object, static_cast<int>(kept.entries[i].kind), world ? "world" : "bound", a, b);
		RecordCall(Shadowed::ActorUpdate, true, detail, inputs);
		return;
	}
	RecordCall(Shadowed::ActorUpdate, false, "", "");
}

}  // namespace

UInt32 InstallRouteActorUpdate(const RouteSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.actorUpdate) {
		return 0;
	}
	s_done = true;
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	if (!InstallEntryDetour(kName, eng::kNiAVObjectUpdateAddress, kUpdateEntryBytes, sizeof(kUpdateEntryBytes),
	                        reinterpret_cast<const void*>(&RouteReplacement), &g_original, log, DetourRole::Route)) {
		return 0;
	}
	g_mode = Mode::Route;
	log("Route %s: the call from ActorAnimData::ApplyAnimData 0x00471F20 runs the flat subtree update (SceneUpdateLogic.h); every other "
	    "caller continues in the original",
	    kName);
	return 1;
}

UInt32 InstallShadowActorUpdate(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.actorUpdate) {
		return 0;
	}
	s_done = true;
	g_log = log;
	if (!InstallEntryDetour(kName, eng::kNiAVObjectUpdateAddress, kUpdateEntryBytes, sizeof(kUpdateEntryBytes),
	                        reinterpret_cast<const void*>(&ShadowReplacement), &g_original, log, DetourRole::Shadow)) {
		return 0;
	}
	g_mode = Mode::Shadow;
	return 1;
}

void LogActorUpdateSummary() {
	if (g_mode != Mode::Route || g_log == nullptr) {
		return;
	}
	const LONG64 calls = InterlockedExchange64(&g_calls, 0);
	const LONG64 routed = InterlockedExchange64(&g_routed, 0);
	const LONG64 built = InterlockedExchange64(&g_built, 0);
	const LONG64 rebuilt = InterlockedExchange64(&g_rebuilt, 0);
	const LONG64 fallback = InterlockedExchange64(&g_fallback, 0);
	const LONG64 ticks = InterlockedExchange64(&g_routedTicks, 0);
	const double ms = g_frequency.QuadPart > 0 ? static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_frequency.QuadPart) : 0.0;
	g_log("Route: actor update - %lld calls from ApplyAnimData, %lld run flat in %.2f ms (%.1f us each), %lld orders built, %lld rebuilt after a "
	      "shape change, %lld to the original (root not a plain node)",
	      calls, routed, ms, routed > 0 ? ms * 1000.0 / static_cast<double>(routed) : 0.0, built, rebuilt, fallback);
}

}  // namespace obng::plugin
