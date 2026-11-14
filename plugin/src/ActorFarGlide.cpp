// [Route] ActorFarGlide: see ActorFarGlide.h.

#include "ActorFarGlide.h"

#include <windows.h>
#include <intrin.h>

#include <cstring>

#include "engine/scene/ActorFarGlideLogic.h"

namespace obng::plugin {

namespace {

namespace far_update = engine::scene::actor_far_update;
namespace glide = engine::scene::actor_far_glide;

constexpr UInt32 kRefPosition = 0x2C;          // the reference's position (ActorFarUpdate.cpp)
constexpr UInt32 kRefNode = 0x3C;              // its scene node (ActorCensus.cpp)
constexpr UInt32 kWorldBound = 0x20;           // NiAVObject's world bound: centre, radius (SceneUpdate.h kWorldBoundOffset)
constexpr UInt32 kWorldTranslate = 0x88;       // its world translate (SceneUpdate.h kWorldTransformOffset + 9 floats; xOBSE m_worldTranslate)
constexpr UInt32 kChildren = 0xB0;             // NiNode's child array (SceneUpdate.h kChildrenOffset)
constexpr UInt32 kChildSlots = 0xB6;           // its first free entry, u16 (SceneUpdate.h kChildSlotsOffset)
// NiObject's virtual +0x08: the object as a node, or null (read in the image:
// NiNode 0x00A7E38C, BSFadeNode 0x00A3F944 and BSFaceGenNiNode 0x00A64F5C
// hold 0x007616D0 "mov eax,ecx; ret" there, a geometry's 0x00A7F27C holds
// 0x006F7070 "xor eax,eax; ret"; xOBSE NiObject::GetAsNiNode).
constexpr UInt32 kSlotAsNode = 0x08;
constexpr unsigned kMaxDepth = 40;
using AsNodeFn = UInt32(__thiscall*)(void* self);

bool g_on = false;
LogFn g_log = nullptr;
// The summary's counts; the actors' loop thread writes, the summary reads and clears.
volatile LONG64 g_shifts = 0;
volatile LONG64 g_objects = 0;
volatile LONG64 g_shiftTicks = 0;
volatile LONG64 g_recomposed = 0;     // the root was moved by someone else between two shifts
volatile LONG64 g_updates = 0;        // updates counted for the leap
volatile LONG64 g_leapMilli = 0;      // the leaps, thousandths of a unit
volatile LONG64 g_leftMilli = 0;      // what the glide left of them
volatile LONG64 g_withGain = 0;       // updates that left a gain

UInt32 Dword(UInt32 address) { return *reinterpret_cast<const UInt32*>(address); }

void Add(UInt32 address, const float d[3]) {
	auto* v = reinterpret_cast<float*>(address);
	v[0] += d[0];
	v[1] += d[1];
	v[2] += d[2];
}

// Every object of the subtree: world translate and world bound moved by d.
unsigned ShiftTree(UInt32 object, const float d[3], unsigned depth) {
	Add(object + kWorldTranslate, d);
	Add(object + kWorldBound, d);
	unsigned count = 1;
	if (depth >= kMaxDepth || reinterpret_cast<AsNodeFn>(Dword(Dword(object) + kSlotAsNode))(reinterpret_cast<void*>(object)) == 0) {
		return count;
	}
	const UInt32 children = Dword(object + kChildren);
	const UInt32 slots = *reinterpret_cast<const UInt16*>(object + kChildSlots);
	for (UInt32 i = 0; children != 0 && i < slots; ++i) {
		if (const UInt32 child = Dword(children + 4 * i)) {
			count += ShiftTree(child, d, depth + 1);
		}
	}
	return count;
}

}  // namespace

void ActorFarGlideInit(bool on, LogFn log) {
	g_on = on;
	g_log = log;
	log("Route Actor::Update (far actors): between its updates a far actor is %s", on ? "shown on its way - its subtree shifted by the animation's summed step so far (ActorFarGlide)"
	                                                                                 : "left standing (ActorFarGlide=0)");
}

void ActorFarGlideSkipped(far_update::Entry& entry, UInt32 actor) {
	if (!g_on) {
		return;
	}
	glide::State& state = entry.glide;
	float offset[3];
	if (!glide::Offset(state, entry.hasRootMotion, entry.rootMotion, offset)) {
		return;
	}
	const UInt32 node = Dword(actor + kRefNode);
	if (node == 0) {
		return;
	}
	float root[3];
	std::memcpy(root, reinterpret_cast<const void*>(node + kWorldTranslate), sizeof(root));
	const bool moved = glide::RootMoved(state, root);
	float delta[3];
	if (!glide::Shift(state, offset, moved, delta)) {
		return;
	}
	const unsigned __int64 t0 = __rdtsc();
	const unsigned objects = ShiftTree(node, delta, 0);
	std::memcpy(root, reinterpret_cast<const void*>(node + kWorldTranslate), sizeof(root));
	glide::Wrote(state, root);
	InterlockedExchangeAdd64(&g_shiftTicks, static_cast<LONG64>(__rdtsc() - t0));
	InterlockedIncrement64(&g_shifts);
	InterlockedExchangeAdd64(&g_objects, objects);
	if (moved) {
		InterlockedIncrement64(&g_recomposed);
	}
}

void ActorFarGlideAtUpdate(far_update::Entry& entry, UInt32 actor, const float* step) {
	if (!g_on) {
		return;
	}
	float here[3];
	std::memcpy(here, reinterpret_cast<const void*>(actor + kRefPosition), sizeof(here));
	const float moved[3] = {here[0] - entry.lastPosition[0], here[1] - entry.lastPosition[1], here[2] - entry.lastPosition[2]};
	const float none[3] = {};
	const glide::Measured m = glide::AtUpdate(entry.glide, entry.hasPosition, moved, step != nullptr, step != nullptr ? step : none);
	if (m.counted) {
		InterlockedIncrement64(&g_updates);
		InterlockedExchangeAdd64(&g_leapMilli, static_cast<LONG64>(m.leap * 1000.0f));
		InterlockedExchangeAdd64(&g_leftMilli, static_cast<LONG64>(m.left * 1000.0f));
	}
	if (entry.glide.hasGain) {
		InterlockedIncrement64(&g_withGain);
	}
}

void LogActorFarGlideSummary() {
	if (!g_on) {
		return;
	}
	const LONG64 shifts = InterlockedExchange64(&g_shifts, 0);
	const LONG64 objects = InterlockedExchange64(&g_objects, 0);
	const LONG64 ticks = InterlockedExchange64(&g_shiftTicks, 0);
	const LONG64 updates = InterlockedExchange64(&g_updates, 0);
	const double leap = static_cast<double>(InterlockedExchange64(&g_leapMilli, 0)) / 1000.0;
	const double left = static_cast<double>(InterlockedExchange64(&g_leftMilli, 0)) / 1000.0;
	g_log("Route: far actors' glide - %lld shifts of %.0f objects each, %lld ticks each (the clock's), %lld after someone else composed the subtree anew; "
	      "%lld updates of moving actors: the leap %.1f units on the mean, %.1f left of it with the glide (%.0f %%); %lld updates left a gain",
	      shifts, shifts != 0 ? static_cast<double>(objects) / static_cast<double>(shifts) : 0.0, shifts != 0 ? ticks / shifts : 0,
	      InterlockedExchange64(&g_recomposed, 0), updates, updates != 0 ? leap / static_cast<double>(updates) : 0.0,
	      updates != 0 ? left / static_cast<double>(updates) : 0.0, leap > 0.0 ? left * 100.0 / leap : 0.0, InterlockedExchange64(&g_withGain, 0));
}

}  // namespace obng::plugin
