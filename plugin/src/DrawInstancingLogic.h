#pragma once
// [Route] DrawInstancing: the copies of a mesh that one pass list of the
// shader accumulator draws with ShadowLightShader's SLS2003 programs are held
// back at the list's draw step and drawn at the list's end - each group of
// copies as one instanced draw (2), or each record by itself in the new order
// with the constants the instanced draw would use compared against the
// engine's own uploads (1). The decisions, the grouping, the predictions and
// the matrix work over plain values; DrawInstancing.cpp touches the game,
// DrawInstancingDevice.cpp Direct3D. knowledge/draw-instancing.md has the
// measurements behind it.
//
// Class C (AGENTS.md): another order of the draws and the model-space
// constants rebuilt from the instance transform; mode 1 compares the rebuilt
// constants with the engine's for every record that would be an instance.
//
// 64-bit clean: every engine object is a std::uint32_t handle.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace obng::plugin::draw_instancing {

// ---- The mode -----------------------------------------------------------------

enum class Mode : std::uint8_t {
	kOff = 0,
	kVerify = 1,     // held back and drawn in the new order one by one; the rebuilt constants compared
	kInstanced = 2,  // a group of two or more drawn as one instanced draw
};

constexpr Mode ModeFromIni(int value, bool* known) {
	*known = value >= 0 && value <= 2;
	return *known ? static_cast<Mode>(value) : Mode::kOff;
}

constexpr const char* ModeName(Mode m) {
	return m == Mode::kVerify ? "verify" : m == Mode::kInstanced ? "instanced" : "off";
}

// ---- Whether a record may wait -------------------------------------------------

// What the slot-33 site reads of one record (DrawInstancing.cpp ReadFacts).
struct Facts {
	bool inList = false;    // inside a pass list's draw (BSShaderAccumulator::RenderPassList 0x007AC9A0)
	bool inWalk = false;    // the record is the walk's next item (the list's nodes in order)
	bool first = false;     // the list's first item: drawn inside the pass's shader state (knowledge/culling-and-draw-setup.md)
	bool readable = false;  // the reads below did not fault
	bool geometry = false;  // the record has a geometry
	bool skinned = false;   // the geometry has a skin instance (+0xB8): drawn by partitions, not the whole draw
	float shaderAlpha = 0.0f;    // the shader property's alpha (+0x20; 1 without one)
	float materialAlpha = 0.0f;  // the material's alpha (+0x50; 1 without one)
	bool hasAlpha = false;       // an alpha property in the property state (slot 0)
	std::uint16_t alphaFlags = 0;  // its flags (+0x18): bit 0 blends, bit 9 tests (xOBSE NiAlphaProperty)
	bool hasZ = false;             // a z-buffer property (slot 9)
	std::uint16_t zFlags = 0;      // its flags (+0x18): bit 0 tests, bit 1 writes (xOBSE NiZBufferProperty)
	bool predicted = false;        // this shader, pass id and flags drew with SLS2003 before, always with one pass object
	bool multiPass = false;        // the geometry's shader property has more than one render pass, and those may not wait
};

enum class Why : std::uint8_t {
	kWaits,
	kNotInList,
	kNotInWalk,
	kFirstOfList,
	kUnreadable,
	kNoGeometry,
	kSkinned,
	kTranslucent,
	kBlended,
	kDepthOff,
	kMultiPass,
	kNotPredicted,
	kCount,
};

constexpr const char* WhyName(Why w) {
	constexpr const char* kNames[] = {"waits",   "not in a list", "not the walk's item", "first of list", "unreadable", "no geometry", "skinned",
	                                  "translucent", "blended", "depth not written", "more passes", "pass not predicted", "?"};
	return kNames[static_cast<int>(w)];
}

constexpr bool Blended(const Facts& f) {
	return f.hasAlpha && (f.alphaFlags & 0x0001u) != 0;
}

constexpr bool DepthWritten(const Facts& f) {
	return !f.hasZ || (f.zFlags & 0x0003u) == 0x0003u;
}

// A record drawn at once may go before the records that wait only when its
// result does not depend on the order: opaque, the depth tested and written.
constexpr bool OrderFree(const Facts& f) {
	return f.readable && !Blended(f) && DepthWritten(f);
}

