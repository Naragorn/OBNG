// Tests of the shadow light's range walk (the replacement of 0x007D59E0,
// proposed ShadowSceneLight::CollectGeometryInRange, name unverified):
// ReferenceWalk (the original's recursion as read) and BatchedWalk (the
// replacement) over the same hand-built and generated scenes.
//
// The mapped control flow (re/functions/0x007D59E0.md):
//   B1   0x007D59EB  [0x00B42F48] == 0                 -> AL 0, nothing read or called
//   B2   0x007D5A12  IsInRange false                    -> mask restored, subtree skipped
//   B3   0x007D5A26  vt[+0x08]() null (not a node)      -> geometry path B14
//   B4   0x007D5A45  GetRTTI != 0x00B3FD70 (exact)      -> loop over all children
//   B5   0x007D5A52  switch: active child null          -> restore
//   B6   0x007D5A61  switch: 0x0049FFC0 false           -> restore
//   B7s  0x007D5A6B  switch: active flags bit 0         -> restore, else recurse
//   B8   0x007D5A95  child count 0                      -> restore
//   B10  0x007D5AA6  child null                         -> next
//   B11  0x007D5AB2  child radius == 0.0 (NaN is not)   -> next
//   B12  0x007D5AB8  child flags bit 0                  -> next, else recurse
//   B13l 0x007D5ACE  count re-read > i + 1              -> next child
//   B14  0x007D5AEE  vt[+0x10]() null                   -> restore, else AddGeometry(second vt[+0x10])
// IsInRange's own flows (B1..B8 of re/functions/0x007D2EA0.md) are reached
// through the verified rebuild ShadowSceneLight_IsInRange.h.
//
// What is checked, per section:
//   1. the pure decisions both walks share, against the mapping (read);
//   2. every flow of the walk on hand-built scenes, both walks against the
//      expectation written from the mapping (read) and against each other;
//   3. the differential: generated scenes and lights, ReferenceWalk against
//      BatchedWalk, every event compared;
//   4. the plane-test count: WhichSide port calls of the reference against
//      the batched walk's per-plane classifications.
//
// The reference reaches IsInRange through the verified engine rebuild
// (engine::IsInRange over the views of ShadowSceneLight_IsInRange.h, with
// the WhichSide rebuild as its plane port), so the oracle is the rebuild that
// matched the original's bytes in the emulator (ledger: verified (emulator)),
// not a second copy of its logic. The square root is the emulator's FSQRT
// stub as in ShadowSceneLight_IsInRangeTest.cpp (whether the CRT's 0x00982C30
// rounds like it is unverified there; both walks get the same port here).
//
// What the tests cannot see: the original's machine code. The scene ports
// stand for the engine's virtual calls and helpers; 0x007C59E0 and
// 0x0049FFC0 are modelled after their rea-all pseudocode (unverified), and
// the walk only obeys what the ports answer.

#include "Check.h"
#include "engine/render/ShadowLightRangeWalk.h"
#include "engine/render/ShadowLightRangeWalkLogic.h"
#include "engine/render/ShadowSceneLight_IsInRange.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace walk = obng::engine::render::shadow_light_range_walk;
namespace walk_engine = obng::engine::render::shadow_light_range_walk::engine;
namespace in_range = obng::engine::render::shadow_scene_light_is_in_range;
namespace in_range_engine = obng::engine::render::shadow_scene_light_is_in_range::engine;
namespace ws = obng::engine::render::which_side;
using walk::ObjectHandle;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

constexpr std::uint32_t kRttiNiNode = 0x00B3FAB0;       // xobse.csv:1715
constexpr std::uint32_t kRttiNiLODNode = 0x00B3FD68;    // xobse.csv:1739
constexpr std::uint32_t kRttiNiPointLight = 0x00B3FD80;  // xobse.csv:1742

std::uint32_t Bits(float value) {
	return std::bit_cast<std::uint32_t>(value);
}

float FromBits(std::uint32_t bits) {
	return std::bit_cast<float>(bits);
}

// The square root as the emulator cases give it (ShadowSceneLight_IsInRangeTest.cpp):
// FSQRT at 24 bits, round to nearest - the CRT's 0x00982C30 under every
// caller mode (read).
double StubSquareRoot(float s) {
	return ws::RoundToPc24(std::sqrt(static_cast<double>(s)), ws::X87Rounding::Nearest);
}

// ---- The scene behind the ports ------------------------------------------------------

enum class Kind : std::uint8_t {
	Node,      // vt[+0x08] non-null, RTTI NiNode's
	LodNode,   // vt[+0x08] non-null, RTTI NiLODNode's (not exactly NiSwitchNode's)
	Switch,    // vt[+0x08] non-null, RTTI exactly 0x00B3FD70
	Geometry,  // vt[+0x08] null, vt[+0x10] non-null
	Other,     // neither
};

struct TestObject {
	Kind kind = Kind::Other;
	ws::SphereValues bound{0.0f, 0.0f, 0.0f, 1.0f};
	std::uint8_t flags = 0;
	std::vector<ObjectHandle> children;
	std::int32_t activeIndex = -1;    // the switch node's +0xE0 (0x007C59E0 pseudocode)
	ObjectHandle secondGeometry = 0;  // what the second vt[+0x10] call returns; 0 = the object itself
	int nonZeroRadiusOverride = -1;   // 0x0049FFC0's answer; -1 = its pseudocode (+0x2C != 0.0)
};

// One observable event: a visit ('V') with its range decision and the mask
// entering and leaving IsInRange, or an AddGeometry ('A') with the mask the
// light holds at that moment.
struct Event {
	char kind;
	ObjectHandle object;
	bool inRange;
	std::uint32_t maskIn;
	std::uint32_t maskOut;
	bool operator==(const Event&) const = default;
};

Event V(ObjectHandle object, bool inRange, std::uint32_t maskIn, std::uint32_t maskOut) {
	return Event{'V', object, inRange, maskIn, maskOut};
}

Event A(ObjectHandle geometry, std::uint32_t mask) {
	return Event{'A', geometry, true, mask, mask};
}

std::string Describe(const Event& e) {
	char text[96];
	std::snprintf(text, sizeof text, "%c object %u inRange %d maskIn 0x%08X maskOut 0x%08X", e.kind,
		static_cast<unsigned>(e.object), e.inRange ? 1 : 0, static_cast<unsigned>(e.maskIn),
		static_cast<unsigned>(e.maskOut));
	return text;
}

struct TestScene {
	std::vector<TestObject> objects{TestObject{}};  // handle 0 is null
	std::uint32_t gateValue = 1;
	std::function<void(TestScene&, ObjectHandle)> onAdd;  // a side effect of AddGeometry, if any
	std::vector<Event>* log = nullptr;
	std::vector<int> geometryCalls;
	int addCount = 0;

	ObjectHandle Add(Kind kind, ws::SphereValues bound, std::uint8_t flags = 0) {
		TestObject object;
		object.kind = kind;
		object.bound = bound;
		object.flags = flags;
		objects.push_back(object);
		return static_cast<ObjectHandle>(objects.size() - 1);
	}

	void Link(ObjectHandle parent, ObjectHandle child) {
		objects[parent].children.push_back(child);
	}

	TestObject& At(ObjectHandle handle) {
		return objects[handle];
	}

	// ---- The port surface (ShadowLightRangeWalk.h, RangeWalkScene) ----
	std::uint32_t gate() const {
		return gateValue;
	}

	ObjectHandle asNode(ObjectHandle object) const {
		const Kind kind = objects[object].kind;
		return kind == Kind::Node || kind == Kind::LodNode || kind == Kind::Switch ? object : walk::kNullObject;
	}

	std::uint32_t nodeRtti(ObjectHandle node) const {
		switch (objects[node].kind) {
		case Kind::Switch:
			return walk::kNiRttiNiSwitchNode;
		case Kind::LodNode:
			return kRttiNiLODNode;
		default:
			return kRttiNiNode;
		}
	}

	ObjectHandle activeChild(ObjectHandle node) const {
		const TestObject& object = objects[node];
		const std::int32_t index = object.activeIndex;
		if (-1 < index && index < static_cast<std::int32_t>(object.children.size())) {
			return object.children[static_cast<std::size_t>(index)];
		}
		return walk::kNullObject;
	}

