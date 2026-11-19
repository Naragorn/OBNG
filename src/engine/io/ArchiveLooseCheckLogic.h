#pragma once
// The archives' loose file check 0x0042C1D0 answered from the listing of the loose files made once - the decision over
// plain values ([Route] ArchiveLooseCheck in the plugin's ArchiveLooseCheck.cpp).
//
// The check (read 2026-11-19 in the disassembly; __thiscall(Archive* this, BSAEntry* record, char* path) -> bool in AL,
// RET 8; called by FolderContainsFile 0x0042D000 at the first find of a record, the record either the folder's cached
// index +0x190 or the binary search's hit):
//   1. unless the byte 0x00B04460 (archive invalidation on) is set and bit 31 of the record's +8 is clear: return 0;
//   2. a path: "Data\" + path is built in a 260-byte local and _stat64i32 0x00983754'd; when found and its mtime is
//      later than the archive's [this+0x180]/[this+0x184]: the record's +0xC &= 0x80000000, return 1 (the loose file
//      wins, the archive does not have it);
//   3. otherwise (no path, no such file, or an older one): bit 31 of the record's +8 set (checked), return 0.
// FolderContainsFile takes 0 as "in the archive". The _stat is the operating system's: under Proton about 50 us a
// record, 3 055 records a ride, 1.7 to 3.5 ms at a cell crossing (measured 2026-11-19, runs 20261119-122033 and
// -122357). The listing answers step 2 for the common case, no loose file of that name: the _stat would fail, so the
// record is marked checked and 0 returned without asking. A name the listing has goes to the original, which then asks
// for the time.
//
// Class A: the same answer as long as the listed files do not change while the game runs (LooseFileListLogic.h's
// assumption, the same listing). Paths of a kind the listing does not answer for, or one Windows would rewrite, or one
// under a case twin go to the original.
//
// 64-bit clean: no engine pointer; the record's flags and the switch come in as values.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/io/LooseFileListLogic.h"

namespace obng::engine::io::archive_loose_check {

constexpr std::uint32_t kChecked = 0x80000000u;  // the record's +8 bit 31: asked once already
constexpr char kDataPrefix[] = "Data\\";          // 0x00A2FFB8, copied in front of the path (read)

enum class Decision : std::uint8_t {
	Skip,         // switch off or the record checked before: the original returns 0 and writes nothing
	NoLooseFile,  // the _stat would fail: mark the record checked, return 0
	Loose,        // the listing has a file or folder by that name: the original asks its time
	Refused,      // the listing cannot say: the original answers
};

inline Decision Decide(bool enabled, std::uint32_t recordFlags, const char* path, const loose_file_list::Listing& listing) {
	if (!enabled || (recordFlags & kChecked) != 0) {
		return Decision::Skip;
	}
	if (path == nullptr) {
		return Decision::NoLooseFile;
	}
	if (!listing.Sealed() || !loose_file_list::Eligible(path)) {
		return Decision::Refused;
	}
	const std::size_t prefix = sizeof(kDataPrefix) - 1;
	const std::size_t length = std::strlen(path);
	char joined[loose_file_list::kKeyMax];
	if (prefix + length + 1 > sizeof(joined)) {
		return Decision::Refused;
	}
	std::memcpy(joined, kDataPrefix, prefix);
	std::memcpy(joined + prefix, path, length + 1);
	char key[loose_file_list::kKeyMax];
	if (!loose_file_list::MakeKey(joined, key, sizeof(key)) || listing.Ambiguous(key)) {
		return Decision::Refused;
	}
	return listing.Has(key) ? Decision::Loose : Decision::NoLooseFile;
}

// What the route does with a decision: answer it (true, with the check's result and whether the record is marked) or
// call the original (false).
struct Answer {
	bool answered = false;
	bool mark = false;  // set bit 31 of the record's +8
};

inline Answer AnswerOf(Decision decision) {
	switch (decision) {
	case Decision::Skip: return Answer{true, false};
	case Decision::NoLooseFile: return Answer{true, true};
	case Decision::Loose:
	case Decision::Refused: break;
	}
	return Answer{false, false};
}

}  // namespace obng::engine::io::archive_loose_check
