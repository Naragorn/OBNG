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
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
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

enum class Kind : std::uint8_t { Node, Geometry, Other };

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
		return Kind::Geometry;
	}
	return Kind::Other;
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
//   void MergeBound(Handle to, Handle from)  NiBound::Merge 0x0072A6B0 on to's bound with from's
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
};

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
	if (kind == Kind::Geometry) {
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
void Run(const Order& order, Port& port, float time, bool controllers) {
	for (const Entry& e : order.entries) {
		if (e.kind == Kind::Other) {
			port.UpdateDownwardOriginal(e.object, time, controllers);
			continue;
		}
		if (controllers && port.HasControllers(e.object)) {
			port.UpdateObjectControllers(e.object, time);
		}
		WorldData(port, e.object);
		if (e.kind == Kind::Geometry) {
			port.UpdateWorldBound(e.object);
		}
	}
	for (std::size_t n = order.entries.size(); n-- > 0;) {
		const Entry& e = order.entries[n];
		if (e.kind != Kind::Node) {
			continue;
		}
		port.SetBoundRadius(e.object, 0.0f);
		for (std::uint32_t c = 0; c < e.childCount; ++c) {
			const Handle child = order.entries[order.children[e.childrenBegin + c]].object;
			switch (MergeStepFor(port.BoundRadius(e.object), port.BoundRadius(child))) {
				case MergeStep::Skip:
					break;
				case MergeStep::Copy:
					port.CopyBound(e.object, child);
					break;
				case MergeStep::Merge:
					port.MergeBound(e.object, child);
					break;
			}
		}
	}
}

}  // namespace obng::engine::scene::scene_update
