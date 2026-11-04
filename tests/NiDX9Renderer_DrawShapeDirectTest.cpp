// NiDX9Renderer::DrawShapeDirect 0x007694F0 (proposed name; vtable 0x00A88EA4
// slot 90) rebuilt as DrawShapeDirect(ports, geometry, plan)
// (NiDX9Renderer_DrawShapeDirectLogic.h).
//
// Three parts:
//
// 1. The decisions as pure functions, both outcomes of every branch of the
//    map (re/functions/0x007694F0.md B1..B14), the boundaries (AX/AL only,
//    bone count equal to the limit, strip length 0/1/2, start index wrap)
//    included.
//
// 2. The differential gate, layer 1 (docs/re-workflow.md, "the gate outside
//    the workflow"). The original ran in the emulator
//    (tools/difftest/emulate.py) on the cases of
//    tools/difftest/cases/0x007694F0.json; its result is
//    tools/difftest/expected/0x007694F0.json (2026-11-04: every case returned
//    to its sentinel, no emulator error, stack_delta +4 - the RET 0x4 popping
//    the one stack argument, the balanced result for this callee-cleaned
//    function). Every callee is a traced stub there; the trace shows six
//    stack dwords per call, so the DrawIndexedPrimitive stub is raw code that
//    also stores its 7th dword (primCount) to a log (the case file's "note").
//    The trace (callee, return address, ECX where it is an input, the
//    arguments the callee takes), the writes (site, address, size, value) and
//    EAX at the exit are transcribed below, literally, per case (measured).
//    Here the same inputs are answered by fake ports that hand out the case's
//    addresses as handles and record each port call in the emulator's terms;
//    both sequences must be equal.
//
// 3. The two-eye plan has no original to compare with. Its expectation is
//    derived from the same transcription by StereoDrawDirect.h's rule: the
//    whole arm's draws, or each partition's draws, repeated between
//    eyeSwitch(1) and eyeSwitch(0) right after the first eye's, nothing else
//    moved.
//
// Flow coverage (branch -> emulator case, 1-based; plus the pure checks):
//   B1  suppress byte set / clear                 case 1 / all others; DrawSuppressed
//   B2  AX == 0 with upper bits set / AX != 0     case 2 / case 3 (0xFFFF0001); HasVertices
//   B3  AL == 0 (0xFFFFFF00, 0x100) / AL != 0     cases 3-6 / cases 7-11 (1, 0xFFFFFF01); DrawsBySkinPartition
//   B4  zero partitions / some                    case 7 / cases 8-11; PartitionLoopRuns
//   B5  custom shader / default shader            cases 8, 11 / cases 9, 10
//   B6  bones above / equal / below the limit     cases 9, 10 / case 9 / case 9; SkippedByBoneGate
//   B7  partition record without / with arrays    case 9 (partition 2) / cases 8, 9, 11; HasArrays
//   B8  lengths array / record +0x3C              cases 9, 11 / cases 8, 9, 11; PrimitiveCount
//   B9  one array / several                       case 8 / cases 9, 11; ArrayLoopContinues
//   B10 one partition / several                   case 8 / cases 9-11; MorePartitions
//   B11 no index buffer / indexed                 case 3 / cases 4-6; IsIndexed
//   B12 indexed without / with arrays             case 4 / cases 5, 6
//   B13 lengths (5, 1, 2: the 1 wraps) / +0x3C    case 5 / case 6
//   B14 three / two arrays                        case 5 / case 6
//   C5  slot 18's result ignored (0)              case 4
//   C15 primitive type 4 / 5; partition 0 read with a count of 0   cases 8, 11 / cases 9, 11; case 7
//   C20 flags clear at each of the three sites, top nibble kept    cases 3-11
//   C3/C12 the faults (null cast, null whole record): no refusal, the same
//       null handle reaches the same port (outside the emulator, which
//       cannot return from a fault): TestPreconditions
//
// What it cannot see: what the callees do (they are stubs), the stack locals
// (the a1 slot reused as the partition cursor, C21), and the original's
// faults themselves.

#include "Check.h"
#include "engine/render/NiDX9Renderer_DrawShapeDirect.h"
#include "engine/render/NiDX9Renderer_DrawShapeDirectLogic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace logic = obng::engine::render::draw_shape_direct;
namespace eng = obng::engine::render::draw_shape_direct::engine;
using logic::Handle;
using logic::EyePlan;