constexpr Why Decide(const Facts& f) {
	if (!f.inList) {
		return Why::kNotInList;
	}
	if (!f.inWalk) {
		return Why::kNotInWalk;
	}
	if (f.first) {
		return Why::kFirstOfList;
	}
	if (!f.readable) {
		return Why::kUnreadable;
	}
	if (!f.geometry) {
		return Why::kNoGeometry;
	}
	if (f.skinned) {
		return Why::kSkinned;
	}
	if (f.shaderAlpha != 1.0f || f.materialAlpha != 1.0f) {
		return Why::kTranslucent;
	}
	if (Blended(f)) {
		return Why::kBlended;
	}
	if (!DepthWritten(f)) {
		return Why::kDepthOff;
	}
	if (f.multiPass) {
		// Its later passes (another list) test against the depth this pass
		// writes; the instanced positions are not bit for bit the engine's
		// (measured 2026-11-12: the iron bands' highlight of Anvil's barrels).
		return Why::kMultiPass;
	}
	return f.predicted ? Why::kWaits : Why::kNotPredicted;
}

// ---- The group key ---------------------------------------------------------------
//
// Everything that decides what a draw of SLS2003 reads besides the world
// transform: the pass id, the mesh, the shader, the property state's shared
// properties (by pointer: the copies share them, measured in V1), the shader
// property's class, flags and light scale and the material by value (each copy
// has its own, mostly equal), the record's lights (DrawRenderPass 0x007A9820
// writes their constants) and the dynamic effect state by value.
enum KeyWord : int {
	kWordPass,
	kWordData,
	kWordShader,
	kWordAlphaProperty,
	kWordFogProperty,
	kWordRendererProperty,
	kWordTexturing,
	kWordStencil,
	kWordVertexColour,
	kWordWireframe,
	kWordZBuffer,
	kWordShaderClass,
	kWordShaderFlags,
	kWordLightScale,
	kWordMaterial,
	kWordLights,
	kWordEffects,
	kWordRecord,  // the record's dword at +0x04: its pass type, +0x06 and the byte +0x07 that turns the fog off (Toggles.y = 0, 0x007C9F30)
	kKeyWords,
};

struct Key {
	std::uint32_t w[kKeyWords] = {};
};

inline bool operator==(const Key& a, const Key& b) {
	return std::memcmp(a.w, b.w, sizeof(a.w)) == 0;
}

// FNV-1a over dwords.
inline std::uint32_t HashWords(const std::uint32_t* words, std::size_t count) {
	std::uint32_t h = 2166136261u;
	for (std::size_t i = 0; i < count; ++i) {
		for (int b = 0; b < 4; ++b) {
			h ^= (words[i] >> (8 * b)) & 0xFFu;
			h *= 16777619u;
		}
	}
	return h;
}

inline std::uint32_t KeyHash(const Key& k) {
	return HashWords(k.w, kKeyWords);
}

constexpr const char* KeyWordName(int w) {
	constexpr const char* kNames[] = {"pass",        "data",         "shader",    "alpha",       "fog",        "renderer",
	                                  "texturing",   "stencil",      "vertexcolour", "wireframe", "zbuffer",    "shaderclass",
	                                  "shaderflags", "lightscale",   "material",  "lights",      "effects",    "record",  "?"};
	return kNames[w < 0 || w > kKeyWords ? kKeyWords : w];
}

// [Route] DrawInstancingIgnore: key words left out of the key, by name,
// separated by commas or spaces (for mode 1's question which of them the
// programs really read). The pass, the mesh and the shader cannot be left
// out. Unknown names are counted in *unknown.
inline std::uint32_t IgnoredWords(const char* text, int* unknown) {
	std::uint32_t mask = 0;
	*unknown = 0;
	const char* p = text;
	while (*p != '\0') {
		while (*p == ',' || *p == ' ') {
			++p;
		}
		const char* begin = p;
		while (*p != '\0' && *p != ',' && *p != ' ') {
			++p;
		}
		const std::size_t length = static_cast<std::size_t>(p - begin);
		if (length == 0) {
			continue;
		}
		int found = -1;
		for (int w = kWordAlphaProperty; w < kKeyWords; ++w) {
			const char* name = KeyWordName(w);
			if (std::strlen(name) == length && std::strncmp(name, begin, length) == 0) {
				found = w;
			}
		}
		if (found < 0) {
			++*unknown;
		} else {
			mask |= 1u << found;
		}
	}
	return mask;
}

inline void LeaveOut(Key* k, std::uint32_t ignored) {
	for (int w = kWordAlphaProperty; w < kKeyWords; ++w) {
		if ((ignored >> w) & 1u) {
			k->w[w] = 0;
		}
	}
}

