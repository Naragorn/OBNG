#pragma once

// The [Route] switches: an entry whose rebuild passed its in-game gate can be
// routed - the game runs OBNG's rebuild there instead of the original - so
// its cost can be measured against the original's. Read in Plugin.cpp,
// decided in PluginEntry.cpp (Load), the routes themselves in
// RouteRangeWalk.cpp. No Windows or engine dependency: PluginEntryTest
// includes this.

#include "Types.h"

namespace obng::plugin {

// The range walk's route is one detour: the walk's entry 0x007D59E0.
// AddGeometry 0x007D5790 stays unpatched; the route calls it directly.
constexpr UInt32 kRouteRangeWalkDetours = 1;
// The stereo draw's route is four: DrawPrimitive 0x007672F0,
// DrawSkinnedPrimitive 0x00767520 and the renderer's two direct draw entries
// DrawShapeDirect 0x007694F0 and DrawTriStripsDirect 0x00769700
// (RouteStereoDraw.cpp).
constexpr UInt32 kRouteStereoDrawDetours = 4;
// The occlusion culler's are three: the accumulator's finish 0x007AE070,
// DrawRenderPass 0x007A9820 (the pass record, before its light constants)
// and Do_RenderShape 0x007693E0 (OcclusionCull.cpp); the direct entries are
// the stereo route's, which asks the culler. It needs the route (its eye
// data).
constexpr UInt32 kRouteOcclusionCullDetours = 3;
// The CRT square root's dispatch is one: _CIsqrt's entry 0x00982C30
// (FastSqrt.cpp, FastSqrtLogic.h).
constexpr UInt32 kRouteSqrtDetours = 1;
// The actors' skeleton update is one: NiAVObject::Update's entry 0x00707370
// (FastSceneUpdate.cpp, src/engine/scene/SceneUpdateLogic.h).
constexpr UInt32 kRouteActorUpdateDetours = 1;
// The actors' batch is two: the loop over the high actors 0x00677EC0 and
// ActorAnimData::ApplyAnimData 0x00471F20 (ActorBatch.cpp).
constexpr UInt32 kRouteActorBatchDetours = 2;

// [Route] StereoEyes: 1 is the 1:1 gate (the rebuild's port calls are the
// original's), 2 the double draw (StereoDraw.h). Anything else is unknown
// and read as 1, said in the log.
constexpr UInt8 StereoEyesFromIni(int value, bool* known) {
	*known = value == 1 || value == 2;
	return *known ? static_cast<UInt8>(value) : UInt8{1};
}

// [Route] StereoReapply: what the eye switch re-applies for the second eye.
// slot14 = the shader's slot 14 again (SetupShaderPrograms whole: the
// program bind, both maps, the palettes); view = the rebuilt ApplyConstants
// with the view filter on the vertex map only (ConstantReapply.h); none = no
// constants at all (the rows, the VIEW transform and the viewport only: a
// measurement of the switch's floor, the picture wrong). Anything else is
// unknown and read as view, said in the log.
enum class StereoReapply : UInt8 {
	kSlot14,
	kView,
	kNone,
};

inline StereoReapply StereoReapplyFromIni(const char* value, bool* known) {
	auto equals = [value](const char* word) {
		const char* a = value;
		const char* b = word;
		while (*a != '\0' && *b != '\0' && *a == *b) {
			++a;
			++b;
		}
		return *a == '\0' && *b == '\0';
	};
	if (equals("slot14")) {
		*known = true;
		return StereoReapply::kSlot14;
	}
	if (equals("none")) {
		*known = true;
		return StereoReapply::kNone;
	}
	*known = equals("view");
	return StereoReapply::kView;
}

constexpr const char* StereoReapplyName(StereoReapply r) {
	return r == StereoReapply::kSlot14 ? "slot14" : r == StereoReapply::kNone ? "none" : "view";
}

struct RouteSettings {
	bool rangeWalk = false;   // ShadowSceneLight::CollectGeometryInRange 0x007D59E0 runs BatchedWalk; off in play
	bool stereoDraw = false;  // the two draw functions run OBNG's rebuilds (RouteStereoDraw.cpp); off in play
	UInt8 stereoEyes = 1;     // the eyes the stereo draw may serve (1 or 2); two need eye data (StereoEyesApi.h)
	float stereoTestEyeShift = 0.0f;  // the plugin's own eye data: the viewport split in halves, eye 1 this many
	                                  // game units along the camera's right axis (StereoEyeSwitchLogic.h); 0 = off
	StereoReapply stereoReapply = StereoReapply::kView;  // the second eye's constants re-apply
	bool stereoRestoreDirect = true;  // [Route] StereoRestoreDirect: the direct entries re-apply the first eye's view constants after the second eye's draw; 0 for the comparison
	bool stereoParticles = true;  // [Route] StereoParticles: the particle renderer's device call (0x007667F6) runs through the eye plan; 0 leaves the game's call, particles in the first eye only
	bool stereoCarryConstants = true;  // [Route] StereoCarryConstants: the second eye also gets the view-dependent registers an earlier setup left on the device (the terrain's later texture passes draw with the first pass's ModelViewProj), and the switch back restores every changed register by value (StereoEyeSwitchLogic.h RegisterPairs); 0 for the comparison
	bool stereoTraceConstants = false;  // [Route] StereoTraceConstants: in every sixteenth single-pass frame the device's vertex registers are read back around each routed draw's eye switch, and a line per shader class and pass says which registers the second eye's draw has of its own (EyeConstantsTraceLogic.h); off in play
	bool stereoFollowers = true;  // [Route] StereoFollowers: geometry below the camera-following roots (the sky) gets the second eye's viewport without the view shift; 0 for the comparison
	bool occlusionCull = false;   // [Route] OcclusionCull: geometries hidden in both eyes by a query of the last frames are not drawn (OcclusionCull.cpp, OcclusionCullLogic.h); needs stereoDraw; off in play until measured
	UInt32 occlusionCullMaxAge = 2;     // [Route] OcclusionCullMaxAge: frames a hidden verdict is acted on (1..8)
	UInt32 occlusionCullMargin = 2;     // [Route] OcclusionCullMargin: pixels the test rectangles grow on every side (0..64)
	float occlusionCullMaxMove = 100.0f;  // [Route] OcclusionCullMaxMove: the camera moved further since the verdict: drawn
	float occlusionCullMaxTurn = 10.0f;   // [Route] OcclusionCullMaxTurn: the camera turned further (degrees): drawn
	bool occlusionCullNodes = false;      // [Route] OcclusionCullNodes: a scene node whose draws were all hidden is tested by its own bound and, hidden, not visited by the cull walk (two more detours); off until measured
	UInt32 occlusionCullNodeLog = 0;      // [Route] OcclusionCullNodeLog: the first this many nodes left out are logged by name, parents and children; a measurement
	bool occlusionCullEyeTarget = true;    // [Route] OcclusionCullEyeTarget: with eye data a pass is an eye pass only when its render target holds both eyes' ports (the water's reflection is none); 0 is the earlier behaviour, for the comparison
	bool occlusionCullDriverLog = false;   // [Route] OcclusionCullDriverLog: the driver's calls of a summary window apart by caller, culling process and render target (time, visits, nodes left out); a measurement
	bool occlusionCullStats = false;      // [Route] OcclusionCullStats: every eighth eye pass's draws counted by shared mesh and by hidden parent node (DrawGroupLogic.h); a measurement
	bool occlusionCullFlat = false;       // [Route] OcclusionCullFlat: without the route's eye data the culler runs with the one camera of the flat game (the render target as the port); not for OBVR's dual pass
	UInt32 occlusionCullTrace = 0;        // [Route] OcclusionCullTrace: eye passes to log in detail (camera, surfaces, control rectangles, first tests); 0 off
	float occlusionCullTraceDepth = -1.0f;  // [Route] OcclusionCullTraceDepth: >= 0 puts a traced test's second query at this depth instead of the bound's far one
	bool volatilePack = false;  // [Route] VolatilePack: a volatile geometry (a named NPC's head) with unchanged positions and normals is not packed again (FastVolatilePack.cpp); off until measured
	bool gridUpdate = false;   // [Route] GridUpdate: the cells' selected update (0x007073A0 called by TESObjectCELL::UpdateSelectedNodes 0x004D4970) runs the flat selected run (FastSceneUpdate.cpp); off until measured
	bool constantArrange = false;  // [Route] ConstantArrange: the constant map's entry arranging 0x009A97B0 left out when nothing is to swap (FastConstantArrange.cpp); off until its gate is clean
	bool actorBatch = false;   // [Route] ActorBatch: inside the frame's loop over the high actors 0x00677EC0 ApplyAnimData's update is noted and run after the loop (ActorBatch.cpp); needs ActorUpdate; off until measured
	bool actorUnusedBones = false;  // [Route] ActorUnusedBones: in the actors' route the bones nothing hangs on are left out (SceneUpdateLogic.h UnusedBones; the names from OBNG-unused-bones.txt); needs actorUpdate
	bool unusedBonesProbe = false;  // [Probe] UnusedBones: the same bones counted, with their controllers' classes, and nothing left out; a measurement
	UInt32 actorFarEvery = 1;       // [Route] ActorFarEvery: an actor farther from the player than actorFarDistance gets its skeleton update on every n-th call only (ActorFarRateLogic.h); 1 = off; needs actorUpdate
	float actorFarDistance = 6000.0f;  // [Route] ActorFarDistance, game units
	bool actorFarAlternate = false;  // [Route] ActorFarAlternate: the lower rate only in every second summary window, for measuring it inside one run
	bool collisionWrites = false;  // [Probe] CollisionWrites: in the actors' route every collision object's update is checked for writing its own node (ProbeLogic.h CollisionWrites); a measurement
	bool actorBatchTicks = false;  // [Route] ActorBatchTicks: the batched loops' clock readings logged, for cutting a sampler capture by them
	bool actorUpdate = false;  // [Route] ActorUpdate: the call from ApplyAnimData 0x00471F20 runs the flat subtree update - the same transforms and bounds bit for bit without the recursion's virtual calls (FastSceneUpdate.cpp); off until measured
	bool sqrt = false;  // [Route] Sqrt: _CIsqrt 0x00982C30 runs the generated dispatch - FSQRT without the CRT's exception path for the common case (FastSqrt.cpp); off until measured

