#pragma once
// Oblivion.exe's own bytes, mapped at their image addresses in this 32-bit
// test process, so an original function can run on the host's x87 against a
// rebuild (the pattern of plugin/tests/FastSqrtOracleTest.cpp, which maps
// the CRT's tail; this maps the whole of .text, .rdata and .data). One stub
// replaces the CRT's __set_errno_from_matherr 0x00992EC1 (it would reach the
// per-thread data through imports this process has not): the stub records
// the matherr type instead. The file: $OBNG_ORACLE_EXE, else Steam's default
// under $HOME; it must be the reference build (7 898 624 bytes). Without it
// Map() returns false and the test says so and returns 77 (skipped).
#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace obng::tests::exe_image {

inline constexpr std::size_t kExpectedSize = 7898624;
inline constexpr std::uint32_t kTextStart = 0x00401000, kTextSize = 0x00627000;   // .text 0x00401000..0x00A28000
inline constexpr std::uint32_t kRdataStart = 0x00A28000, kRdataSize = 0x000DA000;  // .rdata
inline constexpr std::uint32_t kDataStart = 0x00B02000, kDataSize = 0x000AA000;    // .data with its zero tail

inline volatile std::uint32_t g_errnoType = 0;
inline volatile std::uint32_t g_errnoCalls = 0;

struct Section {
	std::uint32_t virtualAddress;
	std::uint32_t virtualSize;
	std::uint32_t rawSize;
	std::uint32_t rawOffset;
};

inline bool MapRange(const std::vector<unsigned char>& file, const std::vector<Section>& sections, std::uint32_t address,
                     std::uint32_t size) {
	void* wanted = reinterpret_cast<void*>(address);
	void* got = mmap(wanted, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (got != wanted) {
		if (got != MAP_FAILED) {
			munmap(got, size);
		}
		std::printf("  could not map %08X+%X at its address\n", address, size);
		return false;
	}
	for (const Section& s : sections) {
		const std::uint32_t start = s.virtualAddress > address ? s.virtualAddress : address;
		const std::uint32_t sectionEnd = s.virtualAddress + (s.rawSize < s.virtualSize ? s.rawSize : s.virtualSize);
		const std::uint32_t end = sectionEnd < address + size ? sectionEnd : address + size;
		if (start < end) {
			std::memcpy(reinterpret_cast<void*>(start), file.data() + s.rawOffset + (start - s.virtualAddress), end - start);
		}
	}
	return true;
}

// mov eax,[esp+4]; mov [g_errnoType],eax; inc dword [g_errnoCalls]; ret
inline void StubErrnoSetter() {
	unsigned char* at = reinterpret_cast<unsigned char*>(0x00992EC1);
	const std::uint32_t type = reinterpret_cast<std::uint32_t>(&g_errnoType);
	const std::uint32_t calls = reinterpret_cast<std::uint32_t>(&g_errnoCalls);
	const unsigned char stub[] = {0x8B, 0x44, 0x24, 0x04, 0xA3, static_cast<unsigned char>(type), static_cast<unsigned char>(type >> 8),
	                              static_cast<unsigned char>(type >> 16), static_cast<unsigned char>(type >> 24), 0xFF, 0x05,
	                              static_cast<unsigned char>(calls), static_cast<unsigned char>(calls >> 8),
	                              static_cast<unsigned char>(calls >> 16), static_cast<unsigned char>(calls >> 24), 0xC3};
	std::memcpy(at, stub, sizeof(stub));
}

// Maps the image; *where names the file tried. The caller checks the bytes
// it relies on afterwards.
inline bool Map(std::string* where) {
	std::string path;
	if (const char* env = std::getenv("OBNG_ORACLE_EXE")) {
		path = env;
	} else if (const char* home = std::getenv("HOME")) {
		path = std::string(home) + "/.local/share/Steam/steamapps/common/Oblivion/Oblivion.exe";
	}
	*where = path;
	FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
	if (f == nullptr) {
		return false;
	}
	std::vector<unsigned char> file;
	unsigned char chunk[65536];
	for (std::size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) != 0;) {
		file.insert(file.end(), chunk, chunk + n);
	}
	std::fclose(f);
	if (file.size() != kExpectedSize) {
		std::printf("  %s is %zu bytes, the reference build has %zu\n", path.c_str(), file.size(), kExpectedSize);
		return false;
	}
	std::uint32_t pe;
	std::memcpy(&pe, file.data() + 0x3C, 4);
	std::uint16_t count;
	std::uint16_t optionalSize;
	std::memcpy(&count, file.data() + pe + 6, 2);
	std::memcpy(&optionalSize, file.data() + pe + 20, 2);
	std::uint32_t imageBase;
	std::memcpy(&imageBase, file.data() + pe + 24 + 28, 4);
	std::vector<Section> sections;
	for (unsigned i = 0; i < count; ++i) {
		const unsigned char* row = file.data() + pe + 24 + optionalSize + 40 * i;
		Section s{};
		std::memcpy(&s.virtualSize, row + 8, 4);
		std::memcpy(&s.virtualAddress, row + 12, 4);
		std::memcpy(&s.rawSize, row + 16, 4);
		std::memcpy(&s.rawOffset, row + 20, 4);
		s.virtualAddress += imageBase;
		sections.push_back(s);
	}
	if (!MapRange(file, sections, kTextStart, kTextSize) || !MapRange(file, sections, kRdataStart, kRdataSize) ||
	    !MapRange(file, sections, kDataStart, kDataSize)) {
		return false;
	}
	StubErrnoSetter();
	return mprotect(reinterpret_cast<void*>(kTextStart), kTextSize, PROT_READ | PROT_EXEC) == 0 &&
	       mprotect(reinterpret_cast<void*>(kRdataStart), kRdataSize, PROT_READ) == 0 &&
	       mprotect(reinterpret_cast<void*>(kDataStart), kDataSize, PROT_READ | PROT_WRITE) == 0;
}

inline bool BytesAre(std::uint32_t address, const unsigned char* expected, std::size_t count) {
	return std::memcmp(reinterpret_cast<const void*>(address), expected, count) == 0;
}

}  // namespace obng::tests::exe_image
