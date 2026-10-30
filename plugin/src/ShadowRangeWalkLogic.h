#pragma once

// The range-walk shadow's decisions and flow, over a memory port, so every
// flow runs in a test without the game (plugin/tests/ShadowRangeWalkLogicTest.cpp).
// ShadowRangeWalk.cpp only supplies the port (volatile reads, the engine's
// own calls) and the two entry detours.
//
// The shadow, per outermost call of ShadowSceneLight::CollectGeometryInRange
// 0x007D59E0 (proposed name) on a thread:
//   1. the light's values are read (CaptureLight) BEFORE the original runs;
//   2. the original runs on the game's state, its AddGeometry 0x007D5790 calls
//      recorded through the AddGeometry detour (geometry, live +0x1B0);
//      its own recursive calls come back through the patched entry and pass
//      straight through (the depth counter);
//   3. BatchedWalk (src/engine/render/ShadowLightRangeWalkLogic.h) runs over
//      the same scene through MemoryScene: it reads the scene objects and
//      calls the engine's helpers and virtual slots, but RECORDS instead of
//      performs AddGeometry;
//   4. the two AddGeometry sequences, +0x1B0 afterwards and AL are compared
//      (Compare); the game continues with the original's result.
//
// Marks (AGENTS.md): read = seen in the instructions (address given),
// derived = follows from read facts, unverified = not checked.
//
// 64-bit clean: addresses are std::uint32_t values the memory port resolves;
// nothing here depends on the pointer size. The 32-bit engine shape (calls,
// volatile reads) lives in ShadowRangeWalk.cpp only.

#include "ShadowTimeInfo.h"
#include "engine/render/ShadowLightRangeWalk.h"
#include "engine/render/ShadowLightRangeWalkLogic.h"

#include <cstddef>
#include <cstdint>
#include <bit>
#include <cstdio>
#include <vector>

namespace obng::plugin::range_walk {

namespace walk = engine::render::shadow_light_range_walk;
namespace walk_engine = walk::engine;
namespace which_side = engine::render::which_side;
using walk::ObjectHandle;

// The source light's position and range as IsInRange reads them (FLD
// [ESI+0x88/0x8C/0x90] 0x007D2F5D/6B/79, FLD [ESI+0xF8] 0x007D2FC9; read,
// re/functions/0x007D2EA0.md). ShadowRangeWalk.cpp checks them against
// NiLightView in ShadowSceneLight_IsInRange.h.
inline constexpr std::uint32_t kSourcePositionOffset = 0x88;
inline constexpr std::uint32_t kSourceRangeOffset = 0xF8;
inline constexpr std::uint32_t kPlaneStride = 0x10;  // ADD EBP,0x10 0x007D2F47 (read)

// ---- The recorded events and their comparison ----------------------------------------

// One AddGeometry call: the geometry passed and the value +0x1B0 held at
// the call (the original: read in the AddGeometry detour; the rebuild: the
// mask BatchedWalk hands its port).
struct AddGeometryEvent {
	std::uint32_t geometry;
	std::uint32_t mask;
	bool operator==(const AddGeometryEvent&) const = default;
};

// What one walk produced.
struct WalkOutcome {
	std::vector<AddGeometryEvent> adds;
	std::uint32_t maskAfter = 0;  // +0x1B0 after the walk (the rebuild never writes it: its entry value)
	bool result = false;          // AL

	void Clear() {
		adds.clear();
		maskAfter = 0;
		result = false;
	}
};

inline constexpr std::size_t kNoDifference = static_cast<std::size_t>(-1);

struct Comparison {
	// The first index at which the two sequences differ; when one is a prefix
	// of the other, the shorter one's length. kNoDifference when equal.
	std::size_t firstDifference = kNoDifference;
	std::size_t originalCount = 0;
	std::size_t rebuildCount = 0;
	bool maskDiffers = false;
	bool resultDiffers = false;

