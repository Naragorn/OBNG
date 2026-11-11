// The actors' skeleton update in the game: see FastSceneUpdate.h.

#include "FastSceneUpdate.h"

#include <intrin.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ProbeLogic.h"
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
bool g_ordersHeld = false;  // a batch holds order pointers across several lookups: nothing is dropped meanwhile (ActorBatch.cpp)

// Counters for the summary (interlocked: the summary thread reads them).
volatile LONG64 g_calls = 0;        // calls from ApplyAnimData
volatile LONG64 g_routed = 0;       // of them run flat
volatile LONG64 g_built = 0;        // orders built for a new root
volatile LONG64 g_rebuilt = 0;      // orders rebuilt after the shape changed
volatile LONG64 g_fallback = 0;     // to the original: the root is not a plain node
volatile LONG64 g_routedTicks = 0;  // QPC ticks inside the routed calls
LARGE_INTEGER g_frequency{};
// The routed calls by the subtree's size (entries: <=50, <=100, <=200, <=400,
// more), for the comparison with the probe's node buckets of the original.
constexpr unsigned kSizeBuckets = 5;
constexpr std::uint32_t kSizeBounds[kSizeBuckets - 1] = {50, 100, 200, 400};
volatile LONG64 g_sizeCalls[kSizeBuckets] = {};
volatile LONG64 g_sizeTicks[kSizeBuckets] = {};
// The route's split per bucket: the forward pass (worlds, controllers, the
// geometry bounds), of it the geometry bounds, and the bound pass.
volatile LONG64 g_sizeForwardTicks[kSizeBuckets] = {};
volatile LONG64 g_sizeGeometryTicks[kSizeBuckets] = {};
volatile LONG64 g_sizeGeometryCalls[kSizeBuckets] = {};
volatile LONG64 g_sizeBoundsTicks[kSizeBuckets] = {};
volatile LONG64 g_sizeCollisionTicks[kSizeBuckets] = {};
volatile LONG64 g_sizeCollisionCalls[kSizeBuckets] = {};
volatile LONG64 g_sizeControllerTicks[kSizeBuckets] = {};
volatile LONG64 g_sizeControllerCalls[kSizeBuckets] = {};

// The bones nothing hangs on (SceneUpdateLogic.h UnusedBones): the names
// that may be left out come from OBNG-unused-bones.txt beside the DLL, one a
// line (tools/re/unused-bone-names.py writes it: the installed skeleton's
// names that no skeleton of the game has). g_bonesSkip leaves them out
// ([Route] ActorUnusedBones), g_bonesCount only counts ([Probe] UnusedBones).
constexpr const char* kBoneNamesFile = "OBNG-unused-bones.txt";
constexpr std::uint32_t kNameOffset = 0x08;            // NiObjectNET's name, a char* (OBVR GameAddresses.h kNiObjectNameOffset; CrashLogger prints it as "Name")
constexpr std::uint32_t kControllerNextOffset = 0x34;  // NiTimeController's next (read 0x0047C930: the chain's walk)
bool g_bonesSkip = false;
bool g_bonesCount = false;
std::unordered_set<std::string> g_boneNames;
volatile LONG64 g_bonesOrders = 0;       // orders built that have unused bones
volatile LONG64 g_bonesEntries = 0;      // their entries
volatile LONG64 g_bonesUnused = 0;       // of them unused
volatile LONG64 g_bonesControllers = 0;  // controllers in the unused bones' chains
volatile LONG64 g_bonesWithProperties = 0;  // unused bones with a property list
volatile LONG64 g_bonesLeftOut = 0;      // unused bones over the routed calls (left out, or would be)
constexpr unsigned kBoneClasses = 8;
SRWLOCK g_boneClassLock = SRWLOCK_INIT;
UInt32 g_boneClass[kBoneClasses] = {};   // the controllers' vtables
LONG64 g_boneClassCount[kBoneClasses] = {};

struct EngineBones {
	const std::unordered_set<su::Handle>* skinned;
	bool MayBeUnused(su::Handle h) const {
		const char* name = reinterpret_cast<const char*>(eng::Dword(h, kNameOffset));
		return name != nullptr && g_boneNames.count(name) != 0;
	}
	bool HasCollision(su::Handle h) const { return eng::Dword(h, eng::kCollisionObjectOffset) != 0; }
	bool IsSkinBone(su::Handle h) const { return skinned->count(h) != 0; }
};

