#pragma once
// [Route] BodyTexturePreload: every race's body skin textures loaded and held while a loading screen runs, so that an
// NPC's body build 0x0052D2C0 finds them in the texture cache in the ride instead of loading them on the main thread
// (13-15 ms each with a wait for the loader inside, measured 2026-11-19, runs 20261119-151416 / -151740). The
// decisions over plain values; the plugin's BodyTexturePreload.cpp is the thin caller.
//
// Read 2026-11-19: the body build takes the texture of part p (0..4: param 2..6, 0xF -> 4) and sex s (0 male, 1
// female) from the race's TESTexture array at +0x224, entry p + 5 s, 12 bytes each; the path's char* at +4 of an entry,
// its length (UInt16, 0xFFFF = count it) at +8; "Textures\%s" is put before it. xOBSE's GameForms.h has the same array:
// TESRace::unk11[10] (TESTexture) at 0x224. The races: DataHandler (*0x00B33A98, xOBSE g_dataHandler) +0x44, a tList
// {data, next} (xOBSE DataHandler::races; the binary walks it the same way at 0x005C7BA0).
//
// Class B: what changes is only when a texture is loaded (at the loading screen, not in the ride) and that it stays
// loaded; the textures themselves are the ones the body build would load. Mode 2, async (the body build's loads moved
// to the loader's thread), is not built yet and runs as 1.
// 64-bit clean: offsets as constants, addresses as 32-bit handles in the caller.
#include <cstddef>
#include <cstdint>

namespace obng::engine::render::body_texture_preload {

constexpr std::uint32_t kTextures = 0x224;       // TESRace's body part textures (read; xOBSE unk11)
constexpr std::uint32_t kStride = 0x0C;          // sizeof(TESTexture)
constexpr std::uint32_t kCount = 10;             // 5 parts x 2 sexes
constexpr std::uint32_t kPathPointer = 0x04;     // the BSString's char* in a TESTexture
constexpr std::uint32_t kPathLength = 0x08;      // its UInt16 length
constexpr std::uint16_t kLengthUnknown = 0xFFFF; // count the string
constexpr char kPrefix[] = "Textures\\";
static_assert(kTextures + kStride * kCount == 0x29C, "the array ends where xOBSE's unk12 starts");

enum class Mode : std::uint8_t { Off, On, Async };
enum class Way : std::uint8_t { None, Preload };

inline Mode ModeOf(int value) {
	return value == 1 ? Mode::On : value == 2 ? Mode::Async : Mode::Off;
}

// Async is not built: it preloads as On does.
inline Way WayOf(Mode mode) {
	return mode == Mode::Off ? Way::None : Way::Preload;
}

inline std::uint32_t EntryOffset(std::uint32_t index) {
	return kTextures + kStride * index;
}

// "Textures\" + the race's path into out; false for no path, an empty one or one that does not fit.
inline bool PathOf(const char* texture, std::size_t length, char* out, std::size_t outSize) {
	const std::size_t prefix = sizeof(kPrefix) - 1;
	if (texture == nullptr || length == 0 || out == nullptr || prefix + length + 1 > outSize) {
		return false;
	}
	for (std::size_t i = 0; i < prefix; ++i) {
		out[i] = kPrefix[i];
	}
	for (std::size_t i = 0; i < length; ++i) {
		out[prefix + i] = texture[i];
	}
	out[prefix + length] = '\0';
	return true;
}

// Two paths name the same file: case and the slash's way do not count (the archives and the folder ignore both).
inline bool SameFile(const char* a, const char* b) {
	for (;; ++a, ++b) {
		char x = *a, y = *b;
		x = x == '/' ? '\\' : (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
		y = y == '/' ? '\\' : (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
		if (x != y) {
			return false;
		}
		if (x == '\0') {
			return true;
		}
	}
}

// The race paths' pointers now against the ones of the last preload: a script (a body mod, measured 2026-11-19: 41
// paths of 000224FC Breton, 00019204 HighElf and others changed after the loading screen) gives a race new paths, so
// any difference, or another count, preloads again.
inline bool Changed(const std::uint32_t* before, std::uint32_t countBefore, const std::uint32_t* now, std::uint32_t countNow) {
	if (countBefore != countNow) {
		return true;
	}
	for (std::uint32_t i = 0; i < countNow; ++i) {
		if (before[i] != now[i]) {
			return true;
		}
	}
	return false;
}

}  // namespace obng::engine::render::body_texture_preload
