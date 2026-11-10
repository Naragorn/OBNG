#pragma once
// The constant map's entry arranging 0x009A97B0 (slot +0x48 of the three
// map vtables 0x00AB297C, 0x00AB2A4C, 0x00AB2BAC; read, the whole function)
// - when it has nothing to do.
//
// The function walks the map's entry array (the NiTArray at this+0x0C: data
// +0x10, size +0x16) with a counted reference on the entry at hand, keeps
// the lowest index of an entry whose byte +8 is not 1 ("first other"), and
// when it meets an entry whose byte is 1 behind that index it swaps the two
// (0x009A9670 twice) and goes on from there. Entries that are null are
// stepped over. With one entry or none it returns at once.
//
// So when no entry with the byte at 1 lies behind an entry with another
// byte, the walk swaps nothing: all it does is take and drop a reference
// per entry (two interlocked calls each; 12 700 of them a frame at two
// sites in Anvil, measured 2026-11-10), and every count ends as it began.
// Arranged() is that test over plain values; the caller returns instead of
// running the walk when it holds.
//
// 64-bit clean: handles are std::uint32_t.
#include <cstdint>

namespace obng::engine::render::constant_map_arrange {

using Handle = std::uint32_t;

inline constexpr std::uint32_t kEntriesOffset = 0x10;  // the array's data (read 0x009A97E1)
inline constexpr std::uint32_t kSizeOffset = 0x16;     // its size, u16 (read 0x009A97B6)
inline constexpr std::uint32_t kEntryKindOffset = 0x08;  // the byte the walk sorts by (read 0x009A9831)

// Port: Handle Entry(std::uint32_t index) const; std::uint8_t Kind(Handle entry) const.
template <typename Port>
bool Arranged(const Port& port, std::uint32_t size) {
	if (size <= 1) {
		return true;  // the original's own early return (0x009A97C1)
	}
	std::uint32_t firstOther = 0xFFFFFFFFu;
	for (std::uint32_t i = 0; i < size; ++i) {
		const Handle entry = port.Entry(i);
		if (entry == 0) {
			continue;
		}
		if (port.Kind(entry) == 1) {
			if (firstOther < i) {
				return false;  // the walk would swap here (0x009A983B)
			}
		} else if (firstOther > i) {
			firstOther = i;  // (0x009A98C5)
		}
	}
	return true;
}

}  // namespace obng::engine::render::constant_map_arrange