	bool hasNonZeroBoundRadius(ObjectHandle object) const {
		const TestObject& o = objects[object];
		return o.nonZeroRadiusOverride >= 0 ? o.nonZeroRadiusOverride != 0 : o.bound.radius != 0.0f;
	}

	std::uint16_t childCount(ObjectHandle node) const {
		return static_cast<std::uint16_t>(objects[node].children.size());
	}

	ObjectHandle child(ObjectHandle node, std::uint32_t index) const {
		return objects[node].children[index];
	}

	ws::SphereValues bound(ObjectHandle object) const {
		return objects[object].bound;
	}

	std::uint8_t flags(ObjectHandle object) const {
		return objects[object].flags;
	}

	ObjectHandle asGeometry(ObjectHandle object) {
		const TestObject& o = objects[object];
		if (o.kind != Kind::Geometry) {
			return walk::kNullObject;
		}
		if (geometryCalls.size() < objects.size()) {
			geometryCalls.resize(objects.size(), 0);
		}
		const int call = ++geometryCalls[object];
		return call % 2 == 0 && o.secondGeometry != 0 ? o.secondGeometry : object;
	}

	void addGeometry(ObjectHandle geometry, std::uint32_t liveMask) {
		++addCount;
		if (log != nullptr) {
			log->push_back(A(geometry, liveMask));
		}
		if (onAdd) {
			onAdd(*this, geometry);
		}
	}
};

static_assert(walk_engine::RangeWalkScene<TestScene>, "the test scene has the engine port surface");

struct LogObserver {
	std::vector<Event>* log;
	void onVisit(ObjectHandle object, bool inRange, std::uint32_t maskIn, std::uint32_t maskOut) const {
		log->push_back(V(object, inRange, maskIn, maskOut));
	}
};

// ---- The reference's light: the verified IsInRange rebuild over engine views ----------

std::uint32_t g_sourceRtti = 0;

const void* __thiscall FakeGetRtti(const in_range_engine::NiLightView*) {
	return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_sourceRtti));
}

const in_range_engine::NiObjectVtableView kFakeVtable{nullptr, &FakeGetRtti};

struct EngineLight {
	in_range_engine::ShadowSceneLightView view{};
	in_range_engine::NiLightView source{};
	bool hasSource = true;
	std::vector<in_range_engine::NiAVObjectView> objects;
	std::uint64_t planeTests = 0;  // WhichSide port calls
	int maskWrites = 0;

	ws::X87Rounding rounding;  // the control word IsInRange runs under, as the walk's

	EngineLight(const walk::LightValues& light, const TestScene& scene) : rounding(light.rounding) {
		hasSource = light.hasSource;
		g_sourceRtti = light.sourceRtti;
		view.planeSource = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(light.planeSource));
		for (unsigned i = 0; i < in_range::kPlaneCount; ++i) {
			const ws::PlaneValues& p = light.planes[i];
			view.planes[i] = in_range_engine::NiPlane{{p.normalX, p.normalY, p.normalZ}, p.constant};
		}
		view.activePlanes = light.activePlanes;
		source.vtable = &kFakeVtable;
		source.worldTranslate = in_range_engine::NiPoint3{light.sourcePosition.x, light.sourcePosition.y, light.sourcePosition.z};
		source.range = light.sourceRange;
		objects.resize(scene.objects.size());
		for (std::size_t h = 0; h < scene.objects.size(); ++h) {
			const ws::SphereValues& b = scene.objects[h].bound;
			objects[h].worldBound = in_range_engine::NiBound{{b.centerX, b.centerY, b.centerZ}, b.radius};
		}
	}

	// ---- The port surface (RangeWalkReferenceLight) ----
	std::uint32_t readMask() const {
		return view.activePlanes;
	}

	void writeMask(std::uint32_t mask) {
		++maskWrites;
		view.activePlanes = mask;
	}

	bool isInRange(ObjectHandle object) {
		return in_range_engine::IsInRange(
			view, &objects[object], hasSource ? &source : nullptr, rounding,
			[this](const in_range_engine::NiBound& bound, const in_range_engine::NiPlane& plane) {
				++planeTests;
				return in_range_engine::WhichSidePort{rounding}(bound, plane);
			},
			StubSquareRoot);
	}
};

static_assert(walk_engine::RangeWalkReferenceLight<EngineLight>, "the engine light has the reference port surface");

// ---- Running both walks ---------------------------------------------------------------

struct RunResult {
	bool result = false;
	std::vector<Event> log;
	std::uint32_t finalMask = 0;
	std::uint64_t planeTests = 0;
	std::vector<int> geometryCalls;
};

RunResult RunReference(const TestScene& spec, const walk::LightValues& light, ObjectHandle root) {
	RunResult run;
	TestScene scene = spec;
	scene.log = &run.log;
	EngineLight engineLight(light, scene);
	LogObserver observer{&run.log};
	run.result = walk::ReferenceWalk(engineLight, scene, root, observer);
	run.finalMask = engineLight.readMask();
	run.planeTests = engineLight.planeTests;
	run.geometryCalls = scene.geometryCalls;
	return run;
}

RunResult RunBatched(const TestScene& spec, const walk::LightValues& light, ObjectHandle root) {
	RunResult run;
	TestScene scene = spec;
	scene.log = &run.log;
	walk::BatchedWalkScratch scratch;
	LogObserver observer{&run.log};
	run.result = walk::BatchedWalk(light, scene, root, StubSquareRoot, scratch, observer);
	// The batched walk never writes the light's +0x1B0 (it keeps the mask on
	// its stack): the light's mask afterwards is the value it entered with.
	run.finalMask = light.activePlanes;
	run.planeTests = scratch.planeClassifications;
	run.geometryCalls = scene.geometryCalls;
	return run;
}

std::string FirstDifference(const std::vector<Event>& reference, const std::vector<Event>& batched) {
	const std::size_t common = reference.size() < batched.size() ? reference.size() : batched.size();
	for (std::size_t i = 0; i < common; ++i) {
		if (!(reference[i] == batched[i])) {
			return "event " + std::to_string(i) + ": reference " + Describe(reference[i]) + " / batched " + Describe(batched[i]);
		}
	}
	if (reference.size() != batched.size()) {
		return "lengths differ: reference " + std::to_string(reference.size()) + " events, batched " +
			std::to_string(batched.size());
	}
	return "none";
}

// Runs both, checks each against the expectation from the mapping and the two
// against each other (log, result, final mask, plane count).
void CheckBoth(const char* what, const TestScene& scene, const walk::LightValues& light, ObjectHandle root,
	const std::vector<Event>& expected, bool expectedResult = true) {
	const RunResult reference = RunReference(scene, light, root);
	const RunResult batched = RunBatched(scene, light, root);
	const std::string r = std::string("reference: ") + what;
	const std::string b = std::string("batched:   ") + what;
	Check(reference.log == expected, r.c_str());
	Check(batched.log == expected, b.c_str());
	if (!(reference.log == expected) || !(batched.log == expected)) {
		std::printf("    expected %zu events; reference: %s; batched: %s\n", expected.size(),
			FirstDifference(expected, reference.log).c_str(), FirstDifference(expected, batched.log).c_str());
	}
	Check(reference.result == expectedResult && batched.result == expectedResult, "  AL as the mapping says, in both");
	Check(reference.finalMask == light.activePlanes && batched.finalMask == light.activePlanes,
		"  +0x1B0 afterwards equals its entry value (C3)");
	Check(reference.planeTests == batched.planeTests, "  the same number of per-plane classifications");
}

// ---- Light set-ups --------------------------------------------------------------------

walk::LightValues PlaneLight(std::uint32_t mask) {
	walk::LightValues light{};
	light.hasSource = true;
	light.sourceRtti = kRttiNiPointLight;
	light.planeSource = 1;
	// d = x + 100, d = 100 - x, and four planes d = z + 1000 that every small
	// object near z = 0 is wholly on the positive side of.
	light.planes[0] = ws::PlaneValues{1.0f, 0.0f, 0.0f, -100.0f};
	light.planes[1] = ws::PlaneValues{-1.0f, 0.0f, 0.0f, -100.0f};
	for (unsigned i = 2; i < in_range::kPlaneCount; ++i) {
		light.planes[i] = ws::PlaneValues{0.0f, 0.0f, 1.0f, -1000.0f};
	}
	light.activePlanes = mask;
	light.sourcePosition = in_range::PointValues{0.0f, 0.0f, 0.0f};
	light.sourceRange = 100.0f;
	light.rounding = ws::X87Rounding::Nearest;
	return light;
}

