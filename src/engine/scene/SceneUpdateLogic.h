#pragma once
// The scene graph update of one subtree - NiAVObject::Update 0x00707370 as the
// actor update calls it from ActorAnimData::ApplyAnimData 0x00471F20 (one
// skeleton a call, the controllers detached) - over plain values and ports.
// Two implementations with one contract:
//
//   Reference  the original's recursion as read (re/functions/0x0070A0D0.md):
//              NiNode::UpdateDownwardPass 0x0070A0D0 and NiAVObject::
//              UpdateDownwardPass 0x00707580, each node's controllers, world
//              transform and bound in the original's order. The oracle for
//              the flat run, nothing more.
//   Run        the replacement: the same calls on the same objects in the same
//              order, driven by a flat array built once per subtree (Build)
//              and checked against the tree before every use (Matches) - no
//              recursion, no virtual call per node for the compose and the
//              bound merge decisions.
//
// The contract both keep (the in-game gate compares exactly these):
//   - per object, in the original's depth-first order: the controller update
//     when asked and present, the world transform written (bit for bit:
//     Compose below is 0x0053D7A0's x87 order), the collision object's
//     update, the geometry's bound update;
//   - per node, after its children: the bound - radius 0, then each child in
//     index order copied or merged by MergeStepFor;
//   - nothing else touched.
//
// Every claim carries a mark (AGENTS.md): read = seen in the instructions
// (address given), derived = follows from read facts, unverified = not
// checked. Names are proposals (NiMain's are Gamebryo's public names, taken
// from the vtable order and the bodies); the address stays next to each.
//
// 64-bit clean: objects cross the ports as std::uint32_t handles; offsets and
// sizes are the ports' business (SceneUpdate.h). Nothing here depends on the
// pointer size.
#include <xmmintrin.h>

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace obng::engine::scene::scene_update {

using Handle = std::uint32_t;
inline constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;

// ---- The values -----------------------------------------------------------------------

// NiTransform as the engine lays it out: a row-major 3x3 rotation, the
// translation, the scale (read: 0x0053D7A0 reads +0x00..+0x20, +0x24..+0x2C,
// +0x30 of both operands; 0x0070C120 copies 13 dwords).
struct Transform {
	float rot[9];
	float pos[3];
	float scale;
};
static_assert(sizeof(Transform) == 0x34, "NiTransform is 13 floats");

// NiBound: centre, radius (read: 0x0070A0D0 copies +0x20..+0x2C, tests +0x2C).
struct Bound {
	float center[3];
	float radius;
};
static_assert(sizeof(Bound) == 0x10, "NiBound is 4 floats");

// ---- The arithmetic, in the original's order --------------------------------------------
//
// The exe runs these on the x87 under the game's control words (0x007F and
// 0x0C7F, knowledge/frame-clock.md): precision control 24 bits, so every
// product and every sum rounds to a float - the same result as float
// arithmetic without contraction, product by product and sum by sum, as
// long as nothing over- or underflows the float's exponent on the way
// (derived; the oracle test runs the original's bytes against these on
// random inputs). The grouping of the three-term sums is the instructions'
// (read, re/export of 0x007100A0 and 0x007101F0): it differs per element and
// is kept exactly, because float addition is commutative but not associative.

// 0x007100A0 NiMatrix3 multiply: out = left * right (left in ECX, right in
// EDX; read). Row-major: m[r*3+c].
inline void MultiplyMatrix(const float* left, const float* right, float* out) {
	const float* l = left;
	const float* r = right;
	// Each line is one element in the instructions' order: the first two
	// products are summed first (faddp), the third added to that sum.
	out[0] = (r[0] * l[0] + r[3] * l[1]) + r[6] * l[2];  // 0x007100A4..BC
	out[3] = (l[4] * r[3] + l[3] * r[0]) + r[6] * l[5];  // 0x007100BE..D3
	out[6] = (l[7] * r[3] + l[6] * r[0]) + r[6] * l[8];  // 0x007100D6..EB
	out[1] = (r[1] * l[0] + r[4] * l[1]) + l[2] * r[7];  // 0x007100EE..0103
	out[4] = (r[4] * l[4] + r[1] * l[3]) + r[7] * l[5];  // 0x00710106..011C
	out[7] = (r[4] * l[7] + l[6] * r[1]) + r[7] * l[8];  // 0x0071011F..0135
	out[2] = (r[2] * l[0] + r[5] * l[1]) + l[2] * r[8];  // 0x00710138..014D
	out[5] = (r[5] * l[4] + r[2] * l[3]) + r[8] * l[5];  // 0x00710150..0166
	out[8] = (r[5] * l[7] + l[6] * r[2]) + r[8] * l[8];  // 0x00710169..017F
}

// 0x007101F0 NiMatrix3 times NiPoint3: out = rot * v (rot in ECX, v in EDX;
// read). The first element sums the second product first.
inline void TransformPoint(const float* rot, const float* v, float* out) {
	out[0] = (rot[1] * v[1] + v[0] * rot[0]) + rot[2] * v[2];  // 0x007101F4..020C
	out[1] = (rot[3] * v[0] + rot[4] * v[1]) + rot[5] * v[2];  // 0x0071020E..0223
	out[2] = (rot[6] * v[0] + rot[7] * v[1]) + rot[8] * v[2];  // 0x00710226..023B
}

// 0x0053D7A0 NiTransform compose: out = parent * local (parent in ECX; read).
// scale first, then the rotation, then the translation: parent.scale times
// (parent.rot * local.pos), plus parent.pos, component by component.
inline void Compose(const Transform& parent, const Transform& local, Transform& out) {
	out.scale = local.scale * parent.scale;  // 0x0053D7A7/B4
	MultiplyMatrix(parent.rot, local.rot, out.rot);
	float v[3];
	TransformPoint(parent.rot, local.pos, v);
	const float scaled[3] = {v[0] * parent.scale, v[1] * parent.scale, v[2] * parent.scale};  // 0x0053D7ED..0809
	out.pos[0] = scaled[0] + parent.pos[0];  // 0x0053D80D..0814
	out.pos[1] = parent.pos[1] + scaled[1];  // 0x0053D81C..0823
	out.pos[2] = parent.pos[2] + scaled[2];  // 0x0053D82B..083A
}

// 0x0070C120 NiAVObject::UpdateWorldData's transform part: the world is the
// local composed onto the parent's world, or the local itself without a
// parent (read: TEST [this+0x1C]; the copy of 13 dwords to +0x64).
inline void UpdateWorld(const Transform* parentWorld, const Transform& local, Transform& world) {
	if (parentWorld == nullptr) {
		world = local;
		return;
	}
	Compose(*parentWorld, local, world);
}

// ---- The bounds' arithmetic, in the original's order (phase 2) ----------------------------------
//
// Read 2026-11-09 in the instructions of NiBound::Merge 0x0072A6B0, the bound
// transform 0x0072A820, the transform inverse 0x00718A80 (with the matrix
// transpose 0x00710400), the skin bound 0x0072BB30 and NiGeometry::
// UpdateWorldBound 0x00722AA0 (re/functions/0x0070A0D0.md). As above: every
// product, sum and quotient rounds to a float; the square root is the CRT's
// _CIsqrt (one FSQRT at precision 24 under the caller's rounding: a
// correctly rounded float root, which std::sqrt on a float is as well, under
// the MXCSR's rounding that RoundingFromX87 copies).

// 0x0072A820: the bound carried by a transform - centre = T.pos + T.scale *
// (T.rot * centre), component by component as 0x0053D7A0's position part;
// radius = radius * T.scale (read).
inline void TransformBound(const Bound& bound, const Transform& by, Bound& out) {
	float v[3];
	TransformPoint(by.rot, bound.center, v);
	const float scaled[3] = {v[0] * by.scale, v[1] * by.scale, v[2] * by.scale};
	out.center[0] = by.pos[0] + scaled[0];
	out.center[1] = by.pos[1] + scaled[1];
	out.center[2] = by.pos[2] + scaled[2];
	out.radius = bound.radius * by.scale;
}

// 0x00718A80: the inverse of a transform - the rotation transposed
// (0x00710400), the scale 1/s, the position -(rot^T * pos) * (1/s): the
// negation first, then the transposed rotation applied, then each
// component times the inverse scale (read).
inline void InverseTransform(const Transform& t, Transform& out) {
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			out.rot[r * 3 + c] = t.rot[c * 3 + r];
		}
	}
	out.scale = 1.0f / t.scale;
	const float negated[3] = {-t.pos[0], -t.pos[1], -t.pos[2]};
	float v[3];
	TransformPoint(out.rot, negated, v);
	out.pos[0] = v[0] * out.scale;
	out.pos[1] = v[1] * out.scale;
	out.pos[2] = v[2] * out.scale;
}

