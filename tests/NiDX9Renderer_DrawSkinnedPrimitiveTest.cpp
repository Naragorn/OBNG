// NiDX9Renderer::DrawSkinnedPrimitive 0x00767520 (proposed name) rebuilt as
// DrawSkinnedPrimitive(ports, args, plan) (NiDX9Renderer_DrawSkinnedPrimitiveLogic.h).
//
// Two parts:
//
// 1. The decisions as pure functions, both outcomes of every branch of the
//    map (re/functions/0x00767520.md B1..B14), the boundaries (bone count
//    equal to the limit, strip length 0/1/2, start index wrap) included.
//
// 2. The differential gate, layer 1 (docs/re-workflow.md, "the gate outside
//    the workflow"). The original ran in the emulator
//    (tools/difftest/emulate.py) on the cases of
//    tools/difftest/cases/0x00767520.json; its result is
//    tools/difftest/expected/0x00767520.json (2026-11-04: every case returned
//    to its sentinel, no emulator error, stack_delta +20 - the RET 0x14
//    popping the five stack arguments, the balanced result for this
//    callee-cleaned function, as +24 is for 0x007672F0's RET 0x18). Every
//    callee is a traced stub there; the trace shows six stack dwords per call,
//    so the stubs of the 7- and 8-argument slots and of DrawIndexedPrimitive
//    are raw code that also store the arguments beyond the sixth to a log
//    (the case file's "note"). The trace (callee, return address, ECX where
//    it is an input, the arguments the callee takes), the writes (site,
//    address, size, value) and EAX at the exit are transcribed below,
//    literally, per case (measured). Here the same inputs are answered by
//    fake ports that hand out the case's addresses as handles and record each
//    port call in the emulator's terms; both sequences must be equal.
//
//    The two-eye plan has no original to compare with. Its expectation is
//    derived from the same transcription by StereoDraw.h's rule: each
//    partition's draws repeated between eyeSwitch(1) and eyeSwitch(0), after
//    the first eye's draws and before slot 16, nothing else moved.
//
// Flow coverage (branch -> case):
//   B1  null skin                                 case 1; HasSkin
//   B2  null geometry -> default                  case 2; FallsBackToDefaultShader
//   B3  cast fails / succeeds                     case 3 / cases 4, 5, 7-9
//   B4  slot 10 refuses                           case 2; PipelineRefused
//   B5  slot 18 opens no pass                     case 3; PassOpened
//   B6  zero partitions                           case 4; PartitionLoopRuns
//   B7  custom shader, no bone gate (60 bones)    case 5; SkippedByBoneGate
//   B8  default shader: above skipped, equal and below drawn; all skipped   cases 6, 10
//   B9  slot 15 null on a later / the first partition                     cases 7, 8
//   B10 record without arrays                     case 6 (partition 2)
//   B11 list count / strip length - 2 (and a length of 1 wrapping)        cases 5, 6, 9
//   B12 one array / several arrays                case 5 / cases 6, 9
//   B13 one partition / several                   case 5 / cases 6, 9
//   B14 one pass / two / three passes             cases 4-6 / 9 / 10
//   C3  primitive type 4 and 5                    cases 5 and 6
//   C8  slot 12/17's third argument after a drawn partition / after skips only   cases 9 / 10
//   C9  the flags clear per pass, top nibble kept cases 4 (0x8FFF), 10 (0x0123)
//
// What it cannot see: what the callees do (they are stubs), the stack locals,
// and the faults the original takes on bad memory (a null data with a pass
// open writes to 0x2E; a null record from slot 15 is refused, read B9).

#include "Check.h"
#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitive.h"
#include "engine/render/NiDX9Renderer_DrawSkinnedPrimitiveLogic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace logic = obng::engine::render::draw_skinned_primitive;
namespace eng = obng::engine::render::draw_skinned_primitive::engine;
using logic::Handle;
using obng::engine::render::stereo_draw::EyePlan;