// [Route] DrawInstancingLights: what of the record's lights goes into the key -
// 2 all of them (count and every light), 1 the first only (the light the
// shadow projection is taken from, 0x007C9230 B35), 0 none.
inline std::uint32_t LightsWord(const std::uint32_t* lights, std::uint32_t count, std::uint32_t mode) {
	if (mode == 0) {
		return 0;
	}
	if (mode == 1) {
		const std::uint32_t first[2] = {count != 0 ? 1u : 0u, count != 0 ? lights[0] : 0u};
		return HashWords(first, 2);
	}
	std::uint32_t words[17] = {count};
	for (std::uint32_t i = 0; i < count && i < 16; ++i) {
		words[i + 1] = lights[i];
	}
	return HashWords(words, 17);
}

// ---- The records that wait, grouped by key ---------------------------------------
//
// Groups in the order their first record came, the records of a group in
// theirs. Static storage (about 90 KB); the hash slots used are remembered so
// that a clear touches only them.
class Pending {
public:
	static constexpr std::uint32_t kMaxRecords = 4096;
	static constexpr std::uint32_t kMaxGroups = 1024;
	static constexpr std::uint32_t kSlots = 2048;  // a power of two, twice the groups
	static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

	struct Record {
		std::uint32_t record = 0;
		std::uint32_t geometry = 0;
		std::uint32_t next = kNone;
	};
	struct Group {
		Key key;
		std::uint32_t first = kNone;
		std::uint32_t last = kNone;
		std::uint32_t count = 0;
	};
	enum class Added : std::uint8_t { kNew, kJoined, kFull };

	Pending() {
		for (std::uint32_t& s : slots_) {
			s = kNone;
		}
	}

	Added Add(const Key& key, std::uint32_t record, std::uint32_t geometry) {
		if (records_ == kMaxRecords) {
			return Added::kFull;
		}
		std::uint32_t slot = KeyHash(key) & (kSlots - 1);
		while (slots_[slot] != kNone && !(groups_[slots_[slot]].key == key)) {
			slot = (slot + 1) & (kSlots - 1);
		}
		std::uint32_t g = slots_[slot];
		Added added = Added::kJoined;
		if (g == kNone) {
			if (groupCount_ == kMaxGroups) {
				return Added::kFull;
			}
			g = groupCount_++;
			groups_[g] = Group{key, kNone, kNone, 0};
			slots_[slot] = g;
			used_[usedCount_++] = slot;
			added = Added::kNew;
		}
		const std::uint32_t r = records_++;
		list_[r] = Record{record, geometry, kNone};
		Group& group = groups_[g];
		if (group.last == kNone) {
			group.first = r;
		} else {
			list_[group.last].next = r;
		}
		group.last = r;
		++group.count;
		return added;
	}

	bool Empty() const { return records_ == 0; }
	std::uint32_t GroupCount() const { return groupCount_; }
	std::uint32_t RecordCount() const { return records_; }
	const Group& GroupAt(std::uint32_t i) const { return groups_[i]; }
	const Record& RecordAt(std::uint32_t i) const { return list_[i]; }

	void Clear() {
		for (std::uint32_t i = 0; i < usedCount_; ++i) {
			slots_[used_[i]] = kNone;
		}
		usedCount_ = 0;
		groupCount_ = 0;
		records_ = 0;
	}

private:
	Record list_[kMaxRecords];
	Group groups_[kMaxGroups];
	std::uint32_t slots_[kSlots];
	std::uint32_t used_[kMaxGroups] = {};
	std::uint32_t usedCount_ = 0;
	std::uint32_t groupCount_ = 0;
	std::uint32_t records_ = 0;
};

// ---- What splits the copies of a mesh into several groups ---------------------------
//
// Per flush: the groups taken together by pass and mesh, and for every key
// word the records whose group differs in it from the first group of their
// mesh - what each word costs in draws.
struct Splits {
	std::uint64_t records = 0;
	std::uint64_t groups = 0;
	std::uint64_t meshGroups = 0;
	std::uint64_t differs[kKeyWords] = {};