// The order's unused bones, counted; set on the order when they are to be left out.
void MarkUnusedBones(su::Order* order) {
	if ((!g_bonesSkip && !g_bonesCount) || g_boneNames.empty()) {
		return;
	}
	std::unordered_set<su::Handle> skinned;
	for (const su::Entry& e : order->entries) {
		if (e.kind != su::Kind::Geometry && e.kind != su::Kind::GeometryPlain) {
			continue;
		}
		const su::Handle instance = eng::Dword(e.object, eng::kSkinInstanceOffset);
		if (instance == 0) {
			continue;
		}
		skinned.insert(eng::Dword(instance, eng::kSkinRootParentOffset));
		const su::Handle bones = eng::Dword(instance, eng::kSkinBonesOffset);
		const su::Handle data = eng::Dword(instance, eng::kSkinDataOffset);
		const std::uint32_t count = data != 0 && bones != 0 ? eng::Dword(data, eng::kSkinBoneCountOffset) : 0;
		for (std::uint32_t i = 0; i < count && i < 1024; ++i) {
			skinned.insert(eng::Dword(bones, 4 * i));
		}
	}
	std::vector<std::uint8_t> unused = su::UnusedBones(*order, EngineBones{&skinned});
	const std::size_t n = su::CountUnused(unused);
	if (n == 0) {
		return;
	}
	InterlockedIncrement64(&g_bonesOrders);
	InterlockedAdd64(&g_bonesEntries, static_cast<LONG64>(order->entries.size()));
	InterlockedAdd64(&g_bonesUnused, static_cast<LONG64>(n));
	AcquireSRWLockExclusive(&g_boneClassLock);
	for (std::size_t i = 0; i < unused.size(); ++i) {
		if (unused[i] == 0) {
			continue;
		}
		const su::Handle h = order->entries[i].object;
		if (eng::Dword(h, eng::kPropertyListOffset) != 0) {
			++g_bonesWithProperties;
		}
		unsigned guard = 0;
		for (su::Handle c = eng::Dword(h, eng::kControllerOffset); c != 0 && guard < 16; c = eng::Dword(c, kControllerNextOffset), ++guard) {
			++g_bonesControllers;
			const UInt32 vtable = eng::Dword(c, 0);
			for (unsigned k = 0; k < kBoneClasses; ++k) {
				if (g_boneClass[k] == vtable || g_boneClass[k] == 0) {
					g_boneClass[k] = vtable;
					++g_boneClassCount[k];
					break;
				}
			}
		}
	}
	ReleaseSRWLockExclusive(&g_boneClassLock);
	order->unusedCount = static_cast<std::uint32_t>(n);
	if (g_bonesSkip) {
		order->unused = std::move(unused);
	}
}

// The names file beside this DLL; the number of names read.
std::size_t LoadBoneNames() {
	HMODULE module = nullptr;
	char path[MAX_PATH] = {};
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&LoadBoneNames), &module) ||
	    GetModuleFileNameA(module, path, MAX_PATH) == 0) {
		return 0;
	}
	char* slash = std::strrchr(path, '\\');
	if (slash == nullptr) {
		return 0;
	}
	*(slash + 1) = '\0';
	const std::string file = std::string(path) + kBoneNamesFile;
	FILE* in = std::fopen(file.c_str(), "rb");
	if (in == nullptr) {
		return 0;
	}
	char line[160];
	while (std::fgets(line, sizeof(line), in) != nullptr) {
		std::size_t n = std::strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
			line[--n] = '\0';
		}
		if (n > 0) {
			g_boneNames.insert(line);  // as it stands: a name may begin or end with a blank
		}
	}
	std::fclose(in);
	return g_boneNames.size();
}

bool g_collisionWrites = false;
SRWLOCK g_collisionWritesLock = SRWLOCK_INIT;
probe::CollisionWrites g_collisionWritesSum;

// The engine port with the geometry bounds timed (QueryPerformanceCounter
// around each, about 20 ns a read; 15-25 geometries an actor).
struct TimedPort : eng::Port {
	LONGLONG geometryTicks = 0;
	LONGLONG geometryCalls = 0;
	LONGLONG collisionTicks = 0;
	LONGLONG collisionCalls = 0;
	LONGLONG controllerTicks = 0;
	LONGLONG controllerCalls = 0;
	// Counted, not timed: a clock read per call would cost more than the
	// calls (250 controller walks an armoured actor; run 20261109-191706
	// timed them once: 32-37 us with the clock's own 10 us inside).
	// With [Probe] CollisionWrites: the node's local and world transform
	// before and after the collision object's update, compared.
	std::uint32_t collisionWrote = 0;
	void AfterWorldData(su::Handle h) {
		if (eng::Dword(h, eng::kCollisionObjectOffset) == 0) {
			return;  // the engine port does nothing without one
		}
		++collisionCalls;
		if (!g_collisionWrites) {
			eng::Port::AfterWorldData(h);
			return;
		}
		constexpr std::uint32_t kBoth = eng::kWorldTransformOffset + sizeof(su::Transform) - eng::kLocalTransformOffset;
		UInt8 before[kBoth];
		const void* transforms = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(h + eng::kLocalTransformOffset));
		std::memcpy(before, transforms, kBoth);
		eng::Port::AfterWorldData(h);
		if (std::memcmp(before, transforms, kBoth) != 0) {
			++collisionWrote;
		}
	}
	// What the walks find, counted: list nodes read, property updates and
	// chain controllers fired (reusing the tick fields as plain counts).
	void UpdateObjectControllers(su::Handle h, float time) {
		++controllerCalls;
		for (su::Handle node = eng::Dword(h, eng::kPropertyListOffset); node != 0; node = eng::Dword(node, 0)) {
			++collisionTicks;  // list nodes
			const su::Handle item = eng::Dword(node, 8);
			if (item != 0 && eng::Dword(item, eng::kControllerOffset) != 0) {
				++controllerTicks;  // fired
			}
		}
		for (su::Handle c = eng::Dword(h, eng::kControllerOffset); c != 0; c = eng::Dword(c, 0x34)) {
			++controllerTicks;
		}
		eng::Port::UpdateObjectControllers(h, time);
	}
	void GeometryBoundSelf(su::Handle h) {
		LARGE_INTEGER a;
		LARGE_INTEGER b;
		QueryPerformanceCounter(&a);
		eng::Port::GeometryBoundSelf(h);
		QueryPerformanceCounter(&b);
		geometryTicks += b.QuadPart - a.QuadPart;
		++geometryCalls;
	}
};
unsigned SizeBucket(std::uint32_t entries) {
	for (unsigned i = 0; i < kSizeBuckets - 1; ++i) {
		if (entries <= kSizeBounds[i]) {
			return i;
		}
	}
	return kSizeBuckets - 1;
}

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
	if (g_orders.size() >= kOrdersKeptAtMost && !g_ordersHeld) {
		g_orders.clear();
	}
	InterlockedIncrement64(&g_built);
	auto inserted = g_orders.emplace(root, su::Build(tree, root));
	su::Order* order = &inserted.first->second;
	MarkUnusedBones(order);
	ReleaseSRWLockExclusive(&g_lock);
	return order;
}