// The square root as the CRT's _CIsqrt 0x00982C30 computes it (read,
// re/functions/0x00982C30.md): one FSQRT under (word & 0x0300) | 0x007F -
// the caller's precision control kept, the rounding control cleared to
// nearest. So under the game's 0x0C7F (every other operation rounded toward
// zero) the root alone is rounded to nearest. SSE's sqrtss follows the
// MXCSR's rounding, which RoundingFromX87 sets to the caller's; this takes
// that root and moves it to the nearest float where the mode was not
// nearest: the exact root lies between the directed result and its
// neighbour on the other side, and the midpoint's square (exact in a double:
// 25 significant bits squared) decides, ties to even. NaN, zero and
// infinity are the same in every mode.
inline float SqrtAsCrt(float x) {
	const float r = std::sqrt(x);
	const unsigned rounding = (_mm_getcsr() >> 13) & 3u;  // 0 nearest, 1 down, 2 up, 3 toward zero
	if (rounding == 0 || !(x > 0.0f) || std::isinf(x)) {
		return r;
	}
	const bool below = rounding != 2;  // down and toward zero leave r <= the exact root
	const float other = std::nextafter(r, below ? INFINITY : -INFINITY);
	const double midpoint = (static_cast<double>(r) + static_cast<double>(other)) * 0.5;
	const double square = midpoint * midpoint;
	const double exact = static_cast<double>(x);
	if (square == exact) {  // a tie: the even significand
		std::uint32_t bits;
		std::memcpy(&bits, &r, 4);
		return (bits & 1u) == 0u ? r : other;
	}
	if (below) {
		return square < exact ? other : r;
	}
	return square > exact ? other : r;
}

