#pragma once
// The specular of the one-pass lighting faded with distance, as the game's multi-pass path fades it, for the
// geometry [Probe] Lighting30Kinds moved (the plugin's Lighting30Specular.cpp; knowledge/lighting-passes.md,
// "Specular at a distance").
//
// Read 2026-11-25 in Oblivion.exe 1.2.0.416:
// - ShadowLightShader's pass list getter 0x007D85D0 (the chain's) computes, for a property with the specular flag
//   (+0x1C bit 0) while the value at 0x00B43090 is above 0, the distance from the camera (0x00B46658, 0x00B4665C,
//   0x00B46660) to the geometry's world bound less its radius (the geometry's +0x20 .. +0x2C; sqrt 0x00982C30) and
//   from it the fade (+0x9C): 1 nearer than the value at 0x00B4308C, then linear to 0 at 0x00B43090, 0 beyond. The
//   chain's light setup 0x007ED6C0 multiplies the light's colour by that fade for every specular pass id
//   (0x10F .. 0x129 among them), and its builder 0x0085AE20 leaves the specular passes out at a fade of 0.
// - Lighting30's getter 0x008637D0 computes the environment map's fade (+0xA4, 0x00B43094 .. 0x00B430A0) and no
//   specular fade; its setup 0x007FF4A0 (the shader's slot +0x2C, seven arguments: the geometry, the skin, the
//   buffer data, the property state, the effects, the world transform, the world bound; `ret 0x1C`) picks the
//   program with specular when the pass mask's bit 8 and the property's flag 1 are set, and writes that choice
//   as 1 or 0 into ToggleADTS.w (0x00B46FD4) - which no lighting program reads.
// - Every one-pass lighting program with specular (the ten odd ones of SM3000 .. SM3019 and of SM3LL000 .. SM3LL019,
//   the only ones with a `pow`) ends its specular as `mul_pp rS.xyz, rN.w, rS` - the sum of the lights' specular
//   times the normal map's alpha (the gloss; rN is the texld from the sampler named NormalMap). Of the programs
//   without specular only SM3016 / SM3LL016 (FaceGen with the projected shadow) multiply by that alpha, saturated
//   (`mul_sat_pp`), and not for specular: a program counts only with a pow and an unsaturated multiply.
//
// So the fade is one multiply more in those programs: `mul_pp rS.xyz, rS, cT.w` right after the gloss multiply, cT
// the register of ToggleADTS by the program's own constant table, and ToggleADTS.w the fade for a moved geometry
// (1 as before for everything else: the game's own hair and heads, whose specular programs are chosen only with
// the toggle at 1). The program grows by four tokens; the pixel program maker 0x00801C90 hands the bytes behind its
// package entry to CreatePixelShader and reads no length.
//
// The two sources for each address: the chain's getter and its light setup name the same property word (+0x9C)
// and the same pass ids; the setup's instructions and the programs' constant tables name ToggleADTS c5 alike.
//
// 64-bit clean: plain values, token streams as arrays of std::uint32_t.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "engine/render/Lighting30ClipLogic.h"