float TimeOf(UInt32 bits) {
	float f;
	std::memcpy(&f, &bits, 4);
	return f;
}

// ---- The route -----------------------------------------------------------------------------

// The routed subtree without the parent's upward pass: the forward pass and
// the bounds under the engine's rounding, accounted.
void RoutedDown(su::Order* order, UInt32 timeBits, UInt32 flag) {
	const unsigned bucket = SizeBucket(static_cast<std::uint32_t>(order->entries.size()));
	LARGE_INTEGER start;
	LARGE_INTEGER middle;
	LARGE_INTEGER end;
	TimedPort port;
	QueryPerformanceCounter(&start);
	{
		eng::RoundingFromX87 rounding;
		su::RunForward(*order, port, TimeOf(timeBits), (flag & 0xFF) != 0);
		QueryPerformanceCounter(&middle);
		su::RunBounds(*order, port);
	}
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_routedTicks, end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_routed);
	InterlockedAdd64(&g_sizeTicks[bucket], end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_sizeCalls[bucket]);
	InterlockedAdd64(&g_sizeForwardTicks[bucket], middle.QuadPart - start.QuadPart);
	InterlockedAdd64(&g_sizeGeometryTicks[bucket], port.geometryTicks);
	InterlockedAdd64(&g_sizeGeometryCalls[bucket], port.geometryCalls);
	InterlockedAdd64(&g_sizeBoundsTicks[bucket], end.QuadPart - middle.QuadPart);
	InterlockedAdd64(&g_sizeCollisionTicks[bucket], port.collisionTicks);
	InterlockedAdd64(&g_sizeCollisionCalls[bucket], port.collisionCalls);
	InterlockedAdd64(&g_sizeControllerTicks[bucket], port.controllerTicks);
	InterlockedAdd64(&g_sizeControllerCalls[bucket], port.controllerCalls);
	if (order->unusedCount != 0) {
		InterlockedAdd64(&g_bonesLeftOut, order->unusedCount);
	}
	if (g_collisionWrites) {
		AcquireSRWLockExclusive(&g_collisionWritesLock);
		g_collisionWritesSum.AddSkeleton(static_cast<std::uint32_t>(port.collisionCalls), port.collisionWrote);
		ReleaseSRWLockExclusive(&g_collisionWritesLock);
	}
}

// The parent's upward pass, accounted to the same sums.
void RoutedUp(const su::Order* order) {
	const unsigned bucket = SizeBucket(static_cast<std::uint32_t>(order->entries.size()));
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	eng::UpdateUpwardFrom(order->root);
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_routedTicks, end.QuadPart - start.QuadPart);
	InterlockedAdd64(&g_sizeTicks[bucket], end.QuadPart - start.QuadPart);
	InterlockedAdd64(&g_sizeBoundsTicks[bucket], end.QuadPart - start.QuadPart);
}

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
	RoutedDown(order, timeBits, flag);
	RoutedUp(order);
}

// ---- The timing of the original ---------------------------------------------------------------

void __fastcall TimingReplacement(void* self, void* /*edx*/, UInt32 timeBits, UInt32 flag) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kApplyAnimDataSite) {
		Original(self, timeBits, flag);
		return;
	}
	InterlockedIncrement64(&g_calls);
	// The size bucket from the order (built and kept as the route would, so
	// the original's time per class is comparable with the route's).
	const su::Order* order = OrderFor(reinterpret_cast<UInt32>(self));
	const unsigned bucket = order == nullptr ? kSizeBuckets - 1 : SizeBucket(static_cast<std::uint32_t>(order->entries.size()));
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	Original(self, timeBits, flag);
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_routedTicks, end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_routed);
	InterlockedAdd64(&g_sizeTicks[bucket], end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_sizeCalls[bucket]);
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

// ---- The cells' selected update ------------------------------------------------------------------
//
// NiAVObject's selected update 0x007073A0 (the object's +0x64, then the
// parent's +0x94), for the call from TESObjectCELL::UpdateSelectedNodes
// 0x004D4970 (return site 0x004D4A00): 68 calls a frame on the Gold Road,
// the whole of the grid update's 0.9-1.5 ms. The same three modes as the
// actors' update: the flat selected run, its gate, the timed original.

