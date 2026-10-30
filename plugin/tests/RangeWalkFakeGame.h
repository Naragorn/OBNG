#pragma once

// The fake game the range-walk tests share (ShadowRangeWalkLogicTest.cpp,
// RouteRangeWalkLogicTest.cpp): a byte-addressed memory with the Memory port
// of ShadowRangeWalkLogic.h (reads, the virtual and helper calls, the route's
// one write), a builder for lights and scene objects, a small scene, and the
// original walk 0x007D59E0 as it is read (re/functions/0x007D59E0.md) over
// that memory.
//
// The offsets are written as literal numbers from the mapping
// (re/functions/0x007D59E0.md "Struct accesses", 0x007D2EA0.md), not through
// the headers' constants, so a wrong constant shows in the tests.

#include "ShadowRangeWalkLogic.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace obng::plugin::range_walk_test {

namespace rw = obng::plugin::range_walk;
namespace walk = obng::engine::render::shadow_light_range_walk;
namespace ws = obng::engine::render::which_side;

inline double StubSquareRoot(float s) {
	return ws::RoundToPc24(std::sqrt(static_cast<double>(s)));
}

// ---- Fake game memory ---------------------------------------------------------------

constexpr std::uint32_t kGate = 0x00B42F48;
constexpr std::uint32_t kSwitchRtti = 0x00B3FD70;
constexpr std::uint32_t kNodeRtti = 0x00B3FAB0;
constexpr std::uint32_t kPointLightRtti = 0x00B3FD80;
constexpr std::uint32_t kDirectionalRtti = 0x00B3FCFC;

enum class Kind : std::uint8_t { Node, Switch, Geometry, Other };

struct FakeMemory {
	std::map<std::uint32_t, std::uint8_t> bytes;
	std::map<std::uint32_t, Kind> kinds;              // object address -> what its vtable answers
	std::map<std::uint32_t, std::uint32_t> rtti;      // object address -> GetRTTI (+0x04)
	std::map<std::uint32_t, std::uint32_t> geometry;  // object -> what vt[+0x10] returns (default: itself)
	std::uint64_t reads = 0;
	std::uint64_t calls = 0;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> callLog;  // (function or slot, self)
	std::vector<std::pair<std::uint32_t, std::uint32_t>> writes;   // (address, value) through W32
	// >= 0: the gate dword reads as it stands this many more times through
	// U32, then turns 0 (another thread switching it off mid-walk).
	int gateOnReads = -1;

	void Put32(std::uint32_t address, std::uint32_t value) {
		for (int i = 0; i < 4; ++i) {
			bytes[address + i] = static_cast<std::uint8_t>(value >> (8 * i));
		}
	}
	void Put16(std::uint32_t address, std::uint16_t value) {
		bytes[address] = static_cast<std::uint8_t>(value);
		bytes[address + 1] = static_cast<std::uint8_t>(value >> 8);
	}
	void PutF(std::uint32_t address, float value) {
		Put32(address, std::bit_cast<std::uint32_t>(value));
	}
	std::uint8_t Byte(std::uint32_t address) const {
		const auto it = bytes.find(address);
		return it == bytes.end() ? 0 : it->second;
	}
	std::uint32_t Raw16(std::uint32_t address) const {
		return static_cast<std::uint32_t>(Byte(address)) | (static_cast<std::uint32_t>(Byte(address + 1)) << 8);
	}
	std::uint32_t Raw32(std::uint32_t address) const {
		return static_cast<std::uint32_t>(Byte(address)) | (static_cast<std::uint32_t>(Byte(address + 1)) << 8) |
			(static_cast<std::uint32_t>(Byte(address + 2)) << 16) | (static_cast<std::uint32_t>(Byte(address + 3)) << 24);
	}