	bool Mismatch() const {
		return firstDifference != kNoDifference || maskDiffers || resultDiffers;
	}
};

inline Comparison Compare(const WalkOutcome& original, const WalkOutcome& rebuild) {
	Comparison c;
	c.originalCount = original.adds.size();
	c.rebuildCount = rebuild.adds.size();
	const std::size_t common = c.originalCount < c.rebuildCount ? c.originalCount : c.rebuildCount;
	for (std::size_t i = 0; i < common; ++i) {
		if (!(original.adds[i] == rebuild.adds[i])) {
			c.firstDifference = i;
			break;
		}
	}
	if (c.firstDifference == kNoDifference && c.originalCount != c.rebuildCount) {
		c.firstDifference = common;
	}
	c.maskDiffers = original.maskAfter != rebuild.maskAfter;
	c.resultDiffers = original.result != rebuild.result;
	return c;
}

// The comparison as text for a mismatch line; always terminated, never
// longer than capacity.
inline void DescribeComparison(const Comparison& c, const WalkOutcome& original, const WalkOutcome& rebuild, char* out,
	std::size_t capacity) {
	if (capacity == 0) {
		return;
	}
	out[0] = '\0';
	std::size_t at = 0;
	const auto append = [&](int written) {
		if (written > 0) {
			at += static_cast<std::size_t>(written);
			if (at >= capacity) {
				at = capacity - 1;
			}
		}
	};
	append(std::snprintf(out + at, capacity - at, " adds %zu/%zu;", c.originalCount, c.rebuildCount));
	if (c.firstDifference != kNoDifference) {
		const std::size_t i = c.firstDifference;
		append(std::snprintf(out + at, capacity - at, " first difference at %zu: original ", i));
		if (i < original.adds.size()) {
			append(std::snprintf(out + at, capacity - at, "%08X mask %08X", static_cast<unsigned>(original.adds[i].geometry),
				static_cast<unsigned>(original.adds[i].mask)));
		} else {
			append(std::snprintf(out + at, capacity - at, "(ended)"));
		}
		append(std::snprintf(out + at, capacity - at, ", rebuild "));
		if (i < rebuild.adds.size()) {
			append(std::snprintf(out + at, capacity - at, "%08X mask %08X;", static_cast<unsigned>(rebuild.adds[i].geometry),
				static_cast<unsigned>(rebuild.adds[i].mask)));
		} else {
			append(std::snprintf(out + at, capacity - at, "(ended);"));
		}
	}
	if (c.maskDiffers) {
		append(std::snprintf(out + at, capacity - at, " mask after original %08X rebuild %08X;",
			static_cast<unsigned>(original.maskAfter), static_cast<unsigned>(rebuild.maskAfter)));
	}
	if (c.resultDiffers) {
		append(std::snprintf(out + at, capacity - at, " AL original %d rebuild %d;", original.result ? 1 : 0,
			rebuild.result ? 1 : 0));
	}
}

// ---- The entry decision ------------------------------------------------------------------

enum class EntryAction : std::uint8_t {
	Compare,         // the outermost call: record the original, run the rebuild, compare
	PassThrough,     // the walk's own recursion (0x007D5A74, 0x007D5ABD) re-enters the patched entry
	SkipGateOff,     // [0x00B42F48] == 0: the original returns AL 0 at once (B1, read)
	SkipNullObject,  // nothing to walk; the original would dereference it (0x007D2EA0 reads its bound)
	SkipNoSource,    // this+0x100 null: IsInRange answers false for the root, nothing to compare (B1 of 0x007D2EA0)
};

// depthBefore: walk calls already running on this thread through the detour.
// An inner call is decided first and reads nothing.
constexpr EntryAction DecideEntry(std::uint32_t depthBefore, std::uint32_t gate, ObjectHandle object, std::uint32_t source) {
	if (depthBefore != 0) {
		return EntryAction::PassThrough;
	}
	if (!walk::IsWalkEnabled(gate)) {
		return EntryAction::SkipGateOff;
	}
	if (object == walk::kNullObject) {
		return EntryAction::SkipNullObject;
	}
	if (source == 0) {
		return EntryAction::SkipNoSource;
	}
	return EntryAction::Compare;
}

inline const char* SkipReason(EntryAction action) {
	switch (action) {
	case EntryAction::SkipGateOff:
		return "the walk's gate [0x00B42F48] is 0, the original returns at once";
	case EntryAction::SkipNullObject:
		return "the object is null";
	case EntryAction::SkipNoSource:
		return "the light has no source light (+0x100 null), every object is out of range";
	case EntryAction::Compare:
	case EntryAction::PassThrough:
		return nullptr;
	}
	return nullptr;
}

// ---- Per-thread state: the depth and the recording ---------------------------------------

// One per thread (the detour keeps it thread-local): plain values only.
struct ThreadWalkState {
	std::uint32_t depth = 0;                          // walk calls running through the detour
	std::uint32_t recordingLight = 0;                 // the light of the compared walk; 0 = none
	std::vector<AddGeometryEvent>* recording = nullptr;  // where its AddGeometry calls go
};

// Raises the depth for one call of the patched entry and lowers it again.
class DepthScope {
public:
	explicit DepthScope(ThreadWalkState& state) : state_(state), before_(state.depth++) {}
	~DepthScope() { --state_.depth; }
	DepthScope(const DepthScope&) = delete;
	DepthScope& operator=(const DepthScope&) = delete;
	std::uint32_t Before() const { return before_; }

private:
	ThreadWalkState& state_;
	std::uint32_t before_;
};

// AddGeometry is recorded only while a compared walk of the same light runs
// on this thread: 0x007C6100 calls it too (read, meta.json callers), and
// those calls are not the walk's.
inline bool RecordsAddGeometry(const ThreadWalkState& state, std::uint32_t light) {
	return state.recording != nullptr && state.recordingLight != 0 && state.recordingLight == light;
}

// The AddGeometry detour's part: readMask() is +0x1B0 now, read only when
// the call is recorded.
template <class ReadMask>
void OnAddGeometry(ThreadWalkState& state, std::uint32_t light, std::uint32_t geometry, ReadMask&& readMask) {
	if (RecordsAddGeometry(state, light)) {
		state.recording->push_back(AddGeometryEvent{geometry, static_cast<std::uint32_t>(readMask())});
	}
}

// ---- The memory port and what reads through it --------------------------------------------

// Memory port:
//   U8 / U16 / U32 / F32(address)    reads of the game's memory
//   CallVirtual(object, slot)        object->vtable[slot](), thiscall, no stack argument -> EAX
//   CallThis(function, self)         function(self), thiscall, no stack argument -> EAX

// The light as BatchedWalk takes it (walk::LightValues), read once before the
// original runs. Offsets: ShadowLightRangeWalk.h (read there, 0x007D2EA0.md).
template <class Memory>
walk::LightValues CaptureLight(Memory& memory, std::uint32_t light) {
	walk::LightValues v{};
	const std::uint32_t source = memory.U32(light + walk_engine::kSourceLightOffset);
	v.hasSource = source != 0;
	v.planeSource = memory.U32(light + walk_engine::kPlaneSourceOffset);
	for (unsigned i = 0; i < v.planes.size(); ++i) {
		const std::uint32_t at = light + walk_engine::kPlanesOffset + kPlaneStride * i;
		v.planes[i] = which_side::PlaneValues{memory.F32(at), memory.F32(at + 4), memory.F32(at + 8), memory.F32(at + 12)};
	}
	v.activePlanes = memory.U32(light + walk_engine::kActivePlanesOffset);
	if (v.hasSource) {
		v.sourceRtti = memory.CallVirtual(source, walk_engine::kGetRttiSlot);
		v.sourcePosition = {memory.F32(source + kSourcePositionOffset), memory.F32(source + kSourcePositionOffset + 4),
			memory.F32(source + kSourcePositionOffset + 8)};
		v.sourceRange = memory.F32(source + kSourceRangeOffset);
	}
	return v;
}

// The RangeWalkScene ports over the game's memory (ShadowLightRangeWalk.h
// lists the engine operation behind each). AddGeometry is recorded, never
// called.
template <class Memory>
struct MemoryScene {
	Memory& memory;
	std::vector<AddGeometryEvent>* adds;

