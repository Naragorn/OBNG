// Tests of the rebuild of 0x0047D150 (proposed TimeInfo::TimeInfo): it
// initialises a 0x18-byte TimeInfo in place - byte 0 at +0x00, +0.0f at +0x04,
// +0x08 and +0x0C, integer 0 at +0x10, the stack argument (a GetTickCount
// value at its only call site, derived) at +0x14 - and leaves +0x01..+0x03
// alone. Evidence: re/functions/0x0047D150.md, re/export/time-info/0x0047D150/.
//
// The body has one flow (read: one block 0x0047D150..0x0047D16C, no
// conditional instruction, no jump table):
//   F1  initialise and return - always.
// The test flows re/functions/0x0047D150.md derives for F1:
//   F1a tick 0                -> +0x14 == 0, every written field zero;
//   F1b tick 0xFFFFFFFF       -> stored unchanged (plain MOV at 0x0047D169);
//   F1c sentinel-filled block -> +0x01..+0x03 keep the sentinel (no store there);
//   F1d floats as bit patterns, so a rebuild writing -0.0f fails.
// There is no refusal or fallback path: the body tests nothing (read). A null
// `this` faults at 0x0047D156 in the original; its only caller skips the call
// when FormHeap_Allocate returned null (JZ at 0x005AD307, read in the caller's
// export), so the rebuild has no guard either and the tests do not call it
// with null.
//
// The calling convention flow - ECX = this, one pushed argument popped by the
// callee (RET 0x4 at 0x0047D16C), EAX = the tick on return (last EAX write
// 0x0047D152), the x87 stack as it was (FLDZ 0x0047D150 pushed once, FSTP
// 0x0047D166 popped once) - is checked by calling through one assembly
// harness that records the stack bytes left behind, EAX and the x87 TOP.
//
// Differential section: the harness runs the rebuild and a transcription of
// the nine instructions of asm.s side by side on identical blocks and
// compares the 0x18 bytes, EAX, the stack and the x87 TOP. The transcription
// is OBNG's own assembly text of the read instructions, not a copy of the
// binary; when the local export's bytes.bin is present (it stays out of Git),
// the assembled transcription is compared with it byte for byte, otherwise
// that one check is reported as not run. This is a host-side check, not the
// in-game differential test AGENTS.md requires for "verified".
//
// x87 precision: none of the results depends on the precision control. FLDZ
// loads +0.0 exactly and FST/FSTP float of +0.0 store 0x00000000 under every
// precision and rounding control (derived: zero is exact in every format).
// The tests run under the host's default control word (64-bit precision on
// Linux i386); the 53-bit reading is checked as well by setting the control
// word to 53 bits around the original transcription.

#include "Check.h"
#include "engine/time/TimeInfo.h"
#include "engine/time/TimeInfo_TimeInfo.h"
#include "engine/time/TimeInfo_TimeInfoLogic.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace timeinfo = obng::engine::time;
namespace logic = obng::engine::time::timeinfo_ctor;
namespace engine = obng::engine::time::timeinfo_ctor::engine;

#if !defined(__i386__)
#error "TimeInfo_TimeInfoTest runs the original's instructions on the host and needs an i386 build"
#endif

// The nine instructions of re/export/time-info/0x0047D150/asm.s, transcribed.
// Plain cdecl from C++'s point of view is wrong for it, so it is only ever
// called through RunThiscall below.
extern "C" void OriginalTimeInfoConstructTranscription();
asm(R"(
	.text
	.p2align 4
	.globl OriginalTimeInfoConstructTranscription
	.type OriginalTimeInfoConstructTranscription, @function
OriginalTimeInfoConstructTranscription:
	.intel_syntax noprefix
	fldz
	mov eax, dword ptr [esp + 0x4]
	fst dword ptr [ecx + 0x4]
	mov byte ptr [ecx], 0x0
	fst dword ptr [ecx + 0x8]
	mov dword ptr [ecx + 0x10], 0x0
	fstp dword ptr [ecx + 0xc]
	mov dword ptr [ecx + 0x14], eax
	ret 0x4
	.att_syntax prefix
	.size OriginalTimeInfoConstructTranscription, . - OriginalTimeInfoConstructTranscription
)");