	UInt32 Asked() const {
		return (rangeWalk ? kRouteRangeWalkDetours : 0u) + (stereoDraw ? kRouteStereoDrawDetours : 0u) +
		       (stereoDraw && occlusionCull ? kRouteOcclusionCullDetours : 0u) + (sqrt ? kRouteSqrtDetours : 0u) +
		       (actorUpdate ? kRouteActorUpdateDetours : 0u) + (gridUpdate ? 1u : 0u) + (volatilePack ? 2u : 0u) + (actorBatch ? kRouteActorBatchDetours : 0u);
	}
};

// Who takes the entry 0x007D59E0. The shadow and the route patch the same
// entry, so at most one of them can; when both are asked the shadow wins: a
// route taken without a clean shadow run makes nothing safer, a shadow run
// with the route asked still produces the comparison the route rests on.
enum class RangeWalkMode : UInt8 {
	Off,     // the game's own entry
	Shadow,  // ShadowRangeWalk.cpp: the original runs, BatchedWalk compared
	Route,   // RouteRangeWalk.cpp: BatchedWalk runs instead of the original
};

constexpr RangeWalkMode SelectRangeWalkMode(bool shadowAsked, bool routeAsked) {
	if (shadowAsked) {
		return RangeWalkMode::Shadow;
	}
	return routeAsked ? RangeWalkMode::Route : RangeWalkMode::Off;
}

// The route was asked but the shadow took the entry: Load logs it as skipped.
constexpr bool RouteSkippedForShadow(bool shadowAsked, bool routeAsked) {
	return routeAsked && SelectRangeWalkMode(shadowAsked, routeAsked) != RangeWalkMode::Route;
}

}  // namespace obng::plugin
