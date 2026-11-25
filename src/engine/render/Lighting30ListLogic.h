#pragma once
// Which builder makes the pass list of a geometry [Probe] Lighting30Kinds moved to the one-pass shader: the one-pass
// property's own, or - when the one pass cannot draw what the geometry needs - the game's multi-pass builder, with
// the geometry drawn by ShadowLightShader for as long as that holds (the plugin's Lighting30List.cpp;
// knowledge/lighting-passes.md, "More lights than the one pass draws").
//
// Read 2026-11-25 in Oblivion.exe 1.2.0.416:
// - Lighting30ShaderProperty (vtable 0x00A9576C, run-time type 0x00B47860) is a BSShaderPPLightingProperty (vtable
//   0x00A91384, run-time type 0x00B45DA8, the parent the type's initialiser names). Of the base's slots it replaces
//   the destructor, the type, the clone (+0x18), +0x30, the kind (+0x54: 10 where the base has 5), the setup
//   (+0x58: 0x00864400 calls the base's 0x007DA220 and then takes the geometry's vertex colour property off), the
//   pass list getter (+0x5C: 0x008637D0 where the base has 0x007D85D0), +0x60 and the texture setter (+0x6C). It
//   keeps the base's list builders, +0x98 = 0x00855240 and +0x9C = 0x0085AE20 - the multi-pass chain.
// - ShadowLightShader takes such a property: its attach 0x0080B190 keeps a shader property of kind 5 .. 10, and
//   every test of the kind in its code is that range (0x007A9820, 0x007C9230, 0x007D21F0, 0x007D5320, 0x007D5790,
//   0x007D92C0, 0x007D93E0, 0x007ED6C0), none a 5 alone. The chain's builder tests the run-time type for the three
//   SpeedTree properties only (0x00B468C8 branch, 0x00B478B0 frond, 0x00B468BC leaf).
// - The two shaders pack a geometry alike: their makers 0x007CBDC0 and 0x007FC7D0 build the same four vertex
//   declarations, entry for entry.
// - A pass list is drawn by the shader of its first record's geometry (0x007AC9A0 reads [geometry+0xBC], takes
//   0x007D1320 for the shader ids 1 .. 5 and 0x007FD260 for 0x1B), and every record through its own geometry's
//   shader (0x007A9820) - so a geometry whose list the chain built has to carry ShadowLightShader.
// - Both getters keep their lists in the base's fields: the key at +0x24 (0: stale; a light added 0x007EE330 or
//   taken away 0x007EE3E0 writes that 0), the lists at +0x28, +0x38 (the shadow maps) and +0x48 (the local map).
//   0x007E24C0 empties the three and writes the key 0.
// - The one-pass builder puts every light of the property that is no actor's shadow into its one record behind the
//   scene's first light (0x008637D0: the count is a byte, no cap), the shader's setup caps the count at 8
//   (0x007FF4A0, 0x00B2DCFC) and the pixel program caps it again by its own loop - LightData x14, seven lights, in
//   seventeen of the twenty SM3 lighting programs of the package in use, x16, eight, in three (SM3001, SM3002,
//   SM3010): a geometry under more lights loses the last of its list. The list is in the order of (distance to
//   the geometry's bound less its radius) / the light's radius (0x007ECC60), so on a large mesh the lights lost are
//   not the weak ones. The chain draws every light: its builder walks them all, three to a pass (0x0085AE20,
//   0x00859160 / 0x00859430).
// - Which pixel program a lighting record gets is the picker's 0x00862B40(pass id, property, specular): the number
//   it hands 0x00862AD0, which takes SM3<number> from the table 0x00B46ED8 (SM3LL<number>, four lights, from
//   0x00B46C20 while the record's light count - ftol of 0x00B46FD8 + 0x00B46FDC, the setup's 1 and count less one -
//   is at most the value of 0x00B2DD00, 4 in the image and written by nothing). Plain, skinned and with vertex
//   colours 0 (1 with specular), hair 2 (3), parallax 4 (5), FaceGen 6 (7), glow 8 (9), each ten more with the
//   projected shadow (the pass ids 0x13A .. 0x146). The programs' own constant tables agree: the samplers LayerMap
//   and AnisoMap in 2 and 3, FaceGenMap0 / 1 in 6 and 7, GlowMap in 8 and 9, ShadowMap and ShadowMask from 10 on.
//   Specular is what the setup hands the picker: the pass mask's bit 8 and the property's flag 1.
// - The shader package lives only while the renderer makes its shaders: 0x007BA0F0 loads it into 0x00B430B8, makes
//   the shaders of the kinds 1 .. 0x1B (0x007B4290) and frees it again (0x007DB010, 0x00401F20, the pointer set to
//   0). So a program's light count is read as the loader makes it (the pixel program maker's calls, which
//   Lighting30Clip.cpp takes), never later.
// - With bloom on (the byte 0x00B43073, RendererInfo.txt's "Bloom lighting") the frame's alpha is what the bloom
//   takes as self-illumination (ISBLUR2000.pso: the picture x ((luminance x y)^2 + alpha x z + alphaAdd)). The
//   chain's lighting passes write the colour alone (0x007D1320: colour write mask 7); its builder ends a list with
//   passes that write the alpha alone (mask 8): BSSM_SELFILLUMALPHABLOCK (0x0A / 0x0B, 0x00855170 - SLS1038.pso
//   writes 0) for ordinary geometry and BSSM_SELFILLUMALPHA (0x19E / 0x19F, 0x00854FF0) for a geometry whose
//   material's emissive colour is not the black at 0x00B3FA90, when the ambient pass is on (the mask's bit 1); and
//   its specular passes write colour and alpha (mask 0xF; SLS2037.pso: alpha = the specular colour's r + g + b).
//   The one-pass lighting passes write the colour alone (0x007FD260: mask 7 for 0x12B .. 0x15F) and its builder has
//   no alpha pass: a moved geometry's glow never reaches the bloom, and neither do its highlights. The first is
//   handed to the chain here; the second is not built (knowledge/lighting-passes.md, "With bloom").
//
// The two sources for each address: the two vtables side by side (the slots named above), and the instructions of
// the function each slot leads to; 0x007E24C0 is called by both getters before a rebuild; the emissive colour is
// read at the same offsets by the one-pass setup 0x007FF4A0 and by 0x00854FF0.
//
// 64-bit clean: plain values; engine objects are 32-bit handles in the plugin.

