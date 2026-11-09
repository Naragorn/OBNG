// The actors' skeleton update in the game: see FastSceneUpdate.h.

#include "FastSceneUpdate.h"

#include <intrin.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

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

enum class Mode { Off, Route, Shadow, Timing };
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

// ---- The timing of the original ---------------------------------------------------------------

void __fastcall TimingReplacement(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kApplyAnimDataSite) {
		Original(self, timeBits, flag);
		return;
	}
	InterlockedIncrement64(&g_calls);
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	Original(self, timeBits, flag);
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_routedTicks, end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_routed);
}

// ---- The shadow ------------------------------------------------------------------------------
//
// One run, the rebuild's, with the original's arithmetic beside it: the
// subtree cannot run twice in a frame (the controllers fire once, the heads'
// BSFaceGenNiNode steps the face animation by the tick - re/functions/
// 0x0055D4C0.md - and a controller may rewrite an earlier node's local), so
// the gate verifies each value where it is made. Every world transform is
// computed by Compose and by the original 0x0053D7A0 on the same parent
// world and local, compared bit for bit, and the original's is written;
// every node's bound, after the flat merge pass, is recomputed by the
// original NiNode::UpdateWorldBound 0x0070A360 (vtable +0x78) from the
// children's bounds, compared, and the original's stays. The game thus gets
// the original's numbers; the calls, their order and the merge decisions
// are the rebuild's, which the oracle test holds against the original's
// bytes (tests/SceneUpdateOracleTest.cpp).

using ComposeFn = su::Transform*(__thiscall*)(const su::Transform* parentWorld, su::Transform* out, const su::Transform* local);
constexpr UInt32 kComposeAddress = 0x0053D7A0;  // read: 0x0070C13B calls it with ECX = parent+0x64, (out, local), RET 8

struct VerifyPort : eng::Port {
	unsigned worldsCompared = 0;
	unsigned worldsDiffer = 0;
	su::Handle firstDiffering = 0;
	su::Transform firstOurs{};
	su::Transform firstOriginal{};
	unsigned geometryCompared = 0;
	unsigned geometryDiffer = 0;
	su::Handle firstGeometry = 0;
	su::Bound firstGeometryOurs{};
	su::Bound firstGeometryOriginal{};
	std::uint16_t firstGeometryFlagsOurs = 0;
	std::uint16_t firstGeometryFlagsOriginal = 0;

	// A plain geometry's bound: the original NiGeometry::UpdateWorldBound
	// 0x00722AA0 first (pure in the object's state: worlds, model or skin,
	// the old bound and the flags), its result kept aside, the state put
	// back, the rebuild's GeometryBound, compared, and the original's written.
	void GeometryBoundSelf(su::Handle h) {
		su::Bound before;
		std::uint16_t flagsBefore;
		std::memcpy(&before, eng::Pointer(h + eng::kWorldBoundOffset), sizeof(before));
		std::memcpy(&flagsBefore, eng::Pointer(h + eng::kFlagsOffset), 2);
		UpdateWorldBound(h);
		su::Bound original;
		std::uint16_t flagsOriginal;
		std::memcpy(&original, eng::Pointer(h + eng::kWorldBoundOffset), sizeof(original));
		std::memcpy(&flagsOriginal, eng::Pointer(h + eng::kFlagsOffset), 2);
		std::memcpy(eng::Pointer(h + eng::kWorldBoundOffset), &before, sizeof(before));
		std::memcpy(eng::Pointer(h + eng::kFlagsOffset), &flagsBefore, 2);
		eng::Port::GeometryBoundSelf(h);
		su::Bound ours;
		std::uint16_t flagsOurs;
		std::memcpy(&ours, eng::Pointer(h + eng::kWorldBoundOffset), sizeof(ours));
		std::memcpy(&flagsOurs, eng::Pointer(h + eng::kFlagsOffset), 2);
		++geometryCompared;
		if (std::memcmp(&ours, &original, sizeof(ours)) != 0 || flagsOurs != flagsOriginal) {
			if (geometryDiffer == 0) {
				firstGeometry = h;
				firstGeometryOurs = ours;
				firstGeometryOriginal = original;
				firstGeometryFlagsOurs = flagsOurs;
				firstGeometryFlagsOriginal = flagsOriginal;
			}
			++geometryDiffer;
		}
		std::memcpy(eng::Pointer(h + eng::kWorldBoundOffset), &original, sizeof(original));
		std::memcpy(eng::Pointer(h + eng::kFlagsOffset), &flagsOriginal, 2);
	}

