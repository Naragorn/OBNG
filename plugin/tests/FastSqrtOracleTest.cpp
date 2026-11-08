// The generated square-root dispatch against the original: Oblivion.exe's own
// bytes, mapped at their image addresses in this 32-bit process and run on
// the host's x87 - the CRT's _CIsqrt 0x00982C30 with everything it reaches on
// its paths (0x00982C4D, 0x00991Bxx, 0x00991AB7, __87except 0x0099E2B9,
// __handle_exc, _set_statfp, __ctrlfp, __set_errno_from_matherr and the
// security cookie check, all inside .text 0x00980000..0x00A28000; the
// constants and the name in .data 0x00B30000.., the never-written flag
// 0x00BA9DDC in .data's zero tail, the double 1.0 at 0x00AA5090 in .rdata).
// The replacement's cell points straight at 0x00982C30, as in the game it
// points at the trampoline that continues there.
//
// One function of the mapped CRT is replaced by a stub: __set_errno_from_matherr
// 0x00992EC1, which __87except calls on every path with the matherr type and
// which, for DOMAIN (1), OVERFLOW (2) and SING (3), writes errno through the
// CRT's per-thread data (__getptd through the FLS imports - nothing this
// process has); for INEXACT (8) it returns without a write (read in its
// decompilate). The stub records the type, so the test also sees that the
// original and the dispatch make the same errno writes: the DOMAIN path (a
// negative argument, a NaN) goes through the original in both.
//
// Compared per input and control word: the 80-bit result, the control word
// after the call, the status word's exception flags and stack top, and the
// errno writes the stub below sees. The condition codes C0..C3 are reported,
// not asserted: the original's exception path ends in a deliberately inexact
// FSTP (_set_statfp(0x20): FLDPI; FSTP qword) that may leave C1 differently,
// and no caller of a square root reads them (the next FCOM or FUCOM
// overwrites them). EDX is not compared either: the original clobbers it
// (0x00982C4D pushes EDX and then FSTCW overwrites the low word of that very
// slot before the POP; measured here as 0x5A5A027F or 0x007F007F for an
// entry value of 0x5A5A5A5A), the dispatch preserves it - a superset.
//
// The executable: $OBNG_ORACLE_EXE, else Steam's default under $HOME. The
// file must be the reference build (md5 cdd2f0c5eff198d4f26b7b5b54ce4930, the
// Steam GOTY 1.2.0.416; its size 7 898 624 bytes and the entry bytes at
// 0x00982C30 are checked here). Without it the test says so and returns 77
// (skipped), as GrassFadePatchPackageTest does without the shader package.
//
// Control words with an exception unmasked are not run: the original's own
// FLDCW would raise the pending exception into this process. The dispatch
// hands those words to the original (FastSqrtLogicTest), so nothing changes
// for them.

#include "Check.h"
#include "FastSqrtHost.h"
#include "FastSqrtLogic.h"

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace fs = obng::plugin::fast_sqrt;
using fast_sqrt_host::Outcome;