namespace {

// ---- Part 1: the decisions -------------------------------------------------------------

void TestDecisions() {
	Section("B1: any non-zero suppress byte returns at once");
	Check(!logic::DrawSuppressed(0), "byte 0 -> draw");
	Check(logic::DrawSuppressed(1), "byte 1 -> return (0x00769501 JNZ)");
	Check(logic::DrawSuppressed(0x80), "byte 0x80 -> return (CMP byte, unsigned irrelevant)");

	Section("B2: only AX of the vertex count call counts");
	Check(!logic::HasVertices(0), "0 -> return");
	Check(!logic::HasVertices(0x00010000u), "0x00010000 -> return (TEST AX,AX)");
	Check(logic::HasVertices(1), "1 -> draw");
	Check(logic::HasVertices(0xFFFF0001u), "0xFFFF0001 -> draw");
	Check(logic::HasVertices(0x8000u), "0x8000 -> draw (a negative short is non-zero)");

	Section("B3: only AL of 0x00768890's result decides");
	Check(!logic::DrawsBySkinPartition(0), "0 -> whole");
	Check(!logic::DrawsBySkinPartition(0xFFFFFF00u), "0xFFFFFF00 -> whole (MOV BL,AL)");
	Check(!logic::DrawsBySkinPartition(0x100u), "0x100 -> whole");
	Check(logic::DrawsBySkinPartition(1), "1 -> skinned");
	Check(logic::DrawsBySkinPartition(0xFFFFFF01u), "0xFFFFFF01 -> skinned");

	Section("C15: partition 0's strip count chooses the primitive type");
	Check(logic::PrimitiveTypeFor(0) == 4, "0 strips -> D3DPT_TRIANGLELIST 4");
	Check(logic::PrimitiveTypeFor(1) == 5, "1 strip -> D3DPT_TRIANGLESTRIP 5");
	Check(logic::PrimitiveTypeFor(0xFFFF) == 5, "0xFFFF strips -> 5 (any non-zero word)");

	Section("B4: the partition loop runs unless the count is 0");
	Check(!logic::PartitionLoopRuns(0), "0 partitions -> flags clear");
	Check(logic::PartitionLoopRuns(1), "1 partition -> loop");
	Check(logic::PartitionLoopRuns(0xFFFFFFFFu), "0xFFFFFFFF -> loop (JBE, unsigned)");

	Section("B5/B6: only the default shader is bone-gated, unsigned, equal passes");
	constexpr Handle kDefault = 0x10004000u;
	constexpr Handle kCustom = 0x10006000u;
	Check(!logic::SkippedByBoneGate(kCustom, kDefault, 60, 4), "custom shader, 60 bones > 4 -> drawn (B5)");
	Check(logic::SkippedByBoneGate(kDefault, kDefault, 21, 20), "default, 21 > 20 -> skipped (B6)");
	Check(!logic::SkippedByBoneGate(kDefault, kDefault, 20, 20), "default, 20 == 20 -> drawn (JBE)");
	Check(!logic::SkippedByBoneGate(kDefault, kDefault, 19, 20), "default, 19 < 20 -> drawn");
	Check(!logic::SkippedByBoneGate(kDefault, kDefault, 0xFFFF, 0xFFFFFFFFu), "u16 max against a u32 limit -> drawn");
	Check(logic::SkippedByBoneGate(kDefault, kDefault, 0x8000, 0x7FFF), "0x8000 > 0x7FFF -> skipped (MOVZX, unsigned)");
	Check(logic::SkippedByBoneGate(kDefault, kDefault, 1, 0), "a limit of 0 skips one bone");

	Section("B7/B12: no array, no draw; B9/B14: the array loop, unsigned");
	Check(!logic::HasArrays(0), "0 arrays -> no draw");
	Check(logic::HasArrays(1), "1 array -> draw");
	Check(logic::HasArrays(0x80000000u), "0x80000000 arrays -> draw (JBE, unsigned)");
	Check(!logic::ArrayLoopContinues(1, 1), "index 1 of 1 -> stop");
	Check(logic::ArrayLoopContinues(1, 2), "index 1 of 2 -> next");
	Check(logic::ArrayLoopContinues(1, 0xFFFFFFFFu), "count 0xFFFFFFFF -> next (JC, unsigned)");

	Section("B8/B13: the primitive count, list or strip, the 32-bit wrap below 2");
	Check(logic::PrimitiveCount(false, 0, 0x99u) == 0x99u, "no lengths -> the record's +0x3C");
	Check(logic::PrimitiveCount(true, 5, 0x99u) == 3u, "strip of 5 indices -> 3 triangles");
	Check(logic::PrimitiveCount(true, 2, 0) == 0u, "strip of 2 -> 0");
	Check(logic::PrimitiveCount(true, 1, 0) == 0xFFFFFFFFu, "strip of 1 -> 0xFFFFFFFF (SUB 2 wraps)");
	Check(logic::PrimitiveCount(true, 0, 0) == 0xFFFFFFFEu, "strip of 0 -> 0xFFFFFFFE");
	Check(logic::PrimitiveCount(true, 0xFFFF, 0) == 0xFFFDu, "u16 max zero-extended -> 0xFFFD");

	Section("C19: the next start index is start + count + 2, modulo 2^32");
	Check(logic::NextStartIndex(0, 3) == 5u, "0 + 3 + 2 = 5");
	Check(logic::NextStartIndex(5, 0xFFFFFFFFu) == 6u, "the wrapped count of a 1-index strip adds 1");
	Check(logic::NextStartIndex(0xFFFFFFFFu, 0) == 1u, "start wraps");

	Section("B10: the partitions left after the decrement");
	Check(!logic::MorePartitions(0), "0 left -> flags clear");
	Check(logic::MorePartitions(1), "1 left -> next partition");
	Check(logic::MorePartitions(0xFFFFFFFFu), "0xFFFFFFFF left -> next (JNZ)");

	Section("B11: an index buffer means indexed draws");
	Check(!logic::IsIndexed(0), "0 -> DrawPrimitive");
	Check(logic::IsIndexed(1), "1 -> DrawIndexedPrimitive");

	Section("C20: the flags clear keeps the top nibble");
	Check(logic::ClearedFlags(0xABCD) == 0xA000, "0xABCD -> 0xA000");
	Check(logic::ClearedFlags(0x0FFF) == 0x0000, "0x0FFF -> 0");
	Check(logic::ClearedFlags(0xF000) == 0xF000, "0xF000 unchanged");
}

// ---- Part 2: the differential gate -----------------------------------------------------

constexpr std::uint32_t kAny = 0xFFFF0002u;       // ECX is not an input of this callee (cdecl, COM stdcall)
constexpr std::uint32_t kEyeSwitch = 0xE7E50000u;  // the eye-switch port (no original counterpart)
constexpr std::uint32_t kCallerEax = 0xCAFE0001u;  // EAX at entry in every case ("regs")

// The synthetic memory of the cases (tools/difftest/cases/0x007694F0.json, "note").
constexpr Handle kThis = 0x10000000u;
constexpr Handle kDevice = 0x10002000u;
constexpr Handle kSubObject = 0x10014000u;
constexpr Handle kDefaultShader = 0x10004000u;
constexpr Handle kGeometry = 0x10005000u;
constexpr Handle kCandidate = 0x10005800u;
constexpr Handle kData = 0x10007000u;
constexpr Handle kSkin = 0x10008000u;
constexpr Handle kSkinPartition = 0x10009000u;
constexpr Handle kPartitions = 0x1000A000u;
constexpr Handle kRecords = 0x1000D000u;  // one per 0x100
constexpr Handle kLengths = 0x1000E000u;  // one array per 0x40

// The stubs (vtable targets and direct callees) and the log the DIP stub writes.
constexpr std::uint32_t kVertexCountStub = 0x20003000u;
constexpr std::uint32_t kSlot18Stub = 0x20001200u;
constexpr std::uint32_t kSubObjectSlot1Stub = 0x20002000u;
constexpr std::uint32_t kDrawIndexedPrimitiveStub = 0x20002200u;
constexpr std::uint32_t kDrawPrimitiveStub = 0x20002300u;
constexpr std::uint32_t kLogDrawCount = 0x10F00000u;

// The flags clear has one port but three sites in the original; the fake
// records it with this placeholder and the test puts in the site the
// rebuild's outcome names (eng::FlagsClearSite), so a wrong outcome shows as
// a wrong write.
constexpr std::uint32_t kPendingSite = 0xFFFF0003u;

struct PartitionInput {
	std::uint16_t vertices;
	std::uint16_t bones;
	std::uint16_t strips;
	std::uint32_t record;  // index into the case's records
};

struct RecordInput {
	std::uint32_t vertexCount;  // +0x14
	std::uint32_t indexBuffer;  // +0x30
	std::uint32_t baseVertex;   // +0x34
	std::uint32_t primType;     // +0x38
	std::uint32_t triangles;    // +0x3C
	std::uint32_t arrays;       // +0x44
	bool hasLengths;            // +0x48 non-null
	std::vector<std::uint16_t> lengths;
};

struct CaseInput {
	std::uint8_t suppress = 0;
	std::uint32_t vertexEax = 3;
	std::uint32_t skinnedEax = 0;
	std::uint32_t cast = 0;
	std::uint32_t slot18 = 1;
	std::uint32_t maxBones = 4;
	std::uint16_t flags = 0xABCD;
	bool hasSkin = true;
	int wholeRecord = -1;  // index into records, -1 for data+0x38 = 0
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

// The ports, answering from a case's inputs and recording each call and each
// store in the emulator's terms. Handles are the case's addresses. lastEax
// follows what each stub leaves in EAX (a plain stub its value; the DIP stub
// the primCount it stored) and the MOV EAX,[ESP+0x20] before each flags clear
// (0x0076964A, 0x007696B4, 0x007696E6), so the original's EAX at the exit can
// be compared: the function returns nothing of its own.
struct CasePorts {
	explicit CasePorts(const CaseInput& caseInput) : in(caseInput), flags(caseInput.flags) {}

