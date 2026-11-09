#pragma once
// Skipping the pack of a volatile skinned geometry whose source did not
// change - the decisions, over plain values.
//
// The engine packs a geometry marked volatile (the consistency nibble 0x8000
// in the data's word +0x2E) every time it is drawn: NiDX9Renderer::
// PackSkinnedGeometryBuffer 0x00763130 forces the dirty mask to 0xFFF, every
// stream's chip is replaced and every vertex copied again (read,
// re/functions/0x00763130.md, 0x0076FE20.md). A named NPC's head is such a
// geometry: 3 515 vertices of 88 bytes, 0.3 MB a pack, one or two packs a
// frame; its vertices change in about one pack a frame over a crowd of
// sixty (the face morph), and in 29 377 changed packs with unchanged
// positions and normals no byte differed outside the colour entry, which
// the packer does not write for a data without colours and which therefore
// holds the staging block's leftovers (measured 2026-11-09, knowledge/
// skinned-vertex-streams.md).
//
// The lever: for a pack that would write what the buffers hold already, hand
// the packer the data as static (nibble 0x4000) for that one call - its own
// early-out (not forced, every stream allocated, static: return 1, read
// 0x00763153-0x0076317C) then leaves the record and its buffers as they are.
// Nothing of the packing is rebuilt; the decision is what is new, and it is
// conservative: any doubt packs.
//
// 64-bit clean: handles are std::uint32_t, no pointer size anywhere.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace obng::engine::render::volatile_pack {

using Handle = std::uint32_t;

inline constexpr std::uint16_t kConsistencyMask = 0xF000;
inline constexpr std::uint16_t kVolatile = 0x8000;
inline constexpr std::uint16_t kStatic = 0x4000;

inline bool IsVolatile(std::uint16_t word) { return (word & kConsistencyMask) == kVolatile; }
// The word with the nibble replaced by static, the dirty bits kept.
inline std::uint16_t AsStatic(std::uint16_t word) { return static_cast<std::uint16_t>((word & ~kConsistencyMask) | kStatic); }
// The word for a pack that must run: mutable (nibble 0) with every dirty bit
// set. A volatile pack replaces every stream's chip and takes its bytes from
// the dynamic ring, one vertex buffer that every volatile geometry of that
// stride writes at offset 0 with DISCARD (measured 2026-11-09 22:28: 20 993
// of 21 043 packs found their buffer written by another record since) - so
// nothing a volatile pack writes survives the next one. A mutable pack keeps
// the record's chip while it is valid (read, 0x0076FE20 step 4) and packs
// what the dirty bits name: all of it.
inline std::uint16_t AsMutableAllDirty() { return 0x0FFF; }
// The nibble back to volatile after the call, the dirty bits as the packer left them.
inline std::uint16_t BackToVolatile(std::uint16_t word) { return static_cast<std::uint16_t>((word & ~kConsistencyMask) | kVolatile); }

// ---- The record's allocation, as 0x00777F10 answers it (read) --------------------------------
// True when the record has streams and every stream's chip and the chip's
// vertex buffer are present.
inline bool AllStreamsAllocated(std::uint32_t streamCount, const Handle* chips, const Handle* buffers) {
	if (streamCount == 0) {
		return false;
	}
	for (std::uint32_t i = 0; i < streamCount; ++i) {
		if (chips[i] == 0 || buffers[i] == 0) {
			return false;
		}
	}
	return true;
}

// ---- Who wrote a vertex buffer last -----------------------------------------------------------
// Every unlock of a dynamic buffer names its writer: the record whose pack
// was running, or 0 for anything else. A record's buffers hold its last pack
// only while it is still the last writer of each.
class BufferOwners {
public:
	void Wrote(Handle buffer, Handle record) {
		if (buffer != 0) {
			owners_[buffer] = record;
		}
	}
	bool OwnedBy(Handle buffer, Handle record) const {
		const auto it = owners_.find(buffer);
		return record != 0 && it != owners_.end() && it->second == record;
	}
	std::size_t Size() const { return owners_.size(); }
	void Clear() { owners_.clear(); }

private:
	std::unordered_map<Handle, Handle> owners_;
};

