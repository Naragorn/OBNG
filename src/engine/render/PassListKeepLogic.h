#pragma once
// Keeping a geometry's render passes through a relight that changed nothing -
// the decisions over plain values ([Route] PassListKeep in the plugin's
// PassListKeep.cpp; knowledge/culling-and-draw-setup.md "A geometry's render
// passes").
//
// A lit shader property caches its render passes (+0x28) under the key at
// +0x24, `(render mode << 8) | flags`; GetRenderPasses 0x007D85D0 rebuilds
// them when the key differs (read, 0x007D8A39). Every change of the
// property's light set clears the key (0x007EE330, 0x007EE720 write 0, read).
// RelightGeometry 0x007C6100 drops a geometry from all its lights and adds it
// back to every light in range - for every geometry whose bound changed,
// which a skinned actor's shapes do every frame - so their passes are rebuilt
// every frame even when the relight ended with the same lights in the same
// order.
//
// Here a relight that ended with the property's light list exactly as before
// (the same lights, the same order, the same shadow scene node) gives the key
// back: the cached passes are what a rebuild would make from the same lights
// (class A - the same computation, kept instead of repeated). Only for the
// property classes whose pass getter is 0x007D85D0 itself
// (BSShaderPPLightingProperty, SpeedTreeBranchShaderProperty,
// SpeedTreeShaderPPLightingProperty); Hair's getter 0x00882AE0 decides by the
// geometry's distance to its lights and stays as it is. The check mode writes
// a sentinel instead, lets the getter rebuild and compares the rebuilt passes
// with the cached ones.
//
// 64-bit clean: properties, lights and passes are std::uint32_t handles.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::engine::render::pass_list_keep {

enum class Mode : std::uint8_t { kOff, kVerify, kKeep };

constexpr Mode ModeFromIni(int value) {
	return value == 1 ? Mode::kVerify : value == 2 ? Mode::kKeep : Mode::kOff;
}

// Never a key: a key is a render mode byte shifted by 8 or'ed with a flags
// byte, below 0x10000.
constexpr std::uint32_t kSentinel = 0x0B5E0000u;

constexpr bool IsRealKey(std::uint32_t key) {
	return key != 0 && key != 0xFFFFFFFFu && key != kSentinel;
}

// A property's lights (+0x6C list, in its order).
struct LightSet {
	static constexpr std::uint32_t kMax = 16;
	std::uint32_t count = 0;
	bool overflow = false;
	std::uint32_t light[kMax] = {};
	void Add(std::uint32_t l) {
		if (count == kMax) {
			overflow = true;
			return;
		}
		light[count++] = l;
	}
};

inline bool Same(const LightSet& a, const LightSet& b) {
	if (a.overflow || b.overflow || a.count != b.count) {
		return false;
	}
	for (std::uint32_t i = 0; i < a.count; ++i) {
		if (a.light[i] != b.light[i]) {
			return false;
		}
	}
	return true;
}

// The property before the relight.
struct Before {
	bool ourClass = false;     // its pass getter is 0x007D85D0
	std::uint32_t key = 0;     // +0x24
	std::uint32_t nibble = 0;  // +0x1C >> 28: the shadow scene node it is lit under
	LightSet lights;
};

enum class Why : std::uint8_t {
	kKept,           // the key given back (or the sentinel written)
	kNoProperty,     // no lighting property on the geometry
	kNotOurClass,
	kKeyUnset,       // 0 or -1 before: rebuilt anyway
	kKeyNotCleared,  // the relight left the key as it was (or set -1)
	kNodeChanged,
	kLightsChanged,
	kTooManyLights,
	kCount
};

constexpr const char* kWhyName[static_cast<int>(Why::kCount)] = {
	"kept", "no property", "other class", "key unset before", "key not cleared", "node changed", "lights changed", "too many lights"};