	const CaseInput& in;
	std::uint16_t flags;
	std::uint32_t lastEax = kCallerEax;
	std::vector<TracedCall> calls;
	std::vector<Write> writes;
	std::vector<std::string> faults;  // a port asked about memory the case does not hold

	void Emit(std::uint32_t callee, std::uint32_t from, std::uint32_t ecx, std::vector<std::uint32_t> args) {
		calls.push_back(TracedCall{callee, from, ecx, std::move(args)});
	}
	const PartitionInput& PartitionOf(Handle partition) {
		const std::uint32_t index = (partition - kPartitions) / eng::kPartitionStride;
		if ((partition - kPartitions) % eng::kPartitionStride != 0 || index >= in.partitions.size()) {
			faults.push_back("a partition outside the case's array");
			static const PartitionInput none{};
			return none;
		}
		return in.partitions[index];
	}
	const RecordInput& RecordOf(Handle record) {
		const std::uint32_t index = (record - kRecords) / 0x100u;
		if ((record - kRecords) % 0x100u != 0 || index >= in.records.size()) {
			faults.push_back("a record outside the case's records");
			static const RecordInput none{};
			return none;
		}
		return in.records[index];
	}
	void Expect(Handle got, Handle mapped, const char* what) {
		if (got != mapped) {
			faults.push_back(what);
		}
	}

