// Every flow of the byte comparison (src/engine/memory/EqualBytesLogic.h): each length through the 64-byte blocks,
// the 16-byte blocks and the tail, a difference at every place, unaligned starts - against memcmp throughout.

#include <cstdint>
#include <cstring>
#include <vector>

#include "Check.h"
#include "engine/memory/EqualBytesLogic.h"

using namespace obng::engine::memory;
using namespace obng::test;

int main() {
	Section("Nothing to compare");
	const unsigned char one = 1, two = 2;
	Check(EqualBytes(&one, &two, 0), "zero bytes are equal");
	Check(EqualBytes(nullptr, nullptr, 0), "also without arrays");

	Section("Every length up to 300 bytes, at four alignments");
	bool equalSeen = true, differenceSeen = true;
	std::uint32_t compared = 0;
	for (std::size_t offset = 0; offset < 4; ++offset) {
		for (std::size_t length = 1; length <= 300; ++length) {
			std::vector<unsigned char> a(offset + length + 8), b(offset + length + 8);
			for (std::size_t i = 0; i < a.size(); ++i) {
				a[i] = b[i] = static_cast<unsigned char>(i * 37 + length);
			}
			// The bytes before and after the range differ: they are not the comparison's.
			a[offset + length] ^= 0xFF;
			if (offset != 0) {
				b[offset - 1] ^= 0xFF;
			}
			equalSeen = equalSeen && EqualBytes(a.data() + offset, b.data() + offset, length);
			for (std::size_t at = 0; at < length; ++at) {
				b[offset + at] ^= 0x10;
				differenceSeen = differenceSeen && !EqualBytes(a.data() + offset, b.data() + offset, length) &&
				                 std::memcmp(a.data() + offset, b.data() + offset, length) != 0;
				b[offset + at] ^= 0x10;
				++compared;
			}
		}
	}
	Check(equalSeen, "equal ranges are equal, whatever lies around them");
	Check(differenceSeen && compared == 4 * (300 * 301 / 2), "one differing byte is found at every place of every length");

	Section("The block borders");
	std::vector<unsigned char> a(200, 7), b(200, 7);
	b[63] = 8;
	Check(!EqualBytes(a.data(), b.data(), 64) && EqualBytes(a.data(), b.data(), 63), "the last byte of the first 64-byte block");
	b[63] = 7;
	b[64] = 8;
	Check(!EqualBytes(a.data(), b.data(), 65) && EqualBytes(a.data(), b.data(), 64), "the first byte after it");
	b[64] = 7;
	b[79] = 8;
	Check(!EqualBytes(a.data(), b.data(), 80) && EqualBytes(a.data(), b.data(), 79), "the last byte of a 16-byte block");
	b[79] = 7;
	b[199] = 8;
	Check(!EqualBytes(a.data(), b.data(), 200) && EqualBytes(a.data(), b.data(), 199), "the last byte of a tail");
	b[199] = 7;
	b[5] = 8;
	Check(!EqualBytes(a.data(), b.data(), 9) && EqualBytes(a.data() + 6, b.data() + 6, 9), "under 16 bytes: byte by byte");

	Section("A head's arrays");
	std::vector<unsigned char> head(3515 * 12), kept(3515 * 12);
	for (std::size_t i = 0; i < head.size(); ++i) {
		head[i] = kept[i] = static_cast<unsigned char>(i ^ (i >> 8));
	}
	Check(EqualBytes(head.data(), kept.data(), head.size()), "42 180 bytes, equal");
	kept[42179] ^= 1;
	Check(!EqualBytes(head.data(), kept.data(), head.size()), "its last byte changed");
	return Failures();
}