	std::uint32_t gate() { return memory.U32(walk_engine::kWalkGateAddress); }
	ObjectHandle asNode(ObjectHandle o) { return memory.CallVirtual(o, walk_engine::kGetAsNodeSlot); }
	std::uint32_t nodeRtti(ObjectHandle n) { return memory.CallVirtual(n, walk_engine::kGetRttiSlot); }
	ObjectHandle activeChild(ObjectHandle n) { return memory.CallThis(walk_engine::kSwitchGetActiveChildAddress, n); }
	// TEST AL,AL at 0x007D5A5F (read): only the low byte counts.
	bool hasNonZeroBoundRadius(ObjectHandle o) {
		return (memory.CallThis(walk_engine::kHasNonZeroBoundRadiusAddress, o) & 0xFFu) != 0;
	}
	std::uint16_t childCount(ObjectHandle n) { return memory.U16(n + walk_engine::kChildCountOffset); }
	ObjectHandle child(ObjectHandle n, std::uint32_t i) {
		return memory.U32(memory.U32(n + walk_engine::kChildrenDataOffset) + 4 * i);
	}
	which_side::SphereValues bound(ObjectHandle o) {
		const std::uint32_t at = o + walk_engine::kWorldBoundOffset;
		return which_side::SphereValues{memory.F32(at), memory.F32(at + 4), memory.F32(at + 8), memory.F32(at + 12)};
	}
	std::uint8_t flags(ObjectHandle o) { return memory.U8(o + walk_engine::kFlagsOffset); }
	ObjectHandle asGeometry(ObjectHandle o) { return memory.CallVirtual(o, walk_engine::kGetAsGeometrySlot); }
	void addGeometry(ObjectHandle g, std::uint32_t mask) { adds->push_back(AddGeometryEvent{g, mask}); }
};

// ---- The trace of the rebuild's scene reads (the rebuild-first diagnostic) -----------------

// One scene port call of BatchedWalk with what it answered, or a visit with
// its decision. Two traces of the same walk over the same light differ first
// where an input the walk reads changed between them.
enum class TraceKind : std::uint8_t {
	Gate,           // values[0] = the gate dword
	AsNode,         // values[0] = vt[+0x08]()
	NodeRtti,       // values[0] = vt[+0x04]()
	ActiveChild,    // values[0] = 0x007C59E0's result
	NonZeroRadius,  // values[0] = 0x0049FFC0's AL
	ChildCount,     // values[0] = the u16 +0xB6
	Child,          // values[0] = the index, values[1] = the slot +0xB0[index]
	Bound,          // values[0..3] = the words +0x20..+0x2C, bit for bit
	Flags,          // values[0] = the byte +0x18
	AsGeometry,     // values[0] = vt[+0x10]()
	AddGeometry,    // values[0] = the mask handed over
	Visit,          // values[0] = in range, values[1] = mask in, values[2] = mask out
};

inline const char* TraceKindName(TraceKind kind) {
	switch (kind) {
	case TraceKind::Gate: return "gate";
	case TraceKind::AsNode: return "as-node";
	case TraceKind::NodeRtti: return "node-rtti";
	case TraceKind::ActiveChild: return "active-child";
	case TraceKind::NonZeroRadius: return "non-zero-radius";
	case TraceKind::ChildCount: return "child-count";
	case TraceKind::Child: return "child";
	case TraceKind::Bound: return "bound";
	case TraceKind::Flags: return "flags";
	case TraceKind::AsGeometry: return "as-geometry";
	case TraceKind::AddGeometry: return "add-geometry";
	case TraceKind::Visit: return "visit";
	}
	return "?";
}

struct TraceEvent {
	TraceKind kind;
	std::uint32_t object;  // the object, node or geometry the read was about (0 for the gate)
	std::uint32_t values[4];
	bool operator==(const TraceEvent&) const = default;
};

// A RangeWalkScene that forwards every port to `inner` and logs it.
template <class Scene>
struct TracingScene {
	Scene& inner;
	std::vector<TraceEvent>* trace;

