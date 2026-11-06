#pragma once

// The grass fade fix's decisions ([Fix] GrassFade, GrassFadeFix.cpp), free
// of the engine and of Direct3D (plugin/tests/GrassFadePatchLogicTest.cpp).
//
// The fault (knowledge/culling-and-draw-setup.md, "The grass fades by the
// length of the clip-space position"): the game's grass vertex shaders fade
// a blade by sqrt(dp4(clip, clip)) of its clip-space position against
// AlphaParam. That length is 1.41 times the distance to the camera in the
// middle of the view and less towards the picture's edge, the more the
// wider the frustum - in a headset's frustum a clump between 0.71 and 0.99
// times fGrassEndDistance comes and goes as the head turns (measured, runs
// obng-sewer-exit-single/20261106-111726 and -dual/20261106-111906).
//
// The fix: a twin of each grass vertex shader that takes the length of the
// view-space position instead. The first two rows of ModelViewProj carry
// the projection's two scales as the length of their xyz (row 0's is P11,
// row 1's P22, for a world matrix without scale - measured on the grass's:
// c9.xyz 0.868 long with P11 0.8684, run obng-single-pass-meadow/
// 20261106-120731), so the twin needs nothing uploaded:
//
//     mov rA.xyz, cRow0         ; inserted, rA and rB two unused temps
//     dp3 rA.x, rA, rA          ;   |row 0|^2 = P11^2
//     mov rB.xyz, cRow1
//     dp3 rA.y, rB, rB          ;   |row 1|^2 = P22^2
//     rsq rA.x, rA.x            ;   1 / P11
//     rsq rA.y, rA.y            ;   1 / P22
//     mul rA.xy, rClip, rA      ;   view x, view y
//     mov rA.z, rClip.w         ;   view depth
//     dp3 rLen, rA, rA          ; was dp4 rLen, rClip, rClip
//     add rLen, rLen, rLen      ; inserted for the game's reach: twice the square
//
// Twice the squared distance is what the game's shader has in the middle of
// the view (clip z and clip w both the depth there), so with the add the
// grass reaches as far as the game has it straight ahead, and as far
// everywhere else in the picture (Reach::kGame, the default). Without the
// add the length is the distance itself and fGrassStartFadeDistance /
// fGrassEndDistance are distances in units (Reach::kIni; grass then reaches
// 41 % further).
//
// A register of its own for a scale was the first design and does not
// work: the engine uploads the instances' data from InstanceData's register
// (c20) far beyond the 23 registers its constant map lists - c245 held an
// instance's position at the grass draw (measured, the run above) - and
// twelve of the game's 24 grass shaders name every register below c20.
//
// What a grass vertex shader is, structurally, so no byte image of the
// game's shaders is needed: version 2.0 or later, a constant table naming
// AlphaParam, ModelViewProj, InstanceData and WindData (the distant trees'
// DISTLOD2002/2003.vso name the first three too, and have no wind), exactly
// one dp4 of a temp register with itself, and that register moved to oPos.
// Anything else is left as it is, with the reason.
//
// The Direct3D 9 shader byte code as far as it is read here (d3d9types.h,
// "Direct3D Shader Codes"): the version token 0xFFFE0000 | major << 8 |
// minor for a vertex shader; then instructions - the opcode in bits 0..15,
// from version 2.0 the number of following tokens in bits 24..27; a comment
// is opcode 0xFFFE with its length in tokens in bits 16..30; the end token
// 0x0000FFFF. A parameter token has bit 31 set, the register number in bits
// 0..10, the register type in bits 28..30 (low three bits) and 11..12 (high
// two); a destination's write mask in bits 16..19, a source's swizzle in
// bits 16..23 (0xE4 = xyzw) and its modifier in bits 24..27.
//
// The constant table (d3dx9shader.h, D3DXSHADER_CONSTANTTABLE /
// D3DXSHADER_CONSTANTINFO): a comment starting with 'CTAB'; then, offsets
// counted from after that tag, the dwords Size, Creator, Version, Constants,
// ConstantInfo, Flags, Target; at ConstantInfo one 20-byte record per
// constant - Name (an offset), the words RegisterSet (2 = float),
// RegisterIndex, RegisterCount and Reserved, the dwords TypeInfo and
// DefaultValue.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace obng::plugin::grass_fade {