// ---- What a record's last pack was made from ----------------------------------------------------
struct Source {
	Handle data = 0;
	std::uint32_t vertices = 0;
	std::vector<unsigned char> positions;  // vertices * 12
	std::vector<unsigned char> normals;    // vertices * 12, empty without normals
};

// True when the arrays the next pack would read equal the kept ones.
inline bool SameSource(const Source& kept, Handle data, std::uint32_t vertices, const unsigned char* positions, const unsigned char* normals) {
	if (kept.data != data || kept.vertices != vertices || vertices == 0 || positions == nullptr) {
		return false;
	}
	if (kept.positions.size() != static_cast<std::size_t>(vertices) * 12 || std::memcmp(kept.positions.data(), positions, kept.positions.size()) != 0) {
		return false;
	}
	if (normals == nullptr) {
		return kept.normals.empty();
	}
	return kept.normals.size() == static_cast<std::size_t>(vertices) * 12 && std::memcmp(kept.normals.data(), normals, kept.normals.size()) == 0;
}

inline void Keep(Source& kept, Handle data, std::uint32_t vertices, const unsigned char* positions, const unsigned char* normals) {
	kept.data = data;
	kept.vertices = vertices;
	kept.positions.assign(positions, positions + static_cast<std::size_t>(vertices) * 12);
	if (normals != nullptr) {
		kept.normals.assign(normals, normals + static_cast<std::size_t>(vertices) * 12);
	} else {
		kept.normals.clear();
	}
}

// ---- The decision -------------------------------------------------------------------------------
enum class Verdict : std::uint8_t {
	NotVolatile,   // not this lever's case: the original as it is
	Forced,        // the caller forces a full pack
	NotAllocated,  // a stream has no chip or buffer: the original allocates and packs
	NoSource,      // the data's arrays cannot be read: pack
	FirstPack,     // nothing kept for this record yet: pack and keep
	SourceChanged, // the arrays differ from the kept ones: pack and keep
	BufferLost,    // another writer unlocked one of the record's buffers since: pack
	Skip,          // the buffers hold this source's pack: the early-out
};

struct Inputs {
	std::uint16_t consistencyWord = 0;
	bool force = false;
	bool allocated = false;
	bool sourceReadable = false;
	bool haveKept = false;
	bool sourceSame = false;
	bool buffersOwned = false;
};

inline Verdict Decide(const Inputs& in) {
	if (!IsVolatile(in.consistencyWord)) {
		return Verdict::NotVolatile;
	}
	if (in.force) {
		return Verdict::Forced;
	}
	if (!in.allocated) {
		return Verdict::NotAllocated;
	}
	if (!in.sourceReadable) {
		return Verdict::NoSource;
	}
	if (!in.haveKept) {
		return Verdict::FirstPack;
	}
	if (!in.sourceSame) {
		return Verdict::SourceChanged;
	}
	if (!in.buffersOwned) {
		return Verdict::BufferLost;
	}
	return Verdict::Skip;
}

// After a pack that ran: is the source worth keeping for the next call?
inline bool KeepsSource(Verdict v) {
	return v == Verdict::FirstPack || v == Verdict::SourceChanged || v == Verdict::BufferLost || v == Verdict::NotAllocated || v == Verdict::Forced;
}

// ---- The gate's comparison ------------------------------------------------------------------------
// Two packs of one stream compared, leaving out the bytes of an entry the
// packer does not write (the colour entry of a data without colours): true
// when every other byte is equal. gapOffset >= stride means no gap.
inline bool SameOutsideGap(const unsigned char* a, const unsigned char* b, std::size_t bytes, std::uint32_t stride, std::uint32_t gapOffset,
                           std::uint32_t gapBytes) {
	if (stride == 0 || gapOffset >= stride) {
		return std::memcmp(a, b, bytes) == 0;
	}
	const std::uint32_t gapEnd = gapOffset + gapBytes < stride ? gapOffset + gapBytes : stride;
	for (std::size_t at = 0; at + stride <= bytes; at += stride) {
		if (std::memcmp(a + at, b + at, gapOffset) != 0 || std::memcmp(a + at + gapEnd, b + at + gapEnd, stride - gapEnd) != 0) {
			return false;
		}
	}
	const std::size_t tail = bytes % stride;
	return tail == 0 || std::memcmp(a + bytes - tail, b + bytes - tail, tail) == 0;
}

}  // namespace obng::engine::render::volatile_pack