namespace {

constexpr std::size_t kBodySize = 31;  // 0x0047D150..0x0047D16E (read, meta.json body)

// What one thiscall left behind. Field offsets are used by the assembly below.
struct CallRecord {
	std::uint32_t eax;               // +0  EAX on return
	std::uint32_t stackBytesLeft;    // +4  pushed bytes the callee did not pop (0 for RET 0x4)
	std::uint16_t statusBefore;      // +8  x87 status word before the call
	std::uint16_t statusAfter;       // +10 x87 status word after the call
	std::uint16_t controlWord;       // +12 x87 control word the call ran under
	std::uint16_t controlAfter;      // +14 x87 control word after the call
};
static_assert(offsetof(CallRecord, stackBytesLeft) == 4 && offsetof(CallRecord, statusBefore) == 8 &&
		offsetof(CallRecord, statusAfter) == 10 && offsetof(CallRecord, controlWord) == 12 &&
		offsetof(CallRecord, controlAfter) == 14,
	"the harness writes these offsets");

} // namespace

// cdecl RunThiscall(function, self, argument, controlWord, record): loads the
// given x87 control word, sets ECX = self, pushes the argument, calls, then
// records EAX, the bytes left on the stack, the x87 status word before and
// after and the control word, and restores ESP and the caller's control word.
extern "C" void RunThiscall(const void* function, void* self, std::uint32_t argument, std::uint32_t controlWord,
	CallRecord* record);
asm(R"(
	.text
	.p2align 4
	.globl RunThiscall
	.type RunThiscall, @function
RunThiscall:
	.intel_syntax noprefix
	push ebp
	push ebx
	push esi
	push edi
	sub esp, 4
	fnstcw word ptr [esp]
	mov edi, dword ptr [esp + 40]
	fldcw word ptr [esp + 36]
	fnstcw word ptr [edi + 12]
	fnstsw ax
	mov word ptr [edi + 8], ax
	mov eax, dword ptr [esp + 24]
	mov ecx, dword ptr [esp + 28]
	mov edx, dword ptr [esp + 32]
	mov ebp, esp
	push edx
	call eax
	mov edx, ebp
	sub edx, esp
	mov esp, ebp
	mov edi, dword ptr [esp + 40]
	mov dword ptr [edi], eax
	mov dword ptr [edi + 4], edx
	fnstsw ax
	mov word ptr [edi + 10], ax
	fnstcw word ptr [edi + 14]
	fldcw word ptr [esp]
	add esp, 4
	pop edi
	pop esi
	pop ebx
	pop ebp
	ret
	.att_syntax prefix
	.size RunThiscall, . - RunThiscall
)");