inline Why Decide(const Before& b, std::uint32_t keyAfter, std::uint32_t nibbleAfter, const LightSet& after) {
	if (!b.ourClass) {
		return Why::kNotOurClass;
	}
	if (!IsRealKey(b.key) && b.key != kSentinel) {
		return Why::kKeyUnset;
	}
	if (keyAfter != 0) {
		return Why::kKeyNotCleared;
	}
	if (nibbleAfter != b.nibble) {
		return Why::kNodeChanged;
	}
	if (b.lights.overflow || after.overflow) {
		return Why::kTooManyLights;
	}
	if (!Same(b.lights, after)) {
		return Why::kLightsChanged;
	}
	return Why::kKept;
}

// What a kept relight writes into +0x24.
constexpr std::uint32_t KeyToWrite(Mode mode, const Before& b) {
	return mode == Mode::kKeep ? b.key : kSentinel;
}

// One render pass record (0x10 bytes: +0x04 the pass id, +0x06 and +0x07
// bytes, +0x08 the light count, +0x0C the lights).
struct Pass {
	static constexpr std::uint32_t kMaxLights = 8;
	std::uint16_t id = 0;
	std::uint8_t flags6 = 0;
	std::uint8_t flags7 = 0;
	std::uint32_t lightCount = 0;
	std::uint32_t lights[kMaxLights] = {};
	bool Comparable() const { return lightCount <= kMaxLights; }
};

struct PassList {
	static constexpr std::uint32_t kMax = 24;
	std::uint32_t count = 0;
	bool overflow = false;
	Pass pass[kMax];
	void Add(const Pass& p) {
		if (count == kMax || !p.Comparable()) {
			overflow = true;
			return;
		}
		pass[count++] = p;
	}
};

enum class Diff : std::uint8_t { kSame, kCount, kId, kFlags, kLights, kOverflow, kKinds };

constexpr const char* kDiffName[static_cast<int>(Diff::kKinds)] = {"same", "pass count", "pass id", "pass bytes", "lights", "too long"};

inline Diff Compare(const PassList& a, const PassList& b, std::uint32_t* at) {
	*at = 0;
	if (a.overflow || b.overflow) {
		return Diff::kOverflow;
	}
	const std::uint32_t n = a.count < b.count ? a.count : b.count;
	for (std::uint32_t i = 0; i < n; ++i) {
		*at = i;
		const Pass& x = a.pass[i];
		const Pass& y = b.pass[i];
		if (x.id != y.id) {
			return Diff::kId;
		}
		if (x.flags6 != y.flags6 || x.flags7 != y.flags7) {
			return Diff::kFlags;
		}
		if (x.lightCount != y.lightCount) {
			return Diff::kLights;
		}
		for (std::uint32_t l = 0; l < x.lightCount; ++l) {
			if (x.lights[l] != y.lights[l]) {
				return Diff::kLights;
			}
		}
	}
	*at = n;
	return a.count == b.count ? Diff::kSame : Diff::kCount;
}

// The check mode's cached passes by property, until the getter rebuilds them.
class Pending {
public:
	static constexpr std::uint32_t kSlots = 2048;  // a power of two
	static constexpr std::uint32_t kProbe = 8;
	struct Entry {
		std::uint32_t property = 0;
		std::uint32_t key = 0;
		std::uint32_t age = 0;
		PassList passes;
	};

	void Put(std::uint32_t property, std::uint32_t key, const PassList& passes) {
		++clock_;
		Entry* oldest = nullptr;
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			Entry& e = slots_[(Home(property) + i) & (kSlots - 1)];
			if (e.property == property || e.property == 0) {
				Fill(e, property, key, passes);
				return;
			}
			if (oldest == nullptr || e.age < oldest->age) {
				oldest = &e;
			}
		}
		Fill(*oldest, property, key, passes);
	}

	const Entry* Find(std::uint32_t property) const {
		for (std::uint32_t i = 0; i < kProbe; ++i) {
			const Entry& e = slots_[(Home(property) + i) & (kSlots - 1)];
			if (e.property == property && property != 0) {
				return &e;
			}
		}
		return nullptr;
	}

	void Drop(std::uint32_t property) {
		Entry* e = const_cast<Entry*>(Find(property));
		if (e != nullptr) {
			e->property = 0;
		}
	}