walk::LightValues DistanceLight(std::uint32_t mask) {
	walk::LightValues light = PlaneLight(mask);
	light.planeSource = 0;
	return light;
}

ws::SphereValues S(float x, float y, float z, float r) {
	return ws::SphereValues{x, y, z, r};
}

// ---- 1. The decisions both walks share --------------------------------------------------

void Decisions() {
	Section("B1 IsWalkEnabled: CMP dword [0x00B42F48],0 / JNZ (read 0x007D59E1..0x007D59EB)");
	Check(!walk::IsWalkEnabled(0), "0: off");
	Check(walk::IsWalkEnabled(1) && walk::IsWalkEnabled(3) && walk::IsWalkEnabled(0x80000000u), "1, 3, 0x80000000: on");

	Section("B4 IsExactSwitchNode: CMP EAX,0xB3FD70, an exact address compare (read 0x007D5A37, C8)");
	Check(walk::kNiRttiNiSwitchNode == 0x00B3FD70u, "NiRTTI_NiSwitchNode 0x00B3FD70 (xobse.csv:1740)");
	Check(walk::IsExactSwitchNode(0x00B3FD70u), "0x00B3FD70: switch");
	Check(!walk::IsExactSwitchNode(kRttiNiLODNode), "0x00B3FD68 (NiLODNode): not a switch here");
	Check(!walk::IsExactSwitchNode(kRttiNiNode), "0x00B3FAB0 (NiNode): not a switch");

	Section("B11 HasNonZeroRadius: FLDZ / FCOMP [+0x2C] / TEST AH,0x44 / JNP (read 0x007D5AA8..0x007D5AB2)");
	Check(!walk::HasNonZeroRadius(0.0f) && !walk::HasNonZeroRadius(-0.0f), "+0.0 and -0.0: zero (skipped)");
	Check(walk::HasNonZeroRadius(kNaN), "NaN: non-zero (C2 set, PF set: not skipped)");
	Check(walk::HasNonZeroRadius(FromBits(0x00000001u)), "the smallest denormal: non-zero");
	Check(walk::HasNonZeroRadius(-1.0f) && walk::HasNonZeroRadius(kInf), "-1 and +inf: non-zero");

	Section("B7s/B12 IsAppCulled: TEST byte [+0x18],1 (read 0x007D5A67, 0x007D5AB4)");
	Check(walk::IsAppCulled(0x01) && walk::IsAppCulled(0xFF), "bit 0 set: culled");
	Check(!walk::IsAppCulled(0x00) && !walk::IsAppCulled(0xFE), "bit 0 clear (0x00, 0xFE): not culled");

	Section("B8/B13l LoopContinues: count > index, unsigned (read 0x007D5A95, 0x007D5ACE, C12)");
	Check(!walk::LoopContinues(0, 0), "count 0: no child (B8)");
	Check(walk::LoopContinues(1, 0) && !walk::LoopContinues(1, 1), "count 1: index 0 only");
	Check(walk::LoopContinues(0xFFFF, 0xFFFE) && !walk::LoopContinues(0xFFFF, 0xFFFF), "the u16 bound");

	Section("IsInRange's source cases, chosen once per walk in IsInRange's order (B1, B2, B3 of 0x007D2EA0)");
	Check(walk::SelectSourceMode(false, in_range::kNiRttiNiDirectionalLight, 1) == walk::SourceMode::NoSource,
		"no source beats a directional RTTI (B1 before B2)");
	Check(walk::SelectSourceMode(true, in_range::kNiRttiNiDirectionalLight, 1) == walk::SourceMode::Directional,
		"directional beats the plane source (B2 before B3)");
	Check(walk::SelectSourceMode(true, kRttiNiPointLight, 1) == walk::SourceMode::Planes, "+0x14C non-zero: planes");
	Check(walk::SelectSourceMode(true, kRttiNiPointLight, 0) == walk::SourceMode::Distance, "+0x14C zero: distance");
}

