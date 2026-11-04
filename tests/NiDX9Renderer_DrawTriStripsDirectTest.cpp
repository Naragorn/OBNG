// NiDX9Renderer::DrawTriStripsDirect 0x00769700 (proposed name, vtable
// 0x00A88EA4 slot 91) rebuilt as DrawTriStripsDirect(ports, args, plan)
// (NiDX9Renderer_DrawTriStripsDirectLogic.h).
//
// Four parts:
//
// 1. The decisions as pure functions, both outcomes of every branch of the
//    map (re/functions/0x00769700.md B1..B11b), with the boundaries (AX and
//    AL tests with the other bits set, bone count equal to the limit, strip
//    length 0/1/2, start index wrap, partition countdown) included.
//
// 2. The differential gate, layer 1 (docs/re-workflow.md, "the gate outside
//    the workflow"). The original ran in the emulator
//    (tools/difftest/emulate.py) on the cases of
//    tools/difftest/cases/0x00769700.json; its result is
//    tools/difftest/expected/0x00769700.json (2026-11-04: all eleven cases
//    returned to their sentinel, no emulator error, stack_delta +4 - the
//    RET 0x4 popping the geometry argument, the balanced result for this
//    callee-cleaned function). Every callee is a traced stub; the trace shows
//    six stack dwords per call, so the DrawIndexedPrimitive stub is raw code
//    that also stores its 7th argument (primCount) to a log at 0x10F00000.
//    The trace (callee, return address, ECX where it is an input, the
//    arguments the callee takes), the writes (site, address, size, value)
//    and EAX at the exit are transcribed below, literally, per case
//    (measured, in the emulator). Here the same inputs are answered by fake
//    ports that hand out the case's addresses as handles and record each
//    port call in the emulator's terms; both sequences must be equal.
//
// 3. C7 (read: no null test at 0x0076977E-0x00769780): a null cast result
//    is handed on to shader slot 18, where the original faults reading
//    [0]. There is no emulator case for it (the original does not return);
//    the expectation is derived from the read instructions.
//
// 4. The two-eye plan has no original to compare with. Its expectation is
//    derived from the same transcription by StereoDrawDirect.h's rule: the
//    draw(s) of each unit (the whole geometry; each partition the bone gate
//    lets through) repeated between eyeSwitch(1) and eyeSwitch(0), right
//    after the first eye's draw(s); nothing else moves.
//
// Flow coverage (branch -> case):
//   B1   gate byte set / clear                          case 1 / all others; DrawsSuppressed
//   B2   AX zero with EAX 0x00010000 / AX non-zero      case 2 / all others; HasVertices
//   B3   AL 0 with EAX 0xFFFFFF00 and 0x100 / AL 1 with EAX 0x12345601   cases 3, 5 / case 7; UsesPartitions
//   B4   zero partitions / some                         case 7 / cases 8-10; PartitionLoopRuns
//   B5   custom shader (not gated, bones not read) / default shader       case 8 / cases 9, 10; IsBoneGated
//   B6   21 > 20 skipped, 20 == 20 and 19 drawn; all skipped              cases 9, 10; ExceedsBoneLimit
//   B7   partition record without arrays / with                           case 9 (partition 2) / cases 8, 9
//   B8a  strip lengths / list count                     case 8 / case 9; PrimitiveCount
//   B8b  one array / two arrays                         case 9 (partition 1) / cases 8, 9 (partition 3)
//   B8c  one partition / several                        case 8 / cases 9, 10; NextCountdown
//   B9   no index buffer (DrawPrimitive) / indexed      cases 3, 11 / cases 4-6; IsIndexed
//   B10  zero arrays / some                             case 4 / cases 5, 6; HasArrays
//   B11a list count / strip lengths (a length of 1 wraps)                  case 5 / case 6
//   B11b one array / three arrays                       case 5 / case 6; LoopContinues
//   C5/C6 bound read before 0x00768890, stored after it (old value kept)   case 11
//   C9   record read from a second read of geometry+0xB4; flags clear on the first   case 11
//   C10  primitive type 4 and 5 in the skinned arm      cases 9 / 8
//   exits 0x007698A3 / 0x00769914 / 0x00769946        cases 8-10 / 5, 6 / 3, 4, 7, 11
//
// What it cannot see: what the callees do (they are stubs; 0x00768890's
// decision is an input here), the stack locals, and the faults the original
// takes on bad memory (null record, null partition record, null skin: the
// rebuild hands those handles to the ports exactly as the original
// dereferences them).

#include "Check.h"
#include "engine/render/NiDX9Renderer_DrawTriStripsDirect.h"
#include "engine/render/NiDX9Renderer_DrawTriStripsDirectLogic.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace logic = obng::engine::render::draw_tri_strips_direct;
namespace eng = obng::engine::render::draw_tri_strips_direct::engine;
using logic::Handle;
using logic::Outcome;
using obng::engine::render::stereo_draw_direct::EyePlan;

