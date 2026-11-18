#pragma once
// FileFinder::FindFile's answers kept by path - the decisions and the table, over plain values ([Route] / [Shadow]
// FileFindCache in the plugin's FileFindCache.cpp).
//
// FindFile 0x00431020 (__thiscall(path, out, flags, mask), RET 0x10; read 2026-11-18) copies the path into a
// 256-byte buffer, sets flags 5 for a drive letter and flags 4 for ".\", and asks the finder's slot 0x0C
// (FileFinder 0x00431800: the archives 0x0042EBA0, then _access on the path and on each search folder). It
// returns that slot's result (0 not found, 1 a loose file, 2 in an archive); with a buffer it copies the found
// path into it, or sets its first byte to 0. Under Proton one call costs about 0.1 ms, nearly all of it in Wine's
// ntdll (measured 2026-11-18, knowledge/background-loading.md): 13 400 calls a ride, 5 to 12 ms at every cell
// crossing for the far cells' LOD files.
//
// Class A: the same answer, without asking the operating system again. What makes it the same is that the asset
// files under Data do not change while the game runs; files a mod writes (INIs, logs, text) and paths outside Data
// (a drive letter, ".\") are never kept, and only asset kinds are (KeptKind). The shadow run compares every kept
// answer with the original's.
//
// 64-bit clean: the finder is a std::uint32_t handle; no engine pointer.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace obng::engine::io::file_find_cache {

constexpr std::size_t kPathMax = 256;  // the original's buffer, 0x100 bytes at [esp+0x0C] (0x00431080)

enum class Pass : std::uint8_t {
	Keep,         // kept: a relative path of an asset kind
	Absolute,     // a drive letter (path[1] == ':')
	DotRelative,  // ".\" first
	TooLong,      // does not fit the original's buffer with its terminator
	Kind,         // not an asset kind (an INI, a text, no extension)
	Empty,        // no path or an empty one
	kCount
};

// The extension after the last dot of the last name ("" when none).
inline const char* ExtensionOf(const char* path) {
	const char* dot = nullptr;
	for (const char* p = path; *p != '\0'; ++p) {
		if (*p == '.') {
			dot = p;
		} else if (*p == '\\' || *p == '/') {
			dot = nullptr;
		}
	}
	return dot != nullptr ? dot + 1 : path + std::strlen(path);
}

// The asset kinds the game reads from Data and nothing writes while it runs.
inline bool KeptKind(const char* extension) {
	static constexpr const char* kKinds[] = {"nif", "dds", "kf", "lod", "cmp", "spt", "wav", "mp3", "ogg", "lip", "tga", "egm", "egt", "tri", "fnt", "tex", "dlodsettings"};
	char lower[16];
	std::size_t n = 0;
	for (; extension[n] != '\0'; ++n) {
		if (n + 1 >= sizeof(lower)) {
			return false;
		}
		const char ch = extension[n];
		lower[n] = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
	}
	lower[n] = '\0';
	for (const char* kind : kKinds) {
		if (std::strcmp(lower, kind) == 0) {
			return true;
		}
	}
	return false;
}

inline Pass Classify(const char* path) {
	if (path == nullptr || path[0] == '\0') {
		return Pass::Empty;
	}
	if (path[1] == ':') {
		return Pass::Absolute;
	}
	if (path[0] == '.' && path[1] == '\\') {
		return Pass::DotRelative;
	}
	if (std::strlen(path) + 1 > kPathMax) {
		return Pass::TooLong;
	}
	return KeptKind(ExtensionOf(path)) ? Pass::Keep : Pass::Kind;
}

// The key of a call: the finder, the flags, the mask and the path's bytes as given (the found path copies the
// caller's spelling, so another case is another answer).
class KeyBuffer {
public:
	bool Make(std::uint32_t finder, std::uint32_t flags, std::uint32_t mask, const char* path) {
		const std::size_t length = std::strlen(path);
		if (length + 1 > kPathMax) {
			return false;
		}
		std::memcpy(bytes_.data(), &finder, 4);
		std::memcpy(bytes_.data() + 4, &flags, 4);
		std::memcpy(bytes_.data() + 8, &mask, 4);
		std::memcpy(bytes_.data() + kHead, path, length);
		size_ = kHead + length;
		return true;
	}
	std::string_view View() const { return {bytes_.data(), size_}; }

private:
	static constexpr std::size_t kHead = 12;
	std::array<char, kHead + kPathMax> bytes_{};
	std::size_t size_ = 0;
};

// One answer: the result and, when found, the found path.
struct Answer {
	std::uint32_t result = 0;
	std::string found;

	void Keep(const char* path) { found = result != 0 && path != nullptr ? path : ""; }
	// As FindFile writes its buffer: the found path with its terminator, or the first byte zero.
	void WriteTo(char* out) const {
		if (out == nullptr) {
			return;
		}
		if (result != 0) {
			std::memcpy(out, found.c_str(), found.size() + 1);
		} else {
			out[0] = '\0';
		}
	}
	bool Same(std::uint32_t otherResult, const char* otherFound) const {
		if (otherResult != result) {
			return false;
		}
		return result == 0 || found == otherFound;
	}
};

enum class Kept : std::uint8_t { New, Already, Full };

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

// The answers by key, up to a limit (no eviction: past the limit a call is asked of the original as before).
class Table {
public:
	explicit Table(std::size_t limit = 65536) : limit_(limit) { map_.reserve(limit < 4096 ? limit : 4096); }

	const Answer* Find(std::string_view key) const {
		const auto it = map_.find(key);
		return it != map_.end() ? &it->second : nullptr;
	}
	Kept Keep(std::string_view key, const Answer& answer) {
		if (map_.find(key) != map_.end()) {
			return Kept::Already;
		}
		if (map_.size() >= limit_) {
			return Kept::Full;
		}
		map_.emplace(std::string(key), answer);
		return Kept::New;
	}
	std::size_t Size() const { return map_.size(); }

private:
	std::size_t limit_;
	std::unordered_map<std::string, Answer, KeyHash, KeyEqual> map_;
};

struct Counts {
	std::uint64_t calls = 0;
	std::uint64_t hitsFound = 0;
	std::uint64_t hitsMissing = 0;
	std::uint64_t asked = 0;  // a kept kind asked of the original (its answer kept, or the table full)
	std::uint64_t full = 0;
	std::uint64_t mismatches = 0;
	std::uint64_t passed[static_cast<int>(Pass::kCount)] = {};

	void Note(Pass pass) {
		if (pass != Pass::Keep) {
			++passed[static_cast<int>(pass)];
		}
	}
	std::uint64_t PassedAll() const {
		std::uint64_t n = 0;
		for (const std::uint64_t p : passed) {
			n += p;
		}
		return n;
	}
};

}  // namespace obng::engine::io::file_find_cache