private:
	static constexpr std::uint32_t Home(std::uint32_t p) { return (p * 2654435761u) >> 21; }
	void Fill(Entry& e, std::uint32_t property, std::uint32_t key, const PassList& passes) {
		e.property = property;
		e.key = key;
		e.age = clock_;
		e.passes = passes;
	}
	Entry slots_[kSlots];
	std::uint32_t clock_ = 0;
};

struct Counts {
	std::uint64_t relights = 0;
	std::uint64_t why[static_cast<int>(Why::kCount)] = {};
	std::uint64_t relightTicks = 0;  // OBNG's own part around the relight
	std::uint64_t getterCalls = 0;
	std::uint64_t builds = 0;
	std::uint64_t buildTicks = 0;
	std::uint64_t checks[static_cast<int>(Diff::kKinds)] = {};
	std::uint64_t otherKey = 0;          // rebuilt under another key than the cached one: not compared
	std::uint64_t otherwiseCleared = 0;  // cleared by something else before the getter: not compared
	std::uint64_t frames[2] = {};        // with the alternation: [0] not kept, [1] kept
	std::uint64_t getterTicks[2] = {};   // the getter's time by frame side
};

inline void FormatSummary(const Counts& c, Mode mode, bool alternate, double ticksPerMs, char* out, std::size_t size) {
	const double perUs = ticksPerMs > 0.0 ? ticksPerMs / 1000.0 : 1.0;
	int used = std::snprintf(out, size, "Route: pass lists kept (%s%s) - %llu relights, our part %.3f ms; by reason:", mode == Mode::kKeep ? "keep" : "verify",
	                         alternate ? ", alternating by frame" : "", static_cast<unsigned long long>(c.relights),
	                         static_cast<double>(c.relightTicks) / perUs / 1000.0);
	for (int i = 0; i < static_cast<int>(Why::kCount) && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		if (c.why[i] != 0) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %llu;", kWhyName[i], static_cast<unsigned long long>(c.why[i]));
		}
	}
	if (used > 0 && static_cast<std::size_t>(used) < size) {
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " getter %llu calls, %llu builds (%.2f us a build);",
		                      static_cast<unsigned long long>(c.getterCalls), static_cast<unsigned long long>(c.builds),
		                      c.builds != 0 ? static_cast<double>(c.buildTicks) / perUs / static_cast<double>(c.builds) : 0.0);
	}
	if (mode == Mode::kVerify) {
		for (int i = 0; i < static_cast<int>(Diff::kKinds) && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %llu;", kDiffName[i], static_cast<unsigned long long>(c.checks[i]));
		}
		if (used > 0 && static_cast<std::size_t>(used) < size) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " other key %llu, cleared otherwise %llu;",
			                      static_cast<unsigned long long>(c.otherKey), static_cast<unsigned long long>(c.otherwiseCleared));
		}
	}
	if (alternate && used > 0 && static_cast<std::size_t>(used) < size) {
		const double off = c.frames[0] != 0 ? static_cast<double>(c.frames[0]) : 1.0;
		const double on = c.frames[1] != 0 ? static_cast<double>(c.frames[1]) : 1.0;
		std::snprintf(out + used, size - static_cast<std::size_t>(used), " the getter %.3f ms a frame kept, %.3f not (%llu / %llu frames)",
		              static_cast<double>(c.getterTicks[1]) / perUs / 1000.0 / on, static_cast<double>(c.getterTicks[0]) / perUs / 1000.0 / off,
		              static_cast<unsigned long long>(c.frames[1]), static_cast<unsigned long long>(c.frames[0]));
	}
}

}  // namespace obng::engine::render::pass_list_keep