void PlaneBatchFlows() {
	const walk::LightValues light = PlaneLight(0x3F);

	Section("ClassifyPlanes: only the active lanes are computed and classified (B4 of 0x007D2EA0)");
	{
		const walk::PlaneBatch none = walk::ClassifyPlanes(light.planes, 0, S(0, 0, 0, 10), light.rounding);
		Check(none.lanes == 0 && none.negative == 0 && none.positive == 0, "mask 0: no lane, nothing classified");
		Check(walk::PlaneClassifications(none) == 0, "0 classifications");
		const walk::PlaneBatch gaps = walk::ClassifyPlanes(light.planes, 0xFFFFFF2Au, S(500, 0, 0, 10), light.rounding);
		Check(gaps.lanes == 0x2Au && walk::PlaneClassifications(gaps) == 3, "mask 0xFFFFFF2A: lanes 1, 3, 5 only");
		Check(gaps.negative == 0x02u && gaps.positive == 0x28u, "plane 1 wholly negative, 3 and 5 wholly positive");
		const walk::RangeDecision d = walk::ApplyPlaneBatch(gaps, 0xFFFFFF2Au);
		Check(!d.inRange && d.mask == 0xFFFFFF02u, "out of range; bits 3 and 5 retired, bits 6..31 kept");
	}

	Section("ClassifyPlanes: the per-lane distance is WhichSide's SignedDistance, the class its Classify");
	{
		const ws::SphereValues sphere = S(-30, 0, 0, 80);
		const walk::PlaneBatch batch = walk::ClassifyPlanes(light.planes, 0x3F, sphere, light.rounding);
		bool same = true;
		for (unsigned i = 0; i < in_range::kPlaneCount; ++i) {
			const float expected = ws::SignedDistance(light.planes[i], sphere, light.rounding).distanceStored;
			same = same && Bits(batch.distance[i]) == Bits(expected);
			const auto side = ws::WhichSide(sphere, light.planes[i], light.rounding);
			const std::uint32_t bit = in_range::PlaneBit(i);
			same = same && ((batch.negative & bit) != 0) == (side == ws::PlaneSide::Negative);
			same = same && ((batch.positive & bit) != 0) == (side == ws::PlaneSide::Positive);
		}
		Check(same, "six lanes, bit for bit equal to six WhichSide calls");
		Check(batch.negative == 0 && batch.positive == 0x3Eu, "plane 0 straddled, 1..5 retired");
	}

	Section("ClassifyPlanes: a negative radius makes both tests hold, Negative wins (C6 of 0x0047DA70)");
	{
		const walk::PlaneBatch batch = walk::ClassifyPlanes(light.planes, 0x01, S(-100, 0, 0, -1), light.rounding);
		Check(batch.negative == 0x01u && batch.positive == 0, "d 0, r -1: negative only");
	}

	Section("ClassifyPlanes: NaN in the bound straddles every lane (no comparison holds)");
	{
		const walk::PlaneBatch batch = walk::ClassifyPlanes(light.planes, 0x3F, S(kNaN, 0, 0, 10), light.rounding);
		Check(batch.negative == 0 && batch.positive == 0, "centre x NaN: every lane kept (0 * NaN is NaN too)");
		const walk::PlaneBatch radius = walk::ClassifyPlanes(light.planes, 0x3F, S(0, 0, 0, kNaN), light.rounding);
		Check(radius.negative == 0 && radius.positive == 0, "NaN radius: every lane kept");
	}

	Section("TestObject per source mode (IsInRange's B1, B2, B3/B8 over plain values)");
	{
		std::uint64_t count = 0;
		const auto bound = [] { return S(500, 0, 0, 10); };
		const walk::RangeDecision none =
			walk::TestObject(light, walk::SourceMode::NoSource, 0x3F, bound, StubSquareRoot, count);
		Check(!none.inRange && none.mask == 0x3Fu && count == 0, "no source: out, mask kept, no plane test");
		const walk::RangeDecision directional =
			walk::TestObject(light, walk::SourceMode::Directional, 0x3F, bound, StubSquareRoot, count);
		Check(directional.inRange && directional.mask == 0x3Fu && count == 0, "directional: in, mask kept");
		const walk::RangeDecision planes = walk::TestObject(light, walk::SourceMode::Planes, 0x3F, bound, StubSquareRoot, count);
		Check(!planes.inRange && planes.mask == 0x02u && count == 6, "planes: out, mask 0x02, six classifications");
		const walk::RangeDecision far = walk::TestObject(light, walk::SourceMode::Distance, 0x3F, bound, StubSquareRoot, count);
		Check(!far.inRange && far.mask == 0x3Fu && count == 6, "distance: gap 490 >= range 100, out, no plane test");
		const walk::RangeDecision near =
			walk::TestObject(light, walk::SourceMode::Distance, 0x3F, [] { return S(50, 0, 0, 10); }, StubSquareRoot, count);
		Check(near.inRange && near.mask == 0x3Fu, "distance: gap 40 < range 100, in");
	}

	Section("The walk's rounding mode reaches every range test (LightValues::rounding; emulator cases R1 of 0x0047DA70, G1 of 0x007D2EA0)");
	{
		// R1 of 0x0047DA70-rounding.json: WhichSide 1 under up, 0 otherwise.
		walk::LightValues tie = PlaneLight(0x01);
		tie.planes[0] = ws::PlaneValues{FromBits(0x33800000u), 1.0f, FromBits(0x30800000u), 1.0f};
		const ws::SphereValues sphere{1.0f, 1.0f, FromBits(0x30800000u), FromBits(0x33800000u)};
		for (const ws::X87Rounding mode : {ws::X87Rounding::Nearest, ws::X87Rounding::Down, ws::X87Rounding::Up, ws::X87Rounding::Zero}) {
			tie.rounding = mode;
			const walk::PlaneBatch batch = walk::ClassifyPlanes(tie.planes, 0x01, sphere, tie.rounding);
			Check(batch.positive == (mode == ws::X87Rounding::Up ? 0x01u : 0u) && batch.negative == 0,
				mode == ws::X87Rounding::Up ? "plane path, up: plane 0 retired" : "plane path, nearest/down/truncate: plane 0 kept");
			std::uint64_t count = 0;
			const walk::RangeDecision d = walk::TestObject(tie, walk::SourceMode::Planes, 0x01, [&] { return sphere; }, StubSquareRoot, count);
			Check(d.inRange && d.mask == (mode == ws::X87Rounding::Up ? 0u : 0x01u), "TestObject takes the light's mode on the plane path");
		}
		// G1 of 0x007D2EA0-rounding.json: the in-game mismatch's root.
		walk::LightValues garbage = DistanceLight(0x3F);
		garbage.sourcePosition = in_range::PointValues{FromBits(0xC3F5B6A8u), FromBits(0x4486928Fu), FromBits(0x4401EB54u)};
		garbage.sourceRange = 256.0f;
		const ws::SphereValues root{FromBits(0xFD935C0Fu), FromBits(0xFD935C13u), FromBits(0xFEBFCBA9u), FromBits(0x5FDFFFF8u)};
		std::uint64_t count = 0;
		garbage.rounding = ws::X87Rounding::Nearest;
		Check(!walk::TestObject(garbage, walk::SourceMode::Distance, 0x3F, [&] { return root; }, StubSquareRoot, count).inRange,
			"distance path, nearest: the garbage root is out (s +inf)");
		garbage.rounding = ws::X87Rounding::Zero;
		Check(walk::TestObject(garbage, walk::SourceMode::Distance, 0x3F, [&] { return root; }, StubSquareRoot, count).inRange,
			"distance path, truncate: in (s FLT_MAX) - the original's answer in the game");
		TestScene scene;
		const ObjectHandle sceneRoot = scene.Add(Kind::Node, root);
		const ObjectHandle child = scene.Add(Kind::Geometry, S(-491.0f, 1076.0f, 519.0f, 1.0f));
		scene.Link(sceneRoot, child);
		CheckBoth("both walks, truncate: the root in, the child in and added", scene, garbage, sceneRoot,
			{V(sceneRoot, true, 0x3F, 0x3F), V(child, true, 0x3F, 0x3F), A(child, 0x3F)});
		garbage.rounding = ws::X87Rounding::Nearest;
		CheckBoth("both walks, nearest: the root out, nothing added", scene, garbage, sceneRoot, {V(sceneRoot, false, 0x3F, 0x3F)});
	}
}

// ---- 2. Every flow of the walk on hand-built scenes ----------------------------------------

void GateFlows() {
	Section("B1: gate off - AL 0, no visit, no call, the mask not touched");
	TestScene scene;
	const ObjectHandle root = scene.Add(Kind::Node, S(0, 0, 0, 200));
	scene.Link(root, scene.Add(Kind::Geometry, S(0, 0, 0, 10)));
	scene.gateValue = 0;
	CheckBoth("no event", scene, PlaneLight(0x3F), root, {}, false);
	TestScene probe = scene;
	EngineLight light(PlaneLight(0x3F), probe);
	walk::NoWalkObserver none;
	walk::ReferenceWalk(light, probe, root, none);
	Check(light.maskWrites == 0 && light.planeTests == 0, "reference: no mask write, no plane test");

	Section("B1 per call: the gate turned off by an AddGeometry side effect stops every later visit");
	TestScene flip;
	const ObjectHandle top = flip.Add(Kind::Node, S(0, 0, 0, 200));
	const ObjectHandle g1 = flip.Add(Kind::Geometry, S(0, 0, 0, 10));
	const ObjectHandle n2 = flip.Add(Kind::Node, S(0, 0, 0, 10));
	const ObjectHandle g3 = flip.Add(Kind::Geometry, S(0, 0, 0, 10));
	const ObjectHandle g4 = flip.Add(Kind::Geometry, S(0, 0, 0, 10));
	flip.Link(top, g1);
	flip.Link(top, n2);
	flip.Link(n2, g3);
	flip.Link(top, g4);
	flip.onAdd = [g1](TestScene& s, ObjectHandle added) {
		if (added == g1) {
			s.gateValue = 0;
		}
	};
	CheckBoth("root, g1, AddGeometry(g1), then nothing", flip, PlaneLight(0x3F), top,
		{V(top, true, 0x3F, 0x03), V(g1, true, 0x03, 0x00), A(g1, 0x00)});
}

void RangeFlows() {
	Section("B2: an out-of-range root - its subtree skipped, the narrowing undone, AL 1");
	TestScene scene;
	const ObjectHandle root = scene.Add(Kind::Node, S(500, 0, 0, 10));
	scene.Link(root, scene.Add(Kind::Geometry, S(0, 0, 0, 10)));
	// plane 0: d 600 retires, plane 1: d -400 is wholly negative, planes 2..5 retire
	CheckBoth("one visit, out, mask 0x3F -> 0x02 inside IsInRange", scene, PlaneLight(0x3F), root,
		{V(root, false, 0x3F, 0x02)});

	Section("B2 with no source: IsInRange's B1 - the root out, AL 1");
	walk::LightValues noSource = PlaneLight(0x3F);
	noSource.hasSource = false;
	TestScene near;
	const ObjectHandle nearRoot = near.Add(Kind::Node, S(0, 0, 0, 10));
	near.Link(nearRoot, near.Add(Kind::Geometry, S(0, 0, 0, 10)));
	CheckBoth("one visit, out, mask kept", near, noSource, nearRoot, {V(nearRoot, false, 0x3F, 0x3F)});

	Section("Directional source (IsInRange's B2): every object in, whatever its bound, the mask kept");
	walk::LightValues directional = PlaneLight(0x3F);
	directional.sourceRtti = in_range::kNiRttiNiDirectionalLight;
	TestScene wide;
	const ObjectHandle wideRoot = wide.Add(Kind::Node, S(500, 0, 0, 10));
	const ObjectHandle far = wide.Add(Kind::Geometry, S(1e30f, -1e30f, 0, 1));
	const ObjectHandle nan = wide.Add(Kind::Geometry, S(kNaN, kNaN, kNaN, 5));
	wide.Link(wideRoot, far);
	wide.Link(wideRoot, nan);
	CheckBoth("root, far, NaN all in, mask 0x3F throughout", wide, directional, wideRoot,
		{V(wideRoot, true, 0x3F, 0x3F), V(far, true, 0x3F, 0x3F), A(far, 0x3F), V(nan, true, 0x3F, 0x3F), A(nan, 0x3F)});

	Section("Distance path (+0x14C == 0, IsInRange's B8): range > |light - centre| - radius, the mask kept");
	TestScene dist;
	const ObjectHandle distRoot = dist.Add(Kind::Node, S(0, 0, 0, 10));
	const ObjectHandle inside = dist.Add(Kind::Geometry, S(50, 0, 0, 10));    // gap 40
	const ObjectHandle outside = dist.Add(Kind::Geometry, S(500, 0, 0, 10));  // gap 490
	const ObjectHandle edge = dist.Add(Kind::Geometry, S(110, 0, 0, 10));     // gap 100 == range: out
	dist.Link(distRoot, inside);
	dist.Link(distRoot, outside);
	dist.Link(distRoot, edge);
	CheckBoth("root in, inside in and added, outside out, the tie out", dist, DistanceLight(0x3F), distRoot,
		{V(distRoot, true, 0x3F, 0x3F), V(inside, true, 0x3F, 0x3F), A(inside, 0x3F), V(outside, false, 0x3F, 0x3F),
			V(edge, false, 0x3F, 0x3F)});
	Check(RunReference(dist, DistanceLight(0x3F), distRoot).planeTests == 0 &&
			RunBatched(dist, DistanceLight(0x3F), distRoot).planeTests == 0,
		"  no plane test on the distance path");
}