constexpr std::uint32_t kEndToken = 0x0000FFFFu;
constexpr std::uint32_t kOpcodeMask = 0x0000FFFFu;
constexpr std::uint32_t kOpComment = 0xFFFEu;
constexpr std::uint32_t kOpMov = 1;
constexpr std::uint32_t kOpAdd = 2;
constexpr std::uint32_t kOpMul = 5;
constexpr std::uint32_t kOpRsq = 7;
constexpr std::uint32_t kOpDp3 = 8;
constexpr std::uint32_t kOpDp4 = 9;
constexpr std::uint32_t kOpDcl = 0x1F;
constexpr std::uint32_t kOpDefB = 0x2F;
constexpr std::uint32_t kOpDefI = 0x30;
constexpr std::uint32_t kOpDef = 0x51;

constexpr std::uint32_t kTypeTemp = 0;
constexpr std::uint32_t kTypeConst = 2;
constexpr std::uint32_t kTypeRastOut = 4;  // register 0 is oPos

constexpr std::uint32_t kSwizzleXyzw = 0xE4;
constexpr std::uint32_t kTempRegisters = 12;  // vs_2_0: r0..r11
constexpr std::uint32_t kRegisterSetFloat = 2;
constexpr std::uint32_t kConstantInfoBytes = 20;

constexpr std::uint32_t RegisterNumber(std::uint32_t token) {
	return token & 0x7FFu;
}

constexpr std::uint32_t RegisterType(std::uint32_t token) {
	return ((token >> 28) & 0x7u) | ((token >> 8) & 0x18u);
}

constexpr bool IsVertexShader2OrLater(std::uint32_t versionToken) {
	return (versionToken & 0xFFFF0000u) == 0xFFFE0000u && ((versionToken >> 8) & 0xFFu) >= 2;
}

// The tokens the patch writes.
constexpr std::uint32_t OpToken(std::uint32_t opcode, std::uint32_t parameters) {
	return opcode | (parameters << 24);
}
constexpr std::uint32_t TempDest(std::uint32_t reg, std::uint32_t mask) {
	return 0x80000000u | (mask << 16) | reg;
}
constexpr std::uint32_t TempSource(std::uint32_t reg, std::uint32_t swizzle = kSwizzleXyzw) {
	return 0x80000000u | (swizzle << 16) | reg;
}
constexpr std::uint32_t ConstSource(std::uint32_t reg) {
	return 0xA0000000u | (kSwizzleXyzw << 16) | reg;
}
// The swizzle that reads component c (0 = x .. 3 = w) four times.
constexpr std::uint32_t Replicate(std::uint32_t c) {
	return c | (c << 2) | (c << 4) | (c << 6);
}

// How far the grass reaches with the fix.
enum class Reach : std::uint8_t {
	kGame,  // as far as the game has it in the middle of the view (the length is sqrt(2) times the distance)
	kIni,   // fGrassStartFadeDistance / fGrassEndDistance as distances in units
};

constexpr const char* ReachName(Reach r) {
	return r == Reach::kIni ? "ini" : "game";
}

// [Fix] GrassFadeReach from the INI's text; anything but "ini" and "game"
// is unknown and read as game (said in the log by the caller).
inline Reach ReachFromIni(const char* value, bool* known) {
	if (std::strcmp(value, "ini") == 0) {
		*known = true;
		return Reach::kIni;
	}
	*known = std::strcmp(value, "game") == 0;
	return Reach::kGame;
}

enum class Verdict : std::uint8_t {
	kPatched,
	kNotVertexShader2,  // not a vertex shader of version 2.0 or later (or no byte code at all)
	kMalformed,         // the byte code ends inside an instruction or has no end token
	kNotGrass,          // no constant table that names the grass's four constants
	kNoClipLength,      // no dp4 of a temp register with itself, into one component, whose register goes to oPos
	kAmbiguous,         // more than one such dp4
	kNoFreeTemp,        // the shader leaves fewer than two temp registers unused
};

