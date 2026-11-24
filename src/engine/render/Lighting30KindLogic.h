#pragma once
// [Probe] Lighting30Kinds - the geometry the game lights in several passes given to its own one-pass lighting
// shader instead (the plugin's Lighting30Kind.cpp). A measuring instrument, off as shipped: it changes the picture.
//
// What the game has (read 2026-11-24, Oblivion.exe 1.2.0.416; knowledge/lighting-pass-and-stereo-prior-art.md):
// - Lighting30Shader (shader kind 0x1A, vtable 0x00A930C4; programs SM3%03i.vso / .pso / SM3LL%03i.pso) lights a
//   geometry in one pass: ambient, the scene's first light and the geometry's other lights, specular, fog, in
//   one program. Its property (Lighting30ShaderProperty, vtable 0x00A9576C) builds one lighting record a geometry
//   (GetRenderPasses 0x008637D0: pass ids 0x12D..0x146) where ShadowLightShader's builds three to four.
// - GetShaderDefinition 0x007B4290 makes kind 0x1A only with the byte 0x00B42EA5 set, which the render path's
//   choice 0x007B45F0 sets for an ATI or NVIDIA card with more than 255 pixel shader instruction slots
//   ("3.0 Shaders: yes" in RendererInfo.txt, written by 0x00498340 from the same byte).
// - The chooser 0x007D1920 (int __cdecl (geometry, a, b); one call, at 0x007B8091 in the object preparation
//   0x007B7FC0, for a geometry prepared with kind 1) returns 0x1A only for a material named "hair...", for a
//   geometry that blends or whose material is named "dynalpha...", and for one with a NiGeomMorpherController;
//   0x0E (SkinShader) for a material named "skin", 0x0F (ParallaxShader) for a parallax texture, 0x11 for lava,
//   and 1 (ShadowLightShader) for everything else. FaceGen heads get 0x1A by the same byte in 0x00553BB0.
//
// The two sources for the addresses: the instructions of 0x007D1920 / 0x007B7FC0 / 0x007B4290, and a second path
// in the same binary that agrees on kind 0x1A and the byte (0x00553BB0); TES Reloaded names 0x007B4290
// GetShaderDefinition. The kinds' classes are the RTTI names of the vtables their creators write.
//
// What Lighting30's list does not have (read, 0x008637D0): a record for an actor's shadow falling on the geometry
// (the game's lists get one 2x_SIMPLESHADOW record per shadow light, 0x177..0x17A). Lighting30ShadowLogic.h adds
// them for the geometry moved here; for that the plugin makes a moved geometry's property itself and marks it.
//
// 64-bit clean: plain values, no pointers.

#include <cstdint>

namespace obng::engine::render::lighting30_kind {

inline constexpr std::uint32_t kChooser = 0x007D1920;   // int __cdecl (geometry, a, b)
inline constexpr std::uint32_t kCallSite = 0x007B8091;  // its one call
inline constexpr std::uint32_t kHas30 = 0x00B42EA5;     // byte: the game makes Lighting30Shader

// The shader kinds of GetShaderDefinition 0x007B4290 this probe deals with.
inline constexpr int kShadowLight = 0x01, kSkin = 0x0E, kParallax = 0x0F, kLighting30 = 0x1A;

// The key's bits: which of the game's choices are moved.
inline constexpr std::uint32_t kMoveShadowLight = 1, kMoveSkin = 2, kMoveParallax = 4;
inline constexpr std::uint32_t kAllowed = kMoveShadowLight | kMoveSkin | kMoveParallax;

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

constexpr std::uint32_t BitsOf(int ini) {
	return ini < 0 ? 0 : static_cast<std::uint32_t>(ini) & kAllowed;
}

constexpr bool On(std::uint32_t bits) {
	return bits != 0;
}

// Whether the game's choice is one the bits move to Lighting30Shader.
constexpr bool Moves(int chosen, std::uint32_t bits) {
	return (chosen == kShadowLight && (bits & kMoveShadowLight) != 0) || (chosen == kSkin && (bits & kMoveSkin) != 0) ||
	       (chosen == kParallax && (bits & kMoveParallax) != 0);
}

// The kind the geometry is prepared with. Without the card's flag the game makes no Lighting30Shader and the
// preparation would leave the geometry without a shader, so the game's choice stays.
constexpr int KindFor(int chosen, bool has30, std::uint32_t bits) {
	return has30 && Moves(chosen, bits) ? kLighting30 : chosen;
}

struct Counts {
	std::uint64_t asked = 0;
	std::uint64_t shadowLight = 0, skin = 0, parallax = 0, lighting30 = 0, other = 0;  // by the game's choice
	std::uint64_t moved = 0;
};

constexpr void Count(Counts& counts, int chosen, int result) {
	++counts.asked;
	++(chosen == kShadowLight ? counts.shadowLight
	   : chosen == kSkin      ? counts.skin
	   : chosen == kParallax  ? counts.parallax
	   : chosen == kLighting30 ? counts.lighting30
	                           : counts.other);
	if (result != chosen) {
		++counts.moved;
	}
}

}  // namespace obng::engine::render::lighting30_kind