namespace obng::engine::render::lighting30_specular {

namespace clip = obng::engine::render::lighting30_clip;

inline constexpr std::uint32_t kSetupSlot = 0x00A930C4 + 0x2C;  // Lighting30Shader's vtable: the setup
inline constexpr std::uint32_t kSetup = 0x007FF4A0;             // its function, thiscall with seven arguments
inline constexpr std::uint32_t kLookupCall = 0x00801D2D;        // the pixel program maker's call of the package's lookup
inline constexpr std::uint32_t kCamera = 0x00B46658;            // three floats
inline constexpr std::uint32_t kFadeNear = 0x00B4308C, kFadeFar = 0x00B43090;  // floats
inline constexpr std::uint32_t kSpecularToggle = 0x00B46FD4;    // ToggleADTS.w as the setup writes it
inline constexpr std::uint32_t kCurrentPass = 0x00B42E90;       // dword: the pass id being set up (the setup's own read)
inline constexpr std::uint32_t kBoundCenter = 0x00, kBoundRadius = 0x0C;       // NiBound

// [Probe] Lighting30SpecularFade: what OBNG runs with when OBNG.ini has no key. On with Lighting30Kinds only.
constexpr int Shipped() {
	return 1;
}

constexpr bool On(int ini, bool kindsOn) {
	return kindsOn && ini > 0;
}

// The chain's fade (0x007D85D0) for a distance to the bound's surface: 1 nearer than `start`, linear to 0 at
// `end`, 0 beyond; an `end` of 0 or less fades nothing. (Not near / far: windows.h defines both away.)
constexpr float Fade(float distance, float start, float end) {
	if (end <= 0.0f || distance < start) {
		return 1.0f;
	}
	if (distance > end) {
		return 0.0f;
	}
	const float part = end > start ? (distance - start) / (end - start) : 1.0f;
	return part <= 1.0f ? 1.0f - part : 0.0f;
}

// The distance from the camera to a bound's surface, as the chain takes it (the centre's distance less the radius).
inline float DistanceToBound(const float camera[3], const float center[3], float radius) {
	const float dx = center[0] - camera[0], dy = center[1] - camera[1], dz = center[2] - camera[2];
	return std::sqrt(dx * dx + dy * dy + dz * dz) - radius;
}

// What the setup wrote is the toggle a specular program gets; only that is faded.
constexpr float ToggleFor(float written, float fade) {
	return written == 1.0f ? fade : written;
}

// Only a lighting record's setup is faded (0x12D .. 0x146: the programs with the multiply). The setup writes the
// toggle for every pass id - the actor shadow's records 0x14E .. 0x151 get it from the parallax flag - and the
// other passes' programs are not the ones rewritten.
constexpr bool FadesPass(std::uint32_t pass) {
	return pass >= 0x12D && pass <= 0x146;
}

inline constexpr std::uint32_t kTexld = 66, kMul = 5, kPow = 32;
inline constexpr std::uint32_t kSampler = 10, kSamplerSet = 3;
inline constexpr std::uint32_t kPartial = 1u << 21;
inline constexpr std::uint32_t kAdded = 4;  // the tokens of the multiply put in

enum class Outcome { kFade, kNoSpecular, kNoTable, kBroken };

struct Found {
	Outcome outcome = Outcome::kBroken;
	std::size_t after = 0;           // the index of the first token behind the gloss multiply
	std::uint32_t specular = 0;      // the temporary that holds the specular
	std::uint32_t toggle = 0;        // the float register of ToggleADTS
};

// The register of a sampler by its name in the program's constant table, or clip::kNone.
constexpr std::uint32_t SamplerRegister(const std::uint32_t* tokens, std::size_t count, const char* name) {
	const clip::TablePlace table = clip::TableOf(tokens, count);
	if (table.bytes < 28) {
		return clip::kNone;
	}
	const std::uint32_t constants = clip::TableDword(tokens, table.at, 12);
	const std::uint32_t records = clip::TableDword(tokens, table.at, 16);
	if (records > table.bytes || constants > (table.bytes - records) / 20) {
		return clip::kNone;
	}
	for (std::uint32_t i = 0; i < constants; ++i) {
		const std::uint32_t record = records + i * 20;
		if (clip::TableWord(tokens, table.at, record + 4) == kSamplerSet &&
		    clip::TableNameIs(tokens, table.at, table.bytes, clip::TableDword(tokens, table.at, record), name)) {
			return clip::TableWord(tokens, table.at, record + 6);
		}
	}
	return clip::kNone;
}

constexpr std::uint32_t SwizzleOf(std::uint32_t parameter) {
	return (parameter >> 16) & 0xFF;
}

constexpr std::uint32_t MaskOf(std::uint32_t destination) {
	return (destination >> 16) & 0xF;
}

// The gloss multiply: `mul rS.xyz, rN.w, rS` (or the two sources the other way round), unsaturated, with rN the
// normal map's sample, in a program with a pow. A program with specular has one; a second one is a shape not read.
constexpr Found FindGlossMultiply(const std::uint32_t* tokens, std::size_t count) {
	if (count < 2 || !clip::IsPixelProgram(tokens[0])) {
		return {Outcome::kBroken, 0, 0, 0};
	}
	const std::uint32_t normalMap = SamplerRegister(tokens, count, "NormalMap");
	const std::uint32_t toggle = clip::RegisterInTable(tokens, clip::TableOf(tokens, count).at, clip::TableOf(tokens, count).bytes, "ToggleADTS");
	if (normalMap == clip::kNone || toggle == clip::kNone) {
		return {Outcome::kNoTable, 0, 0, 0};
	}
	std::uint32_t gloss = clip::kNone;
	bool power = false;
	Found found{Outcome::kNoSpecular, 0, 0, toggle};
	std::size_t i = 1;
	while (i < count && tokens[i] != clip::kEnd) {
		const std::uint32_t token = tokens[i];
		if (clip::OpOf(token) == clip::kComment) {
			i += 1 + clip::CommentWords(token);
			continue;
		}
		const std::uint32_t length = clip::LengthOf(token);
		if (i + 1 + length > count) {
			return {Outcome::kBroken, 0, 0, 0};
		}
		const std::uint32_t* p = tokens + i + 1;
		const std::uint32_t op = clip::OpOf(token);
		power = power || op == kPow;
		if (op == kTexld && length == 3 && clip::TypeOf(p[2]) == kSampler && clip::NumberOf(p[2]) == normalMap && clip::TypeOf(p[0]) == clip::kTemp) {
			gloss = clip::NumberOf(p[0]);
		} else if (op == kMul && length == 3 && gloss != clip::kNone && clip::TypeOf(p[0]) == clip::kTemp && MaskOf(p[0]) == 7 &&
		           (p[0] & clip::kSaturate) == 0) {
			const std::uint32_t s = clip::NumberOf(p[0]);
			const bool glossFirst = clip::TypeOf(p[1]) == clip::kTemp && clip::NumberOf(p[1]) == gloss && SwizzleOf(p[1]) == 0xFF &&
			                        clip::TypeOf(p[2]) == clip::kTemp && clip::NumberOf(p[2]) == s;
			const bool glossSecond = clip::TypeOf(p[2]) == clip::kTemp && clip::NumberOf(p[2]) == gloss && SwizzleOf(p[2]) == 0xFF &&
			                         clip::TypeOf(p[1]) == clip::kTemp && clip::NumberOf(p[1]) == s;
			if ((glossFirst || glossSecond) && s != gloss && !clip::AnyRelative(p, length)) {
				if (found.outcome == Outcome::kFade) {
					return {Outcome::kBroken, 0, 0, 0};  // two of them: not the shape read
				}
				found = {Outcome::kFade, i + 1 + length, s, toggle};
			}
		}
		i += 1 + length;
	}
	if (i >= count) {
		return {Outcome::kBroken, 0, 0, 0};
	}
	return found.outcome == Outcome::kFade && !power ? Found{Outcome::kNoSpecular, 0, 0, toggle} : found;
}

// Which of the loader's lighting programs a name is: SM3000 .. SM3019 the slots 0 .. 19, the four-light SM3LL000 ..
// SM3LL019 20 .. 39 ("SM3%03i.pso", "SM3LL%03i.pso"); kNoSlot for any other name.
inline constexpr std::uint32_t kSlots = 40, kNoSlot = 0xFFFFFFFF;

constexpr std::uint32_t SlotOf(const char* name) {
	if (name == nullptr || name[0] != 'S' || name[1] != 'M' || name[2] != '3') {
		return kNoSlot;
	}
	const bool four = name[3] == 'L' && name[4] == 'L';
	const int at = four ? 5 : 3;
	std::uint32_t number = 0;
	for (int i = at; i < at + 3; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return kNoSlot;
		}
		number = number * 10 + static_cast<std::uint32_t>(name[i] - '0');
	}
	const char* tail = name + at + 3;
	const bool pso = tail[0] == '.' && tail[1] == 'p' && tail[2] == 's' && tail[3] == 'o' && tail[4] == 0;
	return pso && number < kSlots / 2 ? number + (four ? kSlots / 2 : 0) : kNoSlot;
}