// 0x0072A6B0 NiBound::Merge(const NiBound& other), on `self` (read, with the
// FCOM parity decisions spelt out):
//   d = self.centre - other.centre; distSq = (d0*d0 + d1*d1) + d2*d2;
//   rdiff = other.radius - self.radius; rdiffSq = rdiff*rdiff.
//   rdiff < 0 or NaN (0x0072A724 taken): other may lie inside self - when
//     distSq <= rdiffSq or either is NaN, nothing changes; else the root path.
//   rdiff >= 0: self may lie inside other - when distSq < rdiffSq or equal,
//     self becomes other; else (greater or NaN) the root path.
//   The root path: dist = sqrt(distSq); when dist <= 1e-6f or NaN, only the
//     radius changes; else t = (dist - rdiff) / (dist + dist) and the centre
//     is other.centre + d*t, component by component. The radius is
//     ((dist + other.radius) + self.radius) * 0.5 either way (a double 0.5 at
//     0x00A2FAA0: exact, so the float product is the same).
inline void MergeBound(Bound& self, const Bound& other) {
	const float d[3] = {self.center[0] - other.center[0], self.center[1] - other.center[1], self.center[2] - other.center[2]};
	const float distSq = (d[0] * d[0] + d[1] * d[1]) + d[2] * d[2];
	const float rdiff = other.radius - self.radius;
	const float rdiffSq = rdiff * rdiff;
	if (rdiff < 0.0f || std::isnan(rdiff)) {
		if (!(distSq > rdiffSq)) {  // less, equal or unordered: other inside self (0x0072A7F2..0x0072A810)
			return;
		}
	} else {
		if (distSq < rdiffSq || distSq == rdiffSq) {  // self inside other (0x0072A72F..0x0072A74C)
			self = other;
			return;
		}
	}
	const float dist = SqrtAsCrt(distSq);
	if (dist > 1.0e-6f) {  // 0x0072A764..0x0072A771: 1e-6 compared with dist, the radius-only path for less-or-equal and NaN
		const float t = (dist - rdiff) / (dist + dist);
		const float scaled[3] = {d[0] * t, d[1] * t, t * d[2]};
		self.center[0] = other.center[0] + scaled[0];
		self.center[1] = scaled[1] + other.center[1];
		self.center[2] = other.center[2] + scaled[2];
	}
	self.radius = ((dist + other.radius) + self.radius) * 0.5f;
}

// 0x0072BB30: a skinned geometry's bound in the skin's own space - every
// bone's bind bound carried by the bone's world transform, merged in bone
// order from the first, then carried into the skin's space by the
// skin-to-root transform composed onto the inverse of the root parent's
// world (read). The bones and the per-bone bounds come through the port.
template <class Skin>
concept SkinPort = requires(const Skin& s, std::uint32_t i) {
	{ s.BoneCount() } -> std::same_as<std::uint32_t>;
	{ s.BoneBound(i) } -> std::same_as<Bound>;        // the bind bound of bone i ([skin data +0x44] + i*0x4C + 0x34: the records sit behind a pointer)
	{ s.BoneWorld(i) } -> std::same_as<Transform>;    // the bone node's world (+0x64)
	{ s.RootParentWorld() } -> std::same_as<Transform>;  // the skin's root parent's world (+0x10 -> +0x64)
	{ s.SkinToRoot() } -> std::same_as<Transform>;    // skin data +0x0C
};

template <SkinPort Skin>
inline Bound SkinBound(const Skin& skin) {
	Bound acc;
	TransformBound(skin.BoneBound(0), skin.BoneWorld(0), acc);
	const std::uint32_t count = skin.BoneCount();
	for (std::uint32_t i = 1; i < count; ++i) {
		Bound bone;
		TransformBound(skin.BoneBound(i), skin.BoneWorld(i), bone);
		MergeBound(acc, bone);
	}
	Transform inverse;
	InverseTransform(skin.RootParentWorld(), inverse);
	Transform toSkin;
	Compose(skin.SkinToRoot(), inverse, toSkin);
	Bound out;
	TransformBound(acc, toSkin, out);
	return out;
}

// 0x00722AA0 NiGeometry::UpdateWorldBound's flag step (read): with bit 7 of
// the flags set, the new bound is compared with the old one field by field
// (0x0072A0A0: radius, then the centre; a NaN compares unequal) and the bit
// is cleared when they differ; with the bit clear nothing happens.
inline std::uint16_t GeometryBoundFlags(std::uint16_t flags, const Bound& before, const Bound& after) {
	if ((flags & 0x0080) == 0) {
		return flags;
	}
	const bool same = after.radius == before.radius && after.center[0] == before.center[0] && after.center[1] == before.center[1] &&
	                  after.center[2] == before.center[2];
	return same ? static_cast<std::uint16_t>(flags | 0x0080) : static_cast<std::uint16_t>(flags & 0xFF7F);
}

// ---- The bound merge's decision ---------------------------------------------------------
//
// Read in 0x0070A0D0 (0x0070A12F..0x0070A16E) and the same in NiNode::
// UpdateWorldBound 0x0070A360: FLDZ; FCOM child.radius; TEST AH,0x44; JNP
// skip - FCOM leaves C3 alone for equal (AH & 0x44 = 0x40, odd parity, the
// jump taken: the child is skipped when its radius is 0.0), C0 or nothing
// for less or greater (0x00: even parity, not taken), and C3, C2 and C0 for
// unordered (0x44: even parity, not taken - a NaN radius is not skipped).
// FCOMP own.radius; TEST AH,0x44; JP merge - taken for a node radius that
// is non-zero (0x00) or NaN (0x44): the child's bound is merged (NiBound::
// Merge 0x0072A6B0); for a node radius of 0.0 the child's bound is copied
// over the node's. The sign of a zero plays no part (-0.0 compares equal).
// The NaN cases were read wrong at first (as skips and copies) and shown
// wrong by the gate run of 2026-11-09 16:53: an actor whose child bound
// carried a NaN radius had it merged by the original.
enum class MergeStep { Skip, Copy, Merge };

inline MergeStep MergeStepFor(float ownRadius, float childRadius) {
	if (childRadius == 0.0f) {
		return MergeStep::Skip;
	}
	if (ownRadius == 0.0f) {
		return MergeStep::Copy;
	}
	return MergeStep::Merge;
}