constexpr const char* VerdictName(Verdict v) {
	switch (v) {
	case Verdict::kPatched: return "patched";
	case Verdict::kNotVertexShader2: return "not a vertex shader of version 2.0 or later";
	case Verdict::kMalformed: return "byte code that ends inside an instruction";
	case Verdict::kNotGrass: return "no grass constant table";
	case Verdict::kNoClipLength: return "no length of the clip position found";
	case Verdict::kAmbiguous: return "more than one length of a temp register";
	default: return "fewer than two free temp registers";
	}
}

// One instruction's place in the byte code.
struct Instruction {
	std::size_t at = 0;      // index of the opcode token
	std::size_t tokens = 0;  // the opcode token and its parameters
	std::uint32_t opcode = 0;
};

// Walks the instructions (comments included, the end token excluded);
// false when the byte code ends inside one or has no end token.
template <class Visit>
bool ForInstructions(const std::uint32_t* code, std::size_t count, Visit&& visit) {
	std::size_t i = 1;  // after the version token
	while (i < count) {
		const std::uint32_t token = code[i];
		if (token == kEndToken) {
			return true;
		}
		Instruction in;
		in.at = i;
		in.opcode = token & kOpcodeMask;
		in.tokens = 1 + (in.opcode == kOpComment ? ((token >> 16) & 0x7FFFu) : ((token >> 24) & 0xFu));
		if (in.tokens > count - i) {
			return false;
		}
		visit(in);
		i += in.tokens;
	}
	return false;
}

// What a comment's constant table says, as far as the fix needs it.
struct ConstantTable {
	bool found = false;  // the comment is a well-formed constant table
	bool alphaParam = false;
	bool modelViewProj = false;
	bool windData = false;
	bool instanceData = false;
	std::uint32_t modelViewProjRegister = 0;  // ModelViewProj's first float register (its row 0)

	constexpr bool IsGrass() const { return found && alphaParam && modelViewProj && windData && instanceData; }
};

// Reads the table of a comment's words (the tokens after the comment
// token). Anything out of the comment's bounds makes it no table.
inline ConstantTable ReadConstantTable(const std::uint32_t* words, std::size_t count) {
	ConstantTable table;
	const auto* bytes = reinterpret_cast<const unsigned char*>(words);
	const std::size_t size = count * sizeof(std::uint32_t);
	if (size < 4 + 28 || std::memcmp(bytes, "CTAB", 4) != 0) {
		return table;
	}
	const unsigned char* base = bytes + 4;
	const std::size_t room = size - 4;
	std::uint32_t head[7];
	std::memcpy(head, base, sizeof(head));
	const std::uint32_t constants = head[3];
	const std::uint32_t info = head[4];
	if (info > room || constants > (room - info) / kConstantInfoBytes) {
		return table;
	}
	for (std::uint32_t i = 0; i < constants; ++i) {
		const unsigned char* record = base + info + i * kConstantInfoBytes;
		std::uint32_t nameOffset = 0;
		std::uint16_t fields[3];
		std::memcpy(&nameOffset, record, sizeof(nameOffset));
		std::memcpy(fields, record + 4, sizeof(fields));
		if (nameOffset >= room) {
			return ConstantTable{};
		}
		const char* name = reinterpret_cast<const char*>(base + nameOffset);
		const std::size_t most = room - nameOffset;
		const auto is = [name, most](const char* word) {
			const std::size_t n = std::strlen(word) + 1;  // with the terminator: the whole name
			return n <= most && std::memcmp(name, word, n) == 0;
		};
		if (fields[0] != kRegisterSetFloat) {
			continue;
		}
		table.alphaParam = table.alphaParam || is("AlphaParam");
		table.windData = table.windData || is("WindData");
		table.instanceData = table.instanceData || is("InstanceData");
		if (is("ModelViewProj") && fields[2] >= 4) {
			table.modelViewProj = true;
			table.modelViewProjRegister = fields[1];
		}
	}
	table.found = true;
	return table;
}

// The index range of an instruction's tokens that are register parameters:
// a definition has one (its floats, ints or bool follow and are not
// registers), a declaration's first token is its usage.
constexpr std::size_t FirstRegisterToken(const Instruction& in) {
	return in.at + (in.opcode == kOpDcl ? 2 : 1);
}