namespace {

// ---- Part 1: the decisions -------------------------------------------------------------

void TestDecisions() {
	Section("B1: a null skin returns at once, any other skin goes on");
	Check(!logic::HasSkin(0), "skin 0 -> return (0x0076752C JZ)");
	Check(logic::HasSkin(0x10008000u), "skin non-null -> draw");

	Section("C3: partition 0's strip count chooses the primitive type");
	Check(logic::PrimitiveTypeFor(0) == 4, "0 strips -> D3DPT_TRIANGLELIST 4");
	Check(logic::PrimitiveTypeFor(1) == 5, "1 strip -> D3DPT_TRIANGLESTRIP 5");
	Check(logic::PrimitiveTypeFor(0xFFFF) == 5, "0xFFFF strips -> 5 (any non-zero word)");

	Section("B2/B3: the default shader when there is no geometry or the cast fails");
	Check(logic::FallsBackToDefaultShader(0, 0), "no geometry -> default");
	Check(logic::FallsBackToDefaultShader(0x10005000u, 0), "cast null -> default");
	Check(!logic::FallsBackToDefaultShader(0x10005000u, 0x10006000u), "cast non-null -> the cast shader");

	Section("B4: slot 10 refuses on any non-zero dword");
	Check(!logic::PipelineRefused(0), "0 -> go on");
	Check(logic::PipelineRefused(1), "1 -> abort");
	Check(logic::PipelineRefused(0x80000000u), "0x80000000 -> abort (TEST EAX,EAX, all 32 bits)");

	Section("B5/B14: slots 18 and 19 open a pass on any non-zero dword");
	Check(!logic::PassOpened(0), "0 -> no pass");
	Check(logic::PassOpened(0x100u), "0x100 -> a pass (not a byte test)");

	Section("B6: the partition loop runs unless the count is 0");
	Check(!logic::PartitionLoopRuns(0), "0 partitions -> skip");
	Check(logic::PartitionLoopRuns(1), "1 partition -> loop");
	Check(logic::PartitionLoopRuns(0xFFFFFFFFu), "0xFFFFFFFF -> loop (unsigned)");

	Section("B7/B8: only the default shader is bone-gated, unsigned, equal passes");
	constexpr Handle kDefault = 0x10004000u;
	constexpr Handle kCustom = 0x10006000u;
	Check(!logic::SkippedByBoneGate(kCustom, kDefault, 60, 4), "custom shader, 60 bones > 4 -> drawn (B7)");
	Check(logic::SkippedByBoneGate(kDefault, kDefault, 21, 20), "default, 21 > 20 -> skipped (B8)");
	Check(!logic::SkippedByBoneGate(kDefault, kDefault, 20, 20), "default, 20 == 20 -> drawn (JBE)");
	Check(!logic::SkippedByBoneGate(kDefault, kDefault, 19, 20), "default, 19 < 20 -> drawn");
	Check(!logic::SkippedByBoneGate(kDefault, kDefault, 0xFFFF, 0xFFFFFFFFu), "u16 max against a u32 limit -> drawn");
	Check(logic::SkippedByBoneGate(kDefault, kDefault, 1, 0), "a limit of 0 skips one bone");

	Section("B9: a null record aborts");
	Check(logic::RecordMissing(0), "record 0 -> abort");
	Check(!logic::RecordMissing(0x1000D000u), "record non-null -> draw");

	Section("B10/B12/B13: the array and partition loops, unsigned");
	Check(!logic::HasArrays(0), "0 arrays -> no draw");
	Check(logic::HasArrays(1), "1 array -> draw");
	Check(!logic::LoopContinues(1, 1), "index 1 of 1 -> stop");
	Check(logic::LoopContinues(1, 2), "index 1 of 2 -> next");
	Check(logic::LoopContinues(1, 0xFFFFFFFFu), "count 0xFFFFFFFF -> next (JC, unsigned)");

	Section("B11 and C5: the primitive count, list or strip, the 32-bit wrap below 2");
	Check(logic::PrimitiveCount(false, 0, 0x99u) == 0x99u, "no lengths -> the record's +0x3C");
	Check(logic::PrimitiveCount(true, 5, 0x99u) == 3u, "strip of 5 indices -> 3 triangles");
	Check(logic::PrimitiveCount(true, 2, 0) == 0u, "strip of 2 -> 0");
	Check(logic::PrimitiveCount(true, 1, 0) == 0xFFFFFFFFu, "strip of 1 -> 0xFFFFFFFF (SUB ECX,2 wraps)");
	Check(logic::PrimitiveCount(true, 0, 0) == 0xFFFFFFFEu, "strip of 0 -> 0xFFFFFFFE");
	Check(logic::PrimitiveCount(true, 0xFFFF, 0) == 0xFFFDu, "u16 max zero-extended -> 0xFFFD");

	Section("C4: the next start index is start + count + 2, modulo 2^32");
	Check(logic::NextStartIndex(0, 3) == 5u, "0 + 3 + 2 = 5");
	Check(logic::NextStartIndex(0, 0xFFFFFFFFu) == 1u, "the wrapped count of a 1-index strip adds 1");
	Check(logic::NextStartIndex(0xFFFFFFFFu, 0) == 1u, "start wraps");

	Section("C9: the flags clear keeps the top nibble");
	Check(logic::ClearedPassFlags(0xABCD) == 0xA000, "0xABCD -> 0xA000");
	Check(logic::ClearedPassFlags(0x0FFF) == 0x0000, "0x0FFF -> 0");
	Check(logic::ClearedPassFlags(0xF000) == 0xF000, "0xF000 unchanged");
}

// ---- Part 2: the differential gate -----------------------------------------------------

constexpr std::uint32_t kAny = 0xFFFF0002u;       // ECX is not an input of this callee (cdecl, COM stdcall)
constexpr std::uint32_t kEyeSwitch = 0xE7E50000u;  // the eye-switch port (no original counterpart)

// The synthetic memory of the cases (tools/difftest/cases/0x00767520.json, "note").
constexpr Handle kThis = 0x10000000u;
constexpr Handle kDevice = 0x10002000u;
constexpr Handle kRenderState = 0x10003000u;
constexpr Handle kSubObject = 0x10014000u;
constexpr Handle kDefaultShader = 0x10004000u;
constexpr Handle kGeometry = 0x10005000u;
constexpr Handle kCandidate = 0x10005800u;
constexpr Handle kData = 0x10007000u;
constexpr Handle kSkin = 0x10008000u;
constexpr Handle kSkinPartition = 0x10009000u;
constexpr Handle kPartitions = 0x1000A000u;
constexpr Handle kWorld = 0x1000B000u;
constexpr Handle kBound = 0x1000C000u;
constexpr Handle kRecords = 0x1000D000u;  // one per 0x100
constexpr Handle kLengths = 0x1000E000u;  // one array per 0x40
constexpr Handle kPropertyState = 0x10100000u;
constexpr Handle kEffectState = 0x10110000u;
constexpr std::uint32_t kBoundWords[4] = {0x3F800000u, 0x40000000u, 0x40400000u, 0x7FC00001u};

// The stubs (vtable targets) and the log their raw code writes to.
constexpr std::uint32_t SlotStub(int slot) {
	return 0x20000000u + static_cast<std::uint32_t>(slot) * 0x100u;
}
constexpr std::uint32_t kSubObjectSlot1Stub = 0x20002000u;
constexpr std::uint32_t kSetDrawFlagStub = 0x20002100u;
constexpr std::uint32_t kDrawIndexedPrimitiveStub = 0x20002200u;
constexpr std::uint32_t kLogDrawCount = 0x10F00000u;
constexpr std::uint32_t LogTail(int slot) {
	switch (slot) {
	case 10: return 0x10F00010u;
	case 11: return 0x10F00014u;
	case 12: return 0x10F00018u;
	case 17: return 0x10F0001Cu;
	case 13: return 0x10F00020u;
	case 14: return 0x10F00028u;
	default: return 0x10F00030u;  // 16
	}
}

struct PartitionInput {
	std::uint16_t vertices;
	std::uint16_t bones;
	std::uint16_t strips;
	std::uint32_t bufferData;
};

struct RecordInput {
	std::uint32_t baseVertex;
	std::uint32_t triangles;
	std::uint32_t arrays;
	bool hasLengths;
	std::vector<std::uint16_t> lengths;
};

struct CaseInput {
	bool geometry = true;
	bool skin = true;
	std::uint32_t cast = 0;
	std::uint32_t slot10 = 0;
	std::uint32_t slot18 = 1;
	std::vector<std::uint32_t> slot19;
	std::vector<std::uint32_t> slot15;
	std::uint32_t maxBones = 4;
	std::uint16_t flags = 0xABCD;
	std::uint32_t partitionCount = 0;
	std::vector<PartitionInput> partitions;  // in memory; [0] exists even with a count of 0
	std::vector<RecordInput> records;
};

struct TracedCall {
	std::uint32_t callee;
	std::uint32_t from;  // the return address: the instruction after the CALL
	std::uint32_t ecx;
	std::vector<std::uint32_t> args;
	bool operator==(const TracedCall&) const = default;
};

struct Write {
	std::uint32_t at;
	std::uint32_t address;
	std::uint32_t size;
	std::uint32_t value;
	bool operator==(const Write&) const = default;
};

struct EmulatorCase {
	const char* name;
	CaseInput input;
	std::vector<TracedCall> calls;  // the original's trace, as transcribed
	std::vector<Write> writes;      // the original's writes outside its stack, as transcribed
	std::uint32_t eax;              // EAX at the original's exit
};

std::uint32_t Next(const std::vector<std::uint32_t>& values, int& hits) {
	std::uint32_t value = values.empty() ? 0u : values[std::min<std::size_t>(hits, values.size() - 1)];
	++hits;
	return value;
}

// The ports, answering from a case's inputs and recording each call and each
// store in the emulator's terms. Handles are the case's addresses. A stub's
// log store is recorded where its raw code makes it (stub + 4, stub + 0xD).
// lastEax follows what each stub leaves in EAX (a plain stub its value; the
// logging stubs the last argument they stored), so the original's EAX at the
// exit can be compared: the function returns nothing of its own.
struct CasePorts {
	explicit CasePorts(const CaseInput& caseInput, Handle skinArgument)
		: in(caseInput), flags(caseInput.flags), lastEax(skinArgument) {}

