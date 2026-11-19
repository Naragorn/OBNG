#pragma once
// FileFinder's search slot 0x00431800 answered from a listing of the loose files made once - the decisions and the
// listing, over plain values ([Route] LooseFileList in the plugin's LooseFileList.cpp).
//
// The slot (read 2026-11-19 in the disassembly; FileFinder's vtable 0x00A36414 slot 3, called by FindFile 0x00431020
// through `call eax` at 0x004310BF; __thiscall(char* path, flags, mask), RET 0x0C):
//   1. unless flags & 1: the archives, 0x0042EBA0(path, mask) - true: return 2;
//   2. unless flags & 2: _access(path, 0) 0x00982D71 (relative to the working folder) - found: return 1;
//   3. unless flags & 4: for each search folder i < word [this+0x10] of the char* array [this+8]: when
//      _strnicmp(path, folder, strlen(folder)) 0x009864D9 is not 0 (the path does not start with it), folder + path is
//      built in a 0xF4-byte local and _access'd - found: that is copied over the path, return 1;
//   4. return 0.
// Steps 2 and 3 are the operating system's: under Proton about 0.1 ms a question, 5 to 10 ms at every cell crossing
// for the far cells' LOD files (knowledge/background-loading.md). The listing answers them from memory: every file and
// folder under the working folder, read once at start. _access(path, 0) is true for a file and for a folder alike.
//
// Class A: the same answer as long as the listed files do not change while the game runs. Only the kinds that
// FileFindCache keeps are answered (asset kinds; no INI, text or log, which mods write), and only paths that Windows
// would not rewrite (no drive, no root, no '.' or '..' part, no doubled separator, no name ending in '.' or ' ', no
// byte over 0x7F, no wildcard); everything else goes to the original whole. The check mode compares every answer.
//
// 64-bit clean: no engine pointer; the archives and _strnicmp come in as callables.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "engine/io/FileFindCacheLogic.h"