constexpr std::size_t EndRegisterToken(const Instruction& in) {
	return in.opcode == kOpDef || in.opcode == kOpDefI || in.opcode == kOpDefB ? in.at + 2 : in.at + in.tokens;
}

struct Patch {
	Verdict verdict = Verdict::kNotVertexShader2;
	std::uint32_t clipRegister = 0;  // the temp register that goes to oPos
	std::uint32_t tempRegister = 0;  // rA; rB is the next
	std::uint32_t rowRegister = 0;   // ModelViewProj's row 0; row 1 is the next
	std::size_t at = 0;              // index of the dp4's opcode token in the original
	std::size_t added = 0;           // tokens the twin has more than the original
};

// Decides and, when the verdict is kPatched, writes the twin's byte code
// into *out.
inline Patch PatchGrassFade(const std::uint32_t* code, std::size_t count, Reach reach, std::vector<std::uint32_t>* out) {
	Patch patch;
	if (code == nullptr || count < 2 || !IsVertexShader2OrLater(code[0])) {
		patch.verdict = Verdict::kNotVertexShader2;
		return patch;
	}
	ConstantTable table;
	std::uint32_t lengths = 0;
	std::uint32_t highestTemp = 0;
	bool anyTemp = false;
	std::uint32_t positionSources = 0;  // bit r: temp register r is moved to oPos
	std::size_t end = 0;
	const bool whole = ForInstructions(code, count, [&](const Instruction& in) {
		end = in.at + in.tokens;
		if (in.opcode == kOpComment) {
			if (!table.found) {
				table = ReadConstantTable(code + in.at + 1, in.tokens - 1);
			}
			return;
		}
		for (std::size_t i = FirstRegisterToken(in); i < EndRegisterToken(in); ++i) {
			const std::uint32_t token = code[i];
			if (RegisterType(token) == kTypeTemp) {
				anyTemp = true;
				if (RegisterNumber(token) > highestTemp) {
					highestTemp = RegisterNumber(token);
				}
			}
		}
		if (in.opcode == kOpDp4 && in.tokens == 4) {
			const std::uint32_t a = code[in.at + 2];
			const std::uint32_t b = code[in.at + 3];
			if (a == b && RegisterType(a) == kTypeTemp && ((a >> 16) & 0xFFu) == kSwizzleXyzw && ((a >> 24) & 0xFu) == 0 &&
			    RegisterType(code[in.at + 1]) == kTypeTemp) {
				++lengths;
				patch.at = in.at;
				patch.clipRegister = RegisterNumber(a);
			}
		}
		if (in.opcode == kOpMov && in.tokens == 3 && RegisterType(code[in.at + 1]) == kTypeRastOut && RegisterNumber(code[in.at + 1]) == 0 &&
		    RegisterType(code[in.at + 2]) == kTypeTemp && RegisterNumber(code[in.at + 2]) < 32) {
			positionSources |= 1u << RegisterNumber(code[in.at + 2]);
		}
	});
	if (!whole) {
		patch.verdict = Verdict::kMalformed;
		return patch;
	}
	if (!table.IsGrass()) {
		patch.verdict = Verdict::kNotGrass;
		return patch;
	}
	if (lengths > 1) {
		patch.verdict = Verdict::kAmbiguous;
		return patch;
	}
	if (lengths == 0 || patch.clipRegister >= 32 || (positionSources >> patch.clipRegister & 1u) == 0) {
		patch.verdict = Verdict::kNoClipLength;
		return patch;
	}
	// The length's destination must name one component, which the add reads back.
	const std::uint32_t destination = code[patch.at + 1];
	const std::uint32_t mask = (destination >> 16) & 0xFu;
	if (mask != 1 && mask != 2 && mask != 4 && mask != 8) {
		patch.verdict = Verdict::kNoClipLength;
		return patch;
	}
	patch.tempRegister = anyTemp ? highestTemp + 1 : 0;
	if (patch.tempRegister + 1 >= kTempRegisters) {
		patch.verdict = Verdict::kNoFreeTemp;
		return patch;
	}
	patch.rowRegister = table.modelViewProjRegister;
	const std::uint32_t a = patch.tempRegister;
	const std::uint32_t b = a + 1;
	const std::uint32_t clip = patch.clipRegister;
	const std::uint32_t component = mask == 1 ? 0u : (mask == 2 ? 1u : (mask == 4 ? 2u : 3u));
	const std::uint32_t length = RegisterNumber(destination);
	const std::uint32_t inserted[] = {
	    OpToken(kOpMov, 2), TempDest(a, 7), ConstSource(patch.rowRegister),         // mov rA.xyz, cRow0
	    OpToken(kOpDp3, 3), TempDest(a, 1), TempSource(a), TempSource(a),           // dp3 rA.x, rA, rA
	    OpToken(kOpMov, 2), TempDest(b, 7), ConstSource(patch.rowRegister + 1),     // mov rB.xyz, cRow1
	    OpToken(kOpDp3, 3), TempDest(a, 2), TempSource(b), TempSource(b),           // dp3 rA.y, rB, rB
	    OpToken(kOpRsq, 2), TempDest(a, 1), TempSource(a, Replicate(0)),            // rsq rA.x, rA.x
	    OpToken(kOpRsq, 2), TempDest(a, 2), TempSource(a, Replicate(1)),            // rsq rA.y, rA.y
	    OpToken(kOpMul, 3), TempDest(a, 3), TempSource(clip), TempSource(a),        // mul rA.xy, rClip, rA
	    OpToken(kOpMov, 2), TempDest(a, 4), TempSource(clip, Replicate(3)),         // mov rA.z, rClip.w
	    OpToken(kOpDp3, 3), destination, TempSource(a), TempSource(a),              // dp3 rLen, rA, rA (in the dp4's place)
	};
	const std::uint32_t doubled[] = {
	    OpToken(kOpAdd, 3), destination, TempSource(length, Replicate(component)), TempSource(length, Replicate(component)),  // add rLen, rLen, rLen
	};
	const std::size_t insertedCount = sizeof(inserted) / sizeof(inserted[0]);
	const std::size_t doubledCount = reach == Reach::kGame ? sizeof(doubled) / sizeof(doubled[0]) : 0;
	patch.added = insertedCount + doubledCount - 4;
	if (out != nullptr) {
		out->clear();
		out->reserve(count + patch.added);
		out->insert(out->end(), code, code + patch.at);
		out->insert(out->end(), inserted, inserted + insertedCount);
		out->insert(out->end(), doubled, doubled + doubledCount);
		out->insert(out->end(), code + patch.at + 4, code + end + 1);  // the rest and the end token
	}
	patch.verdict = Verdict::kPatched;
	return patch;
}

