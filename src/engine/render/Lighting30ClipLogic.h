#pragma once
// [Probe] Lighting30Clip - the one-pass lighting's light sum clipped at 1, as the game's multi-pass path clips it
// (the plugin's Lighting30Clip.cpp). On with [Probe] Lighting30Kinds only: without it the game is not touched.
//
// What differs between the two paths (read 2026-11-25, shaderpackage004.sdp; measured the same day, knowledge/
// lighting-passes.md "The picture gate outdoors"): ShadowLightShader's first pass ends its light sum with a
// saturate before it multiplies by the texture - SLS2003.pso `mad_sat_pp r2.xyz, r3, r1, c1` (the first light
// x shadow + ambient), SLS2012.pso `add_sat_pp r2.xyz, r1, c1` (two lights + ambient), SLS2005.pso the same with
// the glow in the sum - and BSSM_AD3's sum of three lights goes clipped into the target before the texture pass
// multiplies it. Lighting30Shader's pixel programs add ambient and all lights with a plain `add_pp rL.xyz, rS,
// rL` and multiply by the texture unclipped; only the target clips the product. A surface that faces the noon
// sun has a sum of about 1.4 there: its texture shown beyond its own colour (a rock's top +37 %).
//
// The clip is one bit: the saturate modifier on that add's destination, set in the bytecode the shader package
// holds in memory, before the game makes the program from it. Where: Lighting30Shader's program loader
// 0x007FD560 calls the pixel program maker 0x00801C90 at three places (0x007FEAF0 and 0x007FEB29 with a name
// printed from "SM3%03i.pso", 0x007FEBBC from "SM3LL%03i.pso"); the maker looks the name up in the package
// (0x007DAC70 on the object at 0x00B430B8), takes the bytecode at the entry's +0x104 and hands it to the
// device's CreatePixelShader (slot +0x1A8). The probe stands in those three calls.
//
// Which add: by the program's own constant table, not by a place - the first instruction that takes the
// constant named AmbientColor into a temporary's xyz starts the sum, and the first `add` after it that writes
// that temporary's xyz from itself and another temporary completes it (an instruction in between that reworks
// the temporary from itself - the glow variant's - is passed over). In package 004 that finds one add in each
// of the forty lighting programs (SM3000 .. SM3019, SM3LL000 .. SM3LL019: plain, specular, hair, FaceGen, glow,
// each with and without the projected shadow) and nothing in the nineteen others, which have no AmbientColor
// (tools/re/sm3-clip.py does the same outside the game and shows each program's one changed line).
// In the packages built for ps_3_0 (009, 018, 019) the twelve programs without specular have another shape and
// are left as they are: the rule changes only what it knows. Neither game runs those packages.
//
// The two sources for the addresses: the instructions of the loader and the maker (the maker's `ret 0x18`, the
// three call sites' six pushes with the name fourth), and a second path in the same binary - Lighting30Shader's
// DEBUG programs are made through the same maker with the same argument order (0x007FA090: "ps_1_3",
// "DEBUG.pso"); the entry's layout is the package file's own (a 0x100-byte name, the length, the bytecode -
// tools/re/sdp-shader.py reads the file by it).
//
// 64-bit clean: token spans and plain values, no pointers kept.

#include <cstddef>
#include <cstdint>