	const CaseInput& in;
	std::uint16_t flags;
	std::uint32_t lastEax;  // MOV EAX,[ESP+0xC] (a3) at 0x00767520 before any call
	int slot15Hits = 0;
	int slot19Hits = 0;
	std::vector<TracedCall> calls;
	std::vector<Write> writes;
	std::vector<std::string> faults;  // a port asked about memory the case does not hold

	void Emit(std::uint32_t callee, std::uint32_t from, std::uint32_t ecx, std::vector<std::uint32_t> args) {
		calls.push_back(TracedCall{callee, from, ecx, std::move(args)});
	}
	void Store(std::uint32_t at, std::uint32_t address, std::uint32_t size, std::uint32_t value) {
		writes.push_back(Write{at, address, size, value});
	}
	const PartitionInput& PartitionOf(Handle partition) {
		const std::uint32_t index = (partition - kPartitions) / 0x2C;
		if ((partition - kPartitions) % 0x2C != 0 || index >= in.partitions.size()) {
			faults.push_back("a partition outside the case's array");
			static const PartitionInput none{};
			return none;
		}
		return in.partitions[index];
	}
	const RecordInput& RecordOf(Handle record) {
		const std::uint32_t index = (record - kRecords) / 0x100;
		if ((record - kRecords) % 0x100 != 0 || index >= in.records.size()) {
			faults.push_back("a record outside the case's records");
			static const RecordInput none{};
			return none;
		}
		return in.records[index];
	}

