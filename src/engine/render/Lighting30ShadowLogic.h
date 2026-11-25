#pragma once
// The actor shadow records Lighting30Shader's pass list lacks, for the geometry [Probe] Lighting30Kinds moved to it
// (the plugin's Lighting30Kind.cpp; knowledge/lighting-passes.md).
//
// Read 2026-11-24 in Oblivion.exe 1.2.0.416:
// - ShadowLightShader's builder 0x0085AE20 ends (0x0085BAAD - 0x0085BB7B) with a loop over the geometry's lights:
//   when the game draws actor shadows (0x00405A80: render path 3 or more and bit 0x10 of the pass mask) every
//   light with the byte +0xF4 set (an actor's shadow) gets a record of its own - allocated (0x00401F00, 0x10
//   bytes), built by 0x007E2370(memory, geometry, 0, 0, 1, light), pass id 0x177 (0x178 skinned, 0x179 land,
//   0x17A one subclass), byte +7 = 1, put at the list's head (0x006AA320 on the list at property +0x28). The
//   lights come from 0x007ED1A0 (first) and 0x007ED3B0 (next), both on the property.
// - Lighting30ShaderProperty's builder 0x008637D0 has no such loop, though its shader handles the pass ids
//   0x14E (3XLIGHTING_SIMPLESHADOW), 0x14F (_Vc), 0x150 (_S), 0x151 (_VcS): the per-record setup 0x007FF4A0 binds
//   the record's light's shadow map for them and the program picker 0x00862B40 has their programs.
// - The builder puts its one lighting record (0x12D .. 0x146) at the list's head at 0x00864078 (`call 0x006AA320`,
//   ECX the list, the record's slot pushed) - the one place every rebuild of a lit list passes.
//
// The probe marks the properties it made: the word at +0xFC, the fourth component of the hair tint. The
// constructors zero it (0x00863430, 0x00863720), a clone copies it (0x00863540), and no hair program reads it
// (every SM3 hair pixel program reads c2.xyz only). The game's own Lighting30 geometry - hair, heads, blended and
// morphing geometry - carries no mark and keeps its lists as the game builds them.
//
// The two sources for the addresses: the instructions of 0x0085AE20's ending and of 0x008637D0, which name the same
// helpers for the same jobs.
//
// 64-bit clean: plain values; engine objects are 32-bit handles in the plugin.

#include <cstdint>