namespace {

constexpr std::size_t kExpectedSize = 7898624;

struct Section_ {
	std::uint32_t virtualAddress;
	std::uint32_t virtualSize;
	std::uint32_t rawSize;
	std::uint32_t rawOffset;
};

// What the stub for __set_errno_from_matherr records.
volatile std::uint32_t g_errnoType = 0;
volatile std::uint32_t g_errnoCalls = 0;

// Maps [address, address + size) at its own address, filled from the file
// where the section has bytes and zero beyond; false when the system refuses
// the fixed mapping. The protection is applied by Protect afterwards.
bool MapRange(const std::vector<unsigned char>& file, const std::vector<Section_>& sections, std::uint32_t address,
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
	for (const Section_& s : sections) {
		const std::uint32_t start = s.virtualAddress > address ? s.virtualAddress : address;
		const std::uint32_t sectionEnd = s.virtualAddress + (s.rawSize < s.virtualSize ? s.rawSize : s.virtualSize);
		const std::uint32_t end = sectionEnd < address + size ? sectionEnd : address + size;
		if (start < end) {
			std::memcpy(reinterpret_cast<void*>(start), file.data() + s.rawOffset + (start - s.virtualAddress), end - start);
		}
	}
	return true;
}

bool Protect(std::uint32_t address, std::uint32_t size, int protection) {
	return mprotect(reinterpret_cast<void*>(address), size, protection) == 0;
}

// The stub at 0x00992EC1: mov eax, [esp+4]; mov [g_errnoType], eax;
// inc dword ptr [g_errnoCalls]; ret.
void StubErrnoSetter() {
	unsigned char* at = reinterpret_cast<unsigned char*>(0x00992EC1);
	const std::uint32_t type = reinterpret_cast<std::uint32_t>(&g_errnoType);
	const std::uint32_t calls = reinterpret_cast<std::uint32_t>(&g_errnoCalls);
	const unsigned char stub[] = {0x8B, 0x44, 0x24, 0x04,
	                              0xA3, static_cast<unsigned char>(type), static_cast<unsigned char>(type >> 8),
	                              static_cast<unsigned char>(type >> 16), static_cast<unsigned char>(type >> 24),
	                              0xFF, 0x05, static_cast<unsigned char>(calls), static_cast<unsigned char>(calls >> 8),
	                              static_cast<unsigned char>(calls >> 16), static_cast<unsigned char>(calls >> 24),
	                              0xC3};
	std::memcpy(at, stub, sizeof(stub));
}

bool LoadOriginal(std::string* where) {
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
	std::vector<Section_> sections;
	for (unsigned i = 0; i < count; ++i) {
		const unsigned char* row = file.data() + pe + 24 + optionalSize + 40 * i;
		Section_ s{};
		std::memcpy(&s.virtualSize, row + 8, 4);
		std::memcpy(&s.virtualAddress, row + 12, 4);
		std::memcpy(&s.rawSize, row + 16, 4);
		std::memcpy(&s.rawOffset, row + 20, 4);
		s.virtualAddress += imageBase;
		sections.push_back(s);
	}
	// .text's CRT tail (code), .rdata (constants) and .data (the name, the
	// cookie, the x87 constants, the flag).
	if (!MapRange(file, sections, 0x00980000, 0x000A8000) || !MapRange(file, sections, 0x00A28000, 0x000DA000) ||
	    !MapRange(file, sections, 0x00B02000, 0x000AA000)) {
		return false;
	}
	StubErrnoSetter();
	if (!Protect(0x00980000, 0x000A8000, PROT_READ | PROT_EXEC) || !Protect(0x00A28000, 0x000DA000, PROT_READ) ||
	    !Protect(0x00B02000, 0x000AA000, PROT_READ | PROT_WRITE)) {
		return false;
	}
	const unsigned char* entry = reinterpret_cast<const unsigned char*>(fs::kCIsqrtAddress);
	if (std::memcmp(entry, fs::kCIsqrtEntryBytes, sizeof(fs::kCIsqrtEntryBytes)) != 0) {
		std::printf("  the bytes at %08X are not _CIsqrt's\n", fs::kCIsqrtAddress);
		return false;
	}
	std::uint32_t flag;
	std::memcpy(&flag, reinterpret_cast<const void*>(0x00BA9DDC), 4);
	return flag == 0;
}

const void* kOriginal = reinterpret_cast<const void*>(fs::kCIsqrtAddress);

struct Tally {
	unsigned calls = 0;
	unsigned resultDiffers = 0;
	unsigned wordDiffers = 0;
	unsigned flagsDiffer = 0;
	unsigned topDiffers = 0;
	unsigned edxDiffers = 0;
	unsigned conditionCodesDiffer = 0;
	unsigned errnoDiffers = 0;
	unsigned errnoWrites = 0;
	unsigned fastCalls = 0;
	unsigned edxLost = 0;  // a fast-path call that did not keep EDX
	unsigned inexact = 0;
	unsigned exact = 0;