	std::uint32_t Log(TraceKind kind, std::uint32_t object, std::uint32_t value, std::uint32_t second = 0) {
		trace->push_back(TraceEvent{kind, object, {value, second, 0, 0}});
		return value;
	}
	std::uint32_t gate() { return Log(TraceKind::Gate, 0, static_cast<std::uint32_t>(inner.gate())); }
	ObjectHandle asNode(ObjectHandle o) { return Log(TraceKind::AsNode, o, inner.asNode(o)); }
	std::uint32_t nodeRtti(ObjectHandle n) { return Log(TraceKind::NodeRtti, n, inner.nodeRtti(n)); }
	ObjectHandle activeChild(ObjectHandle n) { return Log(TraceKind::ActiveChild, n, inner.activeChild(n)); }
	bool hasNonZeroBoundRadius(ObjectHandle o) {
		return Log(TraceKind::NonZeroRadius, o, inner.hasNonZeroBoundRadius(o) ? 1u : 0u) != 0;
	}
	std::uint16_t childCount(ObjectHandle n) {
		return static_cast<std::uint16_t>(Log(TraceKind::ChildCount, n, inner.childCount(n)));
	}
	ObjectHandle child(ObjectHandle n, std::uint32_t i) {
		const ObjectHandle c = inner.child(n, i);
		Log(TraceKind::Child, n, i, c);
		return c;
	}
	which_side::SphereValues bound(ObjectHandle o) {
		const which_side::SphereValues b = inner.bound(o);
		trace->push_back(TraceEvent{TraceKind::Bound, o,
			{std::bit_cast<std::uint32_t>(b.centerX), std::bit_cast<std::uint32_t>(b.centerY),
				std::bit_cast<std::uint32_t>(b.centerZ), std::bit_cast<std::uint32_t>(b.radius)}});
		return b;
	}
	std::uint8_t flags(ObjectHandle o) { return static_cast<std::uint8_t>(Log(TraceKind::Flags, o, inner.flags(o))); }
	ObjectHandle asGeometry(ObjectHandle o) { return Log(TraceKind::AsGeometry, o, inner.asGeometry(o)); }
	void addGeometry(ObjectHandle g, std::uint32_t mask) {
		Log(TraceKind::AddGeometry, g, mask);
		inner.addGeometry(g, mask);
	}
};

// BatchedWalk's observer: the visits into the same trace.
struct TracingObserver {
	std::vector<TraceEvent>* trace;
	void onVisit(ObjectHandle object, bool inRange, std::uint32_t maskIn, std::uint32_t maskOut) {
		trace->push_back(TraceEvent{TraceKind::Visit, object, {inRange ? 1u : 0u, maskIn, maskOut, 0}});
	}
};

struct TraceDifference {
	bool found = false;
	std::size_t index = 0;  // the first differing event; the shorter length when one is a prefix
};

inline TraceDifference FirstTraceDifference(const std::vector<TraceEvent>& fresh, const std::vector<TraceEvent>& after) {
	const std::size_t common = fresh.size() < after.size() ? fresh.size() : after.size();
	for (std::size_t i = 0; i < common; ++i) {
		if (!(fresh[i] == after[i])) {
			return TraceDifference{true, i};
		}
	}
	if (fresh.size() != after.size()) {
		return TraceDifference{true, common};
	}
	return TraceDifference{};
}

// " inputs first differ at read k: fresh <kind> <object> = <values>, after the
// original <kind> <object> = <values>;" - nothing when no difference.
inline void DescribeTraceDifference(const TraceDifference& d, const std::vector<TraceEvent>& fresh,
	const std::vector<TraceEvent>& after, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return;
	}
	out[0] = '\0';
	if (!d.found) {
		return;
	}
	std::size_t at = 0;
	const auto append = [&](int written) {
		if (written > 0) {
			at += static_cast<std::size_t>(written);
			if (at >= capacity) {
				at = capacity - 1;
			}
		}
	};
	const auto event = [&](const std::vector<TraceEvent>& trace) {
		if (d.index >= trace.size()) {
			append(std::snprintf(out + at, capacity - at, "(ended)"));
			return;
		}
		const TraceEvent& e = trace[d.index];
		append(std::snprintf(out + at, capacity - at, "%s %08X = %08X %08X %08X %08X", TraceKindName(e.kind),
			static_cast<unsigned>(e.object), static_cast<unsigned>(e.values[0]), static_cast<unsigned>(e.values[1]),
			static_cast<unsigned>(e.values[2]), static_cast<unsigned>(e.values[3])));
	};
	append(std::snprintf(out + at, capacity - at, " inputs first differ at read %zu: fresh ", d.index));
	event(fresh);
	append(std::snprintf(out + at, capacity - at, ", after the original "));
	event(after);
	append(std::snprintf(out + at, capacity - at, ";"));
}