#include <cstdint>

namespace obng::engine::render::lighting30_list {

inline constexpr std::uint32_t kPropertyVtable = 0x00A9576C;          // Lighting30ShaderProperty
inline constexpr std::uint32_t kGetterSlot = kPropertyVtable + 0x5C;  // its pass list getter's slot
inline constexpr std::uint32_t kOnePassGetter = 0x008637D0;  // list* __thiscall (property, geometry, mask, count*, build)
inline constexpr std::uint32_t kChainGetter = 0x007D85D0;    // BSShaderPPLightingProperty's, the same arguments
inline constexpr std::uint32_t kClearLists = 0x007E24C0;     // void __thiscall (property)
inline constexpr std::uint32_t kShaderDefinition = 0x007B4290;  // definition* __cdecl (shader kind)
inline constexpr std::uint32_t kShaderOfDefinition = 0x04, kShaderOfGeometry = 0xBC, kKeyOfProperty = 0x24;
inline constexpr std::uint32_t kReferencesOfObject = 0x04;   // NiRefObject: the count
inline constexpr int kChainKind = 0x01, kOnePassKind = 0x1A;
inline constexpr std::uint32_t kBloom = 0x00B43073;             // byte: the game renders with bloom
inline constexpr std::uint32_t kBlack = 0x00B3FA90;             // three floats: the colour an emissive is compared with
inline constexpr std::uint32_t kStateOfGeometry = 0xAC;         // NiGeometry: its property state
inline constexpr std::uint32_t kMaterialOfState = 0x10;         // NiPropertyState: the material property
inline constexpr std::uint32_t kEmissiveOfMaterial = 0x40;      // NiMaterialProperty: three floats
inline constexpr std::uint32_t kAmbientPass = 0x1, kSpecularPass = 0x8;  // the pass mask's bits of bDoAmbientPass, bDoSpecularPass
inline constexpr std::uint32_t kSpecularFlag = 0x1;             // the property's flag
inline constexpr std::uint32_t kSetupCap = 0x00B2DCFC;          // dword: the lights the shader's setup hands a program at most
inline constexpr std::uint32_t kPrograms = 20;                  // SM3000 .. SM3019, the lighting pixel programs
inline constexpr char kProgramName[] = "SM3%03u.pso", kLightsName[] = "LightData";

// The lights every one-pass lighting program of the package in use draws at least: taken for a program whose
// constant table could not be read.
inline constexpr std::uint32_t kLightsDrawn = 7;
inline constexpr std::uint32_t kNoLimit = 0xFFFFFFFF;
inline constexpr std::uint32_t kNoProgram = 0xFFFFFFFF;

// [Probe] Lighting30MostLights: the most lights a one-pass list may hold; a geometry under more gets the chain.
// 8, as shipped: what the record's own program draws (seven or eight). Less: that many at most; 0: every moved
// geometry draws through the chain - the test of that way, whose picture has to be the game's. 255: no limit - the
// lights beyond a program's count are left out again (the comparison).
inline constexpr int kNever = 255;

constexpr int Shipped() {
	return 8;
}

constexpr std::uint32_t MostOf(int ini) {
	return ini < 0 ? 0u : ini > kNever ? static_cast<std::uint32_t>(kNever) : static_cast<std::uint32_t>(ini);
}

// The lights of the one-pass record: the scene's first light and every light of the property that is no actor's
// shadow.
constexpr std::uint32_t RecordLights(std::uint32_t otherLights) {
	return 1 + otherLights;
}

// The pixel program the picker gives a lighting record: its number in SM3<number>.pso; kNoProgram for a pass id
// that is no lighting record's.
constexpr std::uint32_t ProgramOf(std::uint16_t pass, bool specular) {
	if (pass < 0x12D || pass > 0x146) {
		return kNoProgram;
	}
	const bool projected = pass >= 0x13A;
	const std::uint16_t plain = projected ? static_cast<std::uint16_t>(pass - 0x0D) : pass;
	const std::uint32_t family = plain == 0x12F                                      ? 2u    // hair
	                             : plain == 0x130 || plain == 0x137                 ? 4u    // parallax
	                             : plain == 0x131 || plain == 0x132                 ? 6u    // FaceGen
	                             : plain == 0x133 || plain == 0x134 || plain >= 0x138 ? 8u  // glow
	                                                                                 : 0u;  // plain, skinned, vertex colours
	return family + (specular ? 1u : 0u) + (projected ? 10u : 0u);
}

// The number of a lighting pixel program by the name the shader's loader makes it under ("SM3%03i.pso", 0x00A93270):
// "SM3" and three digits and ".pso"; kNoProgram for any other name (the four-light "SM3LL%03i.pso" ones among them:
// the picker 0x00862AD0 takes those only for a record of at most four lights, the value of 0x00B2DD00 - they leave
// no light out).
constexpr std::uint32_t ProgramNumberOf(const char* name) {
	if (name == nullptr || name[0] != 'S' || name[1] != 'M' || name[2] != '3') {
		return kNoProgram;
	}
	std::uint32_t number = 0;
	for (int i = 3; i < 6; ++i) {
		if (name[i] < '0' || name[i] > '9') {
			return kNoProgram;
		}
		number = number * 10 + static_cast<std::uint32_t>(name[i] - '0');
	}
	const bool tail = name[6] == '.' && name[7] == 'p' && name[8] == 's' && name[9] == 'o' && name[10] == 0;
	return tail && number < kPrograms ? number : kNoProgram;
}

// The lights a record is drawn with: the setup's cap, and the program's own count - half of its LightData
// registers; a program whose table gave none counts as the least any draws.
constexpr std::uint32_t DrawnWith(std::uint32_t setupCap, std::uint32_t lightRegisters) {
	const std::uint32_t program = lightRegisters >= 2 ? lightRegisters / 2 : kLightsDrawn;
	return program < setupCap ? program : setupCap;
}

// The most lights a one-pass list of a property may hold: the key, and what is known of the property's program
// (0: nothing yet - then the setup's cap, the most any program can be handed). kNoLimit for the key at 255.
constexpr std::uint32_t LimitFor(std::uint32_t most, std::uint32_t known, std::uint32_t setupCap) {
	if (most >= static_cast<std::uint32_t>(kNever)) {
		return kNoLimit;
	}
	const std::uint32_t drawn = known != 0 ? known : setupCap;
	return most < drawn ? most : drawn;
}

// Whether a record the one-pass builder just made holds more lights than it will be drawn with (or than the key
// allows): its list goes to the chain after all.
constexpr bool Overflows(std::uint32_t recordLights, std::uint32_t most, std::uint32_t drawn) {
	return recordLights > LimitFor(most, drawn, drawn);
}

// The game's own comparison of two colours (0x008AA390 answers "not the same"): every component equal, a NaN never.
constexpr bool SameColour(float r, float g, float b, float r2, float g2, float b2) {
	return r == r2 && g == g2 && b == b2;
}

// Whether the chain would end this geometry's list with the pass that writes its glow into the frame's alpha
// (0x0085AE20's ending, 0x00854FF0): bloom on, the ambient pass on, a material, and its emissive colour not black.
constexpr bool GlowsInBloom(bool bloom, std::uint32_t passMask, bool material, bool emissiveIsBlack) {
	return bloom && (passMask & kAmbientPass) != 0 && material && !emissiveIsBlack;
}

enum class Builder { kOnePass, kChain };

// What the one pass cannot draw: more lights than its program holds, and a glow the bloom is to take.
constexpr Builder BuilderFor(std::uint32_t recordLights, std::uint32_t limit, bool glowsInBloom) {
	return recordLights > limit || glowsInBloom ? Builder::kChain : Builder::kOnePass;
}

// The shader a geometry carries, as far as this rule is concerned. Without both shaders known (the renderer has
// not made them yet) nothing is one of the two, and so nothing is decided.
enum class Carries { kOnePass, kChain, kOther };

constexpr Carries CarriesFor(std::uint32_t shader, std::uint32_t onePassShader, std::uint32_t chainShader) {
	if (shader == 0 || onePassShader == 0 || chainShader == 0) {
		return Carries::kOther;
	}
	return shader == onePassShader ? Carries::kOnePass : shader == chainShader ? Carries::kChain : Carries::kOther;
}

// Whether this call of the getter decides anew: a marked property, a geometry that carries one of the two shaders,
// and a stale list - the getter is about to build it. A valid list stays with the builder that made it; the lights
// cannot have changed under it.
constexpr bool Decides(bool marked, Carries carries, std::uint32_t key) {
	return marked && carries != Carries::kOther && key == 0;
}

struct Step {
	Builder builder;  // whose getter answers this call
	bool change;      // the lists are emptied and the geometry is given the builder's shader first
};

// What a call of the getter does. `recordLights`, `limit` and `glowsInBloom` are read only when Decides() says so.
constexpr Step StepFor(bool marked, Carries carries, std::uint32_t key, std::uint32_t recordLights, std::uint32_t limit, bool glowsInBloom) {
	const Builder now = carries == Carries::kChain ? Builder::kChain : Builder::kOnePass;
	if (!Decides(marked, carries, key)) {
		return {marked ? now : Builder::kOnePass, false};
	}
	const Builder wanted = BuilderFor(recordLights, limit, glowsInBloom);
	return {wanted, wanted != now};
}

struct Counts {
	std::uint64_t decided = 0;      // stale lists of marked properties decided on
	std::uint64_t chain = 0;        // of them, built by the chain
	std::uint64_t manyLights = 0;   // chain lists with more lights than the limit
	std::uint64_t glow = 0;         // chain lists with a glow for the bloom
	std::uint64_t overflowed = 0;   // one-pass lists built and then handed to the chain: the record's program draws fewer
	std::uint64_t toChain = 0;      // geometries handed to ShadowLightShader
	std::uint64_t toOnePass = 0;    // and back
	std::uint64_t other = 0;        // calls for a marked property whose geometry carried neither shader
	std::uint32_t mostLights = 0;   // the most lights a decided list had
};

struct Seen {
	std::uint32_t recordLights;
	std::uint32_t limit;
	bool glowsInBloom;
};

constexpr void Count(Counts& counts, bool marked, Carries carries, std::uint32_t key, Seen seen, Step step) {
	if (marked && carries == Carries::kOther) {
		++counts.other;
	}
	if (!Decides(marked, carries, key)) {
		return;
	}
	++counts.decided;
	if (seen.recordLights > counts.mostLights) {
		counts.mostLights = seen.recordLights;
	}
	if (step.builder == Builder::kChain) {
		++counts.chain;
		if (seen.recordLights > seen.limit) {
			++counts.manyLights;
		}
		if (seen.glowsInBloom) {
			++counts.glow;
		}
	}
	if (step.change) {
		++(step.builder == Builder::kChain ? counts.toChain : counts.toOnePass);
	}
}

// A one-pass list that was built and then handed to the chain: it counts as a chain list for its lights.
constexpr void CountOverflow(Counts& counts) {
	++counts.overflowed;
	++counts.chain;
	++counts.manyLights;
	++counts.toChain;
}

}  // namespace obng::engine::render::lighting30_list