namespace {

// ---- Part 1: the decisions -------------------------------------------------------------

void TestDecisions() {
	Section("B1: any non-zero byte at renderer+0x6F0 suppresses the draw");
	Check(!logic::DrawsSuppressed(0), "0 -> draw (0x0076970D JNZ not taken)");
	Check(logic::DrawsSuppressed(1), "1 -> return");
	Check(logic::DrawsSuppressed(0x80), "0x80 -> return (CMP byte, any non-zero)");

	Section("B2: slot 20's result is tested as AX only (TEST AX,AX at 0x00769729)");
	Check(!logic::HasVertices(0), "0 -> return");
	Check(!logic::HasVertices(0x00010000u), "0x00010000 -> return (the high word is not tested)");
	Check(logic::HasVertices(1), "1 -> draw");
	Check(logic::HasVertices(0x8000u), "0x8000 -> draw (a 16-bit test, not a sign test)");

	Section("B3: 0x00768890's result is tested as AL only (stored as a byte at 0x00769775)");
	Check(!logic::UsesPartitions(0), "0 -> whole");
	Check(!logic::UsesPartitions(0xFFFFFF00u), "0xFFFFFF00 -> whole (only AL is kept)");
	Check(!logic::UsesPartitions(0x100u), "0x100 -> whole");
	Check(logic::UsesPartitions(1), "1 -> partitions");
	Check(logic::UsesPartitions(0x12345601u), "0x12345601 -> partitions");

	Section("C10: partition 0's strip count chooses the primitive type");
	Check(logic::PrimitiveTypeFor(0) == logic::kTriangleList, "0 -> D3DPT_TRIANGLELIST 4");
	Check(logic::PrimitiveTypeFor(1) == logic::kTriangleStrip, "1 -> D3DPT_TRIANGLESTRIP 5");
	Check(logic::PrimitiveTypeFor(0xFFFF) == 5u, "0xFFFF -> 5 (any non-zero word)");

	Section("B4: the partition loop runs unless the count is 0 (TEST ECX,ECX / JBE)");
	Check(!logic::PartitionLoopRuns(0), "0 -> shared exit");
	Check(logic::PartitionLoopRuns(1), "1 -> loop");
	Check(logic::PartitionLoopRuns(0xFFFFFFFFu), "0xFFFFFFFF -> loop (unsigned)");

	Section("B5/B6: only the default shader is bone-gated, the u16 count against the u32 limit, unsigned");
	constexpr Handle kDefault = 0x10004000u;
	constexpr Handle kCustom = 0x10006000u;
	Check(!logic::IsBoneGated(kCustom, kDefault), "custom shader -> drawn, bones not read (B5 JNZ)");
	Check(logic::IsBoneGated(kDefault, kDefault), "the default shader by identity -> bone test");
	Check(logic::ExceedsBoneLimit(21, 20), "21 > 20 -> skipped (B6)");
	Check(!logic::ExceedsBoneLimit(20, 20), "20 == 20 -> drawn (JBE)");
	Check(!logic::ExceedsBoneLimit(19, 20), "19 -> drawn");
	Check(!logic::ExceedsBoneLimit(0xFFFF, 0xFFFFFFFFu), "u16 max against a u32 limit -> drawn");
	Check(logic::ExceedsBoneLimit(1, 0), "a limit of 0 skips one bone");

	Section("B7/B10: arrays drawn unless the count is 0 (CMP / JBE)");
	Check(!logic::HasArrays(0), "0 -> no draw");
	Check(logic::HasArrays(1), "1 -> draw");
	Check(logic::HasArrays(0x80000000u), "0x80000000 -> draw (unsigned)");

	Section("B8a/B11a: the primitive count, list or strip, the 32-bit wrap below 2");
	Check(logic::PrimitiveCount(false, 0, 0x99u) == 0x99u, "no lengths -> the record's +0x3C");
	Check(logic::PrimitiveCount(true, 5, 0x99u) == 3u, "strip of 5 -> 3");
	Check(logic::PrimitiveCount(true, 2, 0) == 0u, "strip of 2 -> 0");
	Check(logic::PrimitiveCount(true, 1, 0) == 0xFFFFFFFFu, "strip of 1 -> 0xFFFFFFFF (SUB ESI,2 wraps)");
	Check(logic::PrimitiveCount(true, 0, 0) == 0xFFFFFFFEu, "strip of 0 -> 0xFFFFFFFE");
	Check(logic::PrimitiveCount(true, 0xFFFF, 0) == 0xFFFDu, "u16 max zero-extended -> 0xFFFD");

	Section("C14: the next start index is start + count + 2, modulo 2^32 (LEA)");
	Check(logic::NextStartIndex(0, 3) == 5u, "0 + 3 + 2");
	Check(logic::NextStartIndex(5, 0xFFFFFFFFu) == 6u, "the wrapped count of a 1-index strip adds 1");
	Check(logic::NextStartIndex(0xFFFFFFFFu, 0) == 1u, "start wraps");

	Section("B8b/B11b: the incremented index against the count, unsigned below (JC)");
	Check(!logic::LoopContinues(1, 1), "1 of 1 -> stop");
	Check(logic::LoopContinues(1, 2), "1 of 2 -> next");
	Check(logic::LoopContinues(1, 0xFFFFFFFFu), "count 0xFFFFFFFF -> next");

	Section("B8c: the partition countdown (SUB [ESP+0x14],1 / JNZ)");
	Check(logic::NextCountdown(1) == 0u, "1 -> 0: stop");
	Check(logic::NextCountdown(4) == 3u, "4 -> 3: next partition");
	Check(logic::NextCountdown(0xFFFFFFFFu) == 0xFFFFFFFEu, "unsigned");

	Section("B9: indexed when the record's +0x30 is non-zero");
	Check(!logic::IsIndexed(0), "0 -> DrawPrimitive");
	Check(logic::IsIndexed(0x10300000u), "non-zero -> DrawIndexedPrimitive");

	Section("Step 9: the flags clear keeps the top nibble (AND word,0xF000)");
	Check(logic::ClearedFlags(0xABCD) == 0xA000, "0xABCD -> 0xA000");
	Check(logic::ClearedFlags(0x0FFF) == 0x0000, "0x0FFF -> 0");
	Check(logic::ClearedFlags(0xF000) == 0xF000, "0xF000 unchanged");

	Section("The original's EAX at each exit (read: 0x0076994F, 0x0076994E, MOV EAX,[ESP+0x20])");
	Check(logic::ExitEax(logic::Result{Outcome::kSuppressed, 7, 0x10007000u}, 0xCAFE0001u) == 0xCAFE0001u,
			"B1: the caller's EAX");
	Check(logic::ExitEax(logic::Result{Outcome::kNoVertices, 0x00010000u, 0x10007000u}, 0xCAFE0001u) == 0x00010000u,
			"B2: slot 20's result");
	Check(logic::ExitEax(logic::Result{Outcome::kPartitionLoopExit, 3, 0x10007000u}, 0) == 0x10007000u,
			"0x007698A3: the data");
	Check(logic::ExitEax(logic::Result{Outcome::kArrayLoopExit, 3, 0x10007000u}, 0) == 0x10007000u, "0x00769914: the data");
	Check(logic::ExitEax(logic::Result{Outcome::kSharedDrawExit, 3, 0x10007000u}, 0) == 0x10007000u,
			"0x00769946: the data");
}

// ---- Part 2: the differential gate -----------------------------------------------------

constexpr std::uint32_t kAny = 0xFFFF0002u;       // ECX is not an input of this callee (cdecl, COM stdcall)
constexpr std::uint32_t kEyeSwitch = 0xE7E50000u;  // the eye-switch port (no original counterpart)
constexpr std::uint32_t kIncomingEax = 0xCAFE0001u;

// The synthetic memory of the cases (tools/difftest/cases/0x00769700.json, "note").
constexpr Handle kThis = 0x10000000u;
constexpr Handle kDevice = 0x10002000u;
constexpr Handle kSubObject = 0x10014000u;
constexpr Handle kDefaultShader = 0x10004000u;
constexpr Handle kGeometry = 0x10005000u;
constexpr Handle kCandidate = 0x10005800u;
constexpr Handle kCandidateRewritten = 0x10005900u;
constexpr Handle kData = 0x10007000u;
constexpr Handle kDataRewritten = 0x10007100u;
constexpr Handle kSkin = 0x10008000u;
constexpr Handle kSkinPartition = 0x10009000u;
constexpr Handle kPartitions = 0x1000A000u;
constexpr Handle kRecords = 0x1000D000u;  // one per 0x100
constexpr Handle kLengths = 0x1000E000u;  // one array per 0x40
constexpr std::uint32_t kBoundWords[4] = {0x3F800000u, 0x40000000u, 0x40400000u, 0x7FC00001u};
constexpr std::uint32_t kRewrittenBound0 = 0x41200000u;
constexpr std::uint16_t kRewrittenDataFlags = 0x5555;

// The stubs (vtable targets and replaced functions) and the log the DIP stub writes.
constexpr std::uint32_t kSlot18Stub = 0x20001200u;
constexpr std::uint32_t kSlot20Stub = 0x20001400u;
constexpr std::uint32_t kSubObjectSlot1Stub = 0x20002000u;
constexpr std::uint32_t kDrawIndexedPrimitiveStub = 0x20002200u;
constexpr std::uint32_t kDrawPrimitiveStub = 0x20002300u;
constexpr std::uint32_t kLogDrawCount = 0x10F00000u;
constexpr std::uint32_t kSlot18Eax = 0x5A5A0018u;
// The raw-code stub at 0x00768890 of case 11: three MOV dword [abs],imm32 (10 bytes each).
constexpr std::uint32_t kRewriteSites[3] = {0x00768890u, 0x0076889Au, 0x007688A4u};

struct PartitionInput {
	std::uint16_t vertices;
	std::uint16_t bones;
	std::uint16_t strips;
	std::uint32_t bufferData;
};

struct RecordInput {
	std::uint32_t indexBuffer;
	std::uint32_t vertexCount;
	std::uint32_t baseVertex;
	std::uint32_t primitiveType;
	std::uint32_t triangles;
	std::uint32_t arrays;
	bool hasLengths;
	std::vector<std::uint16_t> lengths;
};

struct CaseInput {
	std::uint8_t gate = 0;
	std::uint32_t vertexResult = 3;
	std::uint32_t selects = 0;
	bool rewritesGeometry = false;  // 0x00768890 is the raw-code stub that rewrites +0x20/+0xB4/+0xBC, returns 0
	std::uint32_t cast = 0;
	std::uint32_t maxBones = 4;
	std::uint16_t flags = 0xABCD;
	std::uint32_t partitionCount = 0;
	std::vector<PartitionInput> partitions;  // in memory; [0] exists even with a count of 0
	std::vector<RecordInput> records;        // record k at kRecords + 0x100*k; data's +0x38 is record 0, the rewritten data's record 1
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
// store in the emulator's terms. Handles are the case's addresses. The DIP
// stub's log store is recorded where its raw code makes it (stub + 4).
struct CasePorts {
	explicit CasePorts(const CaseInput& caseInput) : in(caseInput), flags(caseInput.flags) {}