	// Reads.
	std::uint8_t drawSuppressed() { return in.suppress; }
	Handle geometryData(Handle g) {
		Expect(g, kGeometry, "data of an unmapped geometry");
		return kData;
	}
	Handle geometrySkin(Handle g) {
		Expect(g, kGeometry, "skin of an unmapped geometry");
		return in.hasSkin ? kSkin : 0u;
	}
	Handle geometryShaderCandidate(Handle g) {
		Expect(g, kGeometry, "candidate of an unmapped geometry");
		return kCandidate;
	}
	Handle dataRecord(Handle d) {
		Expect(d, kData, "record of an unmapped data");
		return in.wholeRecord < 0 ? 0u : kRecords + 0x100u * static_cast<std::uint32_t>(in.wholeRecord);
	}
	std::uint16_t dataFlags(Handle d) {
		Expect(d, kData, "flags of an unmapped data");
		return flags;
	}
	Handle defaultShader() { return kDefaultShader; }
	std::uint32_t maxBonesWithoutShader() { return in.maxBones; }
	Handle device() { return kDevice; }
	Handle skinPartition(Handle s) {
		Expect(s, kSkin, "partition of an unmapped skin");
		return kSkinPartition;
	}
	std::uint32_t partitionCount(Handle sp) {
		Expect(sp, kSkinPartition, "count of an unmapped skin partition");
		return in.partitionCount;
	}
	Handle partitions(Handle sp) {
		Expect(sp, kSkinPartition, "array of an unmapped skin partition");
		return kPartitions;
	}
	Handle partitionAt(Handle array, std::uint32_t index) { return array + eng::kPartitionStride * index; }
	std::uint16_t stripCount(Handle p) { return PartitionOf(p).strips; }
	std::uint16_t boneCount(Handle p) { return PartitionOf(p).bones; }
	std::uint16_t vertexCount(Handle p) { return PartitionOf(p).vertices; }
	Handle partitionRecord(Handle p) { return kRecords + 0x100u * PartitionOf(p).record; }
	std::uint32_t recordVertexCount(Handle r) { return RecordOf(r).vertexCount; }
	std::uint32_t indexBuffer(Handle r) { return RecordOf(r).indexBuffer; }
	std::uint32_t baseVertexIndex(Handle r) { return RecordOf(r).baseVertex; }
	std::uint32_t primitiveType(Handle r) { return RecordOf(r).primType; }
	std::uint32_t triangleCount(Handle r) { return RecordOf(r).triangles; }
	std::uint32_t arrayCount(Handle r) { return RecordOf(r).arrays; }
	Handle arrayLengths(Handle r) {
		return RecordOf(r).hasLengths ? kLengths + 0x40u * ((r - kRecords) / 0x100u) : 0u;
	}
	std::uint16_t arrayLength(Handle lengths, std::uint32_t index) {
		const RecordInput& r = RecordOf(kRecords + 0x100u * ((lengths - kLengths) / 0x40u));
		if (index >= r.lengths.size()) {
			faults.push_back("a length past the case's array");
			return 0;
		}
		return r.lengths[index];
	}

	// Stores.
	void setDataFlags(Handle d, std::uint16_t value) {
		flags = value;
		lastEax = d;  // MOV EAX,[ESP+0x20] before the AND
		writes.push_back(Write{kPendingSite, d + 0x2Eu, 2, value});
	}