	// Reads.
	Handle shaderCandidate(Handle geometry) {
		if (geometry != kGeometry) {
			faults.push_back("candidate of an unmapped geometry");
		}
		return kCandidate;
	}
	Handle defaultShader() { return kDefaultShader; }
	Handle propertyState() { return kPropertyState; }
	Handle effectState() { return kEffectState; }
	Handle skinPartition(Handle skin) {
		if (skin != kSkin) {
			faults.push_back("partition of an unmapped skin");
		}
		return kSkinPartition;
	}
	Handle partitions(Handle) { return kPartitions; }
	std::uint32_t partitionCount(Handle) { return in.partitionCount; }
	Handle partitionAt(Handle array, std::uint32_t index) { return array + eng::kPartitionStride * index; }
	std::uint16_t stripCount(Handle partition) { return PartitionOf(partition).strips; }
	std::uint16_t boneCount(Handle partition) { return PartitionOf(partition).bones; }
	std::uint16_t vertexCount(Handle partition) { return PartitionOf(partition).vertices; }
	Handle bufferData(Handle partition) { return PartitionOf(partition).bufferData; }
	std::uint32_t maxBonesWithoutShader() { return in.maxBones; }
	std::uint32_t worldBoundWord(Handle bound, std::uint32_t word) {
		if (bound != kBound || word >= 4) {
			faults.push_back("an unmapped bound word");
			return 0;
		}
		return kBoundWords[word];
	}
	std::uint16_t dataFlags(Handle data) {
		if (data != kData) {
			faults.push_back("flags of an unmapped data");
		}
		return flags;
	}
	std::uint32_t arrayCount(Handle record) { return RecordOf(record).arrays; }
	Handle arrayLengths(Handle record) {
		const RecordInput& r = RecordOf(record);
		return r.hasLengths ? kLengths + 0x40u * ((record - kRecords) / 0x100u) : 0u;
	}
	std::uint16_t arrayLength(Handle lengths, std::uint32_t index) {
		const RecordInput& r = RecordOf(kRecords + 0x100u * ((lengths - kLengths) / 0x40u));
		if (index >= r.lengths.size()) {
			faults.push_back("a length past the case's array");
			return 0;
		}
		return r.lengths[index];
	}
	std::uint32_t triangleCount(Handle record) { return RecordOf(record).triangles; }
	std::uint32_t baseVertexIndex(Handle record) { return RecordOf(record).baseVertex; }
	Handle device() { return kDevice; }

	// Stores.
	void setCurrentWorldBoundWord(std::uint32_t word, std::uint32_t value) {
		Store(eng::kWorldBoundStoreSites[word], kThis + 0x658u + 4u * word, 4, value);
	}
	void setDataFlags(Handle data, std::uint16_t value) {
		flags = value;
		Store(eng::kFlagsClearSite, data + 0x2Eu, 2, value);
	}