// ---- The objects' kinds --------------------------------------------------------------------
//
// What the flat run handles itself is decided by three vtable slots (read:
// the exe's .rdata, scanned for the update slots on 2026-11-09, knowledge/
// render-thread-outside-the-pass.md):
//   +0x60 UpdateDownwardPass: NiNode's 0x0070A0D0 or NiAVObject's 0x00707580;
//   +0x74 UpdateWorldData: 0x0070C120 (one node class overrides it: 0x007416C0;
//         cameras, lights and particle systems have their own);
//   +0x78 UpdateWorldBound: NiNode's 0x0070A360 for a node; a geometry's own
//         (0x00722AA0 NiGeometry's, 0x00864610 a BS variant) stays a call.
// Everything else - BSFaceGenNiNode 0x0055D4C0, the particle and other
// overrides of +0x60, any +0x74 or +0x78 the run does not know - is Other: the
// original's virtual UpdateDownwardPass runs its whole subtree, and the run
// merges the bound it leaves.
inline constexpr std::uint32_t kNiNodeUpdateDownwardPass = 0x0070A0D0;
inline constexpr std::uint32_t kNiAVObjectUpdateDownwardPass = 0x00707580;
inline constexpr std::uint32_t kNiAVObjectUpdateWorldData = 0x0070C120;
inline constexpr std::uint32_t kNiNodeUpdateWorldBound = 0x0070A360;
inline constexpr std::uint32_t kNiGeometryUpdateWorldBound = 0x00722AA0;

// GeometryPlain: a geometry whose bound is NiGeometry's own (0x00722AA0) -
// the run computes it (GeometryBound below); Geometry: a geometry with
// another bound update (the BS variant 0x00864610) - the run calls it.
enum class Kind : std::uint8_t { Node, GeometryPlain, Geometry, Other };

struct VtableSlots {
	std::uint32_t updateDownwardPass;  // +0x60
	std::uint32_t updateWorldData;     // +0x74
	std::uint32_t updateWorldBound;    // +0x78
};

inline Kind Classify(const VtableSlots& s) {
	if (s.updateWorldData != kNiAVObjectUpdateWorldData) {
		return Kind::Other;
	}
	if (s.updateDownwardPass == kNiNodeUpdateDownwardPass && s.updateWorldBound == kNiNodeUpdateWorldBound) {
		return Kind::Node;
	}
	if (s.updateDownwardPass == kNiAVObjectUpdateDownwardPass) {
		return s.updateWorldBound == kNiGeometryUpdateWorldBound ? Kind::GeometryPlain : Kind::Geometry;
	}
	return Kind::Other;
}

// The whole of 0x00722AA0 NiGeometry::UpdateWorldBound (read): the bound in
// the object's space - the skin's (SkinBound) when the geometry has a skin
// instance, the model data's otherwise - carried by the world transform
// into the world bound; then the flag step on bit 7 against the old bound.
template <class Geometry>
concept GeometryPort = requires(const Geometry& g) {
	{ g.HasSkin() } -> std::same_as<bool>;
	{ g.ModelBound() } -> std::same_as<Bound>;  // [+0xB4] + 0x0C
	{ g.World() } -> std::same_as<Transform>;   // +0x64
	{ g.WorldBound() } -> std::same_as<Bound>;  // +0x20, before
	{ g.Flags() } -> std::same_as<std::uint16_t>;  // +0x18
};

template <GeometryPort Geometry, SkinPort Skin>
inline void GeometryBound(const Geometry& geometry, const Skin& skin, Bound& boundOut, std::uint16_t& flagsOut) {
	const Bound before = geometry.WorldBound();
	const Bound local = geometry.HasSkin() ? SkinBound(skin) : geometry.ModelBound();
	TransformBound(local, geometry.World(), boundOut);
	flagsOut = GeometryBoundFlags(geometry.Flags(), before, boundOut);
}

// ---- The flat order ------------------------------------------------------------------------
//
// One entry per object the run visits, in the original's depth-first order
// (a node, then its children in index order, each with its subtree). A
// node's children (the non-null ones, in index order) are listed in
// `children` from `childrenBegin`, `childCount` of them, as entry indices.
// An Other entry has no children here: its subtree is the original's.
struct Entry {
	Handle object = 0;
	std::uint32_t parent = kNoParent;  // entry index
	Kind kind = Kind::Other;
	std::uint32_t childrenBegin = 0;
	std::uint32_t childCount = 0;
	std::uint32_t slotCount = 0;  // the node's array length (+0xB6), nulls included: part of what Matches checks
};

struct Order {
	Handle root = 0;
	std::vector<Entry> entries;
	std::vector<std::uint32_t> children;
	// Per entry 1 when the run leaves it out (UnusedBones below); empty: every
	// entry is run. Belongs to the shape Matches checks only as far as the
	// tree goes - whoever fills it fills it when the order is built.
	std::vector<std::uint8_t> unused;
	std::uint32_t unusedCount = 0;  // the unused entries found for this order, left out or only counted
	// This root's calls and whether it has had an update (ActorFarRateLogic.h State; kept here so that it lives with the order).
	std::uint32_t farCalls = 0;
	bool farRan = false;
};

// The tree port Build and Matches read through:
//   Kind     KindOf(Handle)                  from the object's vtable slots
//   uint32_t SlotCount(Handle)               a node's +0xB6 (0 for anything but a node)
//   Handle   ChildAt(Handle, uint32_t index) the node's child, 0 for an empty slot
template <class Tree>
concept TreePort = requires(const Tree& t, Handle h, std::uint32_t i) {
	{ t.KindOf(h) } -> std::same_as<Kind>;
	{ t.SlotCount(h) } -> std::same_as<std::uint32_t>;
	{ t.ChildAt(h, i) } -> std::same_as<Handle>;
};

