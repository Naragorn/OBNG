#pragma once
// The game's WAV files kept in memory after their first read, and a sound folder's list of files kept after its first
// listing - the decisions and the tables, over plain values ([Route] SoundFileCache in the plugin's SoundFileCache.cpp).
//
// Read 2026-11-20: a sound that is not an MP3 is made by path each time it plays. 0x006AC610 (the sound by path) asks
// 0x006A9CD0 for one of a folder's files when the path ends in a backslash - a listing of "<folder>*.wav" through
// 0x00431970, one of its files taken by the game's random number 0x0047DF80 - and then 0x006AABD0 reads the file:
// mmioOpen with an I/O procedure of the game's (0x006AAAA0, set at 0x006AAC43) that opens the file through
// 0x00431130 and reads it through 0x0042C8E0, a DirectSound buffer made and filled byte by byte. Measured (Linux,
// run tablet-open-hitch-late/20261120-203805): the menus' open and close sounds cost 0.5 - 2.7 ms each on the main
// thread, 0.19 - 0.27 ms of it the listing.
//
// Class A: the same bytes and the same list, without the file system. What makes them the same is that the sound
// files under Data do not change while the game runs. The check ([Route] SoundFileCache=2) keeps the game's file
// open beside the kept bytes and compares every answer of the procedure, and lists the folder again every time.
//
// 64-bit clean: no engine pointer; the plugin holds the game's file and the mmio record.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obng::engine::sound::file_cache {

constexpr int Shipped() { return 0; }

enum class Mode : std::uint8_t { Off, Route, Check };
constexpr Mode ModeOf(int value) { return value == 1 ? Mode::Route : value == 2 ? Mode::Check : Mode::Off; }

// mmio's messages to an I/O procedure (mmsystem.h MMIOM_READ ... MMIOM_CLOSE) and what the game's procedure
// 0x006AAAA0 answers (read: its jump table at 0x006AABBC has the five, anything else is logged and answered 0x10C).
constexpr std::uint32_t kMessageRead = 0;
constexpr std::uint32_t kMessageWrite = 1;
constexpr std::uint32_t kMessageSeek = 2;
constexpr std::uint32_t kMessageOpen = 3;
constexpr std::uint32_t kMessageClose = 4;
constexpr std::uint32_t kAnswerOk = 0;
constexpr std::uint32_t kAnswerNotFound = 0x101;     // the open: no such file (MMIOERR_FILENOTFOUND)
constexpr std::uint32_t kAnswerCannotClose = 0x104;  // the close without a file
constexpr std::uint32_t kAnswerCannotRead = 0x105;   // the read without a file
constexpr std::uint32_t kAnswerCannotSeek = 0x107;   // the seek without a file
constexpr std::uint32_t kAnswerUnsupported = 0x10C;  // a write, or any other message

// The WAV reader 0x006AABD0 sets its procedure with `mov dword ptr [esp+0xB0], 0x006AAAA0` at 0x006AAC43; the four
// bytes of the address sit at 0x006AAC4A.
constexpr std::uint32_t kProcedure = 0x006AAAA0;
constexpr std::uint32_t kProcedureSite = 0x006AAC4A;
// MMIOINFO as the procedure uses it: lDiskOffset +0x2C, adwInfo[0] +0x30 (the game's file).
constexpr std::uint32_t kDiskOffset = 0x2C;
constexpr std::uint32_t kInfo = 0x30;

constexpr std::size_t kPathMax = 260;
constexpr std::size_t kFileMax = 1u << 20;    // a larger file is read from its file every time, as in the game
constexpr std::size_t kTotalMax = 24u << 20;  // all kept files; the least recently played go first
constexpr std::size_t kFilesMax = 2048;
constexpr std::size_t kFoldersMax = 4096;