namespace {

// x87 control words: all exceptions masked, round to nearest; PC = 10b
// (53 bits) or 11b (64 bits).
constexpr std::uint32_t kControl53Bit = 0x027F;
constexpr std::uint32_t kControl64Bit = 0x037F;

constexpr unsigned X87Top(std::uint16_t status) {
	return (status >> 11) & 0x7u;
}

using Block = std::array<std::uint8_t, sizeof(timeinfo::TimeInfo)>;

Block Filled(std::uint8_t sentinel) {
	Block block{};
	block.fill(sentinel);
	return block;
}

std::uint32_t DwordAt(const Block& block, std::size_t offset) {
	std::uint32_t value = 0;
	std::memcpy(&value, block.data() + offset, sizeof(value));
	return value;
}

const void* RebuildEntry() {
	return reinterpret_cast<const void*>(engine::kRebuild);
}

const void* OriginalEntry() {
	return reinterpret_cast<const void*>(&OriginalTimeInfoConstructTranscription);
}

struct Run {
	Block block;
	CallRecord record;
};

Run RunOn(const void* entry, Block block, std::uint32_t tick, std::uint32_t controlWord) {
	CallRecord record{};
	alignas(4) timeinfo::TimeInfo object{};
	std::memcpy(&object, block.data(), block.size());
	RunThiscall(entry, &object, tick, controlWord, &record);
	std::memcpy(block.data(), &object, block.size());
	return Run{block, record};
}

// The bytes the original leaves in a block filled with `sentinel` (derived
// from the stores at 0x0047D156..0x0047D169, claims 3-7 of the function file).
Block ExpectedAfter(std::uint8_t sentinel, std::uint32_t tick) {
	Block block = Filled(sentinel);
	block[0x00] = 0x00;
	for (std::size_t offset = 0x04; offset < 0x14; ++offset) {
		block[offset] = 0x00;
	}
	std::memcpy(block.data() + 0x14, &tick, sizeof(tick));
	return block;
}

void PureValues() {
	Section("F1 logic: the values 0x0047D150 stores, as plain values");
	constexpr logic::InitialValues zero = logic::InitialValuesFor(0);
	static_assert(zero.freezeDepth == 0 && zero.clockMs == 0 && zero.startTick == 0, "evaluated at compile time");
	Check(zero.freezeDepth == 0, "freezeDepth +0x00 is the byte 0 (MOV byte ptr [ECX],0x0 at 0x0047D159)");
	Check(std::bit_cast<std::uint32_t>(zero.fixedStepMs) == 0x00000000u, "fixedStepMs +0x04 is +0.0f, pattern 0x00000000 (FST at 0x0047D156)");
	Check(std::bit_cast<std::uint32_t>(zero.fixedStepCarryMs) == 0x00000000u, "fixedStepCarryMs +0x08 is +0.0f (FST at 0x0047D15C)");
	Check(std::bit_cast<std::uint32_t>(zero.frameSeconds) == 0x00000000u, "frameSeconds +0x0C is +0.0f (FSTP at 0x0047D166)");
	Check(zero.clockMs == 0, "clockMs +0x10 is integer 0 (MOV dword at 0x0047D15F)");

	const std::array<std::uint32_t, 5> ticks{0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
	bool allUnchanged = true;
	for (std::uint32_t tick : ticks) {
		allUnchanged = allUnchanged && logic::InitialValuesFor(tick).startTick == tick;
	}
	Check(allUnchanged, "startTick +0x14 is the argument unchanged for 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF");
}

void ApplyWritesOnlyTheOriginalsFields() {
	Section("F1 logic: Apply writes the six fields and leaves +0x01..+0x03");
	timeinfo::TimeInfo object{};
	std::memset(&object, 0xCD, sizeof(object));
	logic::Apply(object, logic::InitialValuesFor(0x12345678u));
	Block block{};
	std::memcpy(block.data(), &object, sizeof(object));
	Check(block == ExpectedAfter(0xCD, 0x12345678u), "the block equals the original's bytes for sentinel 0xCD, tick 0x12345678");
	Check(block[1] == 0xCD && block[2] == 0xCD && block[3] == 0xCD, "+0x01..+0x03 keep the sentinel (no store there, claim 7)");
}

void F1a() {
	Section("F1a: tick 0 - every written field zero");
	Run run = RunOn(RebuildEntry(), Filled(0xCD), 0, kControl64Bit);
	Check(run.block[0x00] == 0x00, "+0x00 == 0x00");
	Check(DwordAt(run.block, 0x04) == 0 && DwordAt(run.block, 0x08) == 0 && DwordAt(run.block, 0x0C) == 0,
		"+0x04, +0x08, +0x0C hold 0x00000000");
	Check(DwordAt(run.block, 0x10) == 0, "+0x10 == 0");
	Check(DwordAt(run.block, 0x14) == 0, "+0x14 == 0x00000000");
	Check(run.block == ExpectedAfter(0xCD, 0), "the whole block is the original's");
}

void F1b() {
	Section("F1b: tick 0xFFFFFFFF - stored unchanged, no sign or range handling");
	Run run = RunOn(RebuildEntry(), Filled(0x00), 0xFFFFFFFFu, kControl64Bit);
	Check(DwordAt(run.block, 0x14) == 0xFFFFFFFFu, "+0x14 == 0xFFFFFFFF (MOV at 0x0047D169)");
	Check(run.block == ExpectedAfter(0x00, 0xFFFFFFFFu), "the whole block is the original's");
}

void F1c() {
	Section("F1c: a sentinel-filled block - +0x01..+0x03 are not written");
	const std::array<std::uint8_t, 3> sentinels{0xCD, 0xFF, 0x5A};
	for (std::uint8_t sentinel : sentinels) {
		Run run = RunOn(RebuildEntry(), Filled(sentinel), 0x00C0FFEEu, kControl64Bit);
		char what[96];
		std::snprintf(what, sizeof(what), "sentinel 0x%02X: +0x01..+0x03 unchanged, the rest the original's", sentinel);
		Check(run.block[1] == sentinel && run.block[2] == sentinel && run.block[3] == sentinel &&
				run.block == ExpectedAfter(sentinel, 0x00C0FFEEu),
			what);
	}
}

void F1d() {
	Section("F1d: the floats compared as bit patterns - -0.0f would fail");
	Run run = RunOn(RebuildEntry(), Filled(0x80), 7, kControl64Bit);
	Check(DwordAt(run.block, 0x04) == 0x00000000u, "+0x04 is 0x00000000, not 0x80000000");
	Check(DwordAt(run.block, 0x08) == 0x00000000u, "+0x08 is 0x00000000, not 0x80000000");
	Check(DwordAt(run.block, 0x0C) == 0x00000000u, "+0x0C is 0x00000000, not 0x80000000");
	Block negativeZero = ExpectedAfter(0x80, 7);
	negativeZero[0x07] = 0x80;
	Check(negativeZero != ExpectedAfter(0x80, 7), "the comparison sees -0.0f at +0x04 (the check can fail)");
}

void CallingConvention() {
	Section("F1 convention: ECX = this, RET 0x4, EAX = the tick, x87 stack balanced");
	Run run = RunOn(RebuildEntry(), Filled(0xCD), 0xDEADBEEFu, kControl64Bit);
	Check(run.record.stackBytesLeft == 0, "the callee pops its one argument (RET 0x4 at 0x0047D16C)");
	Check(run.record.eax == 0xDEADBEEFu, "EAX holds the tick on return (last EAX write 0x0047D152)");
	Check(X87Top(run.record.statusBefore) == X87Top(run.record.statusAfter), "x87 TOP is the same before and after");
	Check(run.record.controlAfter == run.record.controlWord, "the x87 control word is not changed");
}

// A callee that returns with a plain RET, as a cdecl function does.
[[gnu::noinline]] void __cdecl CdeclProbe() {}

void HarnessCatchesAWrongConvention() {
	Section("F1 convention: the harness sees a callee that does not pop (the checks can fail)");
	CallRecord record{};
	timeinfo::TimeInfo object{};
	RunThiscall(reinterpret_cast<const void*>(&CdeclProbe), &object, 1, kControl64Bit, &record);
	Check(record.stackBytesLeft == 4, "a cdecl callee leaves the 4 pushed bytes");
}

void DifferentialAgainstTranscription() {
	Section("Differential: rebuild and transcription of asm.s, same blocks, same harness");
	const std::array<std::uint32_t, 6> ticks{0x00000000u, 0x00000001u, 0x0001E240u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
	const std::array<std::uint8_t, 4> sentinels{0x00, 0xCD, 0xFF, 0x80};
	const std::array<std::uint32_t, 2> controls{kControl53Bit, kControl64Bit};
	int compared = 0;
	int agreed = 0;
	for (std::uint32_t control : controls) {
		for (std::uint8_t sentinel : sentinels) {
			for (std::uint32_t tick : ticks) {
				Run original = RunOn(OriginalEntry(), Filled(sentinel), tick, control);
				Run rebuild = RunOn(RebuildEntry(), Filled(sentinel), tick, control);
				++compared;
				bool same = original.block == rebuild.block && original.block == ExpectedAfter(sentinel, tick) &&
					original.record.eax == rebuild.record.eax &&
					original.record.stackBytesLeft == rebuild.record.stackBytesLeft &&
					X87Top(original.record.statusAfter) == X87Top(rebuild.record.statusAfter) &&
					X87Top(original.record.statusBefore) == X87Top(original.record.statusAfter);
				if (same) {
					++agreed;
				} else {
					std::printf("    differs: control 0x%04X sentinel 0x%02X tick 0x%08X\n", control, sentinel, tick);
				}
			}
		}
	}
	char what[96];
	std::snprintf(what, sizeof(what), "%d of %d runs agree byte for byte, EAX, stack and x87 TOP", agreed, compared);
	Check(compared == 48 && agreed == compared, what);

	Run at53 = RunOn(OriginalEntry(), Filled(0xCD), 5, kControl53Bit);
	Check(at53.record.controlWord == kControl53Bit, "the 53-bit reading ran under precision control 10b");
	Check(at53.block == ExpectedAfter(0xCD, 5), "53-bit reading: +0.0f stored at +0x04/+0x08/+0x0C");
}

void TranscriptionMatchesExport() {
	Section("Differential: the assembled transcription is the export's bytes.bin");
	std::string path = __FILE__;
	path = path.substr(0, path.find_last_of('/')) + "/../re/export/time-info/0x0047D150/bytes.bin";
	std::FILE* file = std::fopen(path.c_str(), "rb");
	if (file == nullptr) {
		std::printf("  not run  %s is not present (the export stays out of Git)\n", path.c_str());
		return;
	}
	std::array<std::uint8_t, kBodySize + 1> exported{};
	std::size_t read = std::fread(exported.data(), 1, exported.size(), file);
	std::fclose(file);
	Check(read == kBodySize, "bytes.bin holds 31 bytes (meta.json bytes_bin length 31)");
	Check(std::memcmp(exported.data(), OriginalEntry(), kBodySize) == 0,
		"the transcription assembles to the 31 bytes of 0x0047D150..0x0047D16E");
}

} // namespace

int main() {
	PureValues();
	ApplyWritesOnlyTheOriginalsFields();
	F1a();
	F1b();
	F1c();
	F1d();
	CallingConvention();
	HarnessCatchesAWrongConvention();
	TranscriptionMatchesExport();
	DifferentialAgainstTranscription();
	return Failures();
}