template <TreePort Tree>
Order Build(const Tree& tree, Handle root) {
	Order order;
	order.root = root;
	// Depth-first with an explicit stack of (object, parent entry); the
	// children are pushed last to first so they come out in index order.
	struct Pending {
		Handle object;
		std::uint32_t parent;
	};
	std::vector<Pending> stack;
	stack.push_back({root, kNoParent});
	while (!stack.empty()) {
		const Pending p = stack.back();
		stack.pop_back();
		Entry e;
		e.object = p.object;
		e.parent = p.parent;
		e.kind = tree.KindOf(p.object);
		const std::uint32_t index = static_cast<std::uint32_t>(order.entries.size());
		if (e.kind == Kind::Node) {
			e.slotCount = tree.SlotCount(p.object);
			std::vector<Handle> kids;
			for (std::uint32_t i = 0; i < e.slotCount; ++i) {
				const Handle child = tree.ChildAt(p.object, i);
				if (child != 0) {
					kids.push_back(child);
				}
			}
			e.childCount = static_cast<std::uint32_t>(kids.size());
			for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
				stack.push_back({*it, index});
			}
		}
		order.entries.push_back(e);
	}
	// The children lists: every entry's children are the entries that name
	// it as their parent, in entry order (which is index order: the stack
	// above hands them out that way).
	order.children.resize(order.entries.size() == 0 ? 0 : order.entries.size() - 1);
	std::vector<std::uint32_t> fill(order.entries.size(), 0);
	std::uint32_t at = 0;
	for (std::uint32_t i = 0; i < order.entries.size(); ++i) {
		order.entries[i].childrenBegin = at;
		at += order.entries[i].childCount;
	}
	for (std::uint32_t i = 1; i < order.entries.size(); ++i) {
		const std::uint32_t parent = order.entries[i].parent;
		order.children[order.entries[parent].childrenBegin + fill[parent]] = i;
		++fill[parent];
	}
	return order;
}

// True when the tree still has the shape the order was built from: every
// entry's kind, every node's slot count and its non-null children, handle
// for handle in index order. A tree that changed under a stale order would
// give the wrong result, so a false here sends the call to the original.
template <TreePort Tree>
bool Matches(const Order& order, const Tree& tree) {
	for (std::uint32_t i = 0; i < order.entries.size(); ++i) {
		const Entry& e = order.entries[i];
		if (tree.KindOf(e.object) != e.kind) {
			return false;
		}
		if (e.kind != Kind::Node) {
			continue;
		}
		if (tree.SlotCount(e.object) != e.slotCount) {
			return false;
		}
		std::uint32_t seen = 0;
		for (std::uint32_t s = 0; s < e.slotCount; ++s) {
			const Handle child = tree.ChildAt(e.object, s);
			if (child == 0) {
				continue;
			}
			if (seen >= e.childCount || order.entries[order.children[e.childrenBegin + seen]].object != child) {
				return false;
			}
			++seen;
		}
		if (seen != e.childCount) {
			return false;
		}
	}
	return true;
}

// ---- The run -----------------------------------------------------------------------------
//
// The action port both implementations drive. Every call maps to one thing
// the original does on the object; the engine port does it on the object's
// memory (SceneUpdate.h), the test port records it.
//   bool HasControllers(Handle)              +0x9C property list head or +0x0C controller non-null (read 0x0047C930)
//   void UpdateObjectControllers(Handle, float time)   0x0047C930(time, 1)
//   const Transform* ParentWorld(Handle)     the parent's +0x64, nullptr without a parent (read 0x0070C120)
//   Transform Local(Handle)                  +0x30
//   void WriteWorld(Handle, const Transform&)  +0x64
//   void AfterWorldData(Handle)              the collision object's update when +0xA8 is set (read 0x0070C151..66)
//   void UpdateWorldBound(Handle)            the geometry's own +0x78
//   void UpdateDownwardOriginal(Handle, float time, bool controllers)   the virtual +0x60 of an Other object
//   float BoundRadius(Handle)                +0x2C
//   void SetBoundRadius(Handle, float)       +0x2C
//   void CopyBound(Handle to, Handle from)   16 bytes +0x20
//   void MergeBound(Handle to, Handle from)  NiBound::Merge 0x0072A6B0 on to's bound with from's (the Reference's; Run merges itself)
//   Bound ReadBound(Handle)                  +0x20
//   void WriteBound(Handle, const Bound&)    +0x20
//   void GeometryBoundSelf(Handle)           the run's own 0x00722AA0 (GeometryBound over the object's memory: the
//                                            bound at +0x20 and the flags word at +0x18 written)
template <class Port>
concept ActionPort = requires(Port& p, Handle h, float f, bool b) {
	{ p.HasControllers(h) } -> std::same_as<bool>;
	{ p.UpdateObjectControllers(h, f) };
	{ p.ParentWorld(h) } -> std::same_as<const Transform*>;
	{ p.Local(h) } -> std::same_as<Transform>;
	{ p.WriteWorld(h, std::declval<const Transform&>()) };
	{ p.AfterWorldData(h) };
	{ p.UpdateWorldBound(h) };
	{ p.UpdateDownwardOriginal(h, f, b) };
	{ p.BoundRadius(h) } -> std::same_as<float>;
	{ p.SetBoundRadius(h, f) };
	{ p.CopyBound(h, h) };
	{ p.MergeBound(h, h) };
	{ p.ReadBound(h) } -> std::same_as<Bound>;
	{ p.WriteBound(h, std::declval<const Bound&>()) };
	{ p.GeometryBoundSelf(h) };
};

// Tried and dropped (run 20261109-191349 against -191033): a software
// prefetch of the object three entries ahead (its three cache lines) changed
// nothing within the run-to-run spread; the cost per node (about 105 ns
// against some 40 of arithmetic) is not the object's own lines' latency.

// One object's world data: 0x0070C120 as the port sees it.
template <ActionPort Port>
inline void WorldData(Port& port, Handle object) {
	Transform world;
	UpdateWorld(port.ParentWorld(object), port.Local(object), world);
	port.WriteWorld(object, world);
	port.AfterWorldData(object);
}