	// ---- The Memory port (ShadowRangeWalkLogic.h) ----
	std::uint8_t U8(std::uint32_t address) {
		++reads;
		return Byte(address);
	}
	std::uint16_t U16(std::uint32_t address) {
		++reads;
		return static_cast<std::uint16_t>(Byte(address) | (Byte(address + 1) << 8));
	}
	std::uint32_t U32(std::uint32_t address) {
		++reads;
		if (address == kGate && gateOnReads >= 0) {
			if (gateOnReads == 0) {
				Put32(kGate, 0);
			} else {
				--gateOnReads;
			}
		}
		return Raw32(address);
	}
	// The route's one write (RouteRangeWalkLogic.h): +0x1B0 of the light.
	void W32(std::uint32_t address, std::uint32_t value) {
		writes.emplace_back(address, value);
		Put32(address, value);
	}
	float F32(std::uint32_t address) {
		++reads;
		return std::bit_cast<float>(Raw32(address));
	}
	std::uint32_t CallVirtual(std::uint32_t object, std::uint32_t slot) {
		++calls;
		callLog.emplace_back(slot, object);
		const Kind kind = kinds.count(object) != 0 ? kinds.at(object) : Kind::Other;
		switch (slot) {
		case 0x04:
			return rtti.count(object) != 0 ? rtti.at(object) : kNodeRtti;
		case 0x08:
			return kind == Kind::Node || kind == Kind::Switch ? object : 0;
		case 0x10:
			if (kind != Kind::Geometry) {
				return 0;
			}
			return geometry.count(object) != 0 ? geometry.at(object) : object;
		default:
			return 0xDEADBEEF;
		}
	}
	// 0x007C59E0 and 0x0049FFC0 as their instructions read
	// (re/export/range-callers/0x007C59E0/asm.s, 0x0049FFC0/asm.s).
	std::uint32_t CallThis(std::uint32_t function, std::uint32_t self) {
		++calls;
		callLog.emplace_back(function, self);
		if (function == 0x007C59E0) {
			const auto index = static_cast<std::int32_t>(Raw32(self + 0xE0));
			const std::uint32_t count = Byte(self + 0xB6) | (Byte(self + 0xB7) << 8);
			if (index < 0 || index >= static_cast<std::int32_t>(count)) {
				return 0;
			}
			return Raw32(Raw32(self + 0xB0) + 4 * static_cast<std::uint32_t>(index));
		}
		if (function == 0x0049FFC0) {
			const float radius = std::bit_cast<float>(Raw32(self + 0x2C));
			return radius != 0.0f ? 0xFFFFFF01u : 0xFFFFFF00u;  // only AL counts; the rest is noise on purpose
		}
		return 0xDEADBEEF;
	}
};

// A small heap in the fake address space.
struct Builder {
	FakeMemory& m;
	std::uint32_t next = 0x10000000;

	std::uint32_t Alloc(std::uint32_t size) {
		const std::uint32_t at = next;
		next += (size + 0xFF) & ~0xFFu;
		return at;
	}
	std::uint32_t Object(Kind kind, float x, float y, float z, float radius, std::uint8_t flags = 0) {
		const std::uint32_t o = Alloc(0x100);
		m.kinds[o] = kind;
		m.bytes[o + 0x18] = flags;
		m.PutF(o + 0x20, x);
		m.PutF(o + 0x24, y);
		m.PutF(o + 0x28, z);
		m.PutF(o + 0x2C, radius);
		if (kind == Kind::Switch) {
			m.rtti[o] = kSwitchRtti;
		}
		return o;
	}
	void Children(std::uint32_t node, const std::vector<std::uint32_t>& children) {
		const std::uint32_t data = Alloc(4 * static_cast<std::uint32_t>(children.size() + 1));
		for (std::size_t i = 0; i < children.size(); ++i) {
			m.Put32(data + 4 * static_cast<std::uint32_t>(i), children[i]);
		}
		m.Put32(node + 0xB0, data);
		m.Put16(node + 0xB6, static_cast<std::uint16_t>(children.size()));
	}
	std::uint32_t Source(std::uint32_t rtti, float x, float y, float z, float range) {
		const std::uint32_t s = Alloc(0x100);
		m.rtti[s] = rtti;
		m.PutF(s + 0x88, x);
		m.PutF(s + 0x8C, y);
		m.PutF(s + 0x90, z);
		m.PutF(s + 0xF8, range);
		return s;
	}
	// A ShadowSceneLight: source +0x100, plane source +0x14C, planes +0x150, mask +0x1B0.
	std::uint32_t Light(std::uint32_t source, std::uint32_t planeSource, std::uint32_t mask) {
		const std::uint32_t l = Alloc(0x200);
		m.Put32(l + 0x100, source);
		m.Put32(l + 0x14C, planeSource);
		m.Put32(l + 0x1B0, mask);
		return l;
	}
	void Plane(std::uint32_t light, unsigned i, float nx, float ny, float nz, float c) {
		const std::uint32_t at = light + 0x150 + 16 * i;
		m.PutF(at, nx);
		m.PutF(at + 4, ny);
		m.PutF(at + 8, nz);
		m.PutF(at + 12, c);
	}
};
// root (node, in range) -> a: node -> g1, g2 (geometry); b: switch -> active g3;
// c: geometry out of range; plus a culled and a zero-radius child.
struct SceneShape {
	std::uint32_t light, root, g1, g2, g3, far, culled;
};