	void Add(std::uint64_t bits, std::uint16_t word, const Outcome& original, std::uint32_t originalErrno,
	         std::uint32_t originalErrnoCalls, const Outcome& ours, std::uint32_t oursErrno, std::uint32_t oursErrnoCalls) {
		++calls;
		bool differs = false;
		// What the real __set_errno_from_matherr writes: EDOM for 1, ERANGE
		// for 2 and 3, nothing otherwise - so a type 8 call is no write.
		const auto writes = [](std::uint32_t type, std::uint32_t count) { return count != 0 && type >= 1 && type <= 3 ? type : 0u; };
		const std::uint32_t originalWrite = writes(originalErrno, originalErrnoCalls);
		if (originalWrite != 0) {
			++errnoWrites;
		}
		if (originalWrite != writes(oursErrno, oursErrnoCalls)) {
			++errnoDiffers;
			differs = true;
		}
		if (fs::Decide(static_cast<UInt32>(bits >> 32), word) != fs::Path::Original) {
			++fastCalls;
			if (ours.edx != 0x5A5A5A5A) {
				++edxLost;
				differs = true;
			}
		}
		if (!ours.SameResult(original)) {
			++resultDiffers;
			differs = true;
		}
		if (ours.controlWord != original.controlWord) {
			++wordDiffers;
			differs = true;
		}
		if (ours.Flags() != original.Flags()) {
			++flagsDiffer;
			differs = true;
		}
		if (ours.Top() != original.Top()) {
			++topDiffers;
			differs = true;
		}
		if (ours.edx != original.edx) {
			++edxDiffers;  // reported: the original clobbers EDX
		}
		if (ours.ConditionCodes() != original.ConditionCodes()) {
			++conditionCodesDiffer;
		}
		if (original.Flags() & 0x20) {
			++inexact;
		} else {
			++exact;
		}
		if (differs && resultDiffers + wordDiffers + flagsDiffer + topDiffers + errnoDiffers <= 5) {
			fast_sqrt_host::PrintOutcome("original", bits, word, original);
			fast_sqrt_host::PrintOutcome("ours    ", bits, word, ours);
		}
	}
	bool Clean() const {
		return resultDiffers == 0 && wordDiffers == 0 && flagsDiffer == 0 && topDiffers == 0 && errnoDiffers == 0 && edxLost == 0;
	}
	void Print(const char* label) const {
		std::printf("  %s: %u calls (%u fast, %u inexact, %u exact, %u errno writes) - result %u, word %u, flags %u, top %u, errno %u differ; "
		            "edx lost on %u fast calls, clobbered by the original on %u; condition codes differ %u\n",
		            label, calls, fastCalls, inexact, exact, errnoWrites, resultDiffers, wordDiffers, flagsDiffer, topDiffers, errnoDiffers,
		            edxLost, edxDiffers, conditionCodesDiffer);
	}
};

}  // namespace

// The original under `word`, with the errno stub's record taken.
Outcome CallOriginal(double x, std::uint16_t word, std::uint32_t* errnoType, std::uint32_t* errnoCalls) {
	g_errnoType = 0;
	g_errnoCalls = 0;
	const Outcome out = fast_sqrt_host::Call(kOriginal, x, word);
	*errnoType = g_errnoType;
	*errnoCalls = g_errnoCalls;
	return out;
}