void ChildFlows() {
	Section("B8: an in-range node with no children - one visit");
	{
		TestScene scene;
		const ObjectHandle root = scene.Add(Kind::Node, S(0, 0, 0, 200));
		CheckBoth("root only", scene, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
	}

	Section("B10 B11 B12: null, +0.0, -0.0 and app-culled children skipped; NaN, +inf and flags 0x02 walked");
	{
		TestScene scene;
		const ObjectHandle root = scene.Add(Kind::Node, S(0, 0, 0, 200));
		const ObjectHandle zero = scene.Add(Kind::Geometry, S(0, 0, 0, 0.0f));
		const ObjectHandle minusZero = scene.Add(Kind::Geometry, S(0, 0, 0, -0.0f));
		const ObjectHandle nanRadius = scene.Add(Kind::Geometry, S(0, 0, 0, kNaN));
		const ObjectHandle culled = scene.Add(Kind::Geometry, S(0, 0, 0, 10), 0x01);
		const ObjectHandle otherFlags = scene.Add(Kind::Geometry, S(0, 0, 0, 10), 0x02);
		const ObjectHandle infRadius = scene.Add(Kind::Geometry, S(0, 0, 0, kInf));
		scene.Link(root, walk::kNullObject);
		for (const ObjectHandle h : {zero, minusZero, nanRadius, culled, otherFlags, infRadius}) {
			scene.Link(root, h);
		}
		CheckBoth("root, NaN-radius, flags-0x02, inf-radius", scene, PlaneLight(0x3F), root,
			{V(root, true, 0x3F, 0x03), V(nanRadius, true, 0x03, 0x03), A(nanRadius, 0x03), V(otherFlags, true, 0x03, 0x00),
				A(otherFlags, 0x00), V(infRadius, true, 0x03, 0x03), A(infRadius, 0x03)});
	}

	Section("B3/B14: neither node nor geometry - visited, nothing added");
	{
		TestScene scene;
		const ObjectHandle root = scene.Add(Kind::Other, S(0, 0, 0, 10));
		CheckBoth("one visit, no AddGeometry", scene, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x00)});
	}

	Section("B14: a geometry - AddGeometry gets the second vt[+0x10] result (C10)");
	{
		TestScene scene;
		const ObjectHandle root = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
		scene.At(root).secondGeometry = 0x40000007u;
		CheckBoth("AddGeometry(0x40000007)", scene, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x00), A(0x40000007u, 0x00)});
		const RunResult reference = RunReference(scene, PlaneLight(0x3F), root);
		const RunResult batched = RunBatched(scene, PlaneLight(0x3F), root);
		Check(reference.geometryCalls.size() > root && reference.geometryCalls[root] == 2 && batched.geometryCalls[root] == 2,
			"  vt[+0x10] called twice in both");
	}

	Section("B13l: the child count re-read after every child - growth during the loop is walked");
	{
		TestScene scene;
		const ObjectHandle root = scene.Add(Kind::Node, S(0, 0, 0, 200));
		const ObjectHandle g1 = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
		const ObjectHandle g2 = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
		const ObjectHandle g3 = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
		scene.Link(root, g1);
		scene.Link(root, g2);
		scene.onAdd = [root, g1, g3](TestScene& s, ObjectHandle added) {
			if (added == g1) {
				s.Link(root, g3);
			}
		};
		CheckBoth("g1, g2 and the appended g3", scene, PlaneLight(0x3F), root,
			{V(root, true, 0x3F, 0x03), V(g1, true, 0x03, 0x00), A(g1, 0x00), V(g2, true, 0x03, 0x00), A(g2, 0x00),
				V(g3, true, 0x03, 0x00), A(g3, 0x00)});
	}

	Section("B13l: shrinking during the loop ends it");
	{
		TestScene scene;
		const ObjectHandle root = scene.Add(Kind::Node, S(0, 0, 0, 200));
		const ObjectHandle g1 = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
		scene.Link(root, g1);
		scene.Link(root, scene.Add(Kind::Geometry, S(0, 0, 0, 10)));
		scene.Link(root, scene.Add(Kind::Geometry, S(0, 0, 0, 10)));
		scene.onAdd = [root, g1](TestScene& s, ObjectHandle added) {
			if (added == g1) {
				s.At(root).children.resize(1);
			}
		};
		CheckBoth("only g1", scene, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03), V(g1, true, 0x03, 0x00), A(g1, 0x00)});
	}
}

