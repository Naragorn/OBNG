#pragma once
// The lit geometry's last pass without lights - the decisions over plain values ([Route] FogPassFold in the
// plugin's FogPassFold.cpp).
//
// The pass builder 0x0085AE20 of a lit shader property ends its list in one of two ways (read in the instructions,
// 0x0085B99C - 0x0085BA2A): when its "all four passes" byte ([esp+0x16]: ambient, diffuse, texture and specular
// all asked for, and no branch cleared it) is set, 0x00854190 (call at 0x0085BA25) appends a pass without lights,
// 0x190..0x194, for the geometry - one more draw of it; when the byte is clear, the list's last record gets its
// byte +7 set instead (unless that record is a 0x190 or 0x192), and ShadowLightShader's set up 0x007C9F30 then
// leaves the constant at 0x00B4668C alone for that pass where it zeroes it for every other one (0x007CB5E0). With
// bDoSpecularPass=0 every list ends the second way.
//
// In the tutorial dungeon the pass without lights is 510 of 2 360 - 3 240 records a frame, and most of the
// geometry that has one has no specular light pass (0x115..0x129, one per light, 0x00859880) in its list: the
// specular fade put it out of range, or no light gave one. For such a list the route ends it the second way, as
// the builder itself would with the specular pass off. What the pass without lights draws is not read (the fog,
// by the constant's neighbours - unverified); the gate is the picture, both ways drawn and compared (class C).
//
// 64-bit clean: plain values and 32-bit handles only.

#include <cstdint>

namespace obng::engine::render::fog_pass_fold {

inline constexpr std::uint32_t kCallSite = 0x0085BA25;  // the builder's call of the pass without lights (read)
inline constexpr std::uint32_t kAppendPass = 0x00854190;  // __thiscall(property; geometry, count*, building, first*, a, b, c), RET 0x1C (read)
inline constexpr std::uint16_t kSpecularFirst = 0x115;  // 0x00859880's passes, one per light (read: 0x115..0x129)
inline constexpr std::uint16_t kSpecularLast = 0x129;
inline constexpr std::uint16_t kPlain = 0x190;    // the two ids the builder's own marking leaves alone (0x0085B9B3, 0x0085B9BA)
inline constexpr std::uint16_t kPlainOther = 0x192;

// What OBNG runs with when OBNG.ini has no key.
constexpr int Shipped() {
	return 0;
}

// 1 folds the lists of one record (the pass and the constant meet in one draw, nothing blends in between), 2 only
// counts the lists by shape and leaves every one to the game, 3 folds every list without a specular light pass.
enum class Mode { Off, FoldSingle, Census, FoldAll };

constexpr Mode ModeOf(int ini) {
	return ini == 1 ? Mode::FoldSingle : ini == 2 ? Mode::Census : ini == 3 ? Mode::FoldAll : Mode::Off;
}

constexpr bool IsSpecularLight(std::uint16_t pass) {
	return pass >= kSpecularFirst && pass <= kSpecularLast;
}

// A list as the call site finds it. Counted: the builder only counts passes (its building byte is not 1), there is
// no list to look at. Extra: the property has the object at +0x8C, so 0x0085A200 may have appended a record after
// the one the builder's own marking would take. Specular: a specular light pass is in the list.
enum class Shape { Counted, Empty, Extra, Specular, Single, Several };

// The shape, walking at most `count` records back from the last node: prev(node) is the node before, passOf(node)
// its record's pass id. A list whose nodes end before `count` is taken as far as it goes.
template <typename Prev, typename PassOf>
Shape ShapeOf(bool building, bool extra, std::uint32_t count, std::uint32_t last, Prev prev, PassOf passOf) {
	if (!building) {
		return Shape::Counted;
	}
	if (count == 0 || last == 0) {
		return Shape::Empty;
	}
	if (extra) {
		return Shape::Extra;
	}
	std::uint32_t node = last;
	for (std::uint32_t i = 0; i < count && node != 0; ++i, node = prev(node)) {
		if (IsSpecularLight(passOf(node))) {
			return Shape::Specular;
		}
	}
	return count == 1 ? Shape::Single : Shape::Several;
}

// Whether the pass without lights is left out for this list.
constexpr bool Folds(Mode mode, Shape shape) {
	return (mode == Mode::FoldSingle && shape == Shape::Single) || (mode == Mode::FoldAll && (shape == Shape::Single || shape == Shape::Several));
}

// The alternation for comparing both endings inside one run ([Route] FogPassFoldAlternate=1): every summary is a
// switch, the run starts with the game's ending, and at each switch the plugin flips kRebuildBit in the pass mask
// 0x00B42F40 - the mask is part of every list's key (0x007D85D0: +0x24 against (render mode << 8) | flags, the
// flags being the mask as 0x007AD270 passes it), so every list is built anew under the other ending. The builder
// reads bits 1, 2, 4, 8 of its flags and 0x007D85D0 bit 0x10 of the mask; 0x40 is read by neither (read).
inline constexpr std::uint32_t kPassMask = 0x00B42F40;
inline constexpr std::uint32_t kRebuildBit = 0x40;

constexpr bool Active(bool alternate, std::uint64_t switches) {
	return !alternate || (switches % 2) == 1;
}

// The builder's own marking of the last record (0x0085B9AF - 0x0085B9C1): not a 0x190, not a 0x192.
constexpr bool MarksLast(std::uint16_t lastPass) {
	return lastPass != kPlain && lastPass != kPlainOther;
}

// What a run did, for the summary.
struct Counts {
	std::uint64_t calls = 0;
	std::uint64_t byShape[6] = {};
	std::uint64_t folded = 0;
	std::uint64_t marked = 0;
};

constexpr void Count(Counts& counts, Shape shape, bool folded, bool marked) {
	++counts.calls;
	++counts.byShape[static_cast<unsigned>(shape)];
	counts.folded += folded ? 1 : 0;
	counts.marked += marked ? 1 : 0;
}

}  // namespace obng::engine::render::fog_pass_fold