	void WriteWorld(su::Handle h, const su::Transform& ours) {
		su::Transform original;
		const su::Transform* parent = ParentWorld(h);
		const su::Transform local = Local(h);
		if (parent == nullptr) {
			original = local;
		} else {
			reinterpret_cast<ComposeFn>(kComposeAddress)(parent, &original, &local);
		}
		++worldsCompared;
		if (std::memcmp(&original, &ours, sizeof(su::Transform)) != 0) {
			if (worldsDiffer == 0) {
				firstDiffering = h;
				firstOurs = ours;
				firstOriginal = original;
			}
			++worldsDiffer;
		}
		eng::Port::WriteWorld(h, original);
	}
};

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
	const su::Order kept = *order;  // the run's own copy: another root's rebuild must not move it
	VerifyPort port;
	{
		eng::RoundingFromX87 rounding;
		su::Run(kept, port, TimeOf(timeBits), (flag & 0xFF) != 0);
	}
	// The bounds: children first (the reverse of the depth-first order), the
	// original's own merge from the children's bounds against the flat pass's.
	unsigned boundsCompared = 0;
	unsigned boundsDiffer = 0;
	su::Handle firstBound = 0;
	su::Bound firstBoundOurs{};
	su::Bound firstBoundOriginal{};
	for (std::size_t n = kept.entries.size(); n-- > 0;) {
		const su::Entry& e = kept.entries[n];
		if (e.kind != su::Kind::Node) {
			continue;
		}
		su::Bound ours;
		std::memcpy(&ours, eng::Pointer(e.object + eng::kWorldBoundOffset), sizeof(ours));
		port.UpdateWorldBound(e.object);  // NiNode::UpdateWorldBound 0x0070A360 through the vtable: radius 0, the children merged
		su::Bound original;
		std::memcpy(&original, eng::Pointer(e.object + eng::kWorldBoundOffset), sizeof(original));
		++boundsCompared;
		if (std::memcmp(&ours, &original, sizeof(ours)) != 0) {
			if (boundsDiffer == 0) {
				firstBound = e.object;
				firstBoundOurs = ours;
				firstBoundOriginal = original;
			}
			++boundsDiffer;
		}
	}
	eng::UpdateUpwardFrom(root);
	InterlockedIncrement64(&g_routed);
	if (port.worldsDiffer == 0 && boundsDiffer == 0 && port.geometryDiffer == 0) {
		RecordCall(Shadowed::ActorUpdate, false, "", "");
		return;
	}
	char inputs[96];
	std::snprintf(inputs, sizeof(inputs), "root=%08X entries=%u time=%g flag=%u", root, static_cast<unsigned>(kept.entries.size()),
	              TimeOf(timeBits), flag & 0xFF);
	char a[sizeof(su::Transform) * 2 + 1];
	char b[sizeof(su::Transform) * 2 + 1];
	char detail[400];
	if (port.worldsDiffer != 0) {
		Hex(&port.firstOriginal, sizeof(su::Transform), a);
		Hex(&port.firstOurs, sizeof(su::Transform), b);
		std::snprintf(detail, sizeof(detail), " %u of %u worlds differ, %u of %u bounds, %u of %u geometry bounds; first world: object %08X original %s; rebuild %s",
		              port.worldsDiffer, port.worldsCompared, boundsDiffer, boundsCompared, port.geometryDiffer, port.geometryCompared, port.firstDiffering,
		              a, b);
	} else if (port.geometryDiffer != 0) {
		Hex(&port.firstGeometryOriginal, sizeof(su::Bound), a);
		Hex(&port.firstGeometryOurs, sizeof(su::Bound), b);
		std::snprintf(detail, sizeof(detail),
		              " %u of %u geometry bounds differ (worlds all equal, %u; node bounds %u of %u); first geometry: object %08X original %s flags %04X; "
		              "rebuild %s flags %04X",
		              port.geometryDiffer, port.geometryCompared, port.worldsCompared, boundsDiffer, boundsCompared, port.firstGeometry, a,
		              port.firstGeometryFlagsOriginal, b, port.firstGeometryFlagsOurs);
	} else {
		Hex(&firstBoundOriginal, sizeof(su::Bound), a);
		Hex(&firstBoundOurs, sizeof(su::Bound), b);
		std::snprintf(detail, sizeof(detail), " %u of %u bounds differ (worlds all equal, %u); first bound: node %08X original %s; rebuild %s",
		              boundsDiffer, boundsCompared, port.worldsCompared, firstBound, a, b);
	}
	RecordCall(Shadowed::ActorUpdate, true, detail, inputs);
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

UInt32 InstallActorUpdateTiming(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.actorUpdateTiming) {
		return 0;
	}
	s_done = true;
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	if (!InstallEntryDetour(kName, eng::kNiAVObjectUpdateAddress, kUpdateEntryBytes, sizeof(kUpdateEntryBytes),
	                        reinterpret_cast<const void*>(&TimingReplacement), &g_original, log, DetourRole::Shadow)) {
		return 0;
	}
	g_mode = Mode::Timing;
	log("Probe %s: the call from ActorAnimData::ApplyAnimData 0x00471F20 runs the original, timed as the route is", kName);
	return 1;
}

void LogActorUpdateSummary() {
	if ((g_mode != Mode::Route && g_mode != Mode::Timing) || g_log == nullptr) {
		return;
	}
	const LONG64 calls = InterlockedExchange64(&g_calls, 0);
	const LONG64 routed = InterlockedExchange64(&g_routed, 0);
	const LONG64 built = InterlockedExchange64(&g_built, 0);
	const LONG64 rebuilt = InterlockedExchange64(&g_rebuilt, 0);
	const LONG64 fallback = InterlockedExchange64(&g_fallback, 0);
	const LONG64 ticks = InterlockedExchange64(&g_routedTicks, 0);
	const double ms = g_frequency.QuadPart > 0 ? static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_frequency.QuadPart) : 0.0;
	g_log("%s: actor update - %lld calls from ApplyAnimData, %lld run %s in %.2f ms (%.1f us each), %lld orders built, %lld rebuilt after a "
	      "shape change, %lld to the original (root not a plain node)",
	      g_mode == Mode::Route ? "Route" : "Probe", calls, routed, g_mode == Mode::Route ? "flat" : "in the original, timed", ms, routed > 0 ? ms * 1000.0 / static_cast<double>(routed) : 0.0, built, rebuilt, fallback);
}

}  // namespace obng::plugin