constexpr UInt32 kSelectedUpdateAddress = 0x007073A0;
constexpr UInt8 kSelectedEntryBytes[] = {0xD9, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1};  // fld [esp+4]; push esi; mov esi,ecx (read)
constexpr UInt32 kCellUpdateSite = 0x004D4A00;  // the return into TESObjectCELL::UpdateSelectedNodes (read: CALL at 0x004D49FB)
using SelectedFn = void(__fastcall*)(void* self, void* edx, UInt32 timeBits);

Mode g_gridMode = Mode::Off;
void* g_gridOriginal = nullptr;
SRWLOCK g_gridLock = SRWLOCK_INIT;
std::unordered_map<su::Handle, su::SelectedOrder> g_gridOrders;
volatile LONG64 g_gridCalls = 0;
volatile LONG64 g_gridRouted = 0;
volatile LONG64 g_gridBuilt = 0;
volatile LONG64 g_gridRebuilt = 0;
volatile LONG64 g_gridFallback = 0;
volatile LONG64 g_gridTicks = 0;
volatile LONG64 g_gridEntries = 0;

void GridOriginal(void* self, UInt32 timeBits) { reinterpret_cast<SelectedFn>(g_gridOriginal)(self, nullptr, timeBits); }

su::SelectedOrder* GridOrderFor(su::Handle root) {
	eng::Tree tree;
	AcquireSRWLockExclusive(&g_gridLock);
	auto it = g_gridOrders.find(root);
	if (it != g_gridOrders.end() && su::MatchesSelected(it->second, tree)) {
		su::SelectedOrder* order = &it->second;
		ReleaseSRWLockExclusive(&g_gridLock);
		return order;
	}
	if (tree.KindOf(root) != su::Kind::Node) {
		if (it != g_gridOrders.end()) {
			g_gridOrders.erase(it);
		}
		ReleaseSRWLockExclusive(&g_gridLock);
		InterlockedIncrement64(&g_gridFallback);
		return nullptr;
	}
	if (it != g_gridOrders.end()) {
		InterlockedIncrement64(&g_gridRebuilt);
		it->second = su::BuildSelected(tree, root);
		su::SelectedOrder* order = &it->second;
		ReleaseSRWLockExclusive(&g_gridLock);
		return order;
	}
	if (g_gridOrders.size() >= kOrdersKeptAtMost) {
		g_gridOrders.clear();
	}
	InterlockedIncrement64(&g_gridBuilt);
	auto inserted = g_gridOrders.emplace(root, su::BuildSelected(tree, root));
	su::SelectedOrder* order = &inserted.first->second;
	ReleaseSRWLockExclusive(&g_gridLock);
	return order;
}

void __fastcall GridRouteReplacement(void* self, void* /*edx*/, UInt32 timeBits) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kCellUpdateSite) {
		GridOriginal(self, timeBits);
		return;
	}
	InterlockedIncrement64(&g_gridCalls);
	const su::Handle root = reinterpret_cast<UInt32>(self);
	su::SelectedOrder* order = GridOrderFor(root);
	if (order == nullptr) {
		GridOriginal(self, timeBits);
		return;
	}
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	{
		eng::Port port;
		eng::RoundingFromX87 rounding;
		su::RunSelected(*order, port, TimeOf(timeBits));
	}
	eng::UpdateUpwardFrom(root);
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_gridTicks, end.QuadPart - start.QuadPart);
	InterlockedAdd64(&g_gridEntries, static_cast<LONG64>(order->entries.size()));
	InterlockedIncrement64(&g_gridRouted);
}

void __fastcall GridTimingReplacement(void* self, void* /*edx*/, UInt32 timeBits) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kCellUpdateSite) {
		GridOriginal(self, timeBits);
		return;
	}
	InterlockedIncrement64(&g_gridCalls);
	LARGE_INTEGER start;
	LARGE_INTEGER end;
	QueryPerformanceCounter(&start);
	GridOriginal(self, timeBits);
	QueryPerformanceCounter(&end);
	InterlockedAdd64(&g_gridTicks, end.QuadPart - start.QuadPart);
	InterlockedIncrement64(&g_gridRouted);
}