Outcome CallDispatch(const void* code, double x, std::uint16_t word, std::uint32_t* errnoType, std::uint32_t* errnoCalls) {
	g_errnoType = 0;
	g_errnoCalls = 0;
	const Outcome out = fast_sqrt_host::Call(code, x, word);
	*errnoType = g_errnoType;
	*errnoCalls = g_errnoCalls;
	return out;
}

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0);  // a crash in the original's bytes keeps what was printed
	Section("The original's bytes");
	std::string where;
	if (!LoadOriginal(&where)) {
		std::printf("  no reference Oblivion.exe at %s (OBNG_ORACLE_EXE) - skipped\n", where.empty() ? "(no path)" : where.c_str());
		return 77;
	}
	Check(true, "mapped: .text 0x00980000.., .rdata, .data; _CIsqrt's entry bytes and the zero flag 0x00BA9DDC in place; the errno setter stubbed");

	unsigned char* page = fast_sqrt_host::ExecutablePage(4096);
	if (page == nullptr) {
		Check(false, "an executable page");
		return Failures();
	}
	std::uint32_t* cell = reinterpret_cast<std::uint32_t*>(page + 1024);
	*cell = fs::kCIsqrtAddress;
	Check(fs::BuildFastSqrt(page, 512, reinterpret_cast<UInt32>(page), reinterpret_cast<UInt32>(cell)) == fs::kCodeSize, "the dispatch built, its cell on the original");

	Section("The original alone: what the game gets today");
	{
		const Outcome two24 = fast_sqrt_host::Call(kOriginal, 2.0, 0x007F);
		const Outcome two24t = fast_sqrt_host::Call(kOriginal, 2.0, 0x0C7F);
		const Outcome two53 = fast_sqrt_host::Call(kOriginal, 2.0, 0x027F);
		const Outcome four24 = fast_sqrt_host::Call(kOriginal, 4.0, 0x007F);
		fast_sqrt_host::PrintOutcome("sqrt(2) 0x007F", 0x4000000000000000ull, 0x007F, two24);
		fast_sqrt_host::PrintOutcome("sqrt(2) 0x0C7F", 0x4000000000000000ull, 0x0C7F, two24t);
		fast_sqrt_host::PrintOutcome("sqrt(2) 0x027F", 0x4000000000000000ull, 0x027F, two53);
		Check(two24.Significand() == 0xB504F30000000000ull && two24.SignExponent() == 0x3FFF, "sqrt(2) at 0x007F: 24 significant bits (0xB504F333F9.. rounds down)");
		Check(two24t.SameResult(two24) && two24t.controlWord == 0x0C7F, "sqrt(2) at 0x0C7F: the same root (nearest, not truncate), the word back");
		Check(two53.Significand() == 0xB504F333F9DE6800ull, "sqrt(2) at 0x027F: 53 significant bits");
		Check((two24.Flags() & 0x20) != 0 && (four24.Flags() & 0x20) == 0, "PE set for an inexact root, clear for an exact one");
		Check(two24.edx == 0x007F007F && two53.edx == 0x5A5A027F, "EDX clobbered by the original: the pushed slot's low word is overwritten by FSTCW");
		std::uint32_t type = 0, calls = 0;
		const Outcome negative = CallOriginal(-2.0, 0x007F, &type, &calls);
		fast_sqrt_host::PrintOutcome("sqrt(-2) 0x007F", 0xC000000000000000ull, 0x007F, negative);
		Check(calls == 1 && type == 1 && negative.SignExponent() == 0xFFFF && negative.Significand() == 0xC000000000000000ull,
		      "sqrt(-2): the indefinite NaN and one DOMAIN (1) errno report");
		CallOriginal(2.0, 0x007F, &type, &calls);
		Check(calls == 1 && type == 8, "sqrt(2): the errno setter is reached with INEXACT (8), which writes nothing");
		CallOriginal(2.0, 0x027F, &type, &calls);
		Check(calls == 0, "sqrt(2) at 0x027F: the exception path is not taken at all");
	}

	Section("Random inputs, six control words, original against the dispatch");
	const std::uint16_t words[] = {0x007F, 0x0C7F, 0x047F, 0x087F, 0x027F, 0x037F};
	const char* names[] = {"0x007F nearest 24", "0x0C7F truncate 24", "0x047F down 24", "0x087F up 24", "0x027F nearest 53", "0x037F nearest 64"};
	fast_sqrt_host::Random random;
	bool clean = true;
	unsigned conditionCodes = 0;
	for (unsigned w = 0; w < 6; ++w) {
		Tally tally;
		for (unsigned i = 0; i < 300000; ++i) {
			const std::uint64_t bits = random.DoubleBits();
			const double x = fast_sqrt_host::FromBits(bits);
			std::uint32_t originalType = 0, originalCalls = 0, oursType = 0, oursCalls = 0;
			const Outcome original = CallOriginal(x, words[w], &originalType, &originalCalls);
			const Outcome ours = CallDispatch(page, x, words[w], &oursType, &oursCalls);
			tally.Add(bits, words[w], original, originalType, originalCalls, ours, oursType, oursCalls);
		}
		tally.Print(names[w]);
		clean = clean && tally.Clean();
		conditionCodes += tally.conditionCodesDiffer;
	}
	Check(clean, "1.8 million calls: result, control word, exception flags, stack top and errno writes equal on every one; EDX kept by the dispatch");
	std::printf("  condition codes differed on %u calls (reported, not asserted - see the header)\n", conditionCodes);

	Section("Register arguments beyond the double image");
	{
		// 2^-1100 in the register stores as +0.0: the original classifies it
		// by the image (finite, positive) and takes the root of the register
		// value; so does the dispatch.
		const unsigned char tiny[10] = {0, 0, 0, 0, 0, 0, 0, 0x80, static_cast<unsigned char>((0x3FFF - 1100) & 0xFF), static_cast<unsigned char>((0x3FFF - 1100) >> 8)};
		const Outcome original = fast_sqrt_host::CallExtended(kOriginal, tiny, 0x007F);
		const Outcome ours = fast_sqrt_host::CallExtended(page, tiny, 0x007F);
		fast_sqrt_host::PrintOutcome("original 2^-1100", 0, 0x007F, original);
		Check(ours.SameResult(original) && ours.controlWord == original.controlWord && ours.Flags() == original.Flags(),
		      "2^-1100 in the register: the same root (2^-550) and flags");
		// 2^1100 stores as +inf: both hand it to the special-case code, which
		// takes the root of the register value.
		const unsigned char huge[10] = {0, 0, 0, 0, 0, 0, 0, 0x80, static_cast<unsigned char>((0x3FFF + 1100) & 0xFF), static_cast<unsigned char>((0x3FFF + 1100) >> 8)};
		const Outcome originalHuge = fast_sqrt_host::CallExtended(kOriginal, huge, 0x007F);
		const Outcome oursHuge = fast_sqrt_host::CallExtended(page, huge, 0x007F);
		fast_sqrt_host::PrintOutcome("original 2^1100", 0, 0x007F, originalHuge);
		Check(oursHuge.SameResult(originalHuge) && oursHuge.controlWord == originalHuge.controlWord && oursHuge.Flags() == originalHuge.Flags(),
		      "2^1100 in the register: through the original, the same outcome");
	}
	return Failures();
}