// ---- The detour's flow ----------------------------------------------------------------------

// Reusable between walks so a warmed-up shadow does not allocate.
struct WalkBuffers {
	WalkOutcome original;
	WalkOutcome rebuild;
	walk::BatchedWalkScratch scratch;
	// rebuild-first only: the rebuild's reads on the untouched scene, and - after
	// a mismatch - the same rebuild's reads once more after the original ran.
	std::vector<TraceEvent> traceFresh;
	std::vector<TraceEvent> traceAfter;
	std::vector<AddGeometryEvent> afterAdds;  // the second rebuild's AddGeometry calls, only traced
};

struct WalkReport {
	EntryAction action;
	Comparison comparison;  // filled when action == Compare
	walk::SourceMode mode;  // filled when action == Compare
	std::uint32_t light;
	std::uint32_t object;
	std::uint32_t maskBefore;  // +0x1B0 at entry, filled when action == Compare
	RangeWalkOrder order = RangeWalkOrder::OriginalFirst;
	TraceDifference trace{};   // rebuild-first after a mismatch: the first input that changed
	bool lightChanged = false; // rebuild-first after a mismatch: the light's values differ after the original
};

// The light's values bit for bit (a float compare would hide -0.0 and NaN).
inline bool SameLightValues(const walk::LightValues& a, const walk::LightValues& b) {
	if (a.hasSource != b.hasSource || a.sourceRtti != b.sourceRtti || a.planeSource != b.planeSource ||
		a.activePlanes != b.activePlanes) {
		return false;
	}
	const auto same = [](float x, float y) { return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y); };
	for (std::size_t i = 0; i < a.planes.size(); ++i) {
		if (!same(a.planes[i].normalX, b.planes[i].normalX) || !same(a.planes[i].normalY, b.planes[i].normalY) ||
			!same(a.planes[i].normalZ, b.planes[i].normalZ) || !same(a.planes[i].constant, b.planes[i].constant)) {
			return false;
		}
	}
	return same(a.sourcePosition.x, b.sourcePosition.x) && same(a.sourcePosition.y, b.sourcePosition.y) &&
		same(a.sourcePosition.z, b.sourcePosition.z) && same(a.sourceRange, b.sourceRange);
}