// The original's recursion (read, 0x0070A0D0 and 0x00707580), as the oracle
// of Run: kind decided per object through the tree port.
template <TreePort Tree, ActionPort Port>
void Reference(const Tree& tree, Port& port, Handle object, float time, bool controllers) {
	const Kind kind = tree.KindOf(object);
	if (kind == Kind::Other) {
		port.UpdateDownwardOriginal(object, time, controllers);
		return;
	}
	if (controllers && port.HasControllers(object)) {
		port.UpdateObjectControllers(object, time);
	}
	WorldData(port, object);
	if (kind == Kind::Geometry || kind == Kind::GeometryPlain) {
		port.UpdateWorldBound(object);
		return;
	}
	port.SetBoundRadius(object, 0.0f);
	const std::uint32_t slots = tree.SlotCount(object);
	for (std::uint32_t i = 0; i < slots; ++i) {
		const Handle child = tree.ChildAt(object, i);
		if (child == 0) {
			continue;
		}
		Reference(tree, port, child, time, controllers);
		switch (MergeStepFor(port.BoundRadius(object), port.BoundRadius(child))) {
			case MergeStep::Skip:
				break;
			case MergeStep::Copy:
				port.CopyBound(object, child);
				break;
			case MergeStep::Merge:
				port.MergeBound(object, child);
				break;
		}
	}
}

// The flat run: the same calls in the same order, from the order. Two
// passes: forwards for the controllers, the world data and the geometry
// bounds (every parent's world is written before its children read it:
// depth-first order), backwards for the nodes' bounds (every child's bound
// is final before its parent merges it). The merge sequence per node is the
// original's: radius 0, then the children in index order - the original
// interleaves it with the children's subtrees, which changes no value
// (derived: a merge reads only the node's and that child's bound).
template <ActionPort Port>
void RunForward(const Order& order, Port& port, float time, bool controllers) {
	const bool prune = !order.unused.empty();
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		const Entry& e = order.entries[i];
		if (prune && order.unused[i] != 0) {
			continue;  // a bone nothing hangs on: no controllers, no world
		}
		if (e.kind == Kind::Other) {
			port.UpdateDownwardOriginal(e.object, time, controllers);
			continue;
		}
		if (controllers && port.HasControllers(e.object)) {
			port.UpdateObjectControllers(e.object, time);
		}
		WorldData(port, e.object);
		if (e.kind == Kind::GeometryPlain) {
			port.GeometryBoundSelf(e.object);
		} else if (e.kind == Kind::Geometry) {
			port.UpdateWorldBound(e.object);
		}
	}
}

// The nodes' bounds, children first, merged here (MergeBound: 0x0072A6B0's
// arithmetic) on a copy of the node's bound that is written once.
template <ActionPort Port>
void RunBounds(const Order& order, Port& port) {
	const bool prune = !order.unused.empty();
	for (std::size_t n = order.entries.size(); n-- > 0;) {
		const Entry& e = order.entries[n];
		if (e.kind != Kind::Node || (prune && order.unused[n] != 0)) {
			continue;
		}
		Bound own = port.ReadBound(e.object);
		own.radius = 0.0f;
		for (std::uint32_t c = 0; c < e.childCount; ++c) {
			const std::uint32_t childEntry = order.children[e.childrenBegin + c];
			if (prune && order.unused[childEntry] != 0) {
				continue;  // in the original its radius is 0 (no geometry below it): MergeStep::Skip
			}
			const Bound child = port.ReadBound(order.entries[childEntry].object);
			switch (MergeStepFor(own.radius, child.radius)) {
				case MergeStep::Skip:
					break;
				case MergeStep::Copy:
					own = child;
					break;
				case MergeStep::Merge:
					MergeBound(own, child);
					break;
			}
		}
		port.WriteBound(e.object, own);
	}
}

// ---- Bones nothing hangs on ----------------------------------------------------------------
//
// A replaced skeleton can carry several times the game's bones (read
// 2026-11-11: 295 nodes against 71), most of them for meshes an actor does
// not wear. Such a bone's controllers and world transform are computed every
// frame and read by nobody. An entry is unused when it is a node other than
// the root, the port says its name is one that may be left out (a list of
// names no skeleton of the game has: nothing in the engine can ask for it by
// name), it has no collision object, no geometry of the tree is skinned to
// it, and every child is unused too - so the whole subtree is nodes of that
// kind, without geometry, and in the original its bound's radius is 0.
// Leaving it out changes no value of any other entry (derived: a child
// depends on its parent, never the other way; the bound merge skips a radius
// of 0), provided its controllers write nothing but the node itself.
//   bool MayBeUnused(Handle)    by name
//   bool HasCollision(Handle)   +0xA8 set
//   bool IsSkinBone(Handle)     a bone or the root parent of a skin instance in this tree
template <class Port>
concept BonePort = requires(const Port& p, Handle h) {
	{ p.MayBeUnused(h) } -> std::same_as<bool>;
	{ p.HasCollision(h) } -> std::same_as<bool>;
	{ p.IsSkinBone(h) } -> std::same_as<bool>;
};

template <BonePort Port>
std::vector<std::uint8_t> UnusedBones(const Order& order, const Port& port) {
	std::vector<std::uint8_t> unused(order.entries.size(), 0);
	// Backwards: an entry's children all come after it.
	for (std::size_t n = order.entries.size(); n-- > 1;) {
		const Entry& e = order.entries[n];
		if (e.kind != Kind::Node || !port.MayBeUnused(e.object) || port.HasCollision(e.object) || port.IsSkinBone(e.object)) {
			continue;
		}
		bool all = true;
		for (std::uint32_t c = 0; c < e.childCount && all; ++c) {
			all = unused[order.children[e.childrenBegin + c]] != 0;
		}
		unused[n] = all ? 1 : 0;
	}
	return unused;
}

