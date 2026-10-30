#pragma once

// The shadow light's range walk - the replacement of 0x007D59E0 (proposed
// ShadowSceneLight::CollectGeometryInRange; no Bethesda or Gamebryo symbol is
// known) - over plain values and ports. Two implementations with one contract:
//
//   ReferenceWalk  the original's recursion as read (re/functions/0x007D59E0.md),
//                  range test through a light port; in the tests that port is
//                  the verified rebuild of 0x007D2EA0 (ShadowSceneLight_IsInRange.h,
//                  ledger: verified (emulator)). It is the oracle and nothing more.
//   BatchedWalk    the replacement: the same events from the same inputs, with
//                  the plane test done as one batch per object and the recursion
//                  driven by an explicit stack.
//
// The contract both keep (the in-game gate compares exactly these):
//   - the sequence of AddGeometry calls, in order, each with the geometry the
//     second vt[+0x10] call returned and the value +0x1B0 holds at that moment;
//   - per visited object, in visit order: the in/out decision, the mask before
//     and after its range test (the observer's onVisit);
//   - AL (false only when the gate is off at the root) and +0x1B0 afterwards.
//
// Every claim below carries a mark (AGENTS.md): read = seen in the instructions
// (address given; through the mappings cited), derived = follows from read
// facts (which ones said), unverified = not checked.
//
// ---- What the batching removes ------------------------------------------------
//
// R1. The per-plane call. The original calls 0x0047DA70 once per active plane
//     (CALL at 0x007D2F26, read, re/functions/0x007D2EA0.md B4..B7), each call
//     re-deriving the bound from a stack copy. BatchedWalk computes the active
//     lanes' distances in one pass (ClassifyPlanes) and classifies the results
//     in a second; no function is called per plane (derived: ClassifyPlanes
//     calls only inline arithmetic helpers).
// R2. The per-plane mask re-read. The original reads +0x1B0 for every plane
//     (TEST at 0x007D2F19) and again for the AND (0x007D2F3E) (read, C5 of
//     0x007D2EA0). BatchedWalk takes the lanes once. Exact because inside one
//     range test the only write to +0x1B0 is the AND that clears the current
//     plane's bit (read, 0x007D2F3E), later planes test only higher bits
//     (read, B7 ascending), and WhichSide writes no memory but its own
//     argument slot (read, re/functions/0x0047DA70.md; NiBound_WhichSide.h).
//     So the snapshot's bits equal the live bits each plane would read, and
//     the final mask is the snapshot with the retired bits cleared (derived).
// R3. The native recursion: 16 bytes of frame + 4 argument + 4 return address
//     per level (read, mapping "Stack frame") becomes one WalkItem per pending
//     visit or open child loop on an explicit, reusable stack.
// R4. The save/restore of +0x1B0 per visit: one save (0x007D59FF) and one of
//     three restores (0x007D5A7F, 0x007D5AD6, 0x007D5B07) per call (read, C3).
//     BatchedWalk carries the mask on its stack and never writes +0x1B0. The
//     original leaves +0x1B0 at its entry value on every exit (derived, C3),
//     so both end with the same value.
// R5. The per-visit work on the source light: the read of this+0x100
//     (0x007D5A03), the GetRTTI virtual call (0x007D2EC0), the reads of +0x14C,
//     the planes, the light's position and range (read, 0x007D2EA0.md). They are
//     taken once per walk (LightValues, SelectSourceMode). Exact only if nothing
//     the walk calls writes them: the walk body and IsInRange write only +0x1B0
//     (read, both mappings); 0x007D5790, the object's vt[+0x08]/vt[+0x10],
//     the node's GetRTTI and the helpers 0x007C59E0 / 0x0049FFC0 are not read
//     at instruction level (unverified), and the source light's GetRTTI is
//     assumed to answer the same RTTI on every call (unverified; it is the
//     class's virtual).
//
// ---- What it keeps --------------------------------------------------------------
//
// K1. The arithmetic: each lane is WhichSide's own SignedDistance and Classify
//     (NiBound_WhichSideLogic.h, verified on hardware) and IsInRange's own
//     ClassifySide; the distance path is IsInRange's Distance and RangeExceeds.
//     The batching is structural, not numerical: the same function per lane,
//     the same roundings in the same order (derived; the differential test
//     compares every event).
// K2. The number of per-plane classifications: one per active lane per tested
//     object, as many as the original's WhichSide calls (derived from B4 of
//     0x007D2EA0; counted by the tests). No shortcut on the first "wholly
//     negative": the original does not leave the loop either (read, C6 of
//     0x007D2EA0), and the later planes' retirements are part of the mask
//     the AddGeometry and observer see.
// K3. The visit order: pre-order, children in index order, the switch's
//     active child only (read, 0x007D59E0 B4..B13l).
// K4. Every scene query the original makes, in the original's order per
//     visit: the gate per call (0x007D59E1), vt[+0x08], the node's GetRTTI,
//     0x007C59E0 / 0x0049FFC0 / flags on a switch, the count re-read per child
//     (0x007D5AC2) and the slot read per child (0x007D5A9B), vt[+0x10] twice
//     on a geometry (0x007D5AE5, 0x007D5AF2) (read). Keeping them costs a
//     little and keeps the walk exact for a scene that changes under it
//     (AddGeometry is unread); dropping any is a later step that needs the
//     callee read first (the design note lists them).
//
// 64-bit clean: objects are std::uint32_t handles the ports resolve; no
// pointer-size assumption in this header. The RTTI and the +0x14C dword are
// the 32-bit binary's values, compared only.