// What the compared walks covered since the last summary, so the log shows
// which of IsInRange's source paths the game exercised and how many
// AddGeometry calls were compared (an all-skipped or all-empty run proves
// little).
struct WalkStats {
	std::uint64_t walks[4] = {0, 0, 0, 0};  // compared walks by walk::SourceMode
	std::uint64_t adds = 0;                 // original AddGeometry calls compared
	std::uint64_t maxAdds = 0;              // the most in one walk
	std::uint64_t mismatches = 0;

	void Add(const WalkReport& report) {
		if (report.action != EntryAction::Compare) {
			return;
		}
		++walks[static_cast<unsigned>(report.mode)];
		const std::uint64_t n = report.comparison.originalCount;
		adds += n;
		maxAdds = n > maxAdds ? n : maxAdds;
		mismatches += report.comparison.Mismatch() ? 1 : 0;
	}
	std::uint64_t Walks() const { return walks[0] + walks[1] + walks[2] + walks[3]; }
	void Reset() { *this = WalkStats{}; }
};

inline void FormatWalkStats(const WalkStats& s, char* out, std::size_t capacity) {
	if (capacity == 0) {
		return;
	}
	std::snprintf(out, capacity,
		"Shadow range walk: %llu walks compared (planes %llu, distance %llu, directional %llu, no source %llu), "
		"%llu AddGeometry calls compared, at most %llu in one walk, %llu mismatching walks",
		static_cast<unsigned long long>(s.Walks()),
		static_cast<unsigned long long>(s.walks[static_cast<unsigned>(walk::SourceMode::Planes)]),
		static_cast<unsigned long long>(s.walks[static_cast<unsigned>(walk::SourceMode::Distance)]),
		static_cast<unsigned long long>(s.walks[static_cast<unsigned>(walk::SourceMode::Directional)]),
		static_cast<unsigned long long>(s.walks[static_cast<unsigned>(walk::SourceMode::NoSource)]),
		static_cast<unsigned long long>(s.adds), static_cast<unsigned long long>(s.maxAdds),
		static_cast<unsigned long long>(s.mismatches));
}