inline std::size_t CountUnused(const std::vector<std::uint8_t>& unused) {
	std::size_t n = 0;
	for (std::uint8_t u : unused) {
		n += u != 0 ? 1u : 0u;
	}
	return n;
}

template <ActionPort Port>
void Run(const Order& order, Port& port, float time, bool controllers) {
	RunForward(order, port, time, controllers);
	RunBounds(order, port);
}

// ---- The selected update: the cells' pass (read 2026-11-09) -----------------------------------
//
// TESObjectCELL::UpdateSelectedNodes 0x004D4970 runs 0x007073A0 on a cell's
// roots: the root's +0x64 and then the parent's +0x94. NiNode::
// UpdateSelectedDownwardPass 0x0070A190: UpdateObjectControllers(time, bit 3
// of the flags) - the function walks the property list only for the bit
// but the controller chain always; UpdateWorldData when bit 2; then for
// every child: when the child's bit 1 is set, its +0x64 (or its +0x68, the
// rigid pass, when its bit 4 is set too); then the child's bound merged as
// the full pass does, visited or not. NiAVObject::UpdateSelectedDownwardPass
// 0x007075B0 (a geometry): the controllers as above, and when bit 2 the
// world data and the world bound. The rigid pass 0x0070A280 is left to the
// original (its own bound at +0xCC, no merges): an Other of this run.
//
// The flags are read live by Run, as the original reads them; the shape
// the order rests on includes each child's bit 1 (the visited set), so a
// controller that flags or unflags a subtree makes the order stale and
// Matches sends the call to a rebuild.
inline constexpr std::uint16_t kFlagVisit = 0x0002;        // bit 1: the selective update reaches this child
inline constexpr std::uint16_t kFlagTransforms = 0x0004;   // bit 2: world data (and a geometry's bound) on a visit
inline constexpr std::uint16_t kFlagControllers = 0x0008;  // bit 3: the property list's controllers on a visit
inline constexpr std::uint16_t kFlagRigid = 0x0010;        // bit 4: the rigid pass instead of the selected one

struct SelectedEntry {
	Handle object = 0;
	std::uint32_t parent = kNoParent;
	Kind kind = Kind::Other;  // Other here also covers a rigid node (bit 4): the original's +0x68
	bool rigid = false;
	std::uint32_t childrenBegin = 0;  // all non-null children of a node, in index order: entry indices for the visited, handles for the rest
	std::uint32_t childCount = 0;
	std::uint32_t slotCount = 0;
};

// A child of a visited node: visited (an entry) or merge-only (a handle).
struct SelectedChild {
	Handle object = 0;
	std::uint32_t entry = kNoParent;  // kNoParent for a child not visited
};

struct SelectedOrder {
	Handle root = 0;
	std::vector<SelectedEntry> entries;
	std::vector<SelectedChild> children;
};

// The tree port of the selected update adds the flags.
template <class Tree>
concept SelectedTreePort = TreePort<Tree> && requires(const Tree& t, Handle h) {
	{ t.Flags(h) } -> std::same_as<std::uint16_t>;
};

template <SelectedTreePort Tree>
SelectedOrder BuildSelected(const Tree& tree, Handle root) {
	SelectedOrder order;
	order.root = root;
	struct Pending {
		Handle object;
		std::uint32_t parent;
		bool rigid;
	};
	std::vector<Pending> stack;
	stack.push_back({root, kNoParent, false});
	// Children lists are appended as each node is expanded; the visited
	// children's entry indices are filled when those entries are made (a
	// second pass below), since depth-first order interleaves subtrees.
	std::vector<std::pair<std::uint32_t, std::uint32_t>> fixups;  // (children index, the child's object) to resolve
	while (!stack.empty()) {
		const Pending p = stack.back();
		stack.pop_back();
		SelectedEntry e;
		e.object = p.object;
		e.parent = p.parent;
		e.rigid = p.rigid;
		e.kind = p.rigid ? Kind::Other : tree.KindOf(p.object);
		const std::uint32_t index = static_cast<std::uint32_t>(order.entries.size());
		if (e.kind == Kind::Node) {
			e.slotCount = tree.SlotCount(p.object);
			e.childrenBegin = static_cast<std::uint32_t>(order.children.size());
			std::vector<Pending> visited;
			for (std::uint32_t i = 0; i < e.slotCount; ++i) {
				const Handle child = tree.ChildAt(p.object, i);
				if (child == 0) {
					continue;
				}
				SelectedChild c;
				c.object = child;
				const std::uint16_t flags = tree.Flags(child);
				if ((flags & kFlagVisit) != 0) {
					fixups.push_back({static_cast<std::uint32_t>(order.children.size()), child});
					visited.push_back({child, index, (flags & kFlagRigid) != 0});
				}
				order.children.push_back(c);
				++e.childCount;
			}
			for (auto it = visited.rbegin(); it != visited.rend(); ++it) {
				stack.push_back(*it);
			}
		}
		order.entries.push_back(e);
	}
	// Resolve the visited children to their entries: each visited child is
	// an entry whose parent is the node; match by object under that parent.
	for (const auto& [childrenIndex, object] : fixups) {
		for (std::uint32_t i = 0; i < order.entries.size(); ++i) {
			if (order.entries[i].object == object && order.entries[i].parent != kNoParent &&
			    order.entries[order.entries[i].parent].childrenBegin <= childrenIndex &&
			    childrenIndex < order.entries[order.entries[i].parent].childrenBegin + order.entries[order.entries[i].parent].childCount) {
				order.children[childrenIndex].entry = i;
				break;
			}
		}
	}
	return order;
}