// The gate: the flat selected run once, every world against the original
// compose and every plain geometry's bound against 0x00722AA0 (VerifyPort),
// then every visited node's bound against the original NiNode::
// UpdateWorldBound 0x0070A360, children first; the originals' values stay.
void __fastcall GridShadowReplacement(void* self, void* /*edx*/, UInt32 timeBits) {
	if (reinterpret_cast<UInt32>(_ReturnAddress()) != kCellUpdateSite) {
		GridOriginal(self, timeBits);
		return;
	}
	InterlockedIncrement64(&g_gridCalls);
	const su::Handle root = reinterpret_cast<UInt32>(self);
	su::SelectedOrder* order = GridOrderFor(root);
	if (order == nullptr) {
		RecordSkipped(Shadowed::GridUpdate, "the root is not a plain NiNode");
		GridOriginal(self, timeBits);
		return;
	}
	const su::SelectedOrder kept = *order;
	VerifyPort port;
	{
		eng::RoundingFromX87 rounding;
		su::RunSelected(kept, port, TimeOf(timeBits));
	}
	unsigned boundsCompared = 0;
	unsigned boundsDiffer = 0;
	su::Handle firstBound = 0;
	su::Bound firstOurs{};
	su::Bound firstOriginal{};
	for (std::size_t n = kept.entries.size(); n-- > 0;) {
		const su::SelectedEntry& e = kept.entries[n];
		if (e.rigid || e.kind != su::Kind::Node) {
			continue;
		}
		su::Bound ours;
		std::memcpy(&ours, eng::Pointer(e.object + eng::kWorldBoundOffset), sizeof(ours));
		port.UpdateWorldBound(e.object);
		su::Bound original;
		std::memcpy(&original, eng::Pointer(e.object + eng::kWorldBoundOffset), sizeof(original));
		++boundsCompared;
		if (std::memcmp(&ours, &original, sizeof(ours)) != 0) {
			if (boundsDiffer == 0) {
				firstBound = e.object;
				firstOurs = ours;
				firstOriginal = original;
			}
			++boundsDiffer;
		}
	}
	eng::UpdateUpwardFrom(root);
	InterlockedIncrement64(&g_gridRouted);
	if (port.worldsDiffer == 0 && port.geometryDiffer == 0 && boundsDiffer == 0) {
		RecordCall(Shadowed::GridUpdate, false, "", "");
		return;
	}
	char inputs[96];
	std::snprintf(inputs, sizeof(inputs), "root=%08X entries=%u time=%g", root, static_cast<unsigned>(kept.entries.size()), TimeOf(timeBits));
	char a[sizeof(su::Transform) * 2 + 1];
	char b[sizeof(su::Transform) * 2 + 1];
	char detail[400];
	if (port.worldsDiffer != 0) {
		Hex(&port.firstOriginal, sizeof(su::Transform), a);
		Hex(&port.firstOurs, sizeof(su::Transform), b);
		std::snprintf(detail, sizeof(detail), " %u of %u worlds differ, %u of %u geometry bounds, %u of %u node bounds; first world: object %08X original %s; rebuild %s",
		              port.worldsDiffer, port.worldsCompared, port.geometryDiffer, port.geometryCompared, boundsDiffer, boundsCompared, port.firstDiffering, a, b);
	} else if (port.geometryDiffer != 0) {
		Hex(&port.firstGeometryOriginal, sizeof(su::Bound), a);
		Hex(&port.firstGeometryOurs, sizeof(su::Bound), b);
		std::snprintf(detail, sizeof(detail), " %u of %u geometry bounds differ; first: object %08X original %s flags %04X; rebuild %s flags %04X",
		              port.geometryDiffer, port.geometryCompared, port.firstGeometry, a, port.firstGeometryFlagsOriginal, b, port.firstGeometryFlagsOurs);
	} else {
		Hex(&firstOriginal, sizeof(su::Bound), a);
		Hex(&firstOurs, sizeof(su::Bound), b);
		std::snprintf(detail, sizeof(detail), " %u of %u node bounds differ (worlds %u and geometry bounds %u all equal); first: node %08X original %s; rebuild %s",
		              boundsDiffer, boundsCompared, port.worldsCompared, port.geometryCompared, firstBound, a, b);
	}
	RecordCall(Shadowed::GridUpdate, true, detail, inputs);
}

UInt32 InstallGrid(Mode mode, LogFn log) {
	static bool s_done = false;
	if (s_done) {
		return 0;
	}
	s_done = true;
	g_log = log;
	QueryPerformanceFrequency(&g_frequency);
	const void* replacement = mode == Mode::Route ? reinterpret_cast<const void*>(&GridRouteReplacement)
	                          : mode == Mode::Shadow ? reinterpret_cast<const void*>(&GridShadowReplacement)
	                                                 : reinterpret_cast<const void*>(&GridTimingReplacement);
	if (!InstallEntryDetour("NiAVObject::UpdateSelected", kSelectedUpdateAddress, kSelectedEntryBytes, sizeof(kSelectedEntryBytes), replacement,
	                        &g_gridOriginal, log, mode == Mode::Route ? DetourRole::Route : DetourRole::Shadow)) {
		return 0;
	}
	g_gridMode = mode;
	if (mode == Mode::Route) {
		log("Route NiAVObject::UpdateSelected: the call from TESObjectCELL::UpdateSelectedNodes 0x004D4970 runs the flat selected update; every "
		    "other caller continues in the original");
	}
	return 1;
}

void LogGridSummary() {
	if ((g_gridMode != Mode::Route && g_gridMode != Mode::Timing) || g_log == nullptr) {
		return;
	}
	const LONG64 calls = InterlockedExchange64(&g_gridCalls, 0);
	const LONG64 routed = InterlockedExchange64(&g_gridRouted, 0);
	const LONG64 built = InterlockedExchange64(&g_gridBuilt, 0);
	const LONG64 rebuilt = InterlockedExchange64(&g_gridRebuilt, 0);
	const LONG64 fallback = InterlockedExchange64(&g_gridFallback, 0);
	const LONG64 ticks = InterlockedExchange64(&g_gridTicks, 0);
	const LONG64 entries = InterlockedExchange64(&g_gridEntries, 0);
	const double ms = g_frequency.QuadPart > 0 ? static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_frequency.QuadPart) : 0.0;
	g_log("%s: cells' selected update - %lld calls, %lld run %s in %.2f ms (%.2f us each, %.1f visited entries each), %lld orders built, %lld "
	      "rebuilt after a shape or flag change, %lld to the original",
	      g_gridMode == Mode::Route ? "Route" : "Probe", calls, routed, g_gridMode == Mode::Route ? "flat" : "in the original, timed", ms,
	      routed > 0 ? ms * 1000.0 / static_cast<double>(routed) : 0.0, routed > 0 ? static_cast<double>(entries) / static_cast<double>(routed) : 0.0,
	      built, rebuilt, fallback);
}

}  // namespace