namespace obng::engine::render::lighting30_shadow {

inline constexpr std::uint32_t kLightingRecordAdd = 0x00864078;  // the builder's call that adds the lighting record
inline constexpr std::uint32_t kAddHead = 0x006AA320;            // void __thiscall (list, record slot)
inline constexpr std::uint32_t kAllocate = 0x00401F00;           // void* __cdecl (bytes)
inline constexpr std::uint32_t kRecord = 0x007E2370;             // record* __cdecl (memory, geometry, pass id, flag, light count, lights...)
inline constexpr std::uint32_t kFirstLight = 0x007ED1A0;         // light* __thiscall (property)
inline constexpr std::uint32_t kNextLight = 0x007ED3B0;          // light* __thiscall (property)
inline constexpr std::uint32_t kPrepareObject = 0x007B7FC0;      // bool __cdecl (object, shader kind, a, b, c)
inline constexpr std::uint32_t kGetProperty = 0x00707530;        // property* __thiscall (geometry, type)
inline constexpr std::uint32_t kPropertyVtable = 0x00A9576C;     // Lighting30ShaderProperty
inline constexpr std::uint32_t kRenderPath = 0x00B42F48, kPassMask = 0x00B42F40;

inline constexpr std::uint32_t kShadeProperty = 4;                       // the property type of a shader property
inline constexpr std::uint32_t kListOfProperty = 0x28;                   // the pass list inside the property
inline constexpr std::uint32_t kMarkOfProperty = 0xFC, kMark = 0x4F424E47;  // 'OBNG' in the hair tint's fourth word
inline constexpr std::uint32_t kRecordBytes = 0x10, kRecordGeometry = 0x0, kRecordPass = 0x4, kRecordLast = 0x7, kRecordLights = 0x8;
// The lights one lighting record is surely drawn with. The shader's setup 0x007FF4A0 caps the record's count at
// the value of 0x00B2DCFC (8, set by the shader's constructor on every render path below 7), and the pixel
// programs of the package in use cap it again: seven lights in the ones without specular (SM3000.pso, LightData
// x14, its loop bound 7), eight in the ones with (SM3001.pso, x16).
inline constexpr std::uint32_t kLightsDrawn = 7;
inline constexpr std::uint32_t kLightIsActorShadow = 0xF4;

// What OBNG runs with when OBNG.ini has no key: on - it acts on marked properties only, and there are none
// without [Probe] Lighting30Kinds.
constexpr int Shipped() {
	return 1;
}

constexpr bool On(int ini) {
	return ini > 0;
}

// 0x00405A80: whether the game draws actor shadows on their receivers.
constexpr bool ShadowPassesOn(std::uint32_t renderPath, std::uint32_t passMask) {
	return renderPath >= 3 && (passMask & 0x10) != 0;
}

constexpr bool IsLightingPass(std::uint16_t pass) {
	return pass >= 0x12D && pass <= 0x146;
}

// A lighting pass id without its projected (canopy) shadow: the variants with it are 0x0D above (0x13A .. 0x146).
constexpr std::uint16_t WithoutProjectedShadow(std::uint16_t pass) {
	return pass >= 0x13A ? static_cast<std::uint16_t>(pass - 0x0D) : pass;
}

// The shadow receiver pass for a list whose lighting record has this pass id; 0 for none. Hair (0x12F) and
// FaceGen (0x131, 0x132) take none: the game sends them here itself and gives them no shadow.
constexpr std::uint16_t ShadowPassFor(std::uint16_t lightingPass) {
	if (!IsLightingPass(lightingPass)) {
		return 0;
	}
	const std::uint16_t pass = WithoutProjectedShadow(lightingPass);
	if (pass == 0x12F || pass == 0x131 || pass == 0x132) {
		return 0;
	}
	const bool skinned = pass == 0x12E || pass == 0x134 || pass == 0x136 || pass == 0x139;
	const bool vertexColours = pass >= 0x135 && pass <= 0x139;
	return static_cast<std::uint16_t>(0x14E + (skinned ? 2 : 0) + (vertexColours ? 1 : 0));
}

constexpr bool IsMark(std::uint32_t word) {
	return word == kMark;
}

// The shadow pass to add records with for a property whose lighting record was just built; 0 for none.
constexpr std::uint16_t Receives(bool on, std::uint32_t markWord, bool shadowPassesOn, std::uint16_t lightingPass) {
	return on && IsMark(markWord) && shadowPassesOn ? ShadowPassFor(lightingPass) : std::uint16_t{0};
}

// Whether a lighting record carries more lights than the shader draws: the ones beyond are left out of the picture.
constexpr bool LightsLeftOut(std::uint32_t recordLights) {
	return recordLights > kLightsDrawn;
}

struct Counts {
	std::uint64_t lists = 0;       // lighting records built for marked properties
	std::uint64_t eligible = 0;    // of them, with a shadow pass to add
	std::uint64_t withShadow = 0;  // lists that got at least one record
	std::uint64_t records = 0;     // shadow records added
	std::uint64_t leftOut = 0;     // lists whose lighting record has more lights than the shader draws
	std::uint32_t mostLights = 0;  // the largest light count of a lighting record
};

constexpr void Count(Counts& counts, std::uint16_t shadowPass, std::uint32_t added, std::uint32_t recordLights) {
	++counts.lists;
	if (LightsLeftOut(recordLights)) {
		++counts.leftOut;
	}
	if (recordLights > counts.mostLights) {
		counts.mostLights = recordLights;
	}
	if (shadowPass != 0) {
		++counts.eligible;
	}
	if (added != 0) {
		++counts.withShadow;
		counts.records += added;
	}
}

// The flags of a moved geometry's property (its word +0x1C) as its pass list is built: how many lists were built
// with each bit set. The two list builders do not read the same bits - ShadowLightShader's 0x0085AE20 reads 0x1000,
// 0x4000 (the terrain) and 0x400000, Lighting30's 0x008637D0 none of the three - so a bit counted here on moved
// geometry names a case to read before the route is taken.
inline constexpr std::uint32_t kFlagsOfProperty = 0x1C;
inline constexpr int kFlagBits = 32;

struct FlagCounts {
	std::uint64_t lists = 0;
	std::uint64_t bit[kFlagBits] = {};
};

constexpr void CountFlags(FlagCounts& counts, std::uint32_t flags) {
	++counts.lists;
	for (int i = 0; i < kFlagBits; ++i) {
		if (((flags >> i) & 1u) != 0) {
			++counts.bit[i];
		}
	}
}

}  // namespace obng::engine::render::lighting30_shadow
