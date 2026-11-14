#pragma once

// [Probe] CodePatches (CodePatchProbe.cpp): where the running game's code
// differs from Oblivion.exe on the disk - every place another plugin (or
// OBNG) has patched, with where a patched jump or call leads. A route that
// replaces a whole function runs past whatever another plugin patched inside
// it; this says what there is (found for the voice player 0x006A9E00, where
// lines without a voice file were not taken by the route, 2026-11-14).
// Free of Windows so every flow has a test (plugin/tests/CodePatchLogicTest.cpp).

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace obng::plugin::code_patch {

// ---- The executable's sections ------------------------------------------------------

struct Section {
	std::uint32_t address = 0;     // in memory: the image base plus the section's RVA
	std::uint32_t size = 0;        // bytes the file holds for it (never more than its size in memory)
	std::uint32_t fileOffset = 0;
	bool code = false;             // IMAGE_SCN_CNT_CODE or IMAGE_SCN_MEM_EXECUTE
};

constexpr std::uint32_t kMaxSections = 16;

inline std::uint16_t Word(const std::uint8_t* p) {
	return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
inline std::uint32_t Dword(const std::uint8_t* p) {
	return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
	       (static_cast<std::uint32_t>(p[3]) << 24);
}

// The sections of a 32-bit PE file's headers; the count, 0 when the bytes are
// not such a file. A section whose bytes would lie past `fileSize` is cut.
inline std::uint32_t ParseSections(const std::uint8_t* file, std::size_t fileSize, Section out[kMaxSections]) {
	if (file == nullptr || fileSize < 0x40 || Word(file) != 0x5A4D) {
		return 0;
	}
	const std::uint32_t pe = Dword(file + 0x3C);
	if (pe > fileSize || fileSize - pe < 0x18 + 0x60 || Dword(file + pe) != 0x00004550) {
		return 0;
	}
	const std::uint32_t sections = Word(file + pe + 6);
	const std::uint32_t optionalSize = Word(file + pe + 0x14);
	const std::uint8_t* optional = file + pe + 0x18;
	if (Word(optional) != 0x010B) {  // PE32
		return 0;
	}
	const std::uint32_t imageBase = Dword(optional + 0x1C);
	const std::size_t table = static_cast<std::size_t>(pe) + 0x18 + optionalSize;
	std::uint32_t count = 0;
	for (std::uint32_t i = 0; i < sections && count < kMaxSections; ++i) {
		const std::size_t at = table + static_cast<std::size_t>(i) * 40;
		if (at + 40 > fileSize) {
			break;
		}
		const std::uint8_t* h = file + at;
		const std::uint32_t virtualSize = Dword(h + 8);
		const std::uint32_t rawSize = Dword(h + 16);
		const std::uint32_t rawOffset = Dword(h + 20);
		const std::uint32_t flags = Dword(h + 36);
		Section s;
		s.address = imageBase + Dword(h + 12);
		s.fileOffset = rawOffset;
		s.size = virtualSize != 0 && virtualSize < rawSize ? virtualSize : rawSize;
		if (rawOffset >= fileSize) {
			s.size = 0;
		} else if (s.size > fileSize - rawOffset) {
			s.size = static_cast<std::uint32_t>(fileSize - rawOffset);
		}
		s.code = (flags & 0x20000020u) != 0;
		out[count++] = s;
	}
	return count;
}

// ---- The differing runs -------------------------------------------------------------

// Every run of bytes that differ, `sink(offset, length)`; two runs with fewer
// than `gap` equal bytes between them are one (the bytes of a patched
// instruction that happen to be the same). Returns the runs.
template <class Sink>
std::uint32_t DiffRuns(const std::uint8_t* file, const std::uint8_t* memory, std::uint32_t size, std::uint32_t gap, Sink&& sink) {
	std::uint32_t runs = 0;
	std::uint32_t i = 0;
	while (i < size) {
		if (file[i] == memory[i]) {
			++i;
			continue;
		}
		const std::uint32_t start = i;
		std::uint32_t end = i + 1;  // one past the last differing byte
		std::uint32_t j = end;
		while (j < size && j - end < gap) {
			if (file[j] != memory[j]) {
				end = j + 1;
			}
			++j;
		}
		sink(start, end - start);
		++runs;
		i = end;
	}
	return runs;
}

// ---- Where a patch leads ------------------------------------------------------------

enum class Branch : std::uint8_t { kNone, kCall, kJump };

// A patched relative call (E8) or jump (E9) at the run's start: its target.
constexpr Branch BranchAt(const std::uint8_t* bytes, std::uint32_t length, std::uint32_t address, std::uint32_t* target) {
	if (length < 5 || (bytes[0] != 0xE8 && bytes[0] != 0xE9)) {
		return Branch::kNone;
	}
	const std::uint32_t rel = static_cast<std::uint32_t>(bytes[1]) | (static_cast<std::uint32_t>(bytes[2]) << 8) |
	                          (static_cast<std::uint32_t>(bytes[3]) << 16) | (static_cast<std::uint32_t>(bytes[4]) << 24);
	*target = address + 5 + rel;
	return bytes[0] == 0xE8 ? Branch::kCall : Branch::kJump;
}

// A run may begin inside the instruction (a call whose opcode the file has
// too: only the four bytes of the displacement differ). The branch whose
// displacement the run covers, looking one byte back.
constexpr Branch BranchAround(const std::uint8_t* memory, std::uint32_t offset, std::uint32_t length, std::uint32_t base, std::uint32_t* target,
                              std::uint32_t* at) {
	if (const Branch b = BranchAt(memory + offset, length, base + offset, target); b != Branch::kNone) {
		*at = base + offset;
		return b;
	}
	if (offset >= 1 && length <= 4) {
		if (const Branch b = BranchAt(memory + offset - 1, 5, base + offset - 1, target); b != Branch::kNone) {
			*at = base + offset - 1;
			return b;
		}
	}
	return Branch::kNone;
}

// ---- One line a run -----------------------------------------------------------------

constexpr std::uint32_t kBytesShown = 8;

// "Probe: code patch at 0x006A9E57, 5 bytes: E8 12 34 56 78 (the file: FF 15 50 7E B2) - a call to 0x10001234 in Elys_USV.dll"
inline int FormatRun(std::uint32_t address, std::uint32_t length, const std::uint8_t* memory, const std::uint8_t* file, Branch branch,
                     std::uint32_t branchAt, std::uint32_t target, const char* module, char* out, std::size_t capacity) {
	int at = std::snprintf(out, capacity, "Probe: code patch at 0x%08X, %u bytes:", address, length);
	auto text = [&](const char* format, auto... values) {
		if (at >= 0 && static_cast<std::size_t>(at) < capacity) {
			const int n = std::snprintf(out + at, capacity - static_cast<std::size_t>(at), format, values...);
			at = n < 0 ? -1 : at + n;
		}
	};
	const std::uint32_t shown = length < kBytesShown ? length : kBytesShown;
	for (std::uint32_t i = 0; i < shown; ++i) {
		text(" %02X", memory[i]);
	}
	text("%s (the file:", length > shown ? " .." : "");
	for (std::uint32_t i = 0; i < shown; ++i) {
		text(" %02X", file[i]);
	}
	text("%s)", length > shown ? " .." : "");
	if (branch != Branch::kNone) {
		text(" - a %s", branch == Branch::kCall ? "call" : "jump");
		if (branchAt != address) {
			text(" at 0x%08X", branchAt);
		}
		text(" to 0x%08X in %s", target, module != nullptr && module[0] != '\0' ? module : "no module (allocated memory)");
	}
	return at;
}

// ---- The counts by module -----------------------------------------------------------

constexpr std::uint32_t kModules = 24;
constexpr std::size_t kModuleName = 40;

struct ModuleCounts {
	struct Row {
		char name[kModuleName] = {};
		std::uint32_t runs = 0;
	};
	Row rows[kModules];
	std::uint32_t count = 0;
	std::uint32_t others = 0;  // runs of modules beyond the table

	void Add(const char* name) {
		for (std::uint32_t i = 0; i < count; ++i) {
			if (Same(rows[i].name, name)) {
				++rows[i].runs;
				return;
			}
		}
		if (count == kModules) {
			++others;
			return;
		}
		Row& r = rows[count++];
		std::snprintf(r.name, sizeof(r.name), "%s", name);
		r.runs = 1;
	}

private:
	static bool Same(const char* kept, const char* name) {
		std::size_t i = 0;
		for (; i + 1 < kModuleName && name[i] != '\0'; ++i) {
			if (kept[i] != name[i]) {
				return false;
			}
		}
		return kept[i] == '\0';
	}
};

}  // namespace obng::plugin::code_patch