void SwitchFlows() {
	// A switch at the origin with three geometry children; the variants change
	// the active index and the active child.
	const auto makeSwitch = [](Kind kind, std::int32_t active, ObjectHandle& root, ObjectHandle (&children)[3]) {
		TestScene scene;
		root = scene.Add(kind, S(0, 0, 0, 200));
		for (ObjectHandle& child : children) {
			child = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
			scene.Link(root, child);
		}
		scene.At(root).activeIndex = active;
		return scene;
	};
	ObjectHandle root = 0;
	ObjectHandle c[3]{};

	Section("B4 B7s: an exact NiSwitchNode - only its active child");
	{
		const TestScene scene = makeSwitch(Kind::Switch, 1, root, c);
		CheckBoth("root, child 1", scene, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03), V(c[1], true, 0x03, 0x00), A(c[1], 0x00)});
	}

	Section("B5: no active child (index -1, index == count, a null slot)");
	{
		const TestScene minusOne = makeSwitch(Kind::Switch, -1, root, c);
		CheckBoth("index -1: root only", minusOne, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
		const TestScene pastEnd = makeSwitch(Kind::Switch, 3, root, c);
		CheckBoth("index 3 of 3: root only", pastEnd, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
		TestScene nullSlot = makeSwitch(Kind::Switch, 1, root, c);
		nullSlot.At(root).children[1] = walk::kNullObject;
		CheckBoth("null active slot: root only", nullSlot, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
	}

	Section("B6: 0x0049FFC0 false (radius 0.0) skips; the switch path has no inline +0x2C compare (C7)");
	{
		TestScene zero = makeSwitch(Kind::Switch, 1, root, c);
		zero.At(c[1]).bound.radius = 0.0f;
		CheckBoth("radius 0.0: root only", zero, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
		zero.At(c[1]).nonZeroRadiusOverride = 1;
		CheckBoth("radius 0.0 but 0x0049FFC0 true: the child is walked", zero, PlaneLight(0x3F), root,
			{V(root, true, 0x3F, 0x03), V(c[1], true, 0x03, 0x00), A(c[1], 0x00)});
		TestScene nonZero = makeSwitch(Kind::Switch, 1, root, c);
		nonZero.At(c[1]).nonZeroRadiusOverride = 0;
		CheckBoth("radius 10 but 0x0049FFC0 false: skipped", nonZero, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
	}

	Section("B7s: an app-culled active child is skipped");
	{
		TestScene culled = makeSwitch(Kind::Switch, 1, root, c);
		culled.At(c[1]).flags = 0x01;
		CheckBoth("root only", culled, PlaneLight(0x3F), root, {V(root, true, 0x3F, 0x03)});
	}

	Section("B4: an RTTI other than exactly 0x00B3FD70 (NiLODNode's) walks all children (C8)");
	{
		const TestScene lod = makeSwitch(Kind::LodNode, 1, root, c);
		CheckBoth("root, children 0, 1, 2", lod, PlaneLight(0x3F), root,
			{V(root, true, 0x3F, 0x03), V(c[0], true, 0x03, 0x00), A(c[0], 0x00), V(c[1], true, 0x03, 0x00), A(c[1], 0x00),
				V(c[2], true, 0x03, 0x00), A(c[2], 0x00)});
	}

	Section("B2 on a switch: an out-of-range switch does not ask for its active child");
	{
		TestScene out = makeSwitch(Kind::Switch, 1, root, c);
		out.At(root).bound = S(500, 0, 0, 10);
		CheckBoth("root only, out", out, PlaneLight(0x3F), root, {V(root, false, 0x3F, 0x02)});
	}
}

void MaskFlows() {
	// root (0,0,0) r200: planes 0, 1 straddled, 2..5 retired -> 0x03
	//   A (-30,0,0) r80: plane 0 d 70 straddled, plane 1 d 130 retired -> 0x01
	//     A1, A2 (-30,0,0) r10: plane 0 d 70 retired -> 0x00
	//   B (500,0,0) r10: plane 0 retired, plane 1 d -400 wholly negative -> out, 0x02
	//   C (0,0,0) r10: planes 0, 1 retired -> 0x00
	TestScene scene;
	const ObjectHandle root = scene.Add(Kind::Node, S(0, 0, 0, 200));
	const ObjectHandle a = scene.Add(Kind::Node, S(-30, 0, 0, 80));
	const ObjectHandle a1 = scene.Add(Kind::Geometry, S(-30, 0, 0, 10));
	const ObjectHandle a2 = scene.Add(Kind::Geometry, S(-30, 0, 0, 10));
	const ObjectHandle b = scene.Add(Kind::Geometry, S(500, 0, 0, 10));
	const ObjectHandle c = scene.Add(Kind::Geometry, S(0, 0, 0, 10));
	scene.Link(root, a);
	scene.Link(a, a1);
	scene.Link(a, a2);
	scene.Link(root, b);
	scene.Link(root, c);

	Section("C4: the parent's narrowing seen by every child, a child's undone for its siblings, at every depth");
	CheckBoth("the mask per visit", scene, PlaneLight(0x3F), root,
		{V(root, true, 0x3F, 0x03), V(a, true, 0x03, 0x01), V(a1, true, 0x01, 0x00), A(a1, 0x00), V(a2, true, 0x01, 0x00),
			A(a2, 0x00), V(b, false, 0x03, 0x02), V(c, true, 0x03, 0x00), A(c, 0x00)});

	Section("C4 with a gap and bits 6..31 set: only plane 1 tested, the high bits travel untouched");
	CheckBoth("mask 0xFFFFFFC2", scene, PlaneLight(0xFFFFFFC2u), root,
		{V(root, true, 0xFFFFFFC2u, 0xFFFFFFC2u), V(a, true, 0xFFFFFFC2u, 0xFFFFFFC0u), V(a1, true, 0xFFFFFFC0u, 0xFFFFFFC0u),
			A(a1, 0xFFFFFFC0u), V(a2, true, 0xFFFFFFC0u, 0xFFFFFFC0u), A(a2, 0xFFFFFFC0u), V(b, false, 0xFFFFFFC2u, 0xFFFFFFC2u),
			V(c, true, 0xFFFFFFC2u, 0xFFFFFFC0u), A(c, 0xFFFFFFC0u)});

	Section("Mask 0 on the plane path: nothing tested, everything in");
	CheckBoth("mask 0", scene, PlaneLight(0), root,
		{V(root, true, 0, 0), V(a, true, 0, 0), V(a1, true, 0, 0), A(a1, 0), V(a2, true, 0, 0), A(a2, 0), V(b, true, 0, 0),
			A(b, 0), V(c, true, 0, 0), A(c, 0)});

	Section("3. The plane-test count on this tree: 6 + 2 + 1 + 1 + 2 + 2 = 14 in both");
	const RunResult reference = RunReference(scene, PlaneLight(0x3F), root);
	const RunResult batched = RunBatched(scene, PlaneLight(0x3F), root);
	Check(reference.planeTests == 14, "reference: 14 WhichSide port calls");
	Check(batched.planeTests == 14, "batched: 14 per-plane classifications, no port call");
	Check(RunReference(scene, PlaneLight(0), root).planeTests == 0 && RunBatched(scene, PlaneLight(0), root).planeTests == 0,
		"mask 0: 0 in both");
}

// ---- 3. The differential ---------------------------------------------------------------

enum class ValueMode : std::uint8_t {
	Ties,      // small integers: exact ties d == r and d == -r are common
	Moderate,  // floats in [-1000, 1000], radii in [0, 300]
	Wild,      // also random bit patterns, NaN, infinities, denormals, FLT_MAX
};

struct Generator {
	std::mt19937 rng;
	ValueMode mode;

	explicit Generator(std::uint32_t seed, ValueMode m) : rng(seed), mode(m) {}

	std::uint32_t Below(std::uint32_t n) {
		return std::uniform_int_distribution<std::uint32_t>(0, n - 1)(rng);
	}

	bool Chance(std::uint32_t percent) {
		return Below(100) < percent;
	}

	float Special() {
		static constexpr float kSpecials[] = {
			kNaN, -kNaN, kInf, -kInf, 0.0f, -0.0f, std::numeric_limits<float>::denorm_min(),
			-std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
			std::numeric_limits<float>::min(), 1.0f, -1.0f};
		return kSpecials[Below(sizeof kSpecials / sizeof kSpecials[0])];
	}

	float Coordinate() {
		switch (mode) {
		case ValueMode::Ties:
			return static_cast<float>(static_cast<std::int32_t>(Below(17)) - 8);
		case ValueMode::Moderate:
			return std::uniform_real_distribution<float>(-1000.0f, 1000.0f)(rng);
		case ValueMode::Wild:
			if (Chance(15)) {
				return Special();
			}
			if (Chance(15)) {
				return FromBits(static_cast<std::uint32_t>(rng()));
			}
			return std::uniform_real_distribution<float>(-1000.0f, 1000.0f)(rng);
		}
		return 0.0f;
	}

	float Radius() {
		switch (mode) {
		case ValueMode::Ties:
			return static_cast<float>(Below(5));
		case ValueMode::Moderate:
			return std::uniform_real_distribution<float>(0.0f, 300.0f)(rng);
		case ValueMode::Wild:
			if (Chance(20)) {
				return Special();
			}
			return std::uniform_real_distribution<float>(-10.0f, 300.0f)(rng);
		}
		return 1.0f;
	}

	float Normal() {
		if (mode == ValueMode::Ties) {
			return static_cast<float>(static_cast<std::int32_t>(Below(3)) - 1);
		}
		return mode == ValueMode::Wild && Chance(5) ? Special() : std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
	}

	walk::LightValues Light() {
		walk::LightValues light{};
		light.hasSource = !Chance(5);
		light.sourceRtti = Chance(10) ? in_range::kNiRttiNiDirectionalLight : kRttiNiPointLight;
		light.planeSource = Chance(75) ? (Chance(50) ? 1u : static_cast<std::uint32_t>(rng()) | 1u) : 0u;
		for (ws::PlaneValues& plane : light.planes) {
			// Mostly planes facing the origin's side (a negative constant puts the
			// origin on the positive side), so the walk goes deep; the rest random.
			plane = ws::PlaneValues{Normal(), Normal(), Normal(), Chance(80) ? -InwardOffset() : Coordinate()};
		}
		std::uint32_t mask = Chance(25) ? 0x3Fu : Below(64);
		if (Chance(12)) {
			mask |= static_cast<std::uint32_t>(rng()) & 0xFFFFFFC0u;
		}
		light.activePlanes = mask;
		light.sourcePosition = in_range::PointValues{Coordinate(), Coordinate(), Coordinate()};
		light.sourceRange = mode == ValueMode::Ties ? static_cast<float>(Below(12)) : (mode == ValueMode::Wild && Chance(10) ? Special() : std::uniform_real_distribution<float>(0.0f, 1500.0f)(rng));
		return light;
	}

	float InwardOffset() {
		switch (mode) {
		case ValueMode::Ties:
			return static_cast<float>(Below(11));
		case ValueMode::Moderate:
			return std::uniform_real_distribution<float>(0.0f, 1500.0f)(rng);
		case ValueMode::Wild:
			return Chance(10) ? Special() : std::uniform_real_distribution<float>(0.0f, 1500.0f)(rng);
		}
		return 0.0f;
	}

	// A child's bound: mostly inside its parent's (centre within the parent's
	// radius, a smaller radius), as a scene graph's world bounds are; the rest
	// anywhere. Ties mode keeps integers.
	ws::SphereValues ChildBound(const ws::SphereValues& parent) {
		const bool finite = std::isfinite(parent.centerX) && std::isfinite(parent.centerY) && std::isfinite(parent.centerZ) &&
			std::isfinite(parent.radius) && parent.radius > 0.0f && parent.radius < 1e6f;
		if (!finite || Chance(30)) {
			return ws::SphereValues{Coordinate(), Coordinate(), Coordinate(), Radius()};
		}
		if (mode == ValueMode::Ties) {
			const auto offset = [this, &parent] {
				const auto r = static_cast<std::uint32_t>(parent.radius);
				return static_cast<float>(static_cast<std::int32_t>(Below(2 * r + 1)) - static_cast<std::int32_t>(r));
			};
			const float x = parent.centerX + offset();
			const float y = parent.centerY + offset();
			const float z = parent.centerZ + offset();
			return ws::SphereValues{x, y, z, static_cast<float>(Below(static_cast<std::uint32_t>(parent.radius) + 1))};
		}
		std::uniform_real_distribution<float> offset(-parent.radius, parent.radius);
		const float x = parent.centerX + offset(rng);
		const float y = parent.centerY + offset(rng);
		const float z = parent.centerZ + offset(rng);
		return ws::SphereValues{x, y, z, std::uniform_real_distribution<float>(0.0f, parent.radius)(rng)};
	}

	ObjectHandle Tree(TestScene& scene, int depth, int maxDepth) {
		return Tree(scene, depth, maxDepth, ws::SphereValues{RootCoordinate(), RootCoordinate(), RootCoordinate(), RootRadius()});
	}

	// Roots mostly near the origin, the side the inward planes keep.
	float RootCoordinate() {
		if (Chance(25)) {
			return Coordinate();
		}
		if (mode == ValueMode::Ties) {
			return static_cast<float>(static_cast<std::int32_t>(Below(5)) - 2);
		}
		return std::uniform_real_distribution<float>(-100.0f, 100.0f)(rng);
	}

	float RootRadius() {
		if (mode == ValueMode::Ties) {
			return static_cast<float>(Below(9));
		}
		return mode == ValueMode::Wild && Chance(10) ? Special() : std::uniform_real_distribution<float>(0.0f, 1500.0f)(rng);
	}

	ObjectHandle Tree(TestScene& scene, int depth, int maxDepth, ws::SphereValues bound) {
		Kind kind = Kind::Geometry;
		const std::uint32_t roll = Below(100);
		if (depth == 0) {
			kind = roll < 75 ? Kind::Node : roll < 85 ? Kind::Switch : roll < 90 ? Kind::LodNode : roll < 97 ? Kind::Geometry : Kind::Other;
		} else if (depth < maxDepth) {
			kind = roll < 45 ? Kind::Node : roll < 53 ? Kind::Switch : roll < 58 ? Kind::LodNode : roll < 93 ? Kind::Geometry : Kind::Other;
		} else {
			kind = roll < 85 ? Kind::Geometry : Kind::Other;
		}
		const ObjectHandle handle = scene.Add(kind, bound);
		TestObject& object = scene.At(handle);
		if (Chance(8)) {
			object.bound.radius = Chance(50) ? 0.0f : -0.0f;
		}
		object.flags = static_cast<std::uint8_t>(Chance(15) ? (rng() | 1u) : (rng() & 0xFEu));
		if (kind == Kind::Geometry && Chance(10)) {
			object.secondGeometry = 0x40000000u | handle;
		}
		if (Chance(5)) {
			object.nonZeroRadiusOverride = static_cast<int>(Below(2));
		}
		if (kind == Kind::Node || kind == Kind::Switch || kind == Kind::LodNode) {
			const std::uint32_t count = Below(6);
			for (std::uint32_t i = 0; i < count; ++i) {
				const ObjectHandle child = Chance(10) ? walk::kNullObject : Tree(scene, depth + 1, maxDepth, ChildBound(scene.At(handle).bound));
				scene.Link(handle, child);  // handle stays valid: Link indexes, it holds no reference
			}
			scene.At(handle).activeIndex = static_cast<std::int32_t>(Below(count + 2)) - 1;
		}
		return handle;
	}
};

// A side effect of the k-th AddGeometry, the same in both runs: grow or
// shrink a node's children, or turn the gate off.
void AddMutation(Generator& gen, TestScene& scene) {
	std::vector<ObjectHandle> nodes;
	for (ObjectHandle h = 1; h < scene.objects.size(); ++h) {
		if (scene.objects[h].kind != Kind::Geometry && scene.objects[h].kind != Kind::Other) {
			nodes.push_back(h);
		}
	}
	if (nodes.empty()) {
		return;
	}
	const ObjectHandle target = nodes[gen.Below(static_cast<std::uint32_t>(nodes.size()))];
	const ObjectHandle spare = scene.Add(Kind::Geometry, ws::SphereValues{gen.Coordinate(), gen.Coordinate(), gen.Coordinate(), gen.Radius()});
	const int trigger = static_cast<int>(gen.Below(3)) + 1;
	const std::uint32_t action = gen.Below(3);
	scene.onAdd = [target, spare, trigger, action](TestScene& s, ObjectHandle) {
		if (s.addCount != trigger) {
			return;
		}
		if (action == 0) {
			s.Link(target, spare);
		} else if (action == 1) {
			if (!s.At(target).children.empty()) {
				s.At(target).children.pop_back();
			}
		} else {
			s.gateValue = 0;
		}
	};
}

std::string DescribeLight(const walk::LightValues& light) {
	std::string text;
	char line[160];
	std::snprintf(line, sizeof line, "      source %d rtti 0x%08X planeSource 0x%08X mask 0x%08X position %08X %08X %08X range %08X\n",
		light.hasSource ? 1 : 0, static_cast<unsigned>(light.sourceRtti), static_cast<unsigned>(light.planeSource),
		static_cast<unsigned>(light.activePlanes), static_cast<unsigned>(Bits(light.sourcePosition.x)),
		static_cast<unsigned>(Bits(light.sourcePosition.y)), static_cast<unsigned>(Bits(light.sourcePosition.z)),
		static_cast<unsigned>(Bits(light.sourceRange)));
	text += line;
	for (unsigned i = 0; i < in_range::kPlaneCount; ++i) {
		const ws::PlaneValues& p = light.planes[i];
		std::snprintf(line, sizeof line, "      plane %u: %08X %08X %08X %08X\n", i, static_cast<unsigned>(Bits(p.normalX)),
			static_cast<unsigned>(Bits(p.normalY)), static_cast<unsigned>(Bits(p.normalZ)), static_cast<unsigned>(Bits(p.constant)));
		text += line;
	}
	return text;
}

struct DifferentialStats {
	int trees = 0;
	int mutated = 0;
	int byMode[4] = {};  // SourceMode
	int byRounding[4] = {};  // ws::X87Rounding
	std::uint64_t visitsByMode[4] = {};
	std::uint64_t objects = 0;
	std::uint64_t visits = 0;
	std::uint64_t outOfRange = 0;
	std::uint64_t narrowed = 0;
	std::uint64_t adds = 0;
	std::uint64_t planeTests = 0;
	std::uint64_t ties = 0;        // active lanes with d == r or d == -r exactly
	std::uint64_t nanLanes = 0;    // active lanes with a NaN distance or radius
	int maxDepthSeen = 0;
	int failures = 0;
};

void CountLanes(const TestScene& scene, const walk::LightValues& light, const std::vector<Event>& log, DifferentialStats& stats) {
	if (walk::SelectSourceMode(light.hasSource, light.sourceRtti, light.planeSource) != walk::SourceMode::Planes) {
		return;
	}
	for (const Event& e : log) {
		if (e.kind != 'V') {
			continue;
		}
		const ws::SphereValues& b = scene.objects[e.object].bound;
		for (unsigned i = 0; i < in_range::kPlaneCount; ++i) {
			if (!in_range::IsPlaneActive(e.maskIn, i)) {
				continue;
			}
			const float d = ws::SignedDistance(light.planes[i], b, light.rounding).distanceStored;
			if (std::isnan(d) || std::isnan(b.radius)) {
				++stats.nanLanes;
			} else if (d == b.radius || d == -b.radius) {
				++stats.ties;
			}
		}
	}
}

int Depth(const TestScene& scene, ObjectHandle h) {
	int deepest = 0;
	for (const ObjectHandle c : scene.objects[h].children) {
		if (c != walk::kNullObject) {
			const int d = Depth(scene, c);
			deepest = d > deepest ? d : deepest;
		}
	}
	return deepest + 1;
}

void Differential() {
	Section("Differential: ReferenceWalk (through the verified IsInRange) against BatchedWalk on generated scenes");
	constexpr int kTreesPerMode = 10000;
	const ValueMode modes[] = {ValueMode::Ties, ValueMode::Moderate, ValueMode::Wild};
	const char* modeNames[] = {"ties", "moderate", "wild"};
	DifferentialStats stats;
	bool reported = false;
	for (int m = 0; m < 3; ++m) {
		for (int t = 0; t < kTreesPerMode; ++t) {
			const std::uint32_t seed = 0x5EED0000u + static_cast<std::uint32_t>(m * 100000 + t);
			Generator gen(seed, modes[m]);
			TestScene scene;
			const int maxDepth = static_cast<int>(gen.Below(6)) + 1;  // 1..6 levels of nodes under the root
			const ObjectHandle root = gen.Tree(scene, 0, maxDepth);
			if (gen.Chance(15)) {
				AddMutation(gen, scene);
				++stats.mutated;
			}
			walk::LightValues light = gen.Light();
			// The rounding mode by tree index, so every mode gets a quarter of the
			// trees and the generator's sequence stays the one before modes existed.
			light.rounding = static_cast<ws::X87Rounding>(t % 4);
			++stats.byRounding[t % 4];
			if (gen.Chance(3)) {
				scene.gateValue = 0;
			}
			const RunResult reference = RunReference(scene, light, root);
			const RunResult batched = RunBatched(scene, light, root);
			const bool same = reference.log == batched.log && reference.result == batched.result &&
				reference.finalMask == batched.finalMask && reference.planeTests == batched.planeTests;
			++stats.trees;
			++stats.byMode[static_cast<int>(walk::SelectSourceMode(light.hasSource, light.sourceRtti, light.planeSource))];
			stats.objects += scene.objects.size() - 1;
			const int depth = Depth(scene, root);
			stats.maxDepthSeen = depth > stats.maxDepthSeen ? depth : stats.maxDepthSeen;
			for (const Event& e : reference.log) {
				if (e.kind == 'V') {
					++stats.visits;
					++stats.visitsByMode[static_cast<int>(walk::SelectSourceMode(light.hasSource, light.sourceRtti, light.planeSource))];
					stats.outOfRange += e.inRange ? 0 : 1;
					stats.narrowed += e.maskIn != e.maskOut ? 1 : 0;
				} else {
					++stats.adds;
				}
			}
			stats.planeTests += reference.planeTests;
			CountLanes(scene, light, reference.log, stats);
			if (!same) {
				++stats.failures;
				if (!reported) {
					reported = true;
					std::printf("    FIRST DIFFERENCE: mode %s tree %d seed 0x%08X root %u objects %zu\n", modeNames[m], t,
						static_cast<unsigned>(seed), static_cast<unsigned>(root), scene.objects.size() - 1);
					std::printf("      result %d / %d, final mask 0x%08X / 0x%08X, plane tests %llu / %llu\n",
						reference.result ? 1 : 0, batched.result ? 1 : 0, static_cast<unsigned>(reference.finalMask),
						static_cast<unsigned>(batched.finalMask), static_cast<unsigned long long>(reference.planeTests),
						static_cast<unsigned long long>(batched.planeTests));
					std::printf("      %s\n%s", FirstDifference(reference.log, batched.log).c_str(), DescribeLight(light).c_str());
				}
			}
		}
	}
	std::printf("    trees %d (mutating %d), objects %llu, deepest tree %d levels\n", stats.trees, stats.mutated,
		static_cast<unsigned long long>(stats.objects), stats.maxDepthSeen);
	std::printf("    source modes (trees / visits): none %d / %llu, directional %d / %llu, planes %d / %llu, distance %d / %llu\n",
		stats.byMode[0], static_cast<unsigned long long>(stats.visitsByMode[0]), stats.byMode[1],
		static_cast<unsigned long long>(stats.visitsByMode[1]), stats.byMode[2], static_cast<unsigned long long>(stats.visitsByMode[2]),
		stats.byMode[3], static_cast<unsigned long long>(stats.visitsByMode[3]));
	std::printf("    visits %llu (out of range %llu, narrowed %llu), AddGeometry %llu, plane tests %llu, exact ties %llu, NaN lanes %llu\n",
		static_cast<unsigned long long>(stats.visits), static_cast<unsigned long long>(stats.outOfRange),
		static_cast<unsigned long long>(stats.narrowed), static_cast<unsigned long long>(stats.adds),
		static_cast<unsigned long long>(stats.planeTests), static_cast<unsigned long long>(stats.ties),
		static_cast<unsigned long long>(stats.nanLanes));
	Check(stats.trees >= 2000, "at least 2 000 trees");
	Check(stats.failures == 0, "every tree: the same events, AL, final mask and plane-test count");
	Check(stats.byMode[0] > 0 && stats.byMode[1] > 0 && stats.byMode[2] > 0 && stats.byMode[3] > 0, "every source mode reached");
	Check(stats.byRounding[0] == stats.trees / 4 && stats.byRounding[1] == stats.trees / 4 && stats.byRounding[2] == stats.trees / 4 &&
			stats.byRounding[3] == stats.trees / 4,
		"every rounding mode on a quarter of the trees (both walks under the same mode)");
	Check(stats.ties > 0 && stats.nanLanes > 0 && stats.outOfRange > 0 && stats.narrowed > 0 && stats.mutated > 0,
		"ties, NaN lanes, out-of-range visits, narrowing and mutations all reached");
}

// ---- The engine-facing constants -----------------------------------------------------------

void EngineConstants() {
	Section("ShadowLightRangeWalk.h: the addresses and offsets as the mapping reads them");
	Check(walk_engine::kCollectGeometryInRangeAddress == 0x007D59E0u, "the walk 0x007D59E0");
	Check(walk_engine::kAddGeometryAddress == 0x007D5790u, "AddGeometry 0x007D5790");
	Check(walk_engine::kSwitchGetActiveChildAddress == 0x007C59E0u, "active child 0x007C59E0");
	Check(walk_engine::kHasNonZeroBoundRadiusAddress == 0x0049FFC0u, "non-zero radius 0x0049FFC0");
	Check(walk_engine::kWalkGateAddress == 0x00B42F48u, "gate 0x00B42F48");
	Check(walk_engine::kSourceLightOffset == 0x100u && walk_engine::kActivePlanesOffset == 0x1B0u, "light +0x100, +0x1B0");
	Check(walk_engine::kChildrenDataOffset == 0xB0u && walk_engine::kChildCountOffset == 0xB6u, "node +0xB0, +0xB6");
	Check(walk_engine::kFlagsOffset == 0x18u && walk_engine::kBoundRadiusOffset == 0x2Cu, "object +0x18, +0x2C");
}

} // namespace

int main() {
	Decisions();
	PlaneBatchFlows();
	GateFlows();
	RangeFlows();
	ChildFlows();
	SwitchFlows();
	MaskFlows();
	Differential();
	EngineConstants();
	return Failures();
}