bool ActorUpdateRouted() { return g_mode == Mode::Route; }

void HoldActorOrders(bool held) {
	AcquireSRWLockExclusive(&g_lock);
	g_ordersHeld = held;
	ReleaseSRWLockExclusive(&g_lock);
}

const void* PrepareActorSubtree(UInt32 root) {
	InterlockedIncrement64(&g_calls);
	return OrderFor(root);
}

void RunActorSubtreeDown(const void* order, UInt32 timeBits) {
	RoutedDown(const_cast<su::Order*>(static_cast<const su::Order*>(order)), timeBits, 1);
}

void RunActorSubtreeUp(const void* order) { RoutedUp(static_cast<const su::Order*>(order)); }

void RunActorUpdateOriginal(UInt32 root, UInt32 timeBits) { Original(reinterpret_cast<void*>(root), timeBits, 1); }

UInt32 InstallRouteActorUpdate(const RouteSettings& settings, LogFn log) {
	static bool s_done = false;
	const UInt32 grid = settings.gridUpdate ? InstallGrid(Mode::Route, log) : 0;
	if (s_done || !settings.actorUpdate) {
		return grid;
	}
	s_done = true;
	g_log = log;
	g_collisionWrites = settings.collisionWrites;
	g_bonesSkip = settings.actorUnusedBones;
	g_bonesCount = settings.unusedBonesProbe;
	if (g_bonesSkip || g_bonesCount) {
		const std::size_t names = LoadBoneNames();
		log("Route NiAVObject::Update: unused bones %s - %u names from %s%s", g_bonesSkip ? "left out" : "counted only", static_cast<unsigned>(names),
		    kBoneNamesFile, names == 0 ? " (no file or no names: nothing is unused)" : "");
	}
	QueryPerformanceFrequency(&g_frequency);
	if (!InstallEntryDetour(kName, eng::kNiAVObjectUpdateAddress, kUpdateEntryBytes, sizeof(kUpdateEntryBytes),
	                        reinterpret_cast<const void*>(&RouteReplacement), &g_original, log, DetourRole::Route)) {
		return grid;
	}
	g_mode = Mode::Route;
	log("Route %s: the call from ActorAnimData::ApplyAnimData 0x00471F20 runs the flat subtree update (SceneUpdateLogic.h); every other "
	    "caller continues in the original",
	    kName);
	return 1 + grid;
}

UInt32 InstallShadowActorUpdate(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	const UInt32 grid = settings.gridUpdate ? InstallGrid(Mode::Shadow, log) : 0;
	if (s_done || !settings.actorUpdate) {
		return grid;
	}
	s_done = true;
	g_log = log;
	if (!InstallEntryDetour(kName, eng::kNiAVObjectUpdateAddress, kUpdateEntryBytes, sizeof(kUpdateEntryBytes),
	                        reinterpret_cast<const void*>(&ShadowReplacement), &g_original, log, DetourRole::Shadow)) {
		return grid;
	}
	g_mode = Mode::Shadow;
	return 1 + grid;
}

// ---- The actors' pipeline timed (with [Probe] ActorUpdateTiming) -------------------------------
//
// Inclusive time of the steps one actor goes through each frame (read:
// re/functions/0x0040D800.md row 19 and 23, knowledge/render-thread-outside-
// the-pass.md): the process manager's update 0x00678510 (once a frame: the
// frame count), its high-process walk 0x00673C10, each actor's update
// 0x00603CA0 with its animation step 0x00603500 and ApplyAnimData
// 0x00471F20 (0x00471C00 before it on one path), and the process time step
// 0x0067ACA0. The steps nest; what an actor's update costs outside the
// animation and the propagation is the difference.
enum StepId { kStepManager, kStepHigh, kStepActor, kStepAnim, kStepApply, kStepPrepare, kStepTime, kStepCount };
const char* const kStepNames[kStepCount] = {"ProcessManager::Update 0x00678510", "UpdateHighProcesses 0x00673C10", "actor update 0x00603CA0",
                                            "actor animation 0x00603500", "ApplyAnimData 0x00471F20", "0x00471C00", "process time step 0x0067ACA0"};
volatile LONG64 g_stepCalls[kStepCount] = {};
volatile LONG64 g_stepTicks[kStepCount] = {};
void* g_stepOriginal[kStepCount] = {};
bool g_stepsOn = false;