	// Calls.
	Handle castToShaderInterface(Handle object) {
		Emit(eng::kRttiCastAddress, eng::kAfterRttiCast, kAny, {eng::kNiRttiNiD3DShaderInterface, object});
		return lastEax = in.cast;
	}
	void Pipeline(int slot, std::uint32_t from, Handle shader, const logic::PipelineCall& c) {
		Emit(SlotStub(slot), from, shader, {c.geometry, c.skin, c.record, c.propertyState, c.effectState, c.world});
		Store(SlotStub(slot) + 4u, LogTail(slot), 4, c.worldBound);
		lastEax = c.worldBound;
	}
	void Partition(int slot, std::uint32_t from, Handle shader, const logic::PartitionCall& c) {
		Emit(SlotStub(slot), from, shader,
				{c.geometry, c.skin, c.partition, c.record, c.propertyState, c.effectState});
		Store(SlotStub(slot) + 4u, LogTail(slot), 4, c.world);
		Store(SlotStub(slot) + 0xDu, LogTail(slot) + 4u, 4, c.worldBound);
		lastEax = c.worldBound;
	}
	std::uint32_t shaderSlot10(Handle shader, const logic::PipelineCall& c) {
		Pipeline(10, eng::kAfterSlot10, shader, c);
		return lastEax = in.slot10;
	}
	void shaderSlot11(Handle shader, const logic::PipelineCall& c) { Pipeline(11, eng::kAfterSlot11, shader, c); }
	void shaderSlot12(Handle shader, const logic::PipelineCall& c) { Pipeline(12, eng::kAfterSlot12, shader, c); }
	void shaderSlot17(Handle shader, const logic::PipelineCall& c) { Pipeline(17, eng::kAfterSlot17, shader, c); }
	void shaderSlot13(Handle shader, const logic::PartitionCall& c) { Partition(13, eng::kAfterSlot13, shader, c); }
	void shaderSlot14(Handle shader, const logic::PartitionCall& c) { Partition(14, eng::kAfterSlot14, shader, c); }
	void shaderSlot16(Handle shader, const logic::PartitionCall& c) { Partition(16, eng::kAfterSlot16, shader, c); }
	Handle shaderSlot15(Handle shader, const logic::RecordCall& c) {
		Emit(SlotStub(15), eng::kAfterSlot15, shader, {c.geometry, c.partition, c.bufferData, c.propertyState});
		return lastEax = Next(in.slot15, slot15Hits);
	}
	std::uint32_t shaderSlot18(Handle shader) {
		Emit(SlotStub(18), eng::kAfterSlot18, shader, {});
		return lastEax = in.slot18;
	}
	std::uint32_t shaderSlot19(Handle shader) {
		Emit(SlotStub(19), eng::kAfterSlot19, shader, {});
		return lastEax = Next(in.slot19, slot19Hits);
	}
	void cannotRenderPartition(std::uint32_t bones) {
		Emit(eng::kDebugPrintAddress, eng::kAfterDebugPrint, kAny, {eng::kCannotRenderPartitionFormat, bones});
		lastEax = 0;
	}
	void renderStateSubObjectSlot1() {
		Emit(kSubObjectSlot1Stub, eng::kAfterSubObjectSlot1, kSubObject, {});
		lastEax = 0;
	}
	void renderStateSetDrawFlag(std::uint32_t flag) {
		Emit(kSetDrawFlagStub, eng::kAfterSetDrawFlag, kRenderState, {flag});
		lastEax = 0;
	}
	void drawIndexedPrimitive(const logic::DrawIndexedCall& c) {
		Emit(kDrawIndexedPrimitiveStub, eng::kAfterDrawIndexedPrimitive, kAny,
				{c.device, c.primitiveType, c.baseVertexIndex, c.minVertexIndex, c.numVertices, c.startIndex});
		Store(kDrawIndexedPrimitiveStub + 4u, kLogDrawCount, 4, c.primitiveCount);
		lastEax = c.primitiveCount;
	}
	void eyeSwitch(std::uint8_t eye) { Emit(kEyeSwitch, 0, kAny, {eye}); }
};
static_assert(logic::DrawSkinnedPrimitivePorts<CasePorts>);

// The original's results, transcribed from tools/difftest/expected/0x00767520.json
// (measured). Inputs as in tools/difftest/cases/0x00767520.json.
const EmulatorCase kCases[] = {
	{
		"B1 null skin: returns at once",
		{.geometry = true, .skin = false, .cast = 0x00000000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 1, .partitions = {{3, 2, 0, 0x10200000u}}, .records = {}},
		{},
		{},
		0x00000000u,
	},
	{
		"B2 null geometry -> default shader; B4 slot 10 refuses",
		{.geometry = false, .skin = true, .cast = 0x00000000u, .slot10 = 0x00000001u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 1, .partitions = {{3, 2, 0, 0x10200000u}}, .records = {}},
		{
			{0x20000A00u, 0x007675A4u, 0x10004000u, {0x00000000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u}
		},
		0x00000001u,
	},
	{
		"B3 cast fails -> default shader; B5 slot 18 opens no pass",
		{.geometry = true, .skin = true, .cast = 0x00000000u, .slot10 = 0x00000000u, .slot18 = 0x00000000u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 1, .partitions = {{3, 2, 0, 0x10200000u}}, .records = {}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10004000u, {}},
			{0x20001100u, 0x007677F0u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20002100u, 0x00767804u, 0x10003000u, {0x00000000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20001104u, 0x10F0001Cu, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B3 cast succeeds; one pass; B6 zero partitions",
		{.geometry = true, .skin = true, .cast = 0x10006000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u}, .maxBones = 4, .flags = 0x8FFF, .partitionCount = 0, .partitions = {{7, 9, 2, 0x10200000u}}, .records = {}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10006000u, {}},
			{0x20000C00u, 0x00767612u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001300u, 0x007677C6u, 0x10006000u, {}},
			{0x20001100u, 0x007677F0u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20002100u, 0x00767804u, 0x10003000u, {0x00000000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x007677B7u, 0x1000702Eu, 2, 0x00008000u},
			{0x20001104u, 0x10F0001Cu, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B7 custom shader draws 60 bones; B11 list count; one array; one partition",
		{.geometry = true, .skin = true, .cast = 0x10006000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 1, .partitions = {{4660, 60, 0, 0x10200000u}}, .records = {{0x00000040u, 0x00000099u, 1, false, {}}}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10006000u, {}},
			{0x20000C00u, 0x00767612u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x10200000u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A000u, 0x10200000u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000004u, 0x00000040u, 0x00000000u, 0x00001234u, 0x00000000u}},
			{0x20001000u, 0x00767797u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20001300u, 0x007677C6u, 0x10006000u, {}},
			{0x20001100u, 0x007677F0u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000D000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20002100u, 0x00767804u, 0x10003000u, {0x00000000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0x00000099u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x007677B7u, 0x1000702Eu, 2, 0x0000A000u},
			{0x20001104u, 0x10F0001Cu, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B8 default shader bone gate: above skipped, equal and below drawn; B10 no array; B11 strips; B12; B13",
		{.geometry = true, .skin = true, .cast = 0x00000000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u, 0x1000D100u, 0x1000D200u}, .maxBones = 20, .flags = 0xABCD, .partitionCount = 4, .partitions = {{100, 21, 3, 0x10200000u}, {200, 20, 0, 0x10200010u}, {300, 19, 1, 0x10200020u}, {400, 0, 0, 0x10200030u}}, .records = {{0x00000010u, 0x00000000u, 3, true, {5, 4, 7}}, {0x00000020u, 0x00000055u, 0, false, {}}, {0x00000030u, 0x00000066u, 2, false, {}}}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10004000u, {}},
			{0x20000C00u, 0x00767612u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000015u}},
			{0x20000D00u, 0x007676A1u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x10200010u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10004000u, {0x10005000u, 0x1000A02Cu, 0x10200010u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000010u, 0x00000000u, 0x000000C8u, 0x00000000u}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000010u, 0x00000000u, 0x000000C8u, 0x00000005u}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000010u, 0x00000000u, 0x000000C8u, 0x00000009u}},
			{0x20001000u, 0x00767797u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20000D00u, 0x007676A1u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A058u, 0x10200020u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10004000u, {0x10005000u, 0x1000A058u, 0x10200020u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A058u, 0x1000D100u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20001000u, 0x00767797u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A058u, 0x1000D100u, 0x10100000u, 0x10110000u}},
			{0x20000D00u, 0x007676A1u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A084u, 0x10200030u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10004000u, {0x10005000u, 0x1000A084u, 0x10200030u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A084u, 0x1000D200u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000030u, 0x00000000u, 0x00000190u, 0x00000000u}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000030u, 0x00000000u, 0x00000190u, 0x00000068u}},
			{0x20001000u, 0x00767797u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000A084u, 0x1000D200u, 0x10100000u, 0x10110000u}},
			{0x20001300u, 0x007677C6u, 0x10004000u, {}},
			{0x20001100u, 0x007677F0u, 0x10004000u, {0x10005000u, 0x10008000u, 0x1000D200u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20002100u, 0x00767804u, 0x10003000u, {0x00000000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0x00000003u},
			{0x20002204u, 0x10F00000u, 4, 0x00000002u},
			{0x20002204u, 0x10F00000u, 4, 0x00000005u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0x00000066u},
			{0x20002204u, 0x10F00000u, 4, 0x00000066u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x007677B7u, 0x1000702Eu, 2, 0x0000A000u},
			{0x20001104u, 0x10F0001Cu, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B9 slot 15 null on a later partition aborts mid-pass",
		{.geometry = true, .skin = true, .cast = 0x10006000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x1000D000u, 0x00000000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 3, .partitions = {{10, 2, 0, 0x10200000u}, {11, 2, 0, 0x10200010u}, {12, 2, 0, 0x10200020u}}, .records = {{0x00000010u, 0x00000020u, 1, false, {}}}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10006000u, {}},
			{0x20000C00u, 0x00767612u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x10200000u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A000u, 0x10200000u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000004u, 0x00000010u, 0x00000000u, 0x0000000Au, 0x00000000u}},
			{0x20001000u, 0x00767797u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x10200010u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A02Cu, 0x10200010u, 0x10100000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0x00000020u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B9 slot 15 null on the first partition aborts",
		{.geometry = true, .skin = true, .cast = 0x10006000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000000u}, .slot15 = {0x00000000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 1, .partitions = {{10, 2, 1, 0x10200000u}}, .records = {}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10006000u, {}},
			{0x20000C00u, 0x00767612u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x10200000u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A000u, 0x10200000u, 0x10100000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B14 two passes: slot 12 and 17 get the last record; strip length 1 wraps",
		{.geometry = true, .skin = true, .cast = 0x10006000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000001u, 0x00000000u}, .slot15 = {0x1000D000u, 0x1000D100u, 0x1000D100u, 0x1000D000u}, .maxBones = 4, .flags = 0xABCD, .partitionCount = 2, .partitions = {{50, 2, 4, 0x10200000u}, {60, 2, 0, 0x10200010u}}, .records = {{0x00000100u, 0x00000000u, 2, true, {1, 6}}, {0x00000200u, 0x00000030u, 1, false, {}}}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10006000u, {}},
			{0x20000C00u, 0x00767612u, 0x10006000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x10200000u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A000u, 0x10200000u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000100u, 0x00000000u, 0x00000032u, 0x00000000u}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000100u, 0x00000000u, 0x00000032u, 0x00000001u}},
			{0x20001000u, 0x00767797u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x10200010u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A02Cu, 0x10200010u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x1000D100u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000200u, 0x00000000u, 0x0000003Cu, 0x00000000u}},
			{0x20001000u, 0x00767797u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x1000D100u, 0x10100000u, 0x10110000u}},
			{0x20001300u, 0x007677C6u, 0x10006000u, {}},
			{0x20000C00u, 0x00767612u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000D100u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x10200000u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A000u, 0x10200000u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D100u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000200u, 0x00000000u, 0x00000032u, 0x00000000u}},
			{0x20001000u, 0x00767797u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A000u, 0x1000D100u, 0x10100000u, 0x10110000u}},
			{0x20000D00u, 0x007676A1u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x10200010u, 0x10100000u, 0x10110000u}},
			{0x20000F00u, 0x007676B5u, 0x10006000u, {0x10005000u, 0x1000A02Cu, 0x10200010u, 0x10100000u}},
			{0x20000E00u, 0x007676E6u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20002000u, 0x007676F9u, 0x10014000u, {}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000100u, 0x00000000u, 0x0000003Cu, 0x00000000u}},
			{0x20002200u, 0x00767750u, kAny, {0x10002000u, 0x00000005u, 0x00000100u, 0x00000000u, 0x0000003Cu, 0x00000001u}},
			{0x20001000u, 0x00767797u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000A02Cu, 0x1000D000u, 0x10100000u, 0x10110000u}},
			{0x20001300u, 0x007677C6u, 0x10006000u, {}},
			{0x20001100u, 0x007677F0u, 0x10006000u, {0x10005000u, 0x10008000u, 0x1000D000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20002100u, 0x00767804u, 0x10003000u, {0x00000000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0xFFFFFFFFu},
			{0x20002204u, 0x10F00000u, 4, 0x00000004u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0x00000030u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x007677B7u, 0x1000702Eu, 2, 0x0000A000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0x00000030u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x20000D04u, 0x10F00020u, 4, 0x1000B000u},
			{0x20000D0Du, 0x10F00024u, 4, 0x1000C000u},
			{0x20000E04u, 0x10F00028u, 4, 0x1000B000u},
			{0x20000E0Du, 0x10F0002Cu, 4, 0x1000C000u},
			{0x20002204u, 0x10F00000u, 4, 0xFFFFFFFFu},
			{0x20002204u, 0x10F00000u, 4, 0x00000004u},
			{0x20001004u, 0x10F00030u, 4, 0x1000B000u},
			{0x2000100Du, 0x10F00034u, 4, 0x1000C000u},
			{0x007677B7u, 0x1000702Eu, 2, 0x0000A000u},
			{0x20001104u, 0x10F0001Cu, 4, 0x1000C000u}
		},
		0x00000000u,
	},
	{
		"B14 three passes, every partition over the bone limit: nothing drawn",
		{.geometry = true, .skin = true, .cast = 0x00000000u, .slot10 = 0x00000000u, .slot18 = 0x00000001u, .slot19 = {0x00000001u, 0x00000001u, 0x00000000u}, .slot15 = {0x1000D000u}, .maxBones = 1, .flags = 0x0123, .partitionCount = 2, .partitions = {{5, 2, 0, 0x10200000u}, {6, 3, 0, 0x10200010u}}, .records = {}},
		{
			{0x00560920u, 0x0076756Bu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20000A00u, 0x007675A4u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20000B00u, 0x007675CEu, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20001200u, 0x007675E2u, 0x10004000u, {}},
			{0x20000C00u, 0x00767612u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000002u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000003u}},
			{0x20001300u, 0x007677C6u, 0x10004000u, {}},
			{0x20000C00u, 0x00767612u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000002u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000003u}},
			{0x20001300u, 0x007677C6u, 0x10004000u, {}},
			{0x20000C00u, 0x00767612u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000002u}},
			{0x0060D0A0u, 0x0076766Fu, kAny, {0x00A88D90u, 0x00000003u}},
			{0x20001300u, 0x007677C6u, 0x10004000u, {}},
			{0x20001100u, 0x007677F0u, 0x10004000u, {0x10005000u, 0x10008000u, 0x10200000u, 0x10100000u, 0x10110000u, 0x1000B000u}},
			{0x20002100u, 0x00767804u, 0x10003000u, {0x00000000u}}
		},
		{
			{0x20000A04u, 0x10F00010u, 4, 0x1000C000u},
			{0x20000B04u, 0x10F00014u, 4, 0x1000C000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x007677B7u, 0x1000702Eu, 2, 0x00000000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x007677B7u, 0x1000702Eu, 2, 0x00000000u},
			{0x20000C04u, 0x10F00018u, 4, 0x1000C000u},
			{0x0076761Au, 0x10000658u, 4, 0x3F800000u},
			{0x00767623u, 0x1000065Cu, 4, 0x40000000u},
			{0x0076762Cu, 0x10000660u, 4, 0x40400000u},
			{0x00767635u, 0x10000664u, 4, 0x7FC00001u},
			{0x007677B7u, 0x1000702Eu, 2, 0x00000000u},
			{0x20001104u, 0x10F0001Cu, 4, 0x1000C000u}
		},
		0x00000000u,
	},
};