	void Add(const Pending& p) {
		static constexpr std::uint32_t kSlots = 2048;
		static std::uint32_t slotGeneration[kSlots] = {};
		static std::uint32_t slotGroup[kSlots] = {};
		static std::uint32_t generation = 0;
		++generation;
		for (std::uint32_t g = 0; g < p.GroupCount(); ++g) {
			const Pending::Group& group = p.GroupAt(g);
			records += group.count;
			++groups;
			const std::uint32_t mesh[2] = {group.key.w[kWordPass], group.key.w[kWordData]};
			std::uint32_t slot = HashWords(mesh, 2) & (kSlots - 1);
			while (slotGeneration[slot] == generation) {
				const Key& other = p.GroupAt(slotGroup[slot]).key;
				if (other.w[kWordPass] == mesh[0] && other.w[kWordData] == mesh[1]) {
					break;
				}
				slot = (slot + 1) & (kSlots - 1);
			}
			if (slotGeneration[slot] != generation) {
				slotGeneration[slot] = generation;
				slotGroup[slot] = g;
				++meshGroups;
				continue;
			}
			const Key& first = p.GroupAt(slotGroup[slot]).key;
			for (int w = 0; w < kKeyWords; ++w) {
				differs[w] += first.w[w] != group.key.w[w] ? group.count : 0u;
			}
		}
	}
};

inline void FormatSplits(const Splits& s, char* out, std::size_t size) {
	int used = std::snprintf(out, size, "Route: draw instancing splits - %llu records waited in %llu groups; by pass and mesh alone %llu groups; records in a group apart from their mesh's first by:",
	                         static_cast<unsigned long long>(s.records), static_cast<unsigned long long>(s.groups), static_cast<unsigned long long>(s.meshGroups));
	for (int w = 0; w < kKeyWords && used > 0 && static_cast<std::size_t>(used) < size; ++w) {
		if (s.differs[w] != 0) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %llu", KeyWordName(w), static_cast<unsigned long long>(s.differs[w]));
		}
	}
}

// ---- Which shader, pass id and flags draw with SLS2003 ---------------------------
//
// Learned from the records drawn at once: the pass object the shader drew
// with (shader +0x3C) and whether the device held SLS2003's programs then. A
// key seen with two pass objects or two kinds of programs is unstable and
// never predicted again; so is one whose instanced draw found other programs
// (Forget).
struct PredictKey {
	std::uint32_t shader = 0;
	std::uint32_t pass = 0;
	std::uint32_t flags = 0;
};

struct Prediction {
	PredictKey key;
	std::uint32_t passObject = 0;
	bool sls = false;
	bool unstable = false;
	std::uint32_t seen = 0;
};

class Predictions {
public:
	static constexpr std::uint32_t kSlots = 1024;  // a power of two

	const Prediction* Find(const PredictKey& k) const {
		const std::uint32_t s = SlotOf(k);
		return s != kNoSlot && used_[s] ? &table_[s] : nullptr;
	}

	bool Predicts(const PredictKey& k) const {
		const Prediction* p = Find(k);
		return p != nullptr && p->sls && !p->unstable;
	}

	void Note(const PredictKey& k, std::uint32_t passObject, bool sls) {
		const std::uint32_t s = SlotOf(k);
		if (s == kNoSlot) {
			return;
		}
		Prediction& p = table_[s];
		if (!used_[s]) {
			used_[s] = true;
			p = Prediction{k, passObject, sls, false, 1};
			return;
		}
		if (p.passObject != passObject || p.sls != sls) {
			p.unstable = true;
		}
		++p.seen;
	}

	void Forget(const PredictKey& k) {
		const std::uint32_t s = SlotOf(k);
		if (s != kNoSlot && used_[s]) {
			table_[s].unstable = true;
		}
	}

private:
	static constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;
	static bool Same(const PredictKey& a, const PredictKey& b) { return a.shader == b.shader && a.pass == b.pass && a.flags == b.flags; }

	// The key's slot, or the first free one on its probe; kNoSlot when the
	// table is full and the key not in it.
	std::uint32_t SlotOf(const PredictKey& k) const {
		const std::uint32_t words[3] = {k.shader, k.pass, k.flags};
		std::uint32_t s = HashWords(words, 3) & (kSlots - 1);
		for (std::uint32_t probe = 0; probe < kSlots; ++probe) {
			if (!used_[s] || Same(table_[s].key, k)) {
				return s;
			}
			s = (s + 1) & (kSlots - 1);
		}
		return kNoSlot;
	}

	Prediction table_[kSlots];
	bool used_[kSlots] = {};
};

// ---- The instance transform and the constants taken to its space -----------------