struct StepTimer {
	StepId id;
	LARGE_INTEGER start;
	explicit StepTimer(StepId which) : id(which) { QueryPerformanceCounter(&start); }
	~StepTimer() {
		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);
		InterlockedAdd64(&g_stepTicks[id], end.QuadPart - start.QuadPart);
		InterlockedIncrement64(&g_stepCalls[id]);
	}
};
using Step0Fn = UInt32(__fastcall*)(void*, void*);
using Step1Fn = UInt32(__fastcall*)(void*, void*, UInt32);
using Step2Fn = UInt32(__fastcall*)(void*, void*, UInt32, UInt32);
UInt32 __fastcall StepManager(void* self, void* /*edx*/) {
	StepTimer t(kStepManager);
	return reinterpret_cast<Step0Fn>(g_stepOriginal[kStepManager])(self, nullptr);
}
UInt32 __fastcall StepHigh(void* self, void* /*edx*/, UInt32 a, UInt32 b) {
	StepTimer t(kStepHigh);
	return reinterpret_cast<Step2Fn>(g_stepOriginal[kStepHigh])(self, nullptr, a, b);
}
UInt32 __fastcall StepActor(void* self, void* /*edx*/, UInt32 a) {
	StepTimer t(kStepActor);
	return reinterpret_cast<Step1Fn>(g_stepOriginal[kStepActor])(self, nullptr, a);
}
UInt32 __fastcall StepAnim(void* self, void* /*edx*/, UInt32 a) {
	StepTimer t(kStepAnim);
	return reinterpret_cast<Step1Fn>(g_stepOriginal[kStepAnim])(self, nullptr, a);
}
UInt32 __fastcall StepApply(void* self, void* /*edx*/) {
	StepTimer t(kStepApply);
	return reinterpret_cast<Step0Fn>(g_stepOriginal[kStepApply])(self, nullptr);
}
UInt32 __fastcall StepPrepare(void* self, void* /*edx*/, UInt32 a) {
	StepTimer t(kStepPrepare);
	return reinterpret_cast<Step1Fn>(g_stepOriginal[kStepPrepare])(self, nullptr, a);
}
UInt32 __fastcall StepTime(void* self, void* /*edx*/, UInt32 a) {
	StepTimer t(kStepTime);
	return reinterpret_cast<Step1Fn>(g_stepOriginal[kStepTime])(self, nullptr, a);
}

void InstallActorSteps(LogFn log) {
	struct Entry {
		StepId id;
		UInt32 address;
		const UInt8* bytes;
		UInt32 length;
		const void* replacement;
	};
	static const UInt8 kManagerBytes[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1};              // sub esp,8; push esi; mov esi,ecx
	static const UInt8 kHighBytes[] = {0x83, 0xEC, 0x0C, 0x53, 0x55, 0x56};                 // sub esp,0Ch; push ebx; push ebp; push esi
	static const UInt8 kActorBytes[] = {0x6A, 0xFF, 0x68, 0x60, 0x2C, 0x9C, 0x00};          // push -1; push 009C2C60h
	static const UInt8 kAnimBytes[] = {0x83, 0xEC, 0x4C, 0x53, 0x55, 0x56, 0x8B, 0xF1};     // sub esp,4Ch; push ebx; push ebp; push esi; mov esi,ecx
	static const UInt8 kApplyBytes[] = {0x6A, 0xFF, 0x68, 0xE0, 0x0D, 0x9D, 0x00};          // push -1; push 009D0DE0h
	static const UInt8 kPrepareBytes[] = {0x6A, 0xFF, 0x68, 0x58, 0xEC, 0x9A, 0x00};        // push -1; push 009AEC58h
	static const UInt8 kTimeBytes[] = {0x83, 0xEC, 0x10, 0x53, 0x55, 0x56, 0x8B, 0xE9};     // sub esp,10h; push ebx; push ebp; push esi; mov ebp,ecx
	const Entry entries[] = {
	    {kStepManager, 0x00678510, kManagerBytes, sizeof(kManagerBytes), reinterpret_cast<const void*>(&StepManager)},
	    {kStepHigh, 0x00673C10, kHighBytes, sizeof(kHighBytes), reinterpret_cast<const void*>(&StepHigh)},
	    {kStepActor, 0x00603CA0, kActorBytes, sizeof(kActorBytes), reinterpret_cast<const void*>(&StepActor)},
	    {kStepAnim, 0x00603500, kAnimBytes, sizeof(kAnimBytes), reinterpret_cast<const void*>(&StepAnim)},
	    {kStepApply, 0x00471F20, kApplyBytes, sizeof(kApplyBytes), reinterpret_cast<const void*>(&StepApply)},
	    {kStepPrepare, 0x00471C00, kPrepareBytes, sizeof(kPrepareBytes), reinterpret_cast<const void*>(&StepPrepare)},
	    {kStepTime, 0x0067ACA0, kTimeBytes, sizeof(kTimeBytes), reinterpret_cast<const void*>(&StepTime)},
	};
	unsigned made = 0;
	for (const Entry& e : entries) {
		if (InstallEntryDetour(kStepNames[e.id], e.address, e.bytes, e.length, e.replacement, &g_stepOriginal[e.id], log, DetourRole::Shadow)) {
			++made;
		}
	}
	g_stepsOn = made != 0;
	log("Probe: actor steps - %u of %u timers made", made, static_cast<unsigned>(kStepCount));
}

void LogActorSteps() {
	if (!g_stepsOn || g_log == nullptr) {
		return;
	}
	probe::TimedStep steps[kStepCount];
	for (int i = 0; i < kStepCount; ++i) {
		steps[i].name = kStepNames[i];
		steps[i].calls = static_cast<std::uint64_t>(InterlockedExchange64(&g_stepCalls[i], 0));
		steps[i].ticks = static_cast<std::uint64_t>(InterlockedExchange64(&g_stepTicks[i], 0));
	}
	char line[900];
	const int at = std::snprintf(line, sizeof(line), "Probe: actor steps - ");
	probe::FormatTimedSteps(steps, kStepCount, steps[kStepManager].calls,
	                        g_frequency.QuadPart > 0 ? static_cast<double>(g_frequency.QuadPart) / 1000.0 : 0.0, line + at, sizeof(line) - at);
	g_log("%s", line);
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
	InstallGrid(Mode::Timing, log);
	InstallActorSteps(log);
	return 1;
}