inline SceneShape BuildScene(Builder& b, bool planes) {
	const std::uint32_t source = b.Source(kPointLightRtti, 0, 0, 0, 100.0f);
	const std::uint32_t light = b.Light(source, planes ? 1u : 0u, 0x3F);
	if (planes) {
		// Six axis planes of the box |x|,|y|,|z| <= 50, normals inwards.
		b.Plane(light, 0, 1, 0, 0, -50);
		b.Plane(light, 1, -1, 0, 0, -50);
		b.Plane(light, 2, 0, 1, 0, -50);
		b.Plane(light, 3, 0, -1, 0, -50);
		b.Plane(light, 4, 0, 0, 1, -50);
		b.Plane(light, 5, 0, 0, -1, -50);
	}
	b.m.Put32(kGate, 3);
	SceneShape s{};
	s.light = light;
	s.root = b.Object(Kind::Node, 0, 0, 0, 60);
	const std::uint32_t a = b.Object(Kind::Node, 10, 0, 0, 20);
	s.g1 = b.Object(Kind::Geometry, 10, 0, 0, 5);
	s.g2 = b.Object(Kind::Geometry, 15, 5, 0, 5);
	b.Children(a, {s.g1, 0, s.g2});
	const std::uint32_t sw = b.Object(Kind::Switch, -10, 0, 0, 10);
	const std::uint32_t inactive = b.Object(Kind::Geometry, -10, 0, 0, 2);
	s.g3 = b.Object(Kind::Geometry, -12, 0, 0, 2);
	b.Children(sw, {inactive, s.g3});
	b.m.Put32(sw + 0xE0, 1);
	s.far = b.Object(Kind::Geometry, 500, 0, 0, 5);
	s.culled = b.Object(Kind::Geometry, 0, 0, 0, 5, 0x01);
	const std::uint32_t zero = b.Object(Kind::Geometry, 0, 0, 0, 0);
	b.Children(s.root, {a, sw, s.far, s.culled, zero});
	return s;
}

// IsInRange 0x007D2EA0 as the logic computes it, on the live mask in memory,
// narrowing it there as the original does.
inline bool FakeIsInRange(FakeMemory& m, std::uint32_t light, std::uint32_t object) {
	const walk::LightValues v = rw::CaptureLight(m, light);
	const walk::SourceMode mode = walk::SelectSourceMode(v.hasSource, v.sourceRtti, v.planeSource);
	std::uint64_t count = 0;
	const walk::RangeDecision d = walk::TestObject(
		v, mode, m.Raw32(light + 0x1B0),
		[&] {
			return ws::SphereValues{m.F32(object + 0x20), m.F32(object + 0x24), m.F32(object + 0x28), m.F32(object + 0x2C)};
		},
		StubSquareRoot, count);
	m.Put32(light + 0x1B0, d.mask);
	return d.inRange;
}

inline constexpr std::uint32_t kGateOffEax = 0xAB00;  // XOR AL,AL: the upper bits are whatever EAX held
inline constexpr std::uint32_t kWalkedEax = 0x101;    // MOV AL,1 over some upper bits

// One call of the original 0x007D59E0 as read: the gate (B1), the mask saved,
// IsInRange, a switch's active child or a plain node's children (the count
// re-read per child) entered through enter(light, child) - the patched entry
// in a patched game -, a geometry handed to add(light, object, geometry),
// the mask restored, AL 1.
template <class Enter, class Add>
std::uint32_t OriginalWalk(FakeMemory& m, std::uint32_t light, std::uint32_t object, Enter&& enter, Add&& add) {
	if (m.Raw32(kGate) == 0) {
		return kGateOffEax;
	}
	const std::uint32_t saved = m.Raw32(light + 0x1B0);
	if (FakeIsInRange(m, light, object)) {
		const Kind kind = m.kinds.count(object) != 0 ? m.kinds.at(object) : Kind::Other;
		if (kind == Kind::Switch) {
			const std::uint32_t active = m.CallThis(0x007C59E0, object);
			if (active != 0 && (m.CallThis(0x0049FFC0, active) & 0xFF) != 0 && (m.Byte(active + 0x18) & 1) == 0) {
				enter(light, active);
			}
		} else if (kind == Kind::Node) {
			for (std::uint32_t i = 0; i < m.Raw16(object + 0xB6); ++i) {  // the count re-read per child
				const std::uint32_t child = m.Raw32(m.Raw32(object + 0xB0) + 4 * i);
				if (child != 0 && std::bit_cast<float>(m.Raw32(child + 0x2C)) != 0.0f && (m.Byte(child + 0x18) & 1) == 0) {
					enter(light, child);
				}
			}
		} else if (kind == Kind::Geometry) {
			add(light, object, m.geometry.count(object) != 0 ? m.geometry.at(object) : object);
		}
	}
	m.Put32(light + 0x1B0, saved);
	return kWalkedEax;
}

}  // namespace obng::plugin::range_walk_test