// The world transform's rows as 0x00761AE0 packs them for the engine's
// ModelViewProj (read 2026-11-12): the NiTransform's rotation times its
// scale, its translation minus the origin 0x00B3F92C, each element one float
// operation (the x87's exact product or difference, rounded once to float on
// the store - the same value as one IEEE single operation). Row r acts on a
// column position: world = rows * (p, 1).
struct InstanceRows {
	float r[3][4];
};

inline InstanceRows PackWorld(const float rotation[9], const float translation[3], float scale, const float origin[3]) {
	InstanceRows w;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			const float element = rotation[3 * row + col] * scale;
			w.r[row][col] = element;
		}
		const float moved = translation[row] - origin[row];
		w.r[row][3] = moved;
	}
	return w;
}

// What the instance stream carries per copy: the rows above, and a fourth
// float4 - the absolute translation and the scale, for the second form of the
// shadow rows below.
struct InstanceData {
	InstanceRows world;
	float extra[4];
};
static_assert(sizeof(InstanceData) == 64, "the instance stream is four float4");

inline InstanceData PackInstance(const float rotation[9], const float translation[3], float scale, const float origin[3]) {
	InstanceData d;
	d.world = PackWorld(rotation, translation, scale, origin);
	d.extra[0] = translation[0];
	d.extra[1] = translation[1];
	d.extra[2] = translation[2];
	d.extra[3] = scale;
	return d;
}

struct Mat4 {
	double m[4][4];
};

// The second form the engine's c28..c31 take (measured 2026-11-12 in mode 1:
// the registers of a draw whose shadow projection 0x007C9230 did not compute
// were the draw's own world - 0x00B45560 of B40 in re/functions/0x007C9230.md,
// "world as rows (R row | t), last row (0, 0, 0, scale)"): the rotation
// without the scale, the absolute translation, and (0, 0, 0, scale). That is
// not one affine map of the packed world for copies of different scale, so
// the instanced draw computes it per copy (ShadowMode 1).
inline void WorldRows(const float rotation[9], const float translation[3], float scale, double out[4][4]) {
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			out[row][col] = rotation[3 * row + col];
		}
		out[row][3] = translation[row];
	}
	out[3][0] = 0.0;
	out[3][1] = 0.0;
	out[3][2] = 0.0;
	out[3][3] = scale;
}

inline Mat4 ToMat4(const InstanceRows& w) {
	Mat4 out{};
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 4; ++col) {
			out.m[row][col] = w.r[row][col];
		}
	}
	out.m[3][3] = 1.0;
	return out;
}

inline Mat4 ToMat4FromRows(const float rows[4][4]) {
	Mat4 out{};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			out.m[row][col] = rows[row][col];
		}
	}
	return out;
}

inline Mat4 Product(const Mat4& a, const Mat4& b) {
	Mat4 out{};
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			double sum = 0.0;
			for (int k = 0; k < 4; ++k) {
				sum += a.m[i][k] * b.m[k][j];
			}
			out.m[i][j] = sum;
		}
	}
	return out;
}