void LogActorUpdateSummary() {
	LogGridSummary();
	LogActorSteps();
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
	char line[1400];
	int at = std::snprintf(line, sizeof(line), "%s: actor update by subtree size (entries):", g_mode == Mode::Route ? "Route" : "Probe");
	for (unsigned i = 0; i < kSizeBuckets && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++i) {
		const LONG64 c = InterlockedExchange64(&g_sizeCalls[i], 0);
		const LONG64 t = InterlockedExchange64(&g_sizeTicks[i], 0);
		if (c == 0) {
			continue;
		}
		const double perUs = g_frequency.QuadPart > 0 ? 1.0e6 / static_cast<double>(g_frequency.QuadPart) / static_cast<double>(c) : 0.0;
		const double us = static_cast<double>(t) * perUs;
		const LONG64 forward = InterlockedExchange64(&g_sizeForwardTicks[i], 0);
		const LONG64 geometry = InterlockedExchange64(&g_sizeGeometryTicks[i], 0);
		const LONG64 geometryCalls = InterlockedExchange64(&g_sizeGeometryCalls[i], 0);
		const LONG64 bounds = InterlockedExchange64(&g_sizeBoundsTicks[i], 0);
		const LONG64 collision = InterlockedExchange64(&g_sizeCollisionTicks[i], 0);
		const LONG64 collisionCalls = InterlockedExchange64(&g_sizeCollisionCalls[i], 0);
		const LONG64 controller = InterlockedExchange64(&g_sizeControllerTicks[i], 0);
		const LONG64 controllerCalls = InterlockedExchange64(&g_sizeControllerCalls[i], 0);
		if (i == kSizeBuckets - 1) {
			at += std::snprintf(line + at, sizeof(line) - at, " >%u: %lld calls %.1f us each", kSizeBounds[i - 1], c, us);
		} else {
			at += std::snprintf(line + at, sizeof(line) - at, " <=%u: %lld calls %.1f us each", kSizeBounds[i], c, us);
		}
		if (g_mode == Mode::Route && at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
			at += std::snprintf(line + at, sizeof(line) - at,
			                    " (forward %.1f, of it %.1f geometry bounds %.1f us, %.1f collision objects, %.1f controller walks over %.1f list nodes "
			                    "firing %.1f updates; bounds and upward %.1f)",
			                    static_cast<double>(forward) * perUs, static_cast<double>(geometryCalls) / static_cast<double>(c),
			                    static_cast<double>(geometry) * perUs, static_cast<double>(collisionCalls) / static_cast<double>(c),
			                    static_cast<double>(controllerCalls) / static_cast<double>(c), static_cast<double>(collision) / static_cast<double>(c),
			                    static_cast<double>(controller) / static_cast<double>(c), static_cast<double>(bounds) * perUs);
		}
		if (at > 0 && static_cast<std::size_t>(at) < sizeof(line)) {
			at += std::snprintf(line + at, sizeof(line) - at, ";");
		}
	}
	g_log("%s", line);
	if (g_bonesSkip || g_bonesCount) {
		const LONG64 orders = InterlockedExchange64(&g_bonesOrders, 0);
		const LONG64 entries = InterlockedExchange64(&g_bonesEntries, 0);
		const LONG64 unused = InterlockedExchange64(&g_bonesUnused, 0);
		const LONG64 controllers = InterlockedExchange64(&g_bonesControllers, 0);
		const LONG64 withProperties = InterlockedExchange64(&g_bonesWithProperties, 0);
		const LONG64 leftOut = InterlockedExchange64(&g_bonesLeftOut, 0);
		int at = std::snprintf(line, sizeof(line),
		                       "Route: unused bones (%s) - %lld bones over the routed calls; %lld orders built with unused bones: %lld entries, %lld unused, "
		                       "%lld controllers in their chains, %lld with a property list; the controllers' classes",
		                       g_bonesSkip ? "left out" : "counted only", leftOut, orders, entries, unused, controllers, withProperties);
		AcquireSRWLockExclusive(&g_boneClassLock);
		for (unsigned k = 0; k < kBoneClasses && g_boneClass[k] != 0 && at > 0 && static_cast<std::size_t>(at) < sizeof(line); ++k) {
			at += std::snprintf(line + at, sizeof(line) - at, " %08X:%lld", g_boneClass[k], g_boneClassCount[k]);
		}
		ReleaseSRWLockExclusive(&g_boneClassLock);
		g_log("%s", line);
	}
	if (g_collisionWrites) {
		AcquireSRWLockExclusive(&g_collisionWritesLock);
		const probe::CollisionWrites taken = g_collisionWritesSum;
		g_collisionWritesSum = probe::CollisionWrites{};
		ReleaseSRWLockExclusive(&g_collisionWritesLock);
		probe::FormatCollisionWrites(taken, line, sizeof(line));
		g_log("%s", line);
	}
}

}  // namespace obng::plugin