// The multiply put in: `mul_pp rS.xyz, rS, cT.wwww`.
constexpr void FadeMultiply(std::uint32_t specular, std::uint32_t toggle, std::uint32_t out[kAdded]) {
	out[0] = kMul | (3u << 24);
	out[1] = 0x80000000u | specular | (7u << 16) | kPartial;                    // rS.xyz, partial precision
	out[2] = 0x80000000u | specular | (0xE4u << 16);                            // rS.xyzw
	out[3] = 0x80000000u | (static_cast<std::uint32_t>(clip::kConst) << 28) | toggle | (0xFFu << 16);  // cT.wwww
}

// The program with the multiply put in behind the gloss multiply: the tokens written to `out`, their count, or 0
// when the program is not one to fade or `out` is too small.
constexpr std::size_t WithFade(const std::uint32_t* tokens, std::size_t count, std::uint32_t* out, std::size_t room) {
	const Found found = FindGlossMultiply(tokens, count);
	std::size_t end = 0;
	while (end < count && tokens[end] != clip::kEnd) {
		++end;
	}
	if (found.outcome != Outcome::kFade || end >= count || end + 1 + kAdded > room) {
		return 0;
	}
	std::size_t n = 0;
	for (std::size_t i = 0; i < found.after; ++i) {
		out[n++] = tokens[i];
	}
	std::uint32_t added[kAdded] = {};
	FadeMultiply(found.specular, found.toggle, added);
	for (const std::uint32_t token : added) {
		out[n++] = token;
	}
	for (std::size_t i = found.after; i <= end; ++i) {
		out[n++] = tokens[i];
	}
	return n;
}

struct Counts {
	std::uint64_t programs = 0;   // lighting programs the loader made
	std::uint64_t faded = 0;      // of them, made with the multiply
	std::uint64_t noSpecular = 0, noTable = 0, broken = 0;
	std::uint64_t setups = 0;     // setups of a moved geometry's specular program
	std::uint64_t dimmed = 0;     // of them, with a fade under 1
	std::uint64_t dark = 0;       // of them, with a fade of 0
};

constexpr void CountProgram(Counts& counts, Outcome outcome) {
	++counts.programs;
	switch (outcome) {
		case Outcome::kFade:
			++counts.faded;
			break;
		case Outcome::kNoSpecular:
			++counts.noSpecular;
			break;
		case Outcome::kNoTable:
			++counts.noTable;
			break;
		case Outcome::kBroken:
			++counts.broken;
			break;
	}
}

constexpr void CountSetup(Counts& counts, float fade) {
	++counts.setups;
	if (fade < 1.0f) {
		++counts.dimmed;
	}
	if (fade <= 0.0f) {
		++counts.dark;
	}
}

}  // namespace obng::engine::render::lighting30_specular