std::string Hex(std::uint32_t value) {
	char text[16];
	std::snprintf(text, sizeof text, "%08X", value);
	return text;
}

std::string Describe(const TracedCall& c) {
	std::string text = Hex(c.callee) + " from " + Hex(c.from) + " ecx " + Hex(c.ecx) + " (";
	for (std::uint32_t a : c.args) {
		text += " " + Hex(a);
	}
	return text + " )";
}

std::string Describe(const Write& w) {
	return "@" + Hex(w.at) + " [" + Hex(w.address) + "] " + std::to_string(w.size) + " = " + Hex(w.value);
}

template <class T>
bool SameSequence(const std::vector<T>& rebuild, const std::vector<T>& original, const char* what) {
	const std::size_t n = std::max(rebuild.size(), original.size());
	for (std::size_t i = 0; i < n; ++i) {
		if (i >= rebuild.size() || i >= original.size() || !(rebuild[i] == original[i])) {
			std::printf("    %s differ at %zu: rebuild %s, original %s\n", what, i,
					i < rebuild.size() ? Describe(rebuild[i]).c_str() : "(none)",
					i < original.size() ? Describe(original[i]).c_str() : "(none)");
			return false;
		}
	}
	return true;
}

logic::DrawArguments ArgumentsOf(const CaseInput& in) {
	return logic::DrawArguments{kThis, in.geometry ? kGeometry : 0u, kData, in.skin ? kSkin : 0u, kWorld, kBound};
}