// The inverse of an affine transform (rows of rotation-scale | translation),
// through the 3x3 cofactors; false when the 3x3 part is singular.
inline bool InvertAffine(const InstanceRows& w, Mat4* out) {
	double a[3][3];
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			a[i][j] = w.r[i][j];
		}
	}
	const double c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
	const double c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
	const double c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
	const double det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
	if (!(std::fabs(det) > 1e-30)) {
		return false;
	}
	const double inv = 1.0 / det;
	double b[3][3];
	b[0][0] = c00 * inv;
	b[1][0] = c01 * inv;
	b[2][0] = c02 * inv;
	b[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * inv;
	b[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * inv;
	b[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * inv;
	b[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * inv;
	b[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * inv;
	b[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * inv;
	*out = Mat4{};
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			out->m[i][j] = b[i][j];
		}
		out->m[i][3] = -(b[i][0] * w.r[0][3] + b[i][1] * w.r[1][3] + b[i][2] * w.r[2][3]);
	}
	out->m[3][3] = 1.0;
	return true;
}

// The vertex registers read back after the leader's constant upload, c0..c33
// (SLS2003.vso's constant table: ModelViewProj c0..c3, LightDirection c13,
// FogParam c23, FogColor c24, ShadowProj c28..c31, ShadowProjData c32,
// ShadowProjTransform c33).
constexpr int kVertexRegisters = 35;  // c0..c34: c34 is the instanced program's ShadowMode, put back like the rest
constexpr int kRegisterViewProj = 0;
constexpr int kRegisterLight = 13;
constexpr int kRegisterShadow = 28;
constexpr int kRegisterShadowMode = 34;
constexpr int kShadowRowsRead[3] = {0, 1, 3};  // SLS2003.vso reads c28, c29 and c31

// The largest RowError of the rows the program reads, c28, c29 and c31,
// against `expected`.
inline double ShadowRowsError(const float (&vs)[kVertexRegisters][4], const double expected[4][4]);

// What the instanced draw uploads in place of the engine's model-space
// registers: ModelViewProj and ShadowProj with the leader's world taken out
// (they act on the instance space then, the instance rows giving each copy
// its own world), and the light direction taken back to the instance space by
// the leader's rotation and scale (only its direction matters: the program
// normalises).
struct Derived {
	float viewProj[4][4];
	float light[4];
	float shadow[4][4];
};

inline bool Derive(const float (&vs)[kVertexRegisters][4], const InstanceRows& leader, Derived* out) {
	Mat4 inverse;
	if (!InvertAffine(leader, &inverse)) {
		return false;
	}
	float mvpRows[4][4];
	float shadowRows[4][4];
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			mvpRows[r][c] = vs[kRegisterViewProj + r][c];
			shadowRows[r][c] = vs[kRegisterShadow + r][c];
		}
	}
	const Mat4 viewProj = Product(ToMat4FromRows(mvpRows), inverse);
	const Mat4 shadow = Product(ToMat4FromRows(shadowRows), inverse);
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			out->viewProj[r][c] = static_cast<float>(viewProj.m[r][c]);
			out->shadow[r][c] = static_cast<float>(shadow.m[r][c]);
		}
	}
	const float* l = vs[kRegisterLight];
	for (int r = 0; r < 3; ++r) {
		out->light[r] = static_cast<float>(static_cast<double>(leader.r[r][0]) * l[0] + static_cast<double>(leader.r[r][1]) * l[1] +
		                                   static_cast<double>(leader.r[r][2]) * l[2]);
	}
	out->light[3] = 0.0f;
	return true;
}

// What the engine would upload for an instance, by the derived constants: the
// check of mode 1.
struct Expected {
	double mvp[4][4];
	double shadow[4][4];
	double light[3];  // in the instance's model space, up to its length
};

inline Expected ExpectFor(const Derived& d, const InstanceRows& w) {
	Expected e{};
	const Mat4 world = ToMat4(w);
	const Mat4 mvp = Product(ToMat4FromRows(d.viewProj), world);
	const Mat4 shadow = Product(ToMat4FromRows(d.shadow), world);
	std::memcpy(e.mvp, mvp.m, sizeof(e.mvp));
	std::memcpy(e.shadow, shadow.m, sizeof(e.shadow));
	Mat4 inverse;
	if (InvertAffine(w, &inverse)) {
		for (int r = 0; r < 3; ++r) {
			e.light[r] = inverse.m[r][0] * d.light[0] + inverse.m[r][1] * d.light[1] + inverse.m[r][2] * d.light[2];
		}
	}
	return e;
}

// The largest difference of a row, relative to the row's largest element.
inline double RowError(const float actual[4], const double expected[4]) {
	double size = 0.0;
	double worst = 0.0;
	for (int i = 0; i < 4; ++i) {
		size = std::fmax(size, std::fabs(expected[i]));
		worst = std::fmax(worst, std::fabs(static_cast<double>(actual[i]) - expected[i]));
	}
	return size > 0.0 ? worst / size : worst;
}

inline double ShadowRowsError(const float (&vs)[kVertexRegisters][4], const double expected[4][4]) {
	double worst = 0.0;
	for (const int row : kShadowRowsRead) {
		worst = std::fmax(worst, RowError(vs[kRegisterShadow + row], expected[row]));
	}
	return worst;
}

// 1 - cos of the angle between two directions; 0 when either has no length.
inline double DirectionError(const float actual[3], const double expected[3]) {
	const double a[3] = {actual[0], actual[1], actual[2]};
	const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	const double le = std::sqrt(expected[0] * expected[0] + expected[1] * expected[1] + expected[2] * expected[2]);
	if (la == 0.0 || le == 0.0) {
		return 0.0;
	}
	return 1.0 - (a[0] * expected[0] + a[1] * expected[1] + a[2] * expected[2]) / (la * le);
}

