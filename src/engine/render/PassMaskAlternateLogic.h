#pragma once
// [Probe] PassMaskAlternate - the game's lighting pass mask switched inside one run, to compare what a lighting
// pass costs under the same load (the plugin's PassMaskAlternate.cpp). A measuring instrument, off as shipped.
//
// The mask 0x00B42F40 is bDoAmbientPass (1) | bDoDiffusePass (2) | bDoTexturePass (4) | bDoSpecularPass (8) of
// Oblivion.ini's [Display] (read 2026-11-23, 0x00406950). It is part of every pass list's key (0x007D85D0: +0x24
// against (render mode << 8) | flags, the flags being the mask as 0x007AD270 passes it), so a changed bit makes
// every list build anew at its next use, under the new mask. The pass builder 0x0085AE20 reads bits 1, 2, 4, 8 of
// its flags, 0x007D85D0 reads bit 0x10 of the mask, 0x40 is read by neither (read): flipping 0x40 alone rebuilds
// every list with the same content - the control for what the rebuilding itself costs.
//
// Why inside one run: the tutorial's fight differs by 2 ms of scene pass from run to run (measured 2026-11-24),
// more than most levers gain. Every summary is a switch; the run starts with the game's own mask.
//
// The same instrument as [Route] FogPassFoldAlternate, which flips 0x40 for its own route; not both at once with
// 0x40 here (the two flips would cancel).
//
// 64-bit clean: plain values, no pointers.

#include <cstdint>

namespace obng::engine::render::pass_mask_alternate {

inline constexpr std::uint32_t kPassMask = 0x00B42F40;
inline constexpr std::uint32_t kAmbient = 1, kDiffuse = 2, kTexture = 4, kSpecular = 8, kRebuild = 0x40;
// 0x10 is left alone: 0x007D85D0 reads it for something this probe does not know.
inline constexpr std::uint32_t kAllowed = kAmbient | kDiffuse | kTexture | kSpecular | kRebuild;

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

// The bits a switch flips, from the key's value: 8 the specular pass, 0x40 the control.
constexpr std::uint32_t BitsOf(int ini) {
	return ini < 0 ? 0 : static_cast<std::uint32_t>(ini) & kAllowed;
}

constexpr bool On(std::uint32_t bits) {
	return bits != 0;
}

constexpr std::uint32_t Flip(std::uint32_t mask, std::uint32_t bits) {
	return mask ^ bits;
}

// Whether the phase after this many switches runs with the bits flipped.
constexpr bool Flipped(std::uint64_t switches) {
	return (switches % 2) == 1;
}

}  // namespace obng::engine::render::pass_mask_alternate