// The shape still holds: every entry's kind and rigid bit, every node's
// slots, its non-null children in order, and which of them carry bit 1.
template <SelectedTreePort Tree>
bool MatchesSelected(const SelectedOrder& order, const Tree& tree) {
	for (std::uint32_t i = 0; i < order.entries.size(); ++i) {
		const SelectedEntry& e = order.entries[i];
		if (e.rigid) {
			continue;  // the original's; its flags are its own
		}
		if (tree.KindOf(e.object) != e.kind) {
			return false;
		}
		if (e.kind != Kind::Node) {
			continue;
		}
		if (tree.SlotCount(e.object) != e.slotCount) {
			return false;
		}
		std::uint32_t seen = 0;
		for (std::uint32_t s = 0; s < e.slotCount; ++s) {
			const Handle child = tree.ChildAt(e.object, s);
			if (child == 0) {
				continue;
			}
			if (seen >= e.childCount) {
				return false;
			}
			const SelectedChild& c = order.children[e.childrenBegin + seen];
			if (c.object != child) {
				return false;
			}
			const std::uint16_t flags = tree.Flags(child);
			const bool visited = (flags & kFlagVisit) != 0;
			if (visited != (c.entry != kNoParent)) {
				return false;
			}
			if (visited && order.entries[c.entry].rigid != ((flags & kFlagRigid) != 0)) {
				return false;
			}
			++seen;
		}
		if (seen != e.childCount) {
			return false;
		}
	}
	return true;
}

// The action port of the selected update adds the flags and the two
// original passes for the objects the run leaves to them.
template <class Port>
concept SelectedActionPort = ActionPort<Port> && requires(Port& p, Handle h, float f, bool b) {
	{ p.Flags(h) } -> std::same_as<std::uint16_t>;
	{ p.HasControllerChain(h) } -> std::same_as<bool>;                 // +0x0C non-null: walked whatever the bit
	{ p.UpdateObjectControllersWith(h, f, b) };                        // 0x0047C930(time, bit 3)
	{ p.UpdateSelectedOriginal(h, f) };                                // the object's own +0x64
	{ p.UpdateRigidOriginal(h, f) };                                   // the object's own +0x68
};

// One visited object's own steps (0x0070A190 / 0x007075B0 before the
// children): the controllers, the world data when bit 2.
template <SelectedActionPort Port>
inline void SelectedOwnSteps(Port& port, Handle object, Kind kind, float time) {
	const std::uint16_t flags = port.Flags(object);
	const bool properties = (flags & kFlagControllers) != 0;
	if (port.HasControllerChain(object) || (properties && port.HasControllers(object))) {
		port.UpdateObjectControllersWith(object, time, properties);
	}
	if ((flags & kFlagTransforms) != 0) {
		WorldData(port, object);
		if (kind == Kind::GeometryPlain) {
			port.GeometryBoundSelf(object);
		} else if (kind == Kind::Geometry) {
			port.UpdateWorldBound(object);
		}
	}
}

// The original's recursion of the selected pass, the oracle of RunSelected.
template <SelectedTreePort Tree, SelectedActionPort Port>
void ReferenceSelected(const Tree& tree, Port& port, Handle object, float time, bool rigid) {
	if (rigid) {
		port.UpdateRigidOriginal(object, time);
		return;
	}
	const Kind kind = tree.KindOf(object);
	if (kind == Kind::Other) {
		port.UpdateSelectedOriginal(object, time);
		return;
	}
	SelectedOwnSteps(port, object, kind, time);
	if (kind != Kind::Node) {
		return;
	}
	port.SetBoundRadius(object, 0.0f);
	const std::uint32_t slots = tree.SlotCount(object);
	for (std::uint32_t i = 0; i < slots; ++i) {
		const Handle child = tree.ChildAt(object, i);
		if (child == 0) {
			continue;
		}
		const std::uint16_t flags = tree.Flags(child);
		if ((flags & kFlagVisit) != 0) {
			ReferenceSelected(tree, port, child, time, (flags & kFlagRigid) != 0);
		}
		switch (MergeStepFor(port.BoundRadius(object), port.BoundRadius(child))) {
			case MergeStep::Skip:
				break;
			case MergeStep::Copy:
				port.CopyBound(object, child);
				break;
			case MergeStep::Merge:
				port.MergeBound(object, child);
				break;
		}
	}
}

// The flat selected run: forwards the visited objects' own steps (the
// originals for Other and rigid entries), backwards the nodes' bounds over
// all their children.
template <SelectedActionPort Port>
void RunSelected(const SelectedOrder& order, Port& port, float time) {
	for (std::size_t i = 0; i < order.entries.size(); ++i) {
		const SelectedEntry& e = order.entries[i];
		if (e.rigid) {
			port.UpdateRigidOriginal(e.object, time);
			continue;
		}
		if (e.kind == Kind::Other) {
			port.UpdateSelectedOriginal(e.object, time);
			continue;
		}
		SelectedOwnSteps(port, e.object, e.kind, time);
	}
	for (std::size_t n = order.entries.size(); n-- > 0;) {
		const SelectedEntry& e = order.entries[n];
		if (e.rigid || e.kind != Kind::Node) {
			continue;
		}
		Bound own = port.ReadBound(e.object);
		own.radius = 0.0f;
		for (std::uint32_t c = 0; c < e.childCount; ++c) {
			const Bound child = port.ReadBound(order.children[e.childrenBegin + c].object);
			switch (MergeStepFor(own.radius, child.radius)) {
				case MergeStep::Skip:
					break;
				case MergeStep::Copy:
					own = child;
					break;
				case MergeStep::Merge:
					MergeBound(own, child);
					break;
			}
		}
		port.WriteBound(e.object, own);
	}
}

}  // namespace obng::engine::scene::scene_update