logic::Outcome ExpectedOutcome(const EmulatorCase& c) {
	if (c.calls.empty()) {
		return logic::Outcome::kNoSkin;
	}
	switch (c.calls.back().callee) {
	case SlotStub(10): return logic::Outcome::kPipelineRefused;
	case SlotStub(15): return logic::Outcome::kRecordMissing;
	default: return logic::Outcome::kClosed;
	}
}

void TestOneEyeAgainstTheOriginal() {
	for (const EmulatorCase& c : kCases) {
		Section(c.name);
		CasePorts ports(c.input, ArgumentsOf(c.input).skin);
		const logic::Outcome outcome = logic::DrawSkinnedPrimitive(ports, ArgumentsOf(c.input), EyePlan{1});
		Check(SameSequence(ports.calls, c.calls, "calls"), "the call sequence equals the original's trace");
		Check(SameSequence(ports.writes, c.writes, "writes"), "the writes equal the original's");
		Check(ports.lastEax == c.eax, "the original's EAX is what the last call left (no return value)");
		Check(outcome == ExpectedOutcome(c), "the outcome names the exit the original took");
		Check(ports.faults.empty(), "no port read outside the case's memory");
		Check(std::none_of(ports.calls.begin(), ports.calls.end(),
						  [](const TracedCall& t) { return t.callee == kEyeSwitch; }),
				"one eye: the eye switch is never called");
	}
}