	const CaseInput& in;
	std::uint16_t flags;
	std::uint16_t rewrittenFlags = kRewrittenDataFlags;
	// The geometry as memory holds it; 0x00768890's raw-code stub rewrites it in case 11.
	Handle geometryDataField = kData;
	Handle candidateField = kCandidate;
	std::uint32_t bound[4] = {kBoundWords[0], kBoundWords[1], kBoundWords[2], kBoundWords[3]};
	std::vector<TracedCall> calls;
	std::vector<Write> writes;
	std::vector<std::string> faults;  // a port asked about memory the case does not hold, or the original faults

	void Emit(std::uint32_t callee, std::uint32_t from, std::uint32_t ecx, std::vector<std::uint32_t> args) {
		calls.push_back(TracedCall{callee, from, ecx, std::move(args)});
	}
	void Store(std::uint32_t at, std::uint32_t address, std::uint32_t size, std::uint32_t value) {
		writes.push_back(Write{at, address, size, value});
	}
	void ExpectGeometry(Handle geometry) {
		if (geometry != kGeometry) {
			faults.push_back("a read of an unmapped geometry");
		}
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

	// Reads.
	std::uint8_t skipsGeometryDraws() { return in.gate; }
	Handle geometryData(Handle geometry) {
		ExpectGeometry(geometry);
		return geometryDataField;
	}
	Handle skinInstance(Handle geometry) {
		ExpectGeometry(geometry);
		return kSkin;
	}
	std::uint32_t worldBoundWord(Handle geometry, std::uint32_t word) {
		ExpectGeometry(geometry);
		if (word >= 4) {
			faults.push_back("a bound word past +0x2C");
			return 0;
		}
		return bound[word];
	}
	Handle shaderCandidate(Handle geometry) {
		ExpectGeometry(geometry);
		return candidateField;
	}
	Handle bufferRecord(Handle data) {
		if (data == kData) {
			return kRecords;
		}
		if (data == kDataRewritten) {
			return kRecords + 0x100u;
		}
		faults.push_back("the record of an unmapped data");
		return 0;
	}
	Handle skinPartition(Handle skin) {
		if (skin != kSkin) {
			faults.push_back("the partition of an unmapped skin");
		}
		return kSkinPartition;
	}
	std::uint32_t partitionCount(Handle) { return in.partitionCount; }
	Handle partitions(Handle) { return kPartitions; }
	Handle partitionAt(Handle array, std::uint32_t index) { return array + eng::kPartitionStride * index; }
	std::uint16_t stripCount(Handle partition) { return PartitionOf(partition).strips; }
	std::uint16_t boneCount(Handle partition) { return PartitionOf(partition).bones; }
	std::uint16_t vertexCount(Handle partition) { return PartitionOf(partition).vertices; }
	Handle bufferData(Handle partition) { return PartitionOf(partition).bufferData; }
	Handle defaultShader() { return kDefaultShader; }
	std::uint32_t maxBonesWithoutShader() { return in.maxBones; }
	Handle device() { return kDevice; }
	std::uint32_t indexBuffer(Handle record) { return RecordOf(record).indexBuffer; }
	std::uint32_t recordVertexCount(Handle record) { return RecordOf(record).vertexCount; }
	std::uint32_t baseVertexIndex(Handle record) { return RecordOf(record).baseVertex; }
	std::uint32_t primitiveType(Handle record) { return RecordOf(record).primitiveType; }
	std::uint32_t triangleCount(Handle record) { return RecordOf(record).triangles; }
	std::uint32_t arrayCount(Handle record) { return RecordOf(record).arrays; }
	Handle arrayLengths(Handle record) {
		return RecordOf(record).hasLengths ? kLengths + 0x40u * ((record - kRecords) / 0x100u) : 0u;
	}
	std::uint16_t arrayLength(Handle lengths, std::uint32_t index) {
		const RecordInput& r = RecordOf(kRecords + 0x100u * ((lengths - kLengths) / 0x40u));
		if (index >= r.lengths.size()) {
			faults.push_back("a length past the case's array");
			return 0;
		}
		return r.lengths[index];
	}
	std::uint16_t dataFlags(Handle data) {
		if (data == kData) {
			return flags;
		}
		if (data == kDataRewritten) {
			return rewrittenFlags;
		}
		faults.push_back("the flags of an unmapped data");
		return 0;
	}

	// Stores.
	void setCurrentWorldBoundWord(std::uint32_t word, std::uint32_t value) {
		Store(eng::kWorldBoundStoreSites[word], kThis + eng::kCurrentWorldBoundOffset + 4u * word, 4, value);
	}
	void setDataFlags(Handle data, std::uint16_t value, Outcome exit) {
		(data == kData ? flags : rewrittenFlags) = value;
		Store(eng::FlagsClearSite(exit), data + eng::kDataFlagsOffset, 2, value);
	}

	// Calls.
	std::uint32_t geometryDataSlot20(Handle data) {
		Emit(kSlot20Stub, eng::kAfterDataSlot20, data, {});
		return in.vertexResult;
	}
	std::uint32_t selectsPartitions(Handle geometry) {
		Emit(eng::kSelectsPartitionsAddress, eng::kAfterSelectsPartitions, kThis, {geometry, 0u});
		if (!in.rewritesGeometry) {
			return in.selects;
		}
		bound[0] = kRewrittenBound0;
		geometryDataField = kDataRewritten;
		candidateField = kCandidateRewritten;
		Store(kRewriteSites[0], kGeometry + 0x20u, 4, kRewrittenBound0);
		Store(kRewriteSites[1], kGeometry + 0xB4u, 4, kDataRewritten);
		Store(kRewriteSites[2], kGeometry + 0xBCu, 4, kCandidateRewritten);
		return 0;
	}
	Handle castToShaderInterface(Handle object) {
		Emit(eng::kRttiCastAddress, eng::kAfterRttiCast, kAny, {eng::kNiRttiNiD3DShaderInterface, object});
		return in.cast;
	}
	std::uint32_t shaderSlot18(Handle shader) {
		Emit(kSlot18Stub, eng::kAfterShaderSlot18, shader, {});
		if (shader == logic::kNull) {
			faults.push_back("shader slot 18 through a null shader: the original faults at 0x00769780");
		}
		return kSlot18Eax;
	}
	void renderStateSubObjectSlot1() { Emit(kSubObjectSlot1Stub, eng::kAfterSubObjectSlot1, kSubObject, {}); }
	void cannotRenderPartition(std::uint32_t bones) {
		Emit(eng::kDebugPrintAddress, eng::kAfterDebugPrint, kAny, {eng::kCannotRenderPartitionFormat, bones});
	}
	void DrawIndexed(std::uint32_t from, const logic::DrawIndexedCall& c) {
		Emit(kDrawIndexedPrimitiveStub, from, kAny,
				{c.device, c.primitiveType, c.baseVertexIndex, c.minVertexIndex, c.numVertices, c.startIndex});
		Store(kDrawIndexedPrimitiveStub + 4u, kLogDrawCount, 4, c.primitiveCount);
	}
	void drawIndexedPrimitiveForPartition(const logic::DrawIndexedCall& c) {
		DrawIndexed(eng::kAfterPartitionDrawIndexedPrimitive, c);
	}
	void drawIndexedPrimitiveForRecord(const logic::DrawIndexedCall& c) {
		DrawIndexed(eng::kAfterRecordDrawIndexedPrimitive, c);
	}
	void drawPrimitive(const logic::DrawCall& c) {
		Emit(kDrawPrimitiveStub, eng::kAfterDrawPrimitive, kAny, {c.device, c.primitiveType, c.startVertex, c.primitiveCount});
	}
	void eyeSwitch(std::uint8_t eye) { Emit(kEyeSwitch, 0, kAny, {eye}); }
};
static_assert(logic::DrawTriStripsDirectPorts<CasePorts>);

// The original's results, transcribed from tools/difftest/expected/0x00769700.json
// (measured in the emulator). Inputs as in tools/difftest/cases/0x00769700.json.
const EmulatorCase kCases[] = {
{
		"B1 renderer +0x6F0 set: returns at once",
		{.gate = 0x80, .vertexResult = 0x00000003u, .selects = 0x00000000u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0, false, {}}}},
		{},
		{},
		0xCAFE0001u,
	},
	{
		"B2 slot 20 leaves AX zero (EAX 0x00010000): returns after the call",
		{.gate = 0x00, .vertexResult = 0x00010000u, .selects = 0x00000000u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}}
		},
		{},
		0x00010000u,
	},
	{
		"B3 whole (AL 0 of EAX 0xFFFFFF00); B9 no index buffer: DrawPrimitive",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0xFFFFFF00u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x00000000u, 0x00001234u, 0x00000040u, 0x00000004u, 0x00000099u, 3, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x20002300u, 0x00769942u, kAny, {0x10002000u, 0x00000004u, 0x00000040u, 0x00000099u}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x00769946u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B10 indexed with zero arrays: no draw, the shared exit",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000000u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0x8FFF, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x10300000u, 0x00001234u, 0x00000040u, 0x00000004u, 0x00000099u, 0, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x00769946u, 0x1000702Eu, 2, 0x00008000u}
		},
		0x10007000u,
	},
	{
		"B11a list count; B11b one array",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000100u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x10300000u, 0x00000200u, 0x00000010u, 0x00000004u, 0x00000055u, 1, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x20002200u, 0x007698FCu, kAny, {0x10002000u, 0x00000004u, 0x00000010u, 0x00000000u, 0x00000200u, 0x00000000u}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x20002204u, 0x10F00000u, 4, 0x00000055u},
			{0x00769914u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B11a strip lengths, a length of 1 wraps; B11b three arrays",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000000u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0x0FFF, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x10300000u, 0x00000300u, 0x00000020u, 0x00000005u, 0x00000077u, 3, true, {5, 1, 7}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x20002200u, 0x007698FCu, kAny, {0x10002000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000300u, 0x00000000u}},
			{0x20002200u, 0x007698FCu, kAny, {0x10002000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000300u, 0x00000005u}},
			{0x20002200u, 0x007698FCu, kAny, {0x10002000u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00000300u, 0x00000006u}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x20002204u, 0x10F00000u, 4, 0x00000003u},
			{0x20002204u, 0x10F00000u, 4, 0xFFFFFFFFu},
			{0x20002204u, 0x10F00000u, 4, 0x00000005u},
			{0x00769914u, 0x1000702Eu, 2, 0x00000000u}
		},
		0x10007000u,
	},
	{
		"B3 skinned (AL 1 of EAX 0x12345601); B4 zero partitions",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x12345601u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 0, .partitions = {{7, 9, 3, 0x1000D000u}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x00769946u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B5 custom shader draws 60 bones; B8a strip lengths; B8b two arrays; B8c one partition",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000001u, .rewritesGeometry = false, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 1, .partitions = {{4660, 60, 2, 0x1000D000u}}, .records = {{0x00000000u, 0x00000000u, 0x00000040u, 0x00000000u, 0x00000099u, 2, true, {6, 4}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x20002200u, 0x00769871u, kAny, {0x10002000u, 0x00000005u, 0x00000040u, 0x00000000u, 0x00001234u, 0x00000000u}},
			{0x20002200u, 0x00769871u, kAny, {0x10002000u, 0x00000005u, 0x00000040u, 0x00000000u, 0x00001234u, 0x00000006u}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x20002204u, 0x10F00000u, 4, 0x00000004u},
			{0x20002204u, 0x10F00000u, 4, 0x00000002u},
			{0x007698A3u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B6 default shader: above skipped, equal and below drawn; B7 no array; B8a list count; B8c four partitions",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000001u, .rewritesGeometry = false, .cast = 0x10004000u, .maxBones = 20, .flags = 0xABCD, .partitionCount = 4, .partitions = {{100, 21, 0, 0x1000D000u}, {200, 20, 0, 0x1000D100u}, {300, 19, 0, 0x1000D200u}, {400, 0, 0, 0x1000D300u}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0, false, {}}, {0x00000000u, 0x00000000u, 0x00000010u, 0x00000000u, 0x00000055u, 1, false, {}}, {0x00000000u, 0x00000000u, 0x00000020u, 0x00000000u, 0x00000099u, 0, false, {}}, {0x00000000u, 0x00000000u, 0x00000030u, 0x00000000u, 0x00000066u, 2, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10004000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x0060D0A0u, 0x00769820u, kAny, {0x00A88D90u, 0x00000015u}},
			{0x20002200u, 0x00769871u, kAny, {0x10002000u, 0x00000004u, 0x00000010u, 0x00000000u, 0x000000C8u, 0x00000000u}},
			{0x20002200u, 0x00769871u, kAny, {0x10002000u, 0x00000004u, 0x00000030u, 0x00000000u, 0x00000190u, 0x00000000u}},
			{0x20002200u, 0x00769871u, kAny, {0x10002000u, 0x00000004u, 0x00000030u, 0x00000000u, 0x00000190u, 0x00000068u}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x20002204u, 0x10F00000u, 4, 0x00000055u},
			{0x20002204u, 0x10F00000u, 4, 0x00000066u},
			{0x20002204u, 0x10F00000u, 4, 0x00000066u},
			{0x007698A3u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
	},
	{
		"B6 every partition over the limit: the diagnostic only",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000001u, .rewritesGeometry = false, .cast = 0x10004000u, .maxBones = 1, .flags = 0x0123, .partitionCount = 2, .partitions = {{5, 2, 0, 0x1000D000u}, {6, 3, 0, 0x1000D000u}}, .records = {{0x00000000u, 0x00000000u, 0x00000000u, 0x00000004u, 0x00000000u, 0, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005800u}},
			{0x20001200u, 0x00769790u, 0x10004000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x0060D0A0u, 0x00769820u, kAny, {0x00A88D90u, 0x00000002u}},
			{0x0060D0A0u, 0x00769820u, kAny, {0x00A88D90u, 0x00000003u}}
		},
		{
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x007698A3u, 0x1000702Eu, 2, 0x00000000u}
		},
		0x10007000u,
	},
	{
		"C6/C9 0x00768890 rewrites the geometry: the old bound is stored, the record comes from the re-read data, the flags clear hits the first data",
		{.gate = 0x00, .vertexResult = 0x00000003u, .selects = 0x00000000u, .rewritesGeometry = true, .cast = 0x10006000u, .maxBones = 4, .flags = 0xABCD, .partitionCount = 0, .partitions = {{1, 1, 0, 0x1000D000u}}, .records = {{0x10300000u, 0x00001234u, 0x00000040u, 0x00000004u, 0x00000099u, 1, false, {}}, {0x00000000u, 0x00000100u, 0x00000077u, 0x00000005u, 0x00000088u, 0, false, {}}}},
		{
			{0x20001400u, 0x00769729u, 0x10007000u, {}},
			{0x00768890u, 0x00769760u, 0x10000000u, {0x10005000u, 0x00000000u}},
			{0x00560920u, 0x0076977Eu, kAny, {0x00B42858u, 0x10005900u}},
			{0x20001200u, 0x00769790u, 0x10006000u, {}},
			{0x20002000u, 0x007697C7u, 0x10014000u, {}},
			{0x20002300u, 0x00769942u, kAny, {0x10002000u, 0x00000005u, 0x00000077u, 0x00000088u}}
		},
		{
			{0x00768890u, 0x10005020u, 4, 0x41200000u},
			{0x0076889Au, 0x100050B4u, 4, 0x10007100u},
			{0x007688A4u, 0x100050BCu, 4, 0x10005900u},
			{0x0076979Cu, 0x10000658u, 4, 0x3F800000u},
			{0x007697A2u, 0x1000065Cu, 4, 0x40000000u},
			{0x007697AEu, 0x10000660u, 4, 0x40400000u},
			{0x007697B4u, 0x10000664u, 4, 0x7FC00001u},
			{0x00769946u, 0x1000702Eu, 2, 0x0000A000u}
		},
		0x10007000u,
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

constexpr logic::DrawArguments kArguments{kThis, kGeometry};

// The exit the original took, read off its own trace: no call (B1), no write
// (B2), else the site of its last write, the flags clear.
Outcome ExpectedOutcome(const EmulatorCase& c) {
	if (c.calls.empty()) {
		return Outcome::kSuppressed;
	}
	if (c.writes.empty()) {
		return Outcome::kNoVertices;
	}
	switch (c.writes.back().at) {
	case 0x007698A3u: return Outcome::kPartitionLoopExit;
	case 0x00769914u: return Outcome::kArrayLoopExit;
	default: return Outcome::kSharedDrawExit;
	}
}

void TestOneEyeAgainstTheOriginal() {
	for (const EmulatorCase& c : kCases) {
		Section(c.name);
		CasePorts ports(c.input);
		const logic::Result result = logic::DrawTriStripsDirect(ports, kArguments, EyePlan{1});
		Check(SameSequence(ports.calls, c.calls, "calls"), "the call sequence equals the original's trace");
		Check(SameSequence(ports.writes, c.writes, "writes"), "the writes equal the original's");
		Check(logic::ExitEax(result, kIncomingEax) == c.eax, "EAX at the exit equals the original's");
		Check(result.outcome == ExpectedOutcome(c), "the outcome names the exit the original took");
		Check(ports.faults.empty(), "no port read outside the case's memory");
	}
}

// ---- Part 3: the null cast (C7) --------------------------------------------------------

void TestNullCastIsHandedOn() {
	Section("C7: a null cast result goes to shader slot 18 unchecked (the original faults reading [0] at 0x00769780)");
	CaseInput in = kCases[2].input;  // the DrawPrimitive case
	in.cast = 0;
	CasePorts ports(in);
	logic::DrawTriStripsDirect(ports, kArguments, EyePlan{1});
	const std::vector<TracedCall> expectedPrefix(kCases[2].calls.begin(), kCases[2].calls.begin() + 3);
	const std::vector<TracedCall> prefix(ports.calls.begin(), ports.calls.begin() + std::min<std::size_t>(3, ports.calls.size()));
	Check(SameSequence(prefix, expectedPrefix, "calls"), "slot 20, 0x00768890, the cast: as the original");
	Check(ports.calls.size() > 3 && ports.calls[3] == TracedCall{kSlot18Stub, eng::kAfterShaderSlot18, logic::kNull, {}},
			"next: slot 18 with ECX = 0, no fallback to renderer+0xA94");
	Check(ports.faults.size() == 1, "the fault is the port's, where the original's is");
}

// ---- Part 4: two eyes ------------------------------------------------------------------

// The units StereoDrawDirect.h frames, read off the case: the whole arm is one
// unit (a DrawPrimitive, or the record's DIPs, possibly none); the skinned arm
// has one unit per partition the bone gate lets through (its DIPs, possibly
// none), a gated partition is its diagnostic call and no unit. The arm is the
// byte the original tests at 0x007697C7 (0 when the rewriting stub runs).
struct Unit {
	bool framed;              // a drawn unit (eye switch around its second run)
	std::size_t calls;        // calls it takes from the trace
	std::size_t countWrites;  // DIP log stores it takes from the writes
};

std::vector<Unit> UnitsOf(const CaseInput& in) {
	const bool skinned = !in.rewritesGeometry && (in.selects & 0xFFu) != 0;
	std::vector<Unit> units;
	if (!skinned) {
		const RecordInput& r = in.records[in.rewritesGeometry ? 1 : 0];
		if (r.indexBuffer == 0) {
			units.push_back(Unit{true, 1, 0});
		} else {
			units.push_back(Unit{true, r.arrays, r.arrays});
		}
		return units;
	}
	for (std::uint32_t k = 0; k < in.partitionCount; ++k) {
		const PartitionInput& p = in.partitions[k];
		if (in.cast == kDefaultShader && p.bones > in.maxBones) {
			units.push_back(Unit{false, 1, 0});
			continue;
		}
		const RecordInput& r = in.records[(p.bufferData - kRecords) / 0x100u];
		units.push_back(Unit{true, r.arrays, r.arrays});
	}
	return units;
}

std::vector<TracedCall> TwoEyeCalls(const EmulatorCase& c) {
	const auto subObject = std::find_if(c.calls.begin(), c.calls.end(),
			[](const TracedCall& t) { return t.callee == kSubObjectSlot1Stub; });
	if (subObject == c.calls.end()) {
		return c.calls;  // B1, B2: no draw stage
	}
	std::vector<TracedCall> out(c.calls.begin(), subObject + 1);
	auto next = subObject + 1;
	for (const Unit& unit : UnitsOf(c.input)) {
		const std::vector<TracedCall> draws(next, next + static_cast<std::ptrdiff_t>(unit.calls));
		next += static_cast<std::ptrdiff_t>(unit.calls);
		out.insert(out.end(), draws.begin(), draws.end());
		if (unit.framed) {
			out.push_back(TracedCall{kEyeSwitch, 0, kAny, {1}});
			out.insert(out.end(), draws.begin(), draws.end());
			out.push_back(TracedCall{kEyeSwitch, 0, kAny, {0}});
		}
	}
	out.insert(out.end(), next, c.calls.end());
	return out;
}

std::vector<Write> TwoEyeWrites(const EmulatorCase& c) {
	std::deque<std::size_t> groups;
	for (const Unit& unit : UnitsOf(c.input)) {
		if (unit.countWrites != 0) {
			groups.push_back(unit.countWrites);
		}
	}
	std::vector<Write> out;
	for (std::size_t i = 0; i < c.writes.size();) {
		if (c.writes[i].address != kLogDrawCount || groups.empty()) {
			out.push_back(c.writes[i++]);
			continue;
		}
		const std::size_t n = groups.front();
		groups.pop_front();
		const std::vector<Write> run(c.writes.begin() + static_cast<std::ptrdiff_t>(i),
				c.writes.begin() + static_cast<std::ptrdiff_t>(i + n));
		out.insert(out.end(), run.begin(), run.end());
		out.insert(out.end(), run.begin(), run.end());
		i += n;
	}
	return out;
}

std::size_t Switches(const std::vector<TracedCall>& calls) {
	return static_cast<std::size_t>(std::count_if(calls.begin(), calls.end(),
			[](const TracedCall& t) { return t.callee == kEyeSwitch; }));
}

void TestTwoEyes() {
	for (const EmulatorCase& c : kCases) {
		Section((std::string("Two eyes: ") + c.name).c_str());
		CasePorts ports(c.input);
		const logic::Result result = logic::DrawTriStripsDirect(ports, kArguments, EyePlan{2});
		Check(SameSequence(ports.calls, TwoEyeCalls(c), "calls"),
				"the original's sequence with each unit's draws doubled around the eye switch");
		Check(SameSequence(ports.writes, TwoEyeWrites(c), "writes"),
				"the same writes, once each (bound, flags), the draw counts doubled");
		Check(result.outcome == ExpectedOutcome(c), "the same exit as one eye");
		Check(ports.faults.empty(), "no port read outside the case's memory");
	}

	Section("Two eyes: which units switch (the contract names no exception for a unit without a draw)");
	const auto switchesOf = [](std::size_t index) {
		CasePorts ports(kCases[index].input);
		logic::DrawTriStripsDirect(ports, kArguments, EyePlan{2});
		return Switches(ports.calls);
	};
	Check(switchesOf(0) == 0, "B1: none");
	Check(switchesOf(1) == 0, "B2: none");
	Check(switchesOf(2) == 2, "B9 DrawPrimitive: one pair");
	Check(switchesOf(3) == 2, "B10 zero arrays: one pair around no draw");
	Check(switchesOf(5) == 2, "three arrays: one pair around all three");
	Check(switchesOf(6) == 0, "B4 zero partitions: none");
	Check(switchesOf(8) == 6, "four partitions, one gated: three pairs (one around partition 2's no draw)");
	Check(switchesOf(9) == 0, "every partition gated: none");
}

} // namespace

int main() {
	TestDecisions();
	TestOneEyeAgainstTheOriginal();
	TestNullCastIsHandedOn();
	TestTwoEyes();
	return Failures();
}