// ---- Mode 1's comparison ------------------------------------------------------------

enum class Compared : std::uint8_t {
	kMvp,         // c0..c3 against the leader's ViewProj times the instance rows
	kLight,       // c13's direction against the leader's light taken to the instance
	kShadow,      // c28, c29, c31 against the leader's ShadowProj times the instance rows
	kShadowWorld,  // c28, c29, c31 against the instance's own world rows (the second form)
	kShadowEither,  // c28, c29, c31 in neither form
	kFogAndData,  // c23, c24, c32, c33 equal to the leader's
	kPixel,       // the pixel registers c1, c2, c7 equal to the leader's
	kTextures,    // the textures on stages 0, 1, 6 and 7 the same objects
	kPrograms,    // the vertex and pixel program and the declaration the same objects
	kCount,
};

constexpr const char* ComparedName(Compared c) {
	constexpr const char* kNames[] = {"ModelViewProj", "light direction", "ShadowProj derived", "ShadowProj as world rows", "ShadowProj in neither form",
	                                  "fog and shadow data", "pixel constants", "textures", "programs", "?"};
	return kNames[static_cast<int>(c)];
}

// The tolerance of the two rebuilt kinds (stated for the ledger; the measured
// largest errors go beside it): a row's difference relative to its size, and
// 1 - cos of the light's angle.
constexpr double kRowTolerance = 1e-4;
constexpr double kDirectionTolerance = 1e-6;

struct VerifyCounts {
	static constexpr int kKinds = static_cast<int>(Compared::kCount);
	std::uint64_t checked[kKinds] = {};
	std::uint64_t over[kKinds] = {};
	double largest[kKinds] = {};

	void Note(Compared c, double error) {
		const int i = static_cast<int>(c);
		++checked[i];
		const double tolerance = c == Compared::kLight ? kDirectionTolerance : kRowTolerance;
		over[i] += error > tolerance ? 1u : 0u;
		largest[i] = std::fmax(largest[i], error);
	}
	void NoteEqual(Compared c, bool equal) {
		const int i = static_cast<int>(c);
		++checked[i];
		over[i] += equal ? 0u : 1u;
	}
};

inline void FormatVerify(const VerifyCounts& v, char* out, std::size_t size) {
	int used = std::snprintf(out, size, "Route: draw instancing verify -");
	for (int i = 0; i < VerifyCounts::kKinds && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		const bool rebuilt = i <= static_cast<int>(Compared::kShadowWorld);
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), rebuilt ? "%s %s %llu checked, %llu over (largest %.2e)" : "%s %s %llu checked, %llu over",
		                      i == 0 ? "" : ";", ComparedName(static_cast<Compared>(i)), static_cast<unsigned long long>(v.checked[i]),
		                      static_cast<unsigned long long>(v.over[i]), v.largest[i]);
	}
}

// ---- The counts and the summary line --------------------------------------------------

enum class Flush : std::uint8_t {
	kListEnd,  // the list's draw returned
	kOrder,    // a record drawn at once depends on the order
	kFull,     // the pending records reached a limit
	kCount,
};

enum class Fallback : std::uint8_t {
	kPassObject,  // the leader drew with another pass object than predicted
	kProgram,     // the device held other programs than SLS2003's
	kPlainDraw,   // the leader's draw was not indexed
	kNotDrawn,    // the leader's draw did not reach the whole draw's device call
	kDevice,      // a device call or a resource failed
	kRoom,        // no room left in the instance buffer this flush
	kCount,
};

constexpr const char* FallbackName(Fallback f) {
	constexpr const char* kNames[] = {"pass object", "programs", "not indexed", "not drawn", "device", "room", "?"};
	return kNames[static_cast<int>(f)];
}

struct Counts {
	std::uint64_t frames = 0;
	std::uint64_t records = 0;  // records at the site inside a list
	std::uint64_t why[static_cast<int>(Why::kCount)] = {};
	std::uint64_t flushes[static_cast<int>(Flush::kCount)] = {};
	std::uint64_t groups = 0;       // groups flushed
	std::uint64_t groupsMulti = 0;  // of them with two or more records
	std::uint64_t instances = 0;    // records in those
	std::uint64_t drawnInstanced = 0;  // groups drawn as one instanced draw
	std::uint64_t fellBack[static_cast<int>(Fallback::kCount)] = {};
	std::uint64_t nestedLists = 0;  // lists begun inside a list's draw: drawn at once, nothing held back
	std::uint64_t learned = 0;      // predictions noted
	std::uint64_t siteTicks = 0;
	std::uint64_t flushTicks = 0;
	std::uint64_t parityFrames[2] = {};  // frames with the instancing off [0] and on [1]
	std::uint64_t listTicks[2] = {};     // the time of the pass lists' draws (all of them, the flushes inside) in those frames
	std::uint64_t instancedCalls = 0;  // instanced device draws (each eye and strip one)
	std::uint64_t instancedTicks = 0;  // their time with everything they set and put back
};