// StereoDraw.h's rule applied to a one-eye sequence: after each partition's
// draws (the run of draws after the sub-object call, possibly empty) come
// eyeSwitch(1), the same draws again, eyeSwitch(0); the draws' count stores
// double the same way. Nothing else moves.
std::vector<TracedCall> TwoEyeCalls(const std::vector<TracedCall>& oneEye) {
	std::vector<TracedCall> out;
	for (std::size_t i = 0; i < oneEye.size(); ++i) {
		out.push_back(oneEye[i]);
		if (oneEye[i].callee != kSubObjectSlot1Stub) {
			continue;
		}
		std::vector<TracedCall> draws;
		while (i + 1 < oneEye.size() && oneEye[i + 1].callee == kDrawIndexedPrimitiveStub) {
			draws.push_back(oneEye[++i]);
		}
		out.insert(out.end(), draws.begin(), draws.end());
		out.push_back(TracedCall{kEyeSwitch, 0, kAny, {1}});
		out.insert(out.end(), draws.begin(), draws.end());
		out.push_back(TracedCall{kEyeSwitch, 0, kAny, {0}});
	}
	return out;
}

std::vector<Write> TwoEyeWrites(const std::vector<Write>& oneEye) {
	std::vector<Write> out;
	for (std::size_t i = 0; i < oneEye.size();) {
		if (oneEye[i].address != kLogDrawCount) {
			out.push_back(oneEye[i++]);
			continue;
		}
		std::vector<Write> run;
		while (i < oneEye.size() && oneEye[i].address == kLogDrawCount) {
			run.push_back(oneEye[i++]);
		}
		out.insert(out.end(), run.begin(), run.end());
		out.insert(out.end(), run.begin(), run.end());
	}
	return out;
}

void TestTwoEyes() {
	for (const EmulatorCase& c : kCases) {
		Section((std::string("Two eyes: ") + c.name).c_str());
		CasePorts ports(c.input, ArgumentsOf(c.input).skin);
		const logic::Outcome outcome = logic::DrawSkinnedPrimitive(ports, ArgumentsOf(c.input), EyePlan{2});
		Check(SameSequence(ports.calls, TwoEyeCalls(c.calls), "calls"),
				"the original's sequence with each partition's draws doubled around the eye switch");
		Check(SameSequence(ports.writes, TwoEyeWrites(c.writes), "writes"), "the same writes, the draw counts doubled");
		Check(outcome == ExpectedOutcome(c), "the same exit as one eye");
		Check(ports.faults.empty(), "no port read outside the case's memory");
	}

	Section("Two eyes: a partition without arrays still switches (the contract names no exception)");
	const EmulatorCase& gate = kCases[5];  // B8 case: partition 2's record has no arrays
	CasePorts ports(gate.input, kSkin);
	logic::DrawSkinnedPrimitive(ports, ArgumentsOf(gate.input), EyePlan{2});
	const auto switches = std::count_if(ports.calls.begin(), ports.calls.end(),
			[](const TracedCall& t) { return t.callee == kEyeSwitch; });
	Check(switches == 6, "three drawn partitions, two switches each");
}

} // namespace

int main() {
	TestDecisions();
	TestOneEyeAgainstTheOriginal();
	TestTwoEyes();
	return Failures();
}