// What the bind hook remembers of the shaders it has seen, without a lock:
// the hook runs for every SetVertexShader of a frame, thousands of times,
// and nearly always for a shader it knows. A direct-mapped table from the
// shader's address to its twin (0: none); an entry is good for the
// generation it was put in, and the generation moves on whenever anything
// is forgotten (a new shader at a remembered address), which empties the
// table at one stroke. The table is the binding thread's alone; the full
// record behind it (GrassFadeFix.cpp) has the lock.
struct BindCache {
	static constexpr std::size_t kEntries = 256;
	struct Entry {
		std::uintptr_t shader = 0;
		std::uintptr_t twin = 0;
		std::uint32_t generation = 0;
	};
	Entry entry[kEntries] = {};

	// Shader objects are heap blocks: the low bits are alignment, the next carry the difference.
	static constexpr std::size_t Slot(std::uintptr_t shader) { return (shader >> 4 ^ shader >> 12) & (kEntries - 1); }

	// generation is never 0 for a caller (0 marks an empty entry).
	bool Find(std::uintptr_t shader, std::uint32_t generation, std::uintptr_t* twin) const {
		const Entry& e = entry[Slot(shader)];
		if (e.shader != shader || e.generation != generation) {
			return false;
		}
		*twin = e.twin;
		return true;
	}

	void Put(std::uintptr_t shader, std::uintptr_t twin, std::uint32_t generation) { entry[Slot(shader)] = Entry{shader, twin, generation}; }
};

}  // namespace obng::plugin::grass_fade
