// FastSqrtLogic.h: the decisions over the stored high dword and the control
// word (every flow), the bytes BuildFastSqrt writes (hand-assembled, the
// rel32 arithmetic in the comments), and the generated code run on the host's
// x87 with a fake original behind the cell: which path each input takes, what
// the fast paths return against a plain FSQRT under the square root's word,
// and that the control word comes back, EDX is preserved and the x87 stack is
// balanced.
//
// The oracle - the original's own bytes - is FastSqrtOracleTest.

#include "Check.h"
#include "FastSqrtHost.h"
#include "FastSqrtLogic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace fs = obng::plugin::fast_sqrt;
using fast_sqrt_host::Outcome;

namespace {

constexpr std::uint16_t kNearest24 = 0x007F;
constexpr std::uint16_t kTruncate24 = 0x0C7F;
constexpr std::uint16_t kDown24 = 0x047F;
constexpr std::uint16_t kUp24 = 0x087F;
constexpr std::uint16_t kNearest53 = 0x027F;
constexpr std::uint16_t kNearest64 = 0x037F;

void TestDecisions() {
	Section("The stored high dword: special or negative");
	Check(!fs::ImageIsSpecialOrNegative(0x3FF00000), "1.0 is neither");
	Check(!fs::ImageIsSpecialOrNegative(0x00000000), "+0 is neither");
	Check(!fs::ImageIsSpecialOrNegative(0x00080000), "a denormal is neither");
	Check(!fs::ImageIsSpecialOrNegative(0x7FEFFFFF), "the largest finite is neither");
	Check(fs::ImageIsSpecialOrNegative(0x80000000), "-0 is negative (the original's -0 case, kept there)");
	Check(fs::ImageIsSpecialOrNegative(0xBFF00000), "-1.0 is negative");
	Check(fs::ImageIsSpecialOrNegative(0x7FF00000), "+inf is special");
	Check(fs::ImageIsSpecialOrNegative(0xFFF00000), "-inf is special and negative");
	Check(fs::ImageIsSpecialOrNegative(0x7FF80000), "a quiet NaN is special");
	Check(fs::ImageIsSpecialOrNegative(0x7FF00001), "a signalling NaN is special");

	Section("The control word: fast when every exception is masked at PC 24 or 53");
	Check(fs::ControlWordIsFast(kNearest24), "0x007F (the game, nearest)");
	Check(fs::ControlWordIsFast(kTruncate24), "0x0C7F (the game's shadow pass, truncate)");
	Check(fs::ControlWordIsFast(kDown24) && fs::ControlWordIsFast(kUp24), "down and up at PC 24");
	Check(fs::ControlWordIsFast(kNearest53), "0x027F (MSVC's default)");
	Check(fs::ControlWordIsFast(0x0E7F), "truncate at PC 53");
	Check(!fs::ControlWordIsFast(kNearest64), "0x037F (PC 64, FNINIT's word): the original");
	Check(!fs::ControlWordIsFast(0x017F), "the reserved PC encoding: the original");
	Check(!fs::ControlWordIsFast(0x005F), "PM clear: the original");
	Check(!fs::ControlWordIsFast(0x007E), "IM clear: the original");
	Check(!fs::ControlWordIsFast(0x0C5F), "truncate with PM clear: the original");
	Check(fs::ControlWordIsFast(0x1C7F), "the infinity-control bit (12) does not matter");

	Section("The square root's word: PC kept, nearest, all masked");
	Check(fs::SqrtControlWord(kNearest24) == 0x007F, "0x007F stays");
	Check(fs::SqrtControlWord(kTruncate24) == 0x007F, "0x0C7F becomes 0x007F");
	Check(fs::SqrtControlWord(kNearest53) == 0x027F, "0x027F stays");
	Check(fs::SqrtControlWord(0x0E7F) == 0x027F, "0x0E7F becomes 0x027F");
	Check(fs::SqrtControlWord(0x1C5F) == 0x007F, "bit 12 and a cleared mask are dropped");

	Section("Whether the word has to be swapped");
	Check(!fs::ControlWordNeedsSwap(kNearest24), "0x007F: no");
	Check(!fs::ControlWordNeedsSwap(kNearest53), "0x027F: no");
	Check(fs::ControlWordNeedsSwap(kTruncate24), "0x0C7F: yes");
	Check(fs::ControlWordNeedsSwap(kDown24) && fs::ControlWordNeedsSwap(kUp24), "down, up: yes");
	Check(fs::ControlWordNeedsSwap(0x1C7F), "bit 12 set: yes (the original loads it clear)");

	Section("The path");
	Check(fs::Decide(0x3FF00000, kNearest24) == fs::Path::FastPlain, "1.0 at 0x007F: plain");
	Check(fs::Decide(0x3FF00000, kTruncate24) == fs::Path::FastSwap, "1.0 at 0x0C7F: swap");
	Check(fs::Decide(0x3FF00000, kNearest53) == fs::Path::FastPlain, "1.0 at 0x027F: plain");
	Check(fs::Decide(0x3FF00000, kNearest64) == fs::Path::Original, "1.0 at 0x037F: original");
	Check(fs::Decide(0x3FF00000, 0x005F) == fs::Path::Original, "1.0 with PM clear: original");
	Check(fs::Decide(0x80000000, kNearest24) == fs::Path::Original, "-0: original");
	Check(fs::Decide(0xBFF00000, kNearest24) == fs::Path::Original, "-1.0: original");
	Check(fs::Decide(0x7FF00000, kNearest24) == fs::Path::Original, "+inf: original");
	Check(fs::Decide(0x7FF80000, kNearest24) == fs::Path::Original, "NaN: original");
	Check(fs::Decide(0x00000000, kNearest24) == fs::Path::FastPlain, "+0: plain");
	Check(fs::Decide(0x00000001, kTruncate24) == fs::Path::FastSwap, "a denormal at 0x0C7F: swap");
}

// Hand-assembled for base 0x30000000 and the cell 0x40000000. The labels:
// sqrt at +118, original at +124; a rel32 is target - (site + 6) for the
// six-byte 0F 8x forms.
constexpr UInt32 kBase = 0x30000000;
constexpr UInt32 kCell = 0x40000000;
const UInt8 kExpected[] = {
	0x83, 0xEC, 0x0C,                    // +0   sub esp, 12
	0xDD, 0x54, 0x24, 0x00,              // +3   fst qword ptr [esp]
	0x8B, 0x44, 0x24, 0x04,              // +7   mov eax, [esp+4]
	0xA9, 0x00, 0x00, 0x00, 0x80,        // +11  test eax, 0x80000000
	0x0F, 0x85, 0x66, 0x00, 0x00, 0x00,  // +16  jnz +124 (124 - 22 = 0x66)
	0x25, 0x00, 0x00, 0xF0, 0x7F,        // +22  and eax, 0x7FF00000
	0x3D, 0x00, 0x00, 0xF0, 0x7F,        // +27  cmp eax, 0x7FF00000
	0x0F, 0x84, 0x56, 0x00, 0x00, 0x00,  // +32  je +124 (124 - 38 = 0x56)
	0xD9, 0x7C, 0x24, 0x08,              // +38  fnstcw [esp+8]
	0x0F, 0xB7, 0x44, 0x24, 0x08,        // +42  movzx eax, word [esp+8]
	0x25, 0x3F, 0x01, 0x00, 0x00,        // +47  and eax, 0x13F
	0x3D, 0x3F, 0x00, 0x00, 0x00,        // +52  cmp eax, 0x3F
	0x0F, 0x85, 0x3D, 0x00, 0x00, 0x00,  // +57  jne +124 (124 - 63 = 0x3D)
	0x0F, 0xB7, 0x44, 0x24, 0x08,        // +63  movzx eax, word [esp+8]
	0x25, 0xFF, 0xFC, 0x00, 0x00,        // +68  and eax, 0xFCFF
	0x3D, 0x7F, 0x00, 0x00, 0x00,        // +73  cmp eax, 0x7F
	0x0F, 0x84, 0x22, 0x00, 0x00, 0x00,  // +78  je +118 (118 - 84 = 0x22)
	0x0F, 0xB7, 0x44, 0x24, 0x08,        // +84  movzx eax, word [esp+8]
	0x25, 0x00, 0x03, 0x00, 0x00,        // +89  and eax, 0x300
	0x0D, 0x7F, 0x00, 0x00, 0x00,        // +94  or eax, 0x7F
	0x66, 0x89, 0x44, 0x24, 0x0A,        // +99  mov [esp+10], ax
	0xD9, 0x6C, 0x24, 0x0A,              // +104 fldcw [esp+10]
	0xD9, 0xFA,                          // +108 fsqrt
	0xD9, 0x6C, 0x24, 0x08,              // +110 fldcw [esp+8]
	0x83, 0xC4, 0x0C,                    // +114 add esp, 12
	0xC3,                                // +117 ret
	0xD9, 0xFA,                          // +118 fsqrt
	0x83, 0xC4, 0x0C,                    // +120 add esp, 12
	0xC3,                                // +123 ret
	0x83, 0xC4, 0x0C,                    // +124 add esp, 12
	0xFF, 0x25, 0x00, 0x00, 0x00, 0x40,  // +127 jmp dword ptr [0x40000000]
};
static_assert(sizeof(kExpected) == fs::kCodeSize, "the pinned size is the hand-assembled one");

void TestBytes() {
	Section("The bytes");
	UInt8 buffer[256];
	std::memset(buffer, 0xCC, sizeof(buffer));
	const UInt32 size = fs::BuildFastSqrt(buffer, sizeof(buffer), kBase, kCell);
	Check(size == fs::kCodeSize, "133 bytes written");
	bool same = size == sizeof(kExpected);
	for (UInt32 i = 0; same && i < size; ++i) {
		if (buffer[i] != kExpected[i]) {
			std::printf("  byte %u is %02X, expected %02X\n", i, buffer[i], kExpected[i]);
			same = false;
		}
	}
	Check(same, "every byte as hand-assembled");
	Check(buffer[size] == 0xCC, "nothing written past the end");
	Check(fs::BuildFastSqrt(buffer, fs::kCodeSize - 1, kBase, kCell) == 0, "one byte short: nothing");
	Check(fs::BuildFastSqrt(buffer, fs::kCodeSize, kBase, kCell) == fs::kCodeSize, "exactly enough: written");
	// The same code at another base differs only in nothing: every jump is
	// relative within the code and the cell is absolute.
	UInt8 other[256];
	Check(fs::BuildFastSqrt(other, sizeof(other), 0x7F000000, kCell) == fs::kCodeSize && std::memcmp(buffer, other, fs::kCodeSize) == 0,
	      "position independent but for the cell");
	Check(fs::BuildFastSqrt(other, sizeof(other), kBase, 0x12345678) == fs::kCodeSize && other[129] == 0x78 && other[130] == 0x56 &&
	          other[131] == 0x34 && other[132] == 0x12,
	      "the cell address is the indirect jump's operand");
}

// A plain FSQRT under `word`, the reference for the fast paths' arithmetic:
// FLDCW word, FLD, FSQRT, then the status and the result.
Outcome PlainSqrt(double x, std::uint16_t word) {
	Outcome out{};
	asm volatile(
		"fnclex\n\t"
		"fldcw %[cw]\n\t"
		"fldl %[x]\n\t"
		"fsqrt\n\t"
		"fnstsw %[sw]\n\t"
		"fnstcw %[cwo]\n\t"
		"fstpt %[res]\n\t"
		"fnclex\n\t"
		"fldcw %[def]"
		: [sw] "=m"(out.statusWord), [cwo] "=m"(out.controlWord), [res] "=m"(out.result)
		: [cw] "m"(word), [x] "m"(x), [def] "m"(fast_sqrt_host::kDefaultControl)
		: "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
	out.edx = 0x5A5A5A5A;
	return out;
}

// The fake original: records that it was reached, and returns ST0 unchanged
// so the caller can see the argument come back.
//   mov byte ptr [flag], 1     C6 05 <addr32> 01
//   ret                        C3
volatile unsigned char g_originalReached = 0;

void TestExecution() {
	Section("The generated code on the host's x87");
	unsigned char* page = fast_sqrt_host::ExecutablePage(4096);
	if (page == nullptr) {
		Check(false, "an executable page for the code");
		return;
	}
	unsigned char* stub = page + 512;
	const std::uint32_t flag = reinterpret_cast<std::uint32_t>(&g_originalReached);
	const unsigned char stubBytes[] = {0xC6, 0x05, static_cast<unsigned char>(flag), static_cast<unsigned char>(flag >> 8),
	                                   static_cast<unsigned char>(flag >> 16), static_cast<unsigned char>(flag >> 24), 0x01, 0xC3};
	std::memcpy(stub, stubBytes, sizeof(stubBytes));
	std::uint32_t* cell = reinterpret_cast<std::uint32_t*>(page + 1024);
	*cell = reinterpret_cast<std::uint32_t>(stub);
	const UInt32 size = fs::BuildFastSqrt(page, 512, reinterpret_cast<UInt32>(page), reinterpret_cast<UInt32>(cell));
	Check(size == fs::kCodeSize, "built at its running address");

	struct Case {
		std::uint64_t bits;
		std::uint16_t word;
		fs::Path path;
		const char* what;
	};
	const Case cases[] = {
		{0x4000000000000000ull, kNearest24, fs::Path::FastPlain, "2.0 at 0x007F"},
		{0x4000000000000000ull, kTruncate24, fs::Path::FastSwap, "2.0 at 0x0C7F"},
		{0x4000000000000000ull, kDown24, fs::Path::FastSwap, "2.0 at 0x047F"},
		{0x4000000000000000ull, kUp24, fs::Path::FastSwap, "2.0 at 0x087F"},
		{0x4000000000000000ull, kNearest53, fs::Path::FastPlain, "2.0 at 0x027F"},
		{0x4000000000000000ull, 0x0E7F, fs::Path::FastSwap, "2.0 at 0x0E7F"},
		{0x4010000000000000ull, kNearest24, fs::Path::FastPlain, "4.0 at 0x007F (exact)"},
		{0x0000000000000001ull, kTruncate24, fs::Path::FastSwap, "the smallest denormal at 0x0C7F"},
		{0x0000000000000000ull, kNearest24, fs::Path::FastPlain, "+0"},
		{0x7FEFFFFFFFFFFFFFull, kNearest24, fs::Path::FastPlain, "the largest finite"},
		{0x4000000000000000ull, kNearest64, fs::Path::Original, "2.0 at 0x037F"},
		{0x4000000000000000ull, 0x005F, fs::Path::Original, "2.0 with PM clear"},
		{0x4000000000000000ull, 0x017F, fs::Path::Original, "2.0 at the reserved PC"},
		{0x8000000000000000ull, kNearest24, fs::Path::Original, "-0"},
		{0xC000000000000000ull, kNearest24, fs::Path::Original, "-2.0"},
		{0x7FF0000000000000ull, kNearest24, fs::Path::Original, "+inf"},
		{0xFFF0000000000000ull, kNearest24, fs::Path::Original, "-inf"},
		{0x7FF8000000000000ull, kNearest24, fs::Path::Original, "a quiet NaN"},
	};
	for (const Case& c : cases) {
		g_originalReached = 0;
		const double x = fast_sqrt_host::FromBits(c.bits);
		// An unmasked word would let the stub return with a pending exception
		// the test's own FNSTSW cannot wait on without raising; the cases with
		// one are decided on the stub's reach and the result only.
		const Outcome ours = fast_sqrt_host::Call(page, x, c.word);
		char what[160];
		if (c.path == fs::Path::Original) {
			std::snprintf(what, sizeof(what), "%s: the original is reached with the argument intact", c.what);
			// The argument's 80-bit image, through the same FLD and the stub alone.
			const Outcome plain = fast_sqrt_host::Call(stub, x, c.word);
			Check(g_originalReached != 0 && ours.SameResult(plain) && ours.controlWord == c.word && ours.edx == 0x5A5A5A5A &&
			          ours.OneValueOnStack(),
			      what);
		} else {
			const Outcome reference = PlainSqrt(x, fs::SqrtControlWord(c.word));
			std::snprintf(what, sizeof(what), "%s: the stub is not reached, ST0 is FSQRT under %04X, the word is back, EDX kept, the stack balanced",
			              c.what, fs::SqrtControlWord(c.word));
			Check(g_originalReached == 0 && ours.SameResult(reference) && ours.controlWord == c.word && ours.edx == 0x5A5A5A5A &&
			          ours.OneValueOnStack() && ours.Flags() == reference.Flags(),
			      what);
			if (!ours.SameResult(reference) || ours.Flags() != reference.Flags()) {
				fast_sqrt_host::PrintOutcome("ours", c.bits, c.word, ours);
				fast_sqrt_host::PrintOutcome("ref ", c.bits, c.word, reference);
			}
		}
	}

	Section("Precision control reaches FSQRT on this hardware (what the game's callers get)");
	const Outcome pc24 = PlainSqrt(2.0, kNearest24);
	const Outcome pc53 = PlainSqrt(2.0, kNearest53);
	const Outcome pc64 = PlainSqrt(2.0, kNearest64);
	std::printf("  sqrt(2): PC 24 %016llx, PC 53 %016llx, PC 64 %016llx\n", static_cast<unsigned long long>(pc24.Significand()),
	            static_cast<unsigned long long>(pc53.Significand()), static_cast<unsigned long long>(pc64.Significand()));
	// sqrt(2) = 0xB504F333F9DE6484...: the 25th bit is 0, so 24 bits round down to 0xB504F3.
	Check(pc24.Significand() == 0xB504F30000000000ull, "PC 24 rounds the register to 24 bits");
	Check(pc53.Significand() == 0xB504F333F9DE6800ull, "PC 53 rounds it to 53 bits");
	Check(pc64.Significand() == 0xB504F333F9DE6484ull, "PC 64 keeps 64 bits");

	Section("Random inputs against the plain FSQRT under the square root's word");
	fast_sqrt_host::Random random;
	const std::uint16_t words[] = {kNearest24, kTruncate24, kDown24, kUp24, kNearest53, 0x0E7F};
	unsigned fast = 0, original = 0, wrong = 0;
	for (unsigned i = 0; i < 200000; ++i) {
		const std::uint64_t bits = random.DoubleBits();
		const std::uint16_t word = words[random.Below(6)];
		const double x = fast_sqrt_host::FromBits(bits);
		g_originalReached = 0;
		const Outcome ours = fast_sqrt_host::Call(page, x, word);
		const fs::Path path = fs::Decide(static_cast<UInt32>(bits >> 32), word);
		if (path == fs::Path::Original) {
			++original;
			if (g_originalReached == 0) {
				++wrong;
			}
			continue;
		}
		++fast;
		const Outcome reference = PlainSqrt(x, fs::SqrtControlWord(word));
		if (g_originalReached != 0 || !ours.SameResult(reference) || ours.controlWord != word || ours.Flags() != reference.Flags() ||
		    ours.edx != 0x5A5A5A5A || !ours.OneValueOnStack()) {
			if (wrong < 5) {
				fast_sqrt_host::PrintOutcome("ours", bits, word, ours);
				fast_sqrt_host::PrintOutcome("ref ", bits, word, reference);
			}
			++wrong;
		}
	}
	std::printf("  %u fast, %u to the original, %u wrong\n", fast, original, wrong);
	Check(wrong == 0, "every input: the decided path, the plain root, the word back, the flags, EDX, the stack");
	Check(fast > 100000 && original > 20000, "both paths exercised");
	munmap(page, 4096);
}

}  // namespace

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	TestDecisions();
	TestBytes();
	TestExecution();
	return Failures();
}