	// Calls.
	std::uint32_t dataVertexCount(Handle d) {
		Emit(kVertexCountStub, eng::kAfterDataVertexCount, d, {});
		return lastEax = in.vertexEax;
	}
	std::uint32_t isSkinned(Handle g, Handle shader) {
		Emit(eng::kIsSkinnedAddress, eng::kAfterIsSkinned, kThis, {g, shader});
		return lastEax = in.skinnedEax;
	}
	Handle castToShaderInterface(Handle object) {
		Emit(eng::kRttiCastAddress, eng::kAfterRttiCast, kAny, {eng::kNiRttiNiD3DShaderInterface, object});
		return lastEax = in.cast;
	}
	void shaderSlot18(Handle shader) {
		Emit(kSlot18Stub, eng::kAfterShaderSlot18, shader, {});
		lastEax = in.slot18;
	}
	void renderStateSubObjectSlot1() {
		Emit(kSubObjectSlot1Stub, eng::kAfterSubObjectSlot1, kSubObject, {});
		lastEax = 0;
	}
	void cannotRenderPartition(std::uint32_t bones) {
		Emit(eng::kDebugPrintAddress, eng::kAfterDebugPrint, kAny, {eng::kCannotRenderPartitionFormat, bones});
		lastEax = 0;
	}
	void DrawIndexed(std::uint32_t from, const logic::DrawIndexedCall& c) {
		Emit(kDrawIndexedPrimitiveStub, from, kAny,
				{c.device, c.primitiveType, c.baseVertexIndex, c.minVertexIndex, c.numVertices, c.startIndex});
		writes.push_back(Write{kDrawIndexedPrimitiveStub + 4u, kLogDrawCount, 4, c.primitiveCount});
		lastEax = c.primitiveCount;
	}
	void drawPartitionIndexed(const logic::DrawIndexedCall& c) {
		DrawIndexed(eng::kAfterSkinnedDrawIndexedPrimitive, c);
	}
	void drawWholeIndexed(const logic::DrawIndexedCall& c) { DrawIndexed(eng::kAfterWholeDrawIndexedPrimitive, c); }
	void drawPrimitive(const logic::DrawCall& c) {
		Emit(kDrawPrimitiveStub, eng::kAfterDrawPrimitive, kAny,
				{c.device, c.primitiveType, c.startVertex, c.primitiveCount});
		lastEax = 0;
	}
	void eyeSwitch(std::uint8_t eye) { Emit(kEyeSwitch, 0, kAny, {eye}); }
};
static_assert(logic::DrawShapeDirectPorts<CasePorts>);

// The original's results, transcribed from tools/difftest/expected/0x007694F0.json
// (measured). Inputs as in tools/difftest/cases/0x007694F0.json.
const EmulatorCase kCases[] = {
	{
		"B1 draw suppressed (byte +0x6F0 = 0x80): returns at once",
		{.suppress = 0x80, .vertexEax = 0x00000003u, .skinnedEax = 0x00000000u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0xABCD, .hasSkin = true, .wholeRecord = 0, .partitionCount = 0, .partitions = {}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, false, {}}}},
		{},
		{},
		0xCAFE0001u,
	},
	{
		"B2 vertex count AX = 0 (EAX 0x00010000): returns without a flags clear",
		{.suppress = 0x00, .vertexEax = 0x00010000u, .skinnedEax = 0x00000000u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0xABCD, .hasSkin = true, .wholeRecord = 0, .partitionCount = 0, .partitions = {}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}}
		},
		{},
		0x00010000u,
	},
	{
		"B3 whole (AL = 0 of 0xFFFFFF00); B11 no index buffer -> DrawPrimitive",
		{.suppress = 0x00, .vertexEax = 0xFFFF0001u, .skinnedEax = 0xFFFFFF00u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0x8FFF, .hasSkin = true, .wholeRecord = 0, .partitionCount = 0, .partitions = {}, .records = {{0x00000077u, 0x00000000u, 0x00000040u, 0x00000004u, 0x00000099u, 0x00000000u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x20002300u, 0x007696E6u, kAny, {0x10002000u, 0x00000004u, 0x00000040u, 0x00000099u}}
		},
		{
			{0x007696EAu, 0x1000702Eu, 2, 0x00008000u}
		},
		0x10007000u,
	},
	{
		"B11 indexed; B12 zero entries -> no draw; slot 18 result 0 ignored",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000000u, .cast = 0x10006000u, .slot18 = 0x00000000u, .maxBones = 4, .flags = 0xABCD, .hasSkin = true, .wholeRecord = 0, .partitionCount = 0, .partitions = {}, .records = {{0x00000077u, 0x10C00000u, 0x00000040u, 0x00000004u, 0x00000099u, 0x00000000u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}}
		},
		{
			{0x007696EAu, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B12 indexed; B13 length array (5, 1, 2: the 1 wraps); B14 three entries",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000000u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0x0123, .hasSkin = true, .wholeRecord = 0, .partitionCount = 0, .partitions = {}, .records = {{0x00000300u, 0x10C00000u, 0x00000012u, 0x00000005u, 0x00000099u, 0x00000003u, true, {5, 1, 2}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x20002200u, 0x007696A8u, kAny, {0x10002000u, 0x00000005u, 0x00000012u, 0x00000000u, 0x00000300u, 0x00000000u}},
			{0x20002200u, 0x007696A8u, kAny, {0x10002000u, 0x00000005u, 0x00000012u, 0x00000000u, 0x00000300u, 0x00000005u}},
			{0x20002200u, 0x007696A8u, kAny, {0x10002000u, 0x00000005u, 0x00000012u, 0x00000000u, 0x00000300u, 0x00000006u}}
		},
		{
			{0x20002204u, 0x10F00000u, 4, 0x00000003u},
			{0x20002204u, 0x10F00000u, 4, 0xFFFFFFFFu},
			{0x20002204u, 0x10F00000u, 4, 0x00000000u},
			{0x007696B8u, 0x1000702Eu, 2, 0x00000000u}
		},
		0x10007000u,
	},
	{
		"B13 no length array -> record +0x3C; B14 two entries",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000100u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0xABCD, .hasSkin = true, .wholeRecord = 1, .partitionCount = 0, .partitions = {}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, false, {}}, {0x00000055u, 0x10C00000u, 0xFFFFFFF0u, 0x00000004u, 0x00000021u, 0x00000002u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x20002200u, 0x007696A8u, kAny, {0x10002000u, 0x00000004u, 0xFFFFFFF0u, 0x00000000u, 0x00000055u, 0x00000000u}},
			{0x20002200u, 0x007696A8u, kAny, {0x10002000u, 0x00000004u, 0xFFFFFFF0u, 0x00000000u, 0x00000055u, 0x00000023u}}
		},
		{
			{0x20002204u, 0x10F00000u, 4, 0x00000021u},
			{0x20002204u, 0x10F00000u, 4, 0x00000021u},
			{0x007696B8u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B3 skinned (AL = 1); B4 zero partitions (partition 0 strips read anyway)",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000001u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0xABCD, .hasSkin = true, .wholeRecord = -1, .partitionCount = 0, .partitions = {{7, 9, 2, 0}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000001u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}}
		},
		{
			{0x007696EAu, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B5 custom shader draws 60 bones; type 4; B7 one entry; B8 record +0x3C; one partition",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0xFFFFFF01u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 4, .flags = 0xABCD, .hasSkin = true, .wholeRecord = -1, .partitionCount = 1, .partitions = {{65535, 60, 0, 0}}, .records = {{0x00000000u, 0x00000000u, 0x00000040u, 0x00000000u, 0x00000099u, 0x00000001u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000004u, 0x00000040u, 0x00000000u, 0x0000FFFFu, 0x00000000u}}
		},
		{
			{0x20002204u, 0x10F00000u, 4, 0x00000099u},
			{0x0076964Eu, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B6 default shader: 21 > 20 gated, 20 drawn (B7 no entries), 19 drawn (lengths), 0 drawn (two); type 5; B10 four partitions",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000001u, .cast = 0x10004000u, .slot18 = 0x00000001u, .maxBones = 20, .flags = 0xABCD, .hasSkin = true, .wholeRecord = -1, .partitionCount = 4, .partitions = {{100, 21, 3, 0}, {200, 20, 0, 1}, {300, 19, 1, 2}, {400, 0, 0, 3}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000001u, false, {}}, {0x00000000u, 0x00000000u, 0x00000020u, 0x00000000u, 0x00000055u, 0x00000000u, false, {}}, {0x00000000u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000000u, 0x00000003u, true, {5, 4, 7}}, {0x00000000u, 0x00000000u, 0x00000030u, 0x00000000u, 0x00000066u, 0x00000002u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10004000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x0060D0A0u, 0x007695D5u, kAny, {0x00A88D90u, 0x00000015u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000010u, 0x00000000u, 0x0000012Cu, 0x00000000u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000010u, 0x00000000u, 0x0000012Cu, 0x00000005u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000010u, 0x00000000u, 0x0000012Cu, 0x00000009u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000030u, 0x00000000u, 0x00000190u, 0x00000000u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000030u, 0x00000000u, 0x00000190u, 0x00000068u}}
		},
		{
			{0x20002204u, 0x10F00000u, 4, 0x00000003u},
			{0x20002204u, 0x10F00000u, 4, 0x00000002u},
			{0x20002204u, 0x10F00000u, 4, 0x00000005u},
			{0x20002204u, 0x10F00000u, 4, 0x00000066u},
			{0x20002204u, 0x10F00000u, 4, 0x00000066u},
			{0x0076964Eu, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B6 default shader, every partition gated (limit 0: 1 and 0xFFFF bones)",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000001u, .cast = 0x10004000u, .slot18 = 0x00000001u, .maxBones = 0, .flags = 0xFFFF, .hasSkin = true, .wholeRecord = -1, .partitionCount = 2, .partitions = {{10, 1, 0, 0}, {20, 65535, 0, 0}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000001u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10004000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x0060D0A0u, 0x007695D5u, kAny, {0x00A88D90u, 0x00000001u}},
			{0x0060D0A0u, 0x007695D5u, kAny, {0x00A88D90u, 0x0000FFFFu}}
		},
		{
			{0x0076964Eu, 0x1000702Eu, 2, 0x0000F000u}
		},
		0x10007000u,
	},
	{
		"B5 custom shader, two partitions with two entries each (lengths 2 and 0 wrap)",
		{.suppress = 0x00, .vertexEax = 0x00000003u, .skinnedEax = 0x00000001u, .cast = 0x10006000u, .slot18 = 0x00000001u, .maxBones = 0, .flags = 0xABCD, .hasSkin = true, .wholeRecord = -1, .partitionCount = 2, .partitions = {{16, 30, 1, 0}, {32, 40, 0, 1}}, .records = {{0x00000000u, 0x00000000u, 0x00000100u, 0x00000000u, 0x00000000u, 0x00000002u, true, {2, 0}}, {0x00000000u, 0x00000000u, 0x00000200u, 0x00000000u, 0x00000007u, 0x00000002u, false, {}}}},
		{
			{0x20003000u, 0x0076951Du, 0x10007000u, {}},
			{0x00768890u, 0x0076953Cu, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x00769558u, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769568u, 0x10006000u, {}},
			{0x20002000u, 0x0076957Bu, 0x10014000u, {}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000100u, 0x00000000u, 0x00000010u, 0x00000000u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000100u, 0x00000000u, 0x00000010u, 0x00000002u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000200u, 0x00000000u, 0x00000020u, 0x00000000u}},
			{0x20002200u, 0x00769624u, kAny, {0x10002000u, 0x00000005u, 0x00000200u, 0x00000000u, 0x00000020u, 0x00000009u}}
		},
		{
			{0x20002204u, 0x10F00000u, 4, 0x00000000u},
			{0x20002204u, 0x10F00000u, 4, 0xFFFFFFFEu},
			{0x20002204u, 0x10F00000u, 4, 0x00000007u},
			{0x20002204u, 0x10F00000u, 4, 0x00000007u},
			{0x0076964Eu, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
};

// Per case (same order): how many eyeSwitch(1)/(0) pairs the two-eye plan
// adds, derived from the case's input by StereoDrawDirect.h's rule - one for
// the whole arm, one per partition that passes the bone gate, none on B1/B2.
constexpr int kSwitchPairs[] = {
	0,  // B1
	0,  // B2
	1,  // whole, DrawPrimitive
	1,  // whole, indexed, no arrays (the pair frames no draw)
	1,  // whole, three arrays
	1,  // whole, two arrays
	0,  // skinned, zero partitions
	1,  // one partition
	3,  // four partitions, the first gated (the second has no arrays)
	0,  // every partition gated
	2,  // two partitions
};
static_assert(std::size(kSwitchPairs) == std::size(kCases), "one count per case");

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

std::string Describe(std::uint32_t value) {
	return Hex(value);
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

// The fake's writes with the flags clear's placeholder replaced by the site
// the outcome names.
std::vector<Write> SitedWrites(const std::vector<Write>& writes, logic::Outcome outcome) {
	std::vector<Write> out = writes;
	for (Write& w : out) {
		if (w.at == kPendingSite) {
			w.at = eng::FlagsClearSite(outcome);
		}
	}
	return out;
}

// The original's exit, read from its own results: the flags clear site it
// wrote at (and for the shared site whether it called DrawPrimitive), or for
// the two exits without a clear whether it called anything.
logic::Outcome OriginalOutcome(const EmulatorCase& c) {
	for (const Write& w : c.writes) {
		if (w.at == eng::kFlagsClearSkinned) {
			return logic::Outcome::kSkinned;
		}
		if (w.at == eng::kFlagsClearWholeIndexed) {
			return logic::Outcome::kWholeIndexed;
		}
		if (w.at == eng::kFlagsClearShared) {
			// B4, B11 or B12: told apart by what the original read and called -
			// the skinned decision's AL (the case input the stub returned) and
			// whether DrawPrimitive ran.
			if ((c.input.skinnedEax & 0xFFu) != 0) {
				return logic::Outcome::kSkinnedNoPartitions;
			}
			const bool drawPrimitive = std::any_of(c.calls.begin(), c.calls.end(),
					[](const TracedCall& t) { return t.callee == kDrawPrimitiveStub; });
			return drawPrimitive ? logic::Outcome::kWholeDrawPrimitive : logic::Outcome::kWholeNoArrays;
		}
	}
	return c.calls.empty() ? logic::Outcome::kSuppressed : logic::Outcome::kNoVertices;
}

void TestFlagsClearSites() {
	Section("The exits and their flags clear sites (asm.s jumps into 0x0076964A / 0x007696B4 / 0x007696E6)");
	Check(eng::FlagsClearSite(logic::Outcome::kSuppressed) == 0, "B1: no clear");
	Check(eng::FlagsClearSite(logic::Outcome::kNoVertices) == 0, "B2: no clear");
	Check(eng::FlagsClearSite(logic::Outcome::kSkinned) == 0x0076964Eu, "skinned, B10 loop end -> 0x0076964E");
	Check(eng::FlagsClearSite(logic::Outcome::kSkinnedNoPartitions) == 0x007696EAu,
			"skinned, B4 zero partitions -> 0x007696EA (JBE 0x007696E6, the shared exit)");
	Check(eng::FlagsClearSite(logic::Outcome::kWholeIndexed) == 0x007696B8u, "whole indexed -> 0x007696B8");
	Check(eng::FlagsClearSite(logic::Outcome::kWholeDrawPrimitive) == 0x007696EAu, "DrawPrimitive -> 0x007696EA");
	Check(eng::FlagsClearSite(logic::Outcome::kWholeNoArrays) == 0x007696EAu, "no arrays -> 0x007696EA");
}

void TestOneEyeAgainstTheOriginal() {
	for (const EmulatorCase& c : kCases) {
		Section(c.name);
		CasePorts ports(c.input);
		const logic::Outcome outcome = logic::DrawShapeDirect(ports, kGeometry, EyePlan{1});
		Check(SameSequence(ports.calls, c.calls, "calls"), "the call sequence equals the original's trace");
		Check(SameSequence(SitedWrites(ports.writes, outcome), c.writes, "writes"),
				"the writes equal the original's, the flags clear at the original's site");
		Check(ports.lastEax == c.eax, "the original's EAX at the exit (no return value)");
		Check(outcome == OriginalOutcome(c), "the outcome names the exit the original took");
		Check(ports.faults.empty(), "no port read outside the case's memory");
		Check(std::none_of(ports.calls.begin(), ports.calls.end(),
						  [](const TracedCall& t) { return t.callee == kEyeSwitch; }),
				"one eye: the eye switch is never called");
	}
}

bool IsDraw(const TracedCall& t) {
	return t.callee == kDrawIndexedPrimitiveStub || t.callee == kDrawPrimitiveStub;
}

bool IsSwitch(const TracedCall& t, std::uint32_t eye) {
	return t.callee == kEyeSwitch && t.args.size() == 1 && t.args[0] == eye;
}

// StereoDrawDirect.h's rule checked on a two-eye sequence: every
// eyeSwitch(1) .. eyeSwitch(0) bracket holds exactly the run of draws that
// directly precedes it (possibly empty), and the sequence without the
// brackets is the one-eye sequence (returned in firstEye). `pairs` counts
// the brackets.
bool SplitTwoEyes(const std::vector<TracedCall>& twoEye, std::vector<TracedCall>& firstEye, int& pairs) {
	pairs = 0;
	std::size_t floor = 0;  // the first-eye calls before this index belong to an earlier bracket's run
	for (std::size_t i = 0; i < twoEye.size(); ++i) {
		if (IsSwitch(twoEye[i], 0)) {
			std::printf("    eyeSwitch(0) without eyeSwitch(1) at %zu\n", i);
			return false;
		}
		if (!IsSwitch(twoEye[i], 1)) {
			firstEye.push_back(twoEye[i]);
			continue;
		}
		std::size_t run = 0;
		while (firstEye.size() - run > floor && IsDraw(firstEye[firstEye.size() - 1 - run])) {
			++run;
		}
		std::size_t j = i + 1;
		std::size_t k = firstEye.size() - run;
		while (j < twoEye.size() && !IsSwitch(twoEye[j], 0)) {
			if (k >= firstEye.size() || !(twoEye[j] == firstEye[k])) {
				std::printf("    bracket at %zu: call %zu is not the first eye's draw\n", i, j);
				return false;
			}
			++j;
			++k;
		}
		if (j >= twoEye.size() || k != firstEye.size()) {
			std::printf("    bracket at %zu: not closed, or fewer draws than the first eye's run\n", i);
			return false;
		}
		++pairs;
		floor = firstEye.size();
		i = j;
	}
	return true;
}

void TestTwoEyes() {
	for (std::size_t n = 0; n < std::size(kCases); ++n) {
		const EmulatorCase& c = kCases[n];
		Section((std::string("Two eyes: ") + c.name).c_str());
		CasePorts ports(c.input);
		const logic::Outcome outcome = logic::DrawShapeDirect(ports, kGeometry, EyePlan{2});

		std::vector<TracedCall> firstEye;
		int pairs = 0;
		Check(SplitTwoEyes(ports.calls, firstEye, pairs),
				"each eyeSwitch(1)..(0) repeats exactly the draws right before it");
		Check(SameSequence(firstEye, c.calls, "calls"), "without the brackets: the original's sequence");
		Check(pairs == kSwitchPairs[n], "one bracket for the whole arm / per partition past the bone gate");

		// The writes: the DIP log doubles with the bracketed DIPs, everything else
		// is the original's. The DIP log's values follow the draws in call order.
		std::vector<Write> others;
		std::vector<std::uint32_t> counts;
		for (const Write& w : SitedWrites(ports.writes, outcome)) {
			if (w.address == kLogDrawCount) {
				counts.push_back(w.value);
			} else {
				others.push_back(w);
			}
		}
		std::vector<Write> originalOthers;
		std::vector<std::uint32_t> originalCounts;
		for (const Write& w : c.writes) {
			if (w.address == kLogDrawCount) {
				originalCounts.push_back(w.value);
			} else {
				originalOthers.push_back(w);
			}
		}
		Check(SameSequence(others, originalOthers, "writes"), "the other writes are the original's");
		std::vector<std::uint32_t> expectedCounts;
		{
			std::size_t next = 0;
			std::vector<std::uint32_t> run;
			std::size_t inBracket = 0;
			bool bracket = false;
			for (const TracedCall& t : ports.calls) {
				if (IsSwitch(t, 1)) {
					bracket = true;
					inBracket = 0;
				} else if (IsSwitch(t, 0)) {
					bracket = false;
					run.clear();
				} else if (t.callee == kDrawIndexedPrimitiveStub) {
					if (bracket) {
						expectedCounts.push_back(inBracket < run.size() ? run[inBracket] : 0xDEADu);
						++inBracket;
					} else {
						const std::uint32_t value = next < originalCounts.size() ? originalCounts[next] : 0xDEADu;
						++next;
						run.push_back(value);
						expectedCounts.push_back(value);
					}
				} else if (t.callee != kDrawPrimitiveStub) {
					run.clear();
				}
			}
		}
		Check(SameSequence(counts, expectedCounts, "draw counts"), "each bracketed DIP repeats its first-eye count");
		Check(outcome == OriginalOutcome(c), "the same exit as one eye");
		Check(ports.faults.empty(), "no port read outside the case's memory");
	}
}

// C3/C4 and C12: the original faults on a null cast result (0x00769558) and on
// a null whole-arm record (0x0076965E); the emulator cannot return from
// either, so they have no transcribed case. The rebuild's contract is the
// original's: no test, no refusal - the same null handle reaches the same
// port, which faults where engine memory backs it.
void TestPreconditions() {
	Section("C3/C4: a null cast result goes to slot 18 unchecked (the original faults at 0x00769558)");
	{
		CaseInput in = kCases[7].input;  // one partition, custom shader
		in.cast = 0;
		CasePorts ports(in);
		const logic::Outcome outcome = logic::DrawShapeDirect(ports, kGeometry, EyePlan{1});
		const auto slot18 = std::find_if(ports.calls.begin(), ports.calls.end(),
				[](const TracedCall& t) { return t.callee == kSlot18Stub; });
		Check(slot18 != ports.calls.end() && slot18->ecx == 0, "slot 18 is called with shader handle 0");
		Check(outcome == logic::Outcome::kSkinned, "no refusal exit: the rebuild goes on where the port lets it");
	}
	Section("C12: a null whole-arm record goes to the record reads unchecked (the original faults at 0x0076965E)");
	{
		CaseInput in = kCases[2].input;  // whole, DrawPrimitive
		in.wholeRecord = -1;
		CasePorts ports(in);
		logic::DrawShapeDirect(ports, kGeometry, EyePlan{1});
		Check(!ports.faults.empty() && ports.faults.front() == "a record outside the case's records",
				"the first read after the sub-object call is the null record's +0x30");
	}
}

} // namespace

int main() {
	TestDecisions();
	TestFlagsClearSites();
	TestOneEyeAgainstTheOriginal();
	TestTwoEyes();
	TestPreconditions();
	return Failures();
}