namespace obng::engine::io::loose_file_list {

constexpr std::size_t kKeyMax = 256;
constexpr std::size_t kCombinedMax = 0xF4;  // 0x00431800's local for folder + path, [esp+0x18] up to the cookie at [esp+0x10C]
constexpr std::uint32_t kFoldersMax = 8;

inline char Lower(char ch) {
	return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

inline bool Separator(char ch) {
	return ch == '\\' || ch == '/';
}

inline bool Forbidden(unsigned char ch) {
	return ch < 0x20 || ch >= 0x80 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*';
}

// One part between separators that Windows takes as it is: not empty, not "." or "..", not ending in '.' or ' '.
inline bool PartKept(const char* begin, std::size_t length) {
	if (length == 0) {
		return false;
	}
	const char last = begin[length - 1];
	return last != '.' && last != ' ';
}

// The listing's key of a path: lower case, '/' as '\'. False when the path is one this listing does not answer for.
inline bool MakeKey(const char* path, char* out, std::size_t capacity) {
	if (path == nullptr || path[0] == '\0') {
		return false;
	}
	std::size_t n = 0;
	std::size_t partStart = 0;
	for (;; ++n) {
		const char ch = path[n];
		if (ch == '\0' || Separator(ch)) {
			if (!PartKept(path + partStart, n - partStart)) {
				return false;
			}
			if (ch == '\0') {
				break;
			}
			partStart = n + 1;
		} else if (Forbidden(static_cast<unsigned char>(ch))) {
			return false;
		}
		if (n + 1 >= capacity) {
			return false;
		}
		out[n] = Separator(ch) ? '\\' : Lower(ch);
	}
	out[n] = '\0';
	return true;
}

struct KeyHash {
	using is_transparent = void;
	std::size_t operator()(std::string_view key) const noexcept {
		std::uint64_t h = 1469598103934665603ull;  // FNV-1a
		for (const char ch : key) {
			h = (h ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
		}
		return static_cast<std::size_t>(h ^ (h >> 32));
	}
};

struct KeyEqual {
	using is_transparent = void;
	bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

// Every file and folder under the working folder by its key; it answers only once sealed (filled on one thread,
// read by any after that).
class Listing {
public:
	void Add(const char* relative) {
		std::string key(relative);
		for (char& ch : key) {
			ch = Separator(ch) ? '\\' : Lower(ch);
		}
		keys_.insert(std::move(key));
	}
	// A name with a twin in its folder that differs only in case (a Linux folder; NTFS has none): Wine's lookup takes
	// one of the two, so the listing answers for nothing at or under it (measured 2026-11-19: Data\Meshes\Armor and
	// Data\Meshes\armor in the Linux game, the check's one mismatch).
	void MarkAmbiguous(const char* relative) {
		std::string key(relative);
		for (char& ch : key) {
			ch = Separator(ch) ? '\\' : Lower(ch);
		}
		ambiguous_.insert(std::move(key));
	}
	// The key or one of its leading folders is marked.
	bool Ambiguous(std::string_view key) const {
		if (ambiguous_.empty()) {
			return false;
		}
		for (std::size_t i = 0; i <= key.size(); ++i) {
			if ((i == key.size() || key[i] == '\\') && ambiguous_.find(key.substr(0, i)) != ambiguous_.end()) {
				return true;
			}
		}
		return false;
	}
	void Seal() { sealed_ = true; }
	bool Sealed() const { return sealed_; }
	bool Has(std::string_view key) const { return sealed_ && keys_.find(key) != keys_.end(); }
	std::size_t Size() const { return keys_.size(); }

private:
	std::unordered_set<std::string, KeyHash, KeyEqual> keys_;
	std::unordered_set<std::string, KeyHash, KeyEqual> ambiguous_;
	bool sealed_ = false;
};

// The names of one folder that have a twin differing only in case, each once by its key, in the order first met.
inline std::vector<std::string> CaseTwins(const std::vector<std::string>& names) {
	std::unordered_set<std::string> seen;
	std::unordered_set<std::string> reported;
	std::vector<std::string> twins;
	for (const std::string& name : names) {
		std::string key(name);
		for (char& ch : key) {
			ch = Lower(ch);
		}
		if (!seen.insert(key).second && reported.insert(key).second) {
			twins.push_back(key);
		}
	}
	return twins;
}

inline bool Eligible(const char* path) {
	return file_find_cache::Classify(path) == file_find_cache::Pass::Keep;
}

struct Folders {
	const char* const* names = nullptr;
	std::uint32_t count = 0;
};

enum class Outcome : std::uint8_t { Refused, Archive, Loose, LooseInFolder, NotFound };

inline std::uint32_t SlotResult(Outcome outcome) {
	return outcome == Outcome::Archive ? 2u : (outcome == Outcome::Loose || outcome == Outcome::LooseInFolder) ? 1u : 0u;
}

// The slot's search with the listing in place of _access. Every key is made before the archives are asked, so a
// refusal leaves nothing done twice when the original then answers the call. On LooseInFolder, *combined is what the
// slot copies over the path.
template <class Archive, class Strnicmp>
Outcome Search(const char* path, std::uint32_t flags, Archive inArchive, Strnicmp strnicmp, const Folders& folders, const Listing& listing,
               std::string* combined) {
	combined->clear();
	if (!listing.Sealed() || !Eligible(path)) {
		return Outcome::Refused;
	}
	char pathKey[kKeyMax];
	if ((flags & 2) == 0 && (!MakeKey(path, pathKey, sizeof(pathKey)) || listing.Ambiguous(pathKey))) {
		return Outcome::Refused;
	}
	char folderKeys[kFoldersMax][kKeyMax];
	bool tried[kFoldersMax] = {};
	if ((flags & 4) == 0) {
		if (folders.count > kFoldersMax) {
			return Outcome::Refused;
		}
		const std::size_t pathLength = std::strlen(path);
		for (std::uint32_t i = 0; i < folders.count; ++i) {
			const char* folder = folders.names[i];
			const std::size_t folderLength = std::strlen(folder);
			if (strnicmp(path, folder, folderLength) == 0) {
				continue;
			}
			char joined[kCombinedMax];
			if (folderLength + pathLength + 1 > sizeof(joined)) {
				return Outcome::Refused;
			}
			std::memcpy(joined, folder, folderLength);
			std::memcpy(joined + folderLength, path, pathLength + 1);
			if (!MakeKey(joined, folderKeys[i], kKeyMax) || listing.Ambiguous(folderKeys[i])) {
				return Outcome::Refused;
			}
			tried[i] = true;
		}
	}
	if ((flags & 1) == 0 && inArchive(path)) {
		return Outcome::Archive;
	}
	if ((flags & 2) == 0 && listing.Has(pathKey)) {
		return Outcome::Loose;
	}
	if ((flags & 4) == 0) {
		for (std::uint32_t i = 0; i < folders.count; ++i) {
			if (tried[i] && listing.Has(folderKeys[i])) {
				*combined = std::string(folders.names[i]) + path;
				return Outcome::LooseInFolder;
			}
		}
	}
	return Outcome::NotFound;
}

}  // namespace obng::engine::io::loose_file_list