namespace obng::engine::render::lighting30_clip {

inline constexpr std::uint32_t kMakerCalls[3] = {0x007FEAF0, 0x007FEB29, 0x007FEBBC};  // in the loader 0x007FD560
inline constexpr std::uint32_t kMaker = 0x00801C90;    // program __thiscall (shader; path, macros, profile, name, 0, 0)
inline constexpr std::uint32_t kPackage = 0x00B430B8;  // the loaded shader package, 0 without one
inline constexpr std::uint32_t kLookup = 0x007DAC70;   // entry __thiscall (package; name), 0 for none
inline constexpr std::uint32_t kEntryLength = 0x100;   // an entry: the name, the bytecode's length in bytes, the bytecode
inline constexpr std::uint32_t kEntryCode = 0x104;
inline constexpr std::uint32_t kMostBytes = 0x10000;   // no program of the packages is a tenth of that

// What OBNG runs with when OBNG.ini has no key. It does something only together with the kinds.
constexpr int Shipped() {
	return 1;
}

constexpr bool On(int ini, bool kindsOn) {
	return kindsOn && ini > 0;
}

// D3D9 shader bytecode, as far as the rule needs it.
inline constexpr std::uint32_t kEnd = 0x0000FFFF;
inline constexpr std::uint32_t kComment = 0xFFFE, kAdd = 2, kDcl = 31, kDef = 81;
inline constexpr std::uint32_t kSaturate = 1u << 20;         // the destination's result modifier
inline constexpr std::uint32_t kRelative = 1u << 13;         // a parameter addressed through another register
inline constexpr std::uint32_t kConstantTable = 0x42415443;  // 'CTAB', the first word of the comment that holds it
inline constexpr std::uint32_t kTemp = 0, kConst = 2;        // register types
inline constexpr std::uint32_t kFloatSet = 2;                // the constant table's register set of float constants
inline constexpr std::uint32_t kNone = 0xFFFFFFFF;

constexpr std::uint32_t OpOf(std::uint32_t token) {
	return token & 0xFFFF;
}

// The tokens that follow an instruction's own (versions 2 and 3 write the count into it).
constexpr std::uint32_t LengthOf(std::uint32_t token) {
	return (token >> 24) & 0xF;
}

constexpr std::uint32_t CommentWords(std::uint32_t token) {
	return (token >> 16) & 0x7FFF;
}

constexpr std::uint32_t TypeOf(std::uint32_t parameter) {
	return ((parameter >> 28) & 7) | ((parameter >> 8) & 0x18);
}

constexpr std::uint32_t NumberOf(std::uint32_t parameter) {
	return parameter & 0x7FF;
}

constexpr bool WritesXyz(std::uint32_t destination) {
	return ((destination >> 16) & 7) == 7;
}

constexpr bool IsPixelProgram(std::uint32_t version) {
	return (version >> 16) == 0xFFFF && ((version >> 8) & 0xFF) >= 2;
}

constexpr std::uint32_t WithSaturate(std::uint32_t destination) {
	return destination | kSaturate;
}

// The tokens of a program of that many bytes, 0 for a length no program has.
constexpr std::size_t TokensOf(std::uint32_t bytes) {
	return bytes == 0 || bytes > kMostBytes || bytes % 4 != 0 ? 0 : bytes / 4;
}

// A byte of the constant table: `table` is the index of its first token, `at` a byte offset into it.
constexpr std::uint32_t TableByte(const std::uint32_t* tokens, std::size_t table, std::uint32_t at) {
	return (tokens[table + at / 4] >> (8 * (at % 4))) & 0xFF;
}

constexpr std::uint32_t TableWord(const std::uint32_t* tokens, std::size_t table, std::uint32_t at) {
	return TableByte(tokens, table, at) | (TableByte(tokens, table, at + 1) << 8);
}

constexpr std::uint32_t TableDword(const std::uint32_t* tokens, std::size_t table, std::uint32_t at) {
	return TableWord(tokens, table, at) | (TableWord(tokens, table, at + 2) << 16);
}

// Whether the table's bytes from `at` spell `name` with its end.
constexpr bool TableNameIs(const std::uint32_t* tokens, std::size_t table, std::uint32_t bytes, std::uint32_t at, const char* name) {
	for (std::uint32_t i = 0;; ++i) {
		if (at >= bytes || at + i >= bytes || TableByte(tokens, table, at + i) != static_cast<unsigned char>(name[i])) {
			return false;
		}
		if (name[i] == 0) {
			return true;
		}
	}
}

// The float register of the constant `name` in a constant table of `bytes` bytes, or kNone. The table's header
// has the count of constants at +12 and the offset of their records at +16; a record is 20 bytes: the name's
// offset, the register set (a word), the register (a word), then what the rule does not need.
constexpr std::uint32_t RegisterInTable(const std::uint32_t* tokens, std::size_t table, std::uint32_t bytes, const char* name) {
	if (bytes < 28) {
		return kNone;
	}
	const std::uint32_t constants = TableDword(tokens, table, 12);
	const std::uint32_t records = TableDword(tokens, table, 16);
	if (records > bytes || constants > (bytes - records) / 20) {
		return kNone;
	}
	for (std::uint32_t i = 0; i < constants; ++i) {
		const std::uint32_t record = records + i * 20;
		if (TableWord(tokens, table, record + 4) == kFloatSet && TableNameIs(tokens, table, bytes, TableDword(tokens, table, record), name)) {
			return TableWord(tokens, table, record + 6);
		}
	}
	return kNone;
}

// The registers the float constant `name` takes in a constant table (the record's word at +8), or 0 - no such
// constant, or a table that does not hold together.
constexpr std::uint32_t RegistersInTable(const std::uint32_t* tokens, std::size_t table, std::uint32_t bytes, const char* name) {
	if (bytes < 28) {
		return 0;
	}
	const std::uint32_t constants = TableDword(tokens, table, 12);
	const std::uint32_t records = TableDword(tokens, table, 16);
	if (records > bytes || constants > (bytes - records) / 20) {
		return 0;
	}
	for (std::uint32_t i = 0; i < constants; ++i) {
		const std::uint32_t record = records + i * 20;
		if (TableWord(tokens, table, record + 4) == kFloatSet && TableNameIs(tokens, table, bytes, TableDword(tokens, table, record), name)) {
			return TableWord(tokens, table, record + 8);
		}
	}
	return 0;
}

// A program's constant table: the index of its first token behind 'CTAB' and its bytes; bytes 0 - no table, or a
// stream that does not hold together.
struct TablePlace {
	std::size_t at = 0;
	std::uint32_t bytes = 0;
};

constexpr TablePlace TableOf(const std::uint32_t* tokens, std::size_t count) {
	std::size_t i = 1;
	while (i < count && tokens[i] != kEnd) {
		const std::uint32_t token = tokens[i];
		if (OpOf(token) != kComment) {
			i += 1 + LengthOf(token);
			continue;
		}
		const std::uint32_t words = CommentWords(token);
		if (i + 1 + words > count) {
			return {};
		}
		if (words >= 1 && tokens[i + 1] == kConstantTable) {
			return {i + 2, (words - 1) * 4};
		}
		i += 1 + words;
	}
	return {};
}

// The float register of the constant named AmbientColor by the program's constant table, or kNone - no table,
// no such constant, or a stream that does not hold together.
constexpr std::uint32_t AmbientRegister(const std::uint32_t* tokens, std::size_t count) {
	const TablePlace table = TableOf(tokens, count);
	return RegisterInTable(tokens, table.at, table.bytes, "AmbientColor");
}

// The registers of the float constant `name` in the program's constant table, 0 for none. LightData takes two a
// light, so half of its registers is the lights a lighting pixel program draws.
constexpr std::uint32_t ConstantRegisters(const std::uint32_t* tokens, std::size_t count, const char* name) {
	const TablePlace table = TableOf(tokens, count);
	return RegistersInTable(tokens, table.at, table.bytes, name);
}

// Whether one of `sources` parameters is the register of that type and number.
constexpr bool Reads(const std::uint32_t* sources, std::uint32_t count, std::uint32_t type, std::uint32_t number) {
	for (std::uint32_t i = 0; i < count; ++i) {
		if (TypeOf(sources[i]) == type && NumberOf(sources[i]) == number) {
			return true;
		}
	}
	return false;
}

constexpr bool AnyRelative(const std::uint32_t* parameters, std::uint32_t count) {
	for (std::uint32_t i = 0; i < count; ++i) {
		if ((parameters[i] & kRelative) != 0) {
			return true;
		}
	}
	return false;
}

// Whether an instruction of `length` parameters at `parameters` is the add that completes the sum in the
// temporary `lit`: add lit.xyz, another temporary, lit - in either order.
constexpr bool CompletesSum(std::uint32_t op, const std::uint32_t* parameters, std::uint32_t length, std::uint32_t lit) {
	if (op != kAdd || length != 3) {
		return false;
	}
	const std::uint32_t a = parameters[1], b = parameters[2];
	const bool aIsLit = TypeOf(a) == kTemp && NumberOf(a) == lit;
	const bool bIsLit = TypeOf(b) == kTemp && NumberOf(b) == lit;
	return (aIsLit && TypeOf(b) == kTemp && !bIsLit) || (bIsLit && TypeOf(a) == kTemp && !aIsLit);
}

enum class Outcome {
	kClip,          // found, without the saturate: `destination` is the token to change
	kAlready,       // found, saturated already
	kNoProgram,     // no pixel program of version 2 or 3
	kNoAmbient,     // no constant AmbientColor: not a lighting program
	kNoSum,         // the ambient colour is read, but no sum is made of it the rule knows
	kBroken,        // the stream does not hold together
};

struct Found {
	Outcome outcome = Outcome::kBroken;
	std::size_t destination = 0;  // the index of the add's destination token, for kClip and kAlready
};

// The add that completes a lighting program's light sum.
constexpr Found FindLightSum(const std::uint32_t* tokens, std::size_t count) {
	if (count < 2) {
		return {Outcome::kBroken, 0};
	}
	if (!IsPixelProgram(tokens[0])) {
		return {Outcome::kNoProgram, 0};
	}
	const std::uint32_t ambient = AmbientRegister(tokens, count);
	if (ambient == kNone) {
		return {Outcome::kNoAmbient, 0};
	}
	std::uint32_t lit = kNone;
	std::size_t i = 1;
	while (i < count && tokens[i] != kEnd) {
		const std::uint32_t token = tokens[i];
		if (OpOf(token) == kComment) {
			i += 1 + CommentWords(token);
			continue;
		}
		const std::uint32_t length = LengthOf(token);
		if (i + 1 + length > count) {
			return {Outcome::kBroken, 0};
		}
		const std::uint32_t* parameters = tokens + i + 1;
		const bool writes = OpOf(token) != kDcl && OpOf(token) != kDef && length >= 2;
		if (writes && TypeOf(parameters[0]) == kTemp && WritesXyz(parameters[0])) {
			const std::uint32_t written = NumberOf(parameters[0]);
			const bool relative = AnyRelative(parameters, length);  // not a shape a pixel program has: not read
			if (lit == kNone) {
				if (!relative && Reads(parameters + 1, length - 1, kConst, ambient)) {
					lit = written;
				}
			} else if (written == lit) {
				if (!relative && CompletesSum(OpOf(token), parameters, length, lit)) {
					return {(parameters[0] & kSaturate) != 0 ? Outcome::kAlready : Outcome::kClip, i + 1};
				}
				if (relative || !Reads(parameters + 1, length - 1, kTemp, lit)) {
					return {Outcome::kNoSum, 0};  // the temporary is something else now, or written in a way not read
				}
			}
		}
		i += 1 + length;
	}
	return {i < count ? Outcome::kNoSum : Outcome::kBroken, 0};
}

struct Counts {
	std::uint64_t asked = 0;      // programs the loader made through the probe
	std::uint64_t clipped = 0, already = 0;
	std::uint64_t noProgram = 0, noAmbient = 0, noSum = 0, broken = 0;
	std::uint64_t noEntry = 0;    // the name is not in the package, or there is no package: nothing to change
	std::uint64_t notWritten = 0; // found, and the token could not be written
};

// One program: `outcome` of its bytecode; `inPackage` false when there was none to look at; `written` whether a
// token to change was changed.
constexpr void Count(Counts& counts, bool inPackage, Outcome outcome, bool written) {
	++counts.asked;
	if (!inPackage) {
		++counts.noEntry;
		return;
	}
	switch (outcome) {
		case Outcome::kClip:
			++(written ? counts.clipped : counts.notWritten);
			break;
		case Outcome::kAlready:
			++counts.already;
			break;
		case Outcome::kNoProgram:
			++counts.noProgram;
			break;
		case Outcome::kNoAmbient:
			++counts.noAmbient;
			break;
		case Outcome::kNoSum:
			++counts.noSum;
			break;
		case Outcome::kBroken:
			++counts.broken;
			break;
	}
}

}  // namespace obng::engine::render::lighting30_clip