// One call of the patched entry 0x007D59E0. callOriginal(light, object)
// runs the original (its EAX is returned unchanged); squareRoot is the
// 0x00982C30 port; report(WalkReport) is called once per outermost call,
// never for a pass-through. The game's state is left as the original left it:
// the rebuild only reads and calls the read-only helpers.
//
// In both orders the light's values are read FIRST, before either side runs
// (CaptureLight); the rebuild never re-reads them.
//   original-first: the original (recorded), then BatchedWalk.
//   rebuild-first:  BatchedWalk with its reads traced, then the original
//                   (recorded); after a mismatch BatchedWalk runs once more,
//                   traced, with the same light values, and the first read
//                   that differs between the two traces is reported, as is
//                   whether the light's own values changed.
template <class Memory, class CallOriginal, class SquareRoot, class Report>
std::uint32_t ShadowWalk(Memory& memory, ThreadWalkState& state, WalkBuffers& buffers, RangeWalkOrder order,
	std::uint32_t light, ObjectHandle object, CallOriginal&& callOriginal, SquareRoot&& squareRoot, Report&& report) {
	const DepthScope scope(state);
	if (scope.Before() != 0) {
		return callOriginal(light, object);
	}
	const std::uint32_t gate = memory.U32(walk_engine::kWalkGateAddress);
	const std::uint32_t source = memory.U32(light + walk_engine::kSourceLightOffset);
	const EntryAction action = DecideEntry(scope.Before(), gate, object, source);
	if (action != EntryAction::Compare) {
		const std::uint32_t eax = callOriginal(light, object);
		report(WalkReport{action, Comparison{}, walk::SourceMode::NoSource, light, object, 0, order});
		return eax;
	}

	const walk::LightValues values = CaptureLight(memory, light);
	const auto runOriginal = [&] {
		buffers.original.Clear();
		state.recordingLight = light;
		state.recording = &buffers.original.adds;
		const std::uint32_t eax = callOriginal(light, object);
		state.recordingLight = 0;
		state.recording = nullptr;
		buffers.original.result = (eax & 0xFFu) != 0;  // AL (read, C1 of 0x007D59E0)
		buffers.original.maskAfter = memory.U32(light + walk_engine::kActivePlanesOffset);
		return eax;
	};
	const auto runRebuild = [&](std::vector<AddGeometryEvent>& adds, std::vector<TraceEvent>* trace) {
		MemoryScene<Memory> scene{memory, &adds};
		if (trace == nullptr) {
			walk::NoWalkObserver observer;
			return walk::BatchedWalk(values, scene, object, squareRoot, buffers.scratch, observer);
		}
		trace->clear();
		TracingScene<MemoryScene<Memory>> traced{scene, trace};
		TracingObserver observer{trace};
		return walk::BatchedWalk(values, traced, object, squareRoot, buffers.scratch, observer);
	};

	std::uint32_t eax = 0;
	buffers.rebuild.Clear();
	if (order == RangeWalkOrder::RebuildFirst) {
		buffers.rebuild.result = runRebuild(buffers.rebuild.adds, &buffers.traceFresh);
		eax = runOriginal();
	} else {
		eax = runOriginal();
		buffers.rebuild.result = runRebuild(buffers.rebuild.adds, nullptr);
	}
	buffers.rebuild.maskAfter = values.activePlanes;  // BatchedWalk never writes +0x1B0 (R4)

	const walk::SourceMode mode = walk::SelectSourceMode(values.hasSource, values.sourceRtti, values.planeSource);
	WalkReport result{action, Compare(buffers.original, buffers.rebuild), mode, light, object, values.activePlanes, order};
	if (order == RangeWalkOrder::RebuildFirst && result.comparison.Mismatch()) {
		buffers.afterAdds.clear();
		runRebuild(buffers.afterAdds, &buffers.traceAfter);
		result.trace = FirstTraceDifference(buffers.traceFresh, buffers.traceAfter);
		result.lightChanged = !SameLightValues(values, CaptureLight(memory, light));
	}
	report(result);
	return eax;
}

}  // namespace obng::plugin::range_walk