// The key of a file or a listing: the path in lower case with backslashes. False for no path, an empty one, or one
// that does not fit the game's buffers.
inline bool KeyOf(const char* path, std::string* key) {
	if (path == nullptr || path[0] == '\0') {
		return false;
	}
	const std::size_t length = std::strlen(path);
	if (length + 1 > kPathMax) {
		return false;
	}
	key->resize(length);
	for (std::size_t i = 0; i < length; ++i) {
		const char ch = path[i];
		(*key)[i] = ch == '/' ? '\\' : (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
	}
	return true;
}

using Bytes = std::vector<std::uint8_t>;

enum class Kept : std::uint8_t { New, Already, TooLarge };

// The files by key within a budget. A file handed out stays alive for its holder when it is dropped here.
class Store {
public:
	explicit Store(std::size_t fileMax = kFileMax, std::size_t totalMax = kTotalMax, std::size_t filesMax = kFilesMax)
	    : fileMax_(fileMax), totalMax_(totalMax), filesMax_(filesMax) {}

	// The kept file, marked as just used; null when it is not kept.
	std::shared_ptr<const Bytes> Find(const std::string& key) {
		const auto it = files_.find(key);
		if (it == files_.end()) {
			return nullptr;
		}
		it->second.used = ++clock_;
		return it->second.bytes;
	}
	// A file found too large once is not read whole again.
	bool Refused(const std::string& key) const { return refused_.find(key) != refused_.end(); }
	bool Fits(std::size_t size) const { return size <= fileMax_ && size <= totalMax_; }

	Kept Keep(const std::string& key, std::shared_ptr<const Bytes> bytes) {
		if (files_.find(key) != files_.end()) {
			return Kept::Already;
		}
		if (!Fits(bytes->size())) {
			if (refused_.size() < filesMax_) {
				refused_.emplace(key, true);
			}
			return Kept::TooLarge;
		}
		while (!files_.empty() && (total_ + bytes->size() > totalMax_ || files_.size() >= filesMax_)) {
			DropOldest();
		}
		total_ += bytes->size();
		files_.emplace(key, File{std::move(bytes), ++clock_});
		return Kept::New;
	}
	std::size_t Files() const { return files_.size(); }
	std::size_t Total() const { return total_; }
	std::uint64_t Dropped() const { return dropped_; }

private:
	struct File {
		std::shared_ptr<const Bytes> bytes;
		std::uint64_t used;
	};
	void DropOldest() {
		auto oldest = files_.begin();
		for (auto it = files_.begin(); it != files_.end(); ++it) {
			if (it->second.used < oldest->second.used) {
				oldest = it;
			}
		}
		total_ -= oldest->second.bytes->size();
		files_.erase(oldest);
		++dropped_;
	}

	std::size_t fileMax_;
	std::size_t totalMax_;
	std::size_t filesMax_;
	std::size_t total_ = 0;
	std::uint64_t clock_ = 0;
	std::uint64_t dropped_ = 0;
	std::unordered_map<std::string, File> files_;
	std::unordered_map<std::string, bool> refused_;
};

// What the procedure does with an open.
enum class Plan : std::uint8_t {
	Original,  // the game's file, message by message, as without the route
	Serve,     // the kept bytes
	Fill,      // the game's file read whole once, kept, then the kept bytes
};
constexpr Plan PlanOf(Mode mode, bool keyOk, bool kept, bool refused) {
	if (mode == Mode::Off || !keyOk) {
		return Plan::Original;
	}
	if (kept) {
		return Plan::Serve;
	}
	return refused ? Plan::Original : Plan::Fill;
}

// A reader's place in a kept file.
struct Cursor {
	std::uint32_t position = 0;
};

// An answer of the procedure: its return value and the record's lDiskOffset afterwards.
struct Answer {
	std::uint32_t result = 0;
	std::uint32_t diskOffset = 0;
	constexpr bool operator==(const Answer& other) const { return result == other.result && diskOffset == other.diskOffset; }
};

// MMIOM_READ as the game's procedure answers it (0x006AAB1E): the bytes read are returned and added to lDiskOffset.
inline Answer Read(const Bytes& file, Cursor* cursor, std::uint8_t* out, std::uint32_t wanted, std::uint32_t diskOffset) {
	const std::size_t size = file.size();
	const std::size_t left = cursor->position < size ? size - cursor->position : 0;
	const std::uint32_t taken = static_cast<std::uint32_t>(wanted < left ? wanted : left);
	if (taken != 0) {
		std::memcpy(out, file.data() + cursor->position, taken);
		cursor->position += taken;
	}
	return {taken, diskOffset + taken};
}

// MMIOM_SEEK as the game's procedure answers it (0x006AAB48): origin 0 from the start, 2 from the end, anything else
// from the place; the answer is 0 and lDiskOffset the new place. A place before the start is the start.
inline Answer Seek(const Bytes& file, Cursor* cursor, std::int32_t offset, std::uint32_t origin) {
	const std::int64_t base = origin == 0 ? 0 : origin == 2 ? static_cast<std::int64_t>(file.size()) : static_cast<std::int64_t>(cursor->position);
	std::int64_t place = base + offset;
	if (place < 0) {
		place = 0;
	}
	if (place > 0xFFFFFFFFll) {
		place = 0xFFFFFFFFll;
	}
	cursor->position = static_cast<std::uint32_t>(place);
	return {kAnswerOk, cursor->position};
}

// A folder's files as the lister 0x00431970 gave them, in its order. A node of the list without a name is kept as a
// node (the game's walk counts it) with `named` false.
struct Name {
	std::string text;
	bool named = true;
};
using Names = std::vector<Name>;

// 0x006A9CD0's count (0x006A9D6F): the nodes with a name.
inline std::uint32_t CountOf(const Names& names) {
	std::uint32_t count = 0;
	for (const Name& name : names) {
		count += name.named ? 1u : 0u;
	}
	return count;
}

constexpr int kNoPick = -1;
// 0x006A9CD0's pick (0x006A9D8B - 0x006A9DC1): the random number modulo the count is counted down over every node,
// named or not, and the node it reaches zero on is copied. kNoPick when nothing is named (the game then asks for no
// random number), or when the node reached has no name (the game would read through a null pointer there).
inline int PickOf(const Names& names, std::uint32_t random) {
	const std::uint32_t count = CountOf(names);
	if (count == 0) {
		return kNoPick;
	}
	const std::uint32_t index = random % count;
	return names[index].named ? static_cast<int>(index) : kNoPick;
}

// The listings by key (no budget beyond a count: a listing is a few names).
class Folders {
public:
	explicit Folders(std::size_t limit = kFoldersMax) : limit_(limit) {}
	std::shared_ptr<const Names> Find(const std::string& key) const {
		const auto it = folders_.find(key);
		return it != folders_.end() ? it->second : nullptr;
	}
	// False when the table is full (the listing is then asked every time, as in the game).
	bool Keep(const std::string& key, std::shared_ptr<const Names> names) {
		if (folders_.find(key) != folders_.end()) {
			return true;
		}
		if (folders_.size() >= limit_) {
			return false;
		}
		folders_.emplace(key, std::move(names));
		return true;
	}
	std::size_t Size() const { return folders_.size(); }

private:
	std::size_t limit_;
	std::unordered_map<std::string, std::shared_ptr<const Names>> folders_;
};

inline bool SameNames(const Names& a, const Names& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (a[i].named != b[i].named || a[i].text != b[i].text) {
			return false;
		}
	}
	return true;
}

struct Counts {
	std::uint64_t opens = 0;       // every open the procedure saw
	std::uint64_t served = 0;      // from kept bytes
	std::uint64_t filled = 0;      // read whole and kept
	std::uint64_t original = 0;    // left to the game's file (no key, too large, not found)
	std::uint64_t tooLarge = 0;
	std::uint64_t notFound = 0;
	std::uint64_t compared = 0;    // the check: answers compared
	std::uint64_t mismatches = 0;
	std::uint64_t picks = 0;       // 0x006A9CD0's calls
	std::uint64_t picksKept = 0;   // answered from a kept listing
	std::uint64_t listings = 0;    // asked of the lister
	std::uint64_t listMismatches = 0;
};

}  // namespace obng::engine::sound::file_cache