#include "engine/render/NiBound_WhichSideLogic.h"
#include "engine/render/ShadowSceneLight_IsInRangeLogic.h"

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

namespace obng::engine::render::shadow_light_range_walk {

namespace in_range = shadow_scene_light_is_in_range;

// A scene object (NiAVObject: node or geometry) as the ports know it; 0 is
// null. The engine ports map handles to objects.
using ObjectHandle = std::uint32_t;
inline constexpr ObjectHandle kNullObject = 0;

// The address compared at 0x007D5A37 (CMP EAX,0xB3FD70; read) and named
// NiRTTI_NiSwitchNode by xOBSE (obse/obse/NiRTTI_1_2_416.inl:158, via
// re/symbols/xobse.csv:1740): two sources.
inline constexpr std::uint32_t kNiRttiNiSwitchNode = 0x00B3FD70;

// ---- The walk's decisions (read, re/functions/0x007D59E0.md) ----------------------

// B1: CMP dword [0x00B42F48],0 / JNZ at 0x007D59E1/0x007D59EB.
constexpr bool IsWalkEnabled(std::uint32_t gate) {
	return gate != 0;
}

// B4: an exact compare of the node's GetRTTI result (C8), no IsKindOf walk.
constexpr bool IsExactSwitchNode(std::uint32_t rtti) {
	return rtti == kNiRttiNiSwitchNode;
}

// B11: FLDZ / FCOMP [child+0x2C] / FNSTSW / TEST AH,0x44 / JNP: skipped only
// when the radius compares equal to 0.0 (+0.0 and -0.0); NaN sets C2 and is
// not skipped. The C++ != is true for NaN (derived).
constexpr bool HasNonZeroRadius(float radius) {
	return radius != 0.0f;
}

// B7s/B12: TEST byte [object+0x18],1 (0x007D5A67, 0x007D5AB4).
constexpr bool IsAppCulled(std::uint8_t flags) {
	return (flags & 1u) != 0;
}

// B8 and B13l: the u16 count +0xB6 against the index, unsigned (JBE / JA at
// 0x007D5A95 and 0x007D5ACE, C12). B8 (count == 0) is this with index 0.
constexpr bool LoopContinues(std::uint16_t count, std::uint32_t index) {
	return count > index;
}

// The plain-node child filter, in the original's order: slot null (B10), the
// radius +0x2C (B11), flags bit 0 (B12).
template <class Scene>
bool IsWalkableChild(Scene& scene, ObjectHandle child) {
	return child != kNullObject && HasNonZeroRadius(scene.bound(child).radius) && !IsAppCulled(scene.flags(child));
}

// The switch's active-child filter, in the original's order: null (B5),
// 0x0049FFC0 (B6; no inline +0x2C compare on this path, C7), flags bit 0 (B7s).
template <class Scene>
bool IsWalkableActiveChild(Scene& scene, ObjectHandle active) {
	return active != kNullObject && scene.hasNonZeroBoundRadius(active) && !IsAppCulled(scene.flags(active));
}

// ---- The light, once per walk ---------------------------------------------------------

// IsInRange's source cases in its own order: null source (B1), directional
// (B2), +0x14C non-zero: planes, else distance (B3) (read, 0x007D2EA0).
enum class SourceMode : std::uint8_t {
	NoSource,     // every visit out of range, mask kept
	Directional,  // every visit in range, mask kept
	Planes,       // the six-plane test over the mask
	Distance,     // range > |light - centre| - radius, mask kept
};

constexpr SourceMode SelectSourceMode(bool hasSource, std::uint32_t sourceRtti, std::uint32_t planeSource) {
	if (!hasSource) {
		return SourceMode::NoSource;
	}
	if (in_range::IsDirectional(sourceRtti)) {
		return SourceMode::Directional;
	}
	return in_range::UsesPlanePath(planeSource) ? SourceMode::Planes : SourceMode::Distance;
}

// Everything IsInRange reads from the light, taken once at the walk's entry
// (R5). Offsets per ShadowSceneLight_IsInRange.h (read there).
struct LightValues {
	bool hasSource;                                              // this+0x100 != null (B1)
	std::uint32_t sourceRtti;                                    // source GetRTTI (B2); used only when hasSource
	std::uint32_t planeSource;                                   // the dword this+0x14C (B3), only compared with 0
	std::array<which_side::PlaneValues, in_range::kPlaneCount> planes;  // this+0x150 + 16*i
	std::uint32_t activePlanes;                                  // this+0x1B0 at the walk's entry
	in_range::PointValues sourcePosition;                        // source+0x88
	float sourceRange;                                           // source+0xF8
};

// ---- The batched plane test ---------------------------------------------------------------

inline constexpr std::uint32_t kPlaneLanes = (std::uint32_t{1} << in_range::kPlaneCount) - 1;

// One object against every active plane: the lanes taken from the mask once
// (R2), the distances in one pass, the classification in a second (R1).
struct PlaneBatch {
	std::uint32_t lanes;     // the active planes (mask bits 0..5)
	std::uint32_t negative;  // lanes wholly on the negative side (WhichSide 2)
	std::uint32_t positive;  // lanes wholly on the positive side (WhichSide 1)
	std::array<float, in_range::kPlaneCount> distance;  // d per active lane, as WhichSide stores it
};

inline PlaneBatch ClassifyPlanes(const std::array<which_side::PlaneValues, in_range::kPlaneCount>& planes,
	std::uint32_t mask, which_side::SphereValues sphere) {
	PlaneBatch batch{};
	batch.lanes = mask & kPlaneLanes;
	for (unsigned plane = 0; plane < in_range::kPlaneCount; ++plane) {
		if (in_range::IsPlaneActive(batch.lanes, plane)) {
			batch.distance[plane] = which_side::SignedDistance(planes[plane], sphere).distanceStored;
		}
	}
	for (unsigned plane = 0; plane < in_range::kPlaneCount; ++plane) {
		if (!in_range::IsPlaneActive(batch.lanes, plane)) {
			continue;
		}
		const auto side = static_cast<std::int32_t>(which_side::Classify(batch.distance[plane], sphere.radius));
		switch (in_range::ClassifySide(side)) {
		case in_range::PlaneOutcome::OutOfRange:
			batch.negative |= in_range::PlaneBit(plane);
			break;
		case in_range::PlaneOutcome::Retire:
			batch.positive |= in_range::PlaneBit(plane);
			break;
		case in_range::PlaneOutcome::Keep:
			break;
		}
	}
	return batch;
}

// The per-plane classifications a batch made: one per active lane (K2).
constexpr unsigned PlaneClassifications(const PlaneBatch& batch) {
	return static_cast<unsigned>(std::popcount(batch.lanes));
}

struct RangeDecision {
	bool inRange;
	std::uint32_t mask;  // +0x1B0 as IsInRange leaves it
};

// TestPlanes' result from the batch: out of range iff some lane was wholly
// negative (B5, no early exit), every wholly positive lane retired (B6), bits
// 6..31 untouched (B7) (derived from TestPlanes, R2).
constexpr RangeDecision ApplyPlaneBatch(const PlaneBatch& batch, std::uint32_t mask) {
	return RangeDecision{batch.negative == 0, mask & ~batch.positive};
}

// IsInRange over plain values for one object. readBound() is the object's
// world bound (+0x20..+0x2C); it is read only on the two paths that use it,
// as IsInRange copies it only after B1 and B2. planeClassifications counts K2.
template <class ReadBound, class SquareRoot>
RangeDecision TestObject(const LightValues& light, SourceMode mode, std::uint32_t mask, ReadBound&& readBound,
	SquareRoot&& squareRoot, std::uint64_t& planeClassifications) {
	switch (mode) {
	case SourceMode::NoSource:
		return RangeDecision{false, mask};
	case SourceMode::Directional:
		return RangeDecision{true, mask};
	case SourceMode::Planes: {
		const PlaneBatch batch = ClassifyPlanes(light.planes, mask, readBound());
		planeClassifications += PlaneClassifications(batch);
		return ApplyPlaneBatch(batch, mask);
	}
	case SourceMode::Distance: {
		const which_side::SphereValues b = readBound();
		const in_range::BoundValues bound{{b.centerX, b.centerY, b.centerZ}, b.radius};
		const float gap = in_range::Distance(light.sourcePosition, bound, squareRoot).gap;
		return RangeDecision{in_range::RangeExceeds(light.sourceRange, gap), mask};
	}
	}
	return RangeDecision{false, mask};  // not reached: every SourceMode is handled
}

// ---- Observation ---------------------------------------------------------------------------

// onVisit(object, inRange, maskIn, maskOut) per visited object, in visit order.
struct NoWalkObserver {
	void onVisit(ObjectHandle, bool, std::uint32_t, std::uint32_t) const {}
};

// ---- (a) ReferenceWalk: the original's recursion as read ----------------------------------

// Ports (ShadowLightRangeWalk.h states their engine shapes):
//   light.readMask() / light.writeMask(mask)  this+0x1B0
//   light.isInRange(object)                   0x007D2EA0 with this+0x100
//   scene.*                                   the RangeWalkScene surface
// One call is one invocation of 0x007D59E0; the return value is its AL.
// The observer's maskOut and AddGeometry's live mask are extra reads of
// +0x1B0 for observation; the original makes neither.
template <class Light, class Scene, class Observer>
bool ReferenceWalk(Light& light, Scene& scene, ObjectHandle object, Observer& observer) {
	if (!IsWalkEnabled(scene.gate())) {  // B1: before any read of +0x1B0
		return false;
	}
	const std::uint32_t savedMask = light.readMask();  // 0x007D59F4 / 0x007D59FF
	const bool inRange = light.isInRange(object);      // 0x007D5A0B
	observer.onVisit(object, inRange, savedMask, light.readMask());
	if (inRange) {  // B2
		const ObjectHandle node = scene.asNode(object);  // vt[+0x08], 0x007D5A20
		if (node != kNullObject) {                       // B3
			if (IsExactSwitchNode(scene.nodeRtti(node))) {  // B4
				const ObjectHandle active = scene.activeChild(node);  // 0x007C59E0
				if (IsWalkableActiveChild(scene, active)) {           // B5, B6, B7s
					ReferenceWalk(light, scene, active, observer);    // 0x007D5A74, AL ignored
				}
			} else {
				for (std::uint32_t i = 0; LoopContinues(scene.childCount(node), i); ++i) {  // B8, B13l
					const ObjectHandle child = scene.child(node, i);                       // 0x007D5A9B
					if (IsWalkableChild(scene, child)) {                                   // B10, B11, B12
						ReferenceWalk(light, scene, child, observer);                     // 0x007D5ABD, AL ignored
					}
				}
			}
		} else if (scene.asGeometry(object) != kNullObject) {  // B14: vt[+0x10], first call
			scene.addGeometry(scene.asGeometry(object), light.readMask());  // second call, 0x007D5AFC
		}
	}
	light.writeMask(savedMask);  // Exit R
	return true;
}

// ---- (b) BatchedWalk: the replacement -------------------------------------------------------

// One entry of the explicit stack: a pending visit (the object and the mask
// its parent left, the state the original enters a call with), or an open
// child loop of a plain node (the node, its narrowed mask, the next index).
struct WalkItem {
	ObjectHandle object;
	std::uint32_t mask;
	std::uint32_t nextChild;
	bool childLoop;
};

// Reusable between walks so a warmed-up walk does not allocate.
struct BatchedWalkScratch {
	std::vector<WalkItem> stack;
	std::uint64_t planeClassifications = 0;  // of the last walk (K2)
};

// The same contract as ReferenceWalk over the same scene ports; the light is
// a snapshot (R5) and +0x1B0 is never written (R4). squareRoot is the port for
// 0x00982C30 as IsInRange's distance path takes it.
template <class Scene, class SquareRoot, class Observer>
bool BatchedWalk(const LightValues& light, Scene& scene, ObjectHandle root, SquareRoot&& squareRoot,
	BatchedWalkScratch& scratch, Observer& observer) {
	scratch.planeClassifications = 0;
	if (!IsWalkEnabled(scene.gate())) {  // B1 of the root call
		return false;
	}
	const SourceMode mode = SelectSourceMode(light.hasSource, light.sourceRtti, light.planeSource);
	std::vector<WalkItem>& stack = scratch.stack;
	stack.clear();
	stack.push_back(WalkItem{root, light.activePlanes, 0, false});

	// The original's recursive call starts with its own gate read (B1): a
	// child is entered only while the gate is on. Nothing runs between this
	// read and the visit, as nothing runs between the original's CALL and its
	// first instruction.
	const auto enter = [&](ObjectHandle child, std::uint32_t mask) {
		if (IsWalkEnabled(scene.gate())) {
			stack.push_back(WalkItem{child, mask, 0, false});
		}
	};

	while (!stack.empty()) {
		WalkItem& top = stack.back();
		if (top.childLoop) {
			if (!LoopContinues(scene.childCount(top.object), top.nextChild)) {  // B8, B13l
				stack.pop_back();  // the node's Exit R: nothing to restore
				continue;
			}
			const ObjectHandle node = top.object;
			const std::uint32_t mask = top.mask;
			const std::uint32_t index = top.nextChild++;
			const ObjectHandle child = scene.child(node, index);
			if (IsWalkableChild(scene, child)) {  // B10, B11, B12
				enter(child, mask);               // may reallocate: top is not used after this
			}
			continue;
		}

		const WalkItem visit = top;
		stack.pop_back();
		const RangeDecision decision = TestObject(
			light, mode, visit.mask, [&] { return scene.bound(visit.object); }, squareRoot, scratch.planeClassifications);
		observer.onVisit(visit.object, decision.inRange, visit.mask, decision.mask);
		if (!decision.inRange) {  // B2: the subtree skipped
			continue;
		}
		const ObjectHandle node = scene.asNode(visit.object);
		if (node != kNullObject) {
			if (IsExactSwitchNode(scene.nodeRtti(node))) {
				const ObjectHandle active = scene.activeChild(node);
				if (IsWalkableActiveChild(scene, active)) {
					enter(active, decision.mask);
				}
			} else {
				stack.push_back(WalkItem{node, decision.mask, 0, true});
			}
		} else if (scene.asGeometry(visit.object) != kNullObject) {
			scene.addGeometry(scene.asGeometry(visit.object), decision.mask);
		}
	}
	return true;
}

} // namespace obng::engine::render::shadow_light_range_walk