inline void Format(const Counts& c, Mode mode, bool onInWindow, double ticksPerMillisecond, char* out, std::size_t size) {
	const double frames = c.frames != 0 ? static_cast<double>(c.frames) : 1.0;
	int used = std::snprintf(out, size, "Route: draw instancing (%s%s) - %.1f records a frame in pass lists, %.1f waited; at once:", ModeName(mode),
	                         onInWindow ? "" : ", off in this window", static_cast<double>(c.records) / frames,
	                         static_cast<double>(c.why[static_cast<int>(Why::kWaits)]) / frames);
	for (int i = 1; i < static_cast<int>(Why::kCount) && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		if (c.why[i] != 0) {
			used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %.1f,", WhyName(static_cast<Why>(i)), static_cast<double>(c.why[i]) / frames);
		}
	}
	if (used > 0 && static_cast<std::size_t>(used) < size) {
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used),
		                      " a frame; flushes a frame %.1f (list end %.1f, order %.1f, full %.1f), nested lists %llu; %llu of %llu groups of two or more drawn instanced (%llu records in them, %llu groups in all), fell back:",
		                      static_cast<double>(c.flushes[0] + c.flushes[1] + c.flushes[2]) / frames, static_cast<double>(c.flushes[0]) / frames,
		                      static_cast<double>(c.flushes[1]) / frames, static_cast<double>(c.flushes[2]) / frames, static_cast<unsigned long long>(c.nestedLists),
		                      static_cast<unsigned long long>(c.drawnInstanced), static_cast<unsigned long long>(c.groupsMulti),
		                      static_cast<unsigned long long>(c.instances), static_cast<unsigned long long>(c.groups));
	}
	for (int i = 0; i < static_cast<int>(Fallback::kCount) && used > 0 && static_cast<std::size_t>(used) < size; ++i) {
		used += std::snprintf(out + used, size - static_cast<std::size_t>(used), " %s %llu", FallbackName(static_cast<Fallback>(i)), static_cast<unsigned long long>(c.fellBack[i]));
	}
	if (used > 0 && static_cast<std::size_t>(used) < size) {
		const double tpm = ticksPerMillisecond > 0.0 ? ticksPerMillisecond : 1.0;
		std::snprintf(out + used, size - static_cast<std::size_t>(used),
		              "; %llu predictions noted; the record step %.3f ms a frame, the flushes %.3f ms a frame (the replayed draws inside), of that the instanced device draws %.3f ms (%.1f a frame, %.2f us each)",
		              static_cast<unsigned long long>(c.learned), static_cast<double>(c.siteTicks) / tpm / frames, static_cast<double>(c.flushTicks) / tpm / frames,
		              static_cast<double>(c.instancedTicks) / tpm / frames, static_cast<double>(c.instancedCalls) / frames,
		              c.instancedCalls != 0 ? static_cast<double>(c.instancedTicks) / tpm * 1000.0 / static_cast<double>(c.instancedCalls) : 0.0);
		used = static_cast<int>(std::strlen(out));
	}
	if (used > 0 && static_cast<std::size_t>(used) < size && c.parityFrames[0] != 0 && c.parityFrames[1] != 0) {
		const double tpm = ticksPerMillisecond > 0.0 ? ticksPerMillisecond : 1.0;
		std::snprintf(out + used, size - static_cast<std::size_t>(used), "; the pass lists' draws %.3f ms a frame with it on (%llu frames), %.3f ms off (%llu frames)",
		              static_cast<double>(c.listTicks[1]) / tpm / static_cast<double>(c.parityFrames[1]), static_cast<unsigned long long>(c.parityFrames[1]),
		              static_cast<double>(c.listTicks[0]) / tpm / static_cast<double>(c.parityFrames[0]), static_cast<unsigned long long>(c.parityFrames[0]));
	}
}

}  // namespace obng::plugin::draw_instancing
