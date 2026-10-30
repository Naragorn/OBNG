// Differential gate, layer 1 (docs/re-workflow.md, "the gate outside the
// workflow"), for 0x004E1580 (proposed TESObjectREFR::LoadBaseFormModels):
// the original's bytes against the rebuild's LoadBaseFormModels(ports).
//
// Method. The original ran in the emulator (tools/difftest/emulate.py) on the
// synthetic inputs of tools/difftest/cases/0x004E1580.json; its result is
// tools/difftest/expected/0x004E1580.json (2026-10-30, every case returned to
// its caller with a balanced stack and no emulator error). Every callee and
// every virtual slot the body reaches is a traced stub there, so each case's
// trace is the original's complete call sequence: callee, return address,
// ECX and the stack arguments. That trace is transcribed below, literally,
// one entry per case (measured). Here the same inputs are answered by fake
// ports that hand out the same addresses as handles; each port records the
// call the engine-facing port stands for, in the emulator's terms (callee
// address, the return address of its CALL instruction, ECX, arguments), and
// the two sequences must be equal.
//
// What is compared, per case:
//   - every call, in order: callee, return address, ECX (except where ECX is
//     not an input: the cdecl/stdcall callees, marked kAny), and the stack
//     arguments the callee takes as called here (re/functions/0x004E1580.md);
//     0x0051E240's two flag arguments by their low byte only (the body sets
//     only that byte, 0x004E1626/0x004E162B; the rest is stack contents);
//   - the return value: the original's AL (MOV AL,BL at 0x004E18CB);
//   - the paths given to 0x00439EB0: its stub copies each one to a log, read
//     back after the run (the buffer itself is on the stack);
//   - the three floats the context holds when 0x004430C0 / 0x00700610 run:
//     their stubs copy the context's +0x10/+0x14/+0x18 out (last call each);
//   - the refcount at +0x04 of the node 0x004430C0 returns, after the
//     NiPointer assign's increment and InterlockedDecrement.
//
// What it cannot see:
//   - stack-held values: arguments that are addresses in the emulator's stack
//     (the path buffer, &context, the NiPointer local as ECX of 0x0075FA90,
//     the context as ECX of 0x00478B90) are compared only as "a stack
//     address" (kStack); writes to the stack are not recorded at all;
//   - _strrchr (0x00983FD0) and __security_check_cookie (0x009811E2) have no
//     port: the rebuild uses std::strrchr inside ComposeSiblingModelPath and
//     the compiler's own frame. They are dropped from the comparison; each
//     _strrchr is checked to sit between vfn 0x14 and 0x00439EB0 with
//     (buffer, '\'), the cookie check to come after 0x0047D0F0;
//   - the SEH frame link: the body's only writes outside its stack are FS:[0]
//     (0x004E15B5) and its restore (0x004E18D4) in every case (checked on the
//     JSON when it was produced); the rebuild has no counterpart;
//   - what the callees do: they are stubs. 0x0075FA90 is raw code that stores
//     the value and increments its +0x04; that increment is an assumption
//     (unverified, the callee's body is not exported) - the rebuild's
//     Create004430C0 port has the same contract, so the refcount comparison
//     checks the body's decrement and delete decisions, not the assign;
//   - the 0x004D96F0 name 0x00A30968 ("Bip01" in data.json) and the vfn 0x58
//     name: the rebuild's ports do not pass the first (the engine port
//     supplies it), the second is a host string, mapped to 0x00A3D01C here
//     only when its text is the "SkinAttachment" at that address;
//   - ECX of 0x004533F0: the original leaves [0x00B33B00] (0x10008000 here) in
//     it; the rebuild declares the callee stdcall (TESObjectREFR_
//     LoadBaseFormModels.h: unverified which), so ECX is not compared;
//   - the faulting inputs the rebuild refuses (null name with next set, no
//     '\', a path past the 0x104-byte buffer): the original faults on them,
//     so they cannot run here; TESObjectREFR_LoadBaseFormModelsTest.cpp
//     covers the refusals.

#include "Check.h"
#include "engine/time/TESObjectREFR_LoadBaseFormModelsLogic.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace logic = obng::engine::time::load_base_form_models;
using logic::PathStatus;

namespace {

// Markers in the transcribed trace.
constexpr std::uint32_t kStack = 0xFFFF0001u;  // an address inside the emulator's stack
constexpr std::uint32_t kAny = 0xFFFF0002u;    // ECX is not an input of this callee

// The synthetic memory of the cases (tools/difftest/cases/0x004E1580.json, "note").
constexpr std::uint32_t kThis = 0x10000000u;
constexpr std::uint32_t kForm = 0x10002000u;
constexpr std::uint32_t kOtherForm = 0x10002800u;
constexpr std::uint32_t kListHead = 0x10005000u;  // nodes every 0x10 bytes
constexpr std::uint32_t kNiNode = 0x10006000u;
constexpr std::uint32_t kNode2 = 0x10006800u;
constexpr std::uint32_t kModel = 0x10009000u;
constexpr std::uint32_t kModel2 = 0x10009800u;
constexpr std::uint32_t kCreated = 0x1000A000u;  // 0x004430C0's stub result
constexpr std::uint32_t kCloned = 0x1000B000u;   // 0x00700610's stub result
constexpr std::uint32_t kTarget = 0x1000C000u;   // +0x1C holds kAt1C
constexpr std::uint32_t kAt1C = 0x1000D000u;
constexpr std::uint32_t kNamed = 0x1000E000u;
constexpr std::uint32_t kSlot0 = 0x1000F000u;
constexpr std::uint32_t kHelper = 0x1000F800u;
constexpr std::uint32_t kSlot4 = 0x10010000u;
constexpr std::uint32_t kTES = 0x10013000u;     // [0x00B333A0]
constexpr std::uint32_t kLoader = 0x10014000u;  // [0x00B33A1C]
constexpr std::uint32_t kTimeInfo = 0x00B33E90u;
constexpr std::uint32_t kBip01Name = 0x00A30968u;
constexpr std::uint32_t kSkinAttachmentName = 0x00A3D01Cu;

// The stubs the vtable slots point at.
constexpr std::uint32_t kVfn168 = 0x20000100u;
constexpr std::uint32_t kVfn164 = 0x20000200u;
constexpr std::uint32_t kVfn190 = 0x20000300u;
constexpr std::uint32_t kVfnEC = 0x20000400u;
constexpr std::uint32_t kVfnFC = 0x20000500u;
constexpr std::uint32_t kVfn14 = 0x20000600u;
constexpr std::uint32_t kVfn58 = 0x20000700u;
constexpr std::uint32_t kVfn84 = 0x20000800u;
constexpr std::uint32_t kVfn0 = 0x20000900u;
constexpr std::uint32_t kInterlockedDecrement = 0x20000A00u;  // [0x00A2807C]
constexpr std::uint32_t kStrrchr = 0x00983FD0u;
constexpr std::uint32_t kSecurityCheckCookie = 0x009811E2u;

constexpr const char* kBasePath = "meshes\\creatures\\wolf\\skeleton.nif";  // at 0x10004000
constexpr const char* kNameText[] = {"wolfhead.nif", "wolfbody.nif", "wolftail.nif"};  // 0x10004100, +0x40, +0x80
constexpr float kScale = 0.75f;  // vfn 0xEC's ST0, from 0x10004F00

// One case's inputs: the values the case's memory and stubs hold. A per-call
// list answers call n with element n, the last one repeating (emulate.py's
// stub rule); loads past the end of their table read 0 (the zeroed page).
struct CaseInput {
	std::uint8_t type = 0x24;           // kForm+0x04
	std::uint8_t otherFormType = 0x24;  // kOtherForm+0x04
	std::uint32_t vfn168 = 0;
	std::uint32_t vfn164 = 0;
	std::uint32_t niNode = kNiNode;  // this+0x3C
	std::uint32_t vfn190 = 0;
	std::optional<std::uint32_t> lookupFlags;  // none: 0x005E0380 returns null
	std::uint32_t createdBaseObjListFlags = 0;
	std::uint32_t probe = 0;
	bool list = false;          // false: 0x006899C0 returns null
	std::vector<int> names;     // per node: index into kNameText, -1 for data 0
	std::vector<std::uint32_t> loads;
	std::vector<std::uint32_t> r480820;
	std::vector<std::uint32_t> r471B80;
	std::vector<std::uint32_t> find;
	std::vector<std::uint32_t> vfn58;
	long refCount = 0;  // kCreated+0x04
	std::uint32_t slot0 = 0;  // what 0x00478B90's stub stores at context +0x00 / +0x04
	std::uint32_t slot4 = 0;
	std::uint32_t vfnFC = 0;
	std::uint32_t formAfter004728C0 = 0;    // 0: 0x004728C0 changes nothing
	std::uint32_t niNodeAfter004728C0 = 0;
};

struct TracedCall {
	std::uint32_t callee;
	std::uint32_t from;  // the return address: the instruction after the CALL
	std::uint32_t ecx;
	std::vector<std::uint32_t> args;
};

struct EmulatorCase {
	const char* name;
	CaseInput input;
	std::vector<TracedCall> calls;  // the original's trace, as transcribed
	std::uint8_t al;
	std::vector<std::string> paths;
	std::uint32_t createScale[3];  // context +0x10/+0x14/+0x18 when 0x004430C0 last ran (0: never)
	std::uint32_t cloneScale[3];   // the same for 0x00700610
	long refCount;                 // kCreated+0x04 afterwards
};

std::uint32_t Next(const std::vector<std::uint32_t>& values, int& hits) {
	std::uint32_t value = values.empty() ? 0u : values[std::min<std::size_t>(hits, values.size() - 1)];
	++hits;
	return value;
}

std::uint32_t FloatBits(float value) {
	std::uint32_t bits;
	std::memcpy(&bits, &value, sizeof bits);
	return bits;
}

// The ports, answering from a case's inputs and recording each call in the
// emulator's terms. Handles are the case's addresses.
struct EmulatorPorts {
	explicit EmulatorPorts(const CaseInput& caseInput)
		: in(caseInput), form(kForm), niNode(caseInput.niNode), refCount(caseInput.refCount) {}

	const CaseInput& in;
	std::uint32_t form;    // this+0x1C
	std::uint32_t niNode;  // this+0x3C
	long refCount;         // kCreated+0x04
	float contextScale = 0.0f;
	std::uint32_t createScale[3] = {};
	std::uint32_t cloneScale[3] = {};
	std::uint32_t deleteSite = 0;
	int loadHits = 0, hits480820 = 0, hits471B80 = 0, findHits = 0, vfn58Hits = 0;
	std::vector<TracedCall> calls;
	std::vector<std::string> paths;
	std::vector<PathStatus> refusals;
	std::vector<std::string> faults;  // a port asked about something the case's memory does not hold

	void Emit(std::uint32_t callee, std::uint32_t from, std::uint32_t ecx, std::vector<std::uint32_t> args) {
		calls.push_back(TracedCall{callee, from, ecx, std::move(args)});
	}
	static bool LowByte(std::uint32_t value) { return (value & 0xFFu) != 0; }  // TEST AL,AL / MOV BL,AL
	std::size_t NodeIndex(std::uint32_t node) const { return (node - kListHead) / 0x10; }

	std::uint32_t BaseForm() { return form; }
	std::uint32_t Vfn168() {
		Emit(kVfn168, 0x004E15CFu, kThis, {});
		return in.vfn168;
	}
	void ClockEnter() { Emit(0x0047D120u, 0x004E15DBu, kTimeInfo, {}); }
	void ClockLeave() { Emit(0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}); }
	std::uint32_t Vfn164() {
		Emit(kVfn164, 0x004E15E8u, kThis, {});
		return in.vfn164;
	}
	void Call004728C0(std::uint32_t object) {
		Emit(0x004728C0u, 0x004E15F3u, object, {});
		if (in.formAfter004728C0 != 0) {
			form = in.formAfter004728C0;
		}
		if (in.niNodeAfter004728C0 != 0) {
			niNode = in.niNodeAfter004728C0;
		}
	}
	std::uint8_t TypeId(std::uint32_t baseForm) {
		if (baseForm == kForm) {
			return in.type;
		}
		if (baseForm == kOtherForm) {
			return in.otherFormType;
		}
		faults.push_back("type byte of an unmapped form");
		return 0;
	}
	std::uint32_t NiNode() { return niNode; }
	void Call00526DB0(std::uint32_t baseForm) { Emit(0x00526DB0u, 0x004E18ABu, baseForm, {kThis}); }
	bool VfnFC(std::uint32_t baseForm) {
		Emit(kVfnFC, 0x004E18BFu, baseForm, {});
		return LowByte(in.vfnFC);
	}
	bool Vfn190() {
		Emit(kVfn190, 0x004E1632u, kThis, {});
		return LowByte(in.vfn190);
	}
	std::optional<std::uint32_t> Lookup005E0380Flags() {
		Emit(0x005E0380u, 0x004E163Du, kThis, {});
		return in.lookupFlags;
	}
	std::uint32_t CreatedBaseObjListFlags() { return in.createdBaseObjListFlags; }
	std::uint32_t Call004533F0() {
		Emit(0x004533F0u, 0x004E1677u, kAny, {kThis, 1u});
		return in.probe;
	}
	void Call0051D460(std::uint32_t baseForm) { Emit(0x0051D460u, 0x004E1686u, baseForm, {kThis}); }
	void Call0051E240(std::uint32_t baseForm, bool arg2, bool arg3) {
		Emit(0x0051E240u, 0x004E169Cu, baseForm, {kThis, arg2 ? 1u : 0u, arg3 ? 1u : 0u, 0u});
	}
	std::uint32_t ModelListHead(std::uint32_t baseForm) {
		Emit(0x006899C0u, 0x004E16A7u, baseForm + 0xEC, {});
		return in.list ? kListHead : 0u;
	}
	std::uint32_t ListNext(std::uint32_t node) {
		return NodeIndex(node) + 1 < in.names.size() ? node + 0x10 : 0u;
	}
	const char* ListData(std::uint32_t node) {
		int name = in.names.at(NodeIndex(node));
		return name < 0 ? nullptr : kNameText[name];
	}
	const char* BaseModelPath(std::uint32_t baseForm) {
		Emit(kVfn14, 0x004E16E4u, baseForm + 0xAC, {});
		return kBasePath;
	}
	void RefusePath(PathStatus status, const char* /*fileName*/) { refusals.push_back(status); }
	std::uint32_t LoadModel(const char* path) {
		Emit(0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0u, 3u, 1u});
		paths.emplace_back(path);
		std::uint32_t model = static_cast<std::size_t>(loadHits) < in.loads.size() ? in.loads[loadHits] : 0u;
		++loadHits;
		return model;
	}
	float Vfn0EC() {
		Emit(kVfnEC, 0x004E174Bu, kThis, {});
		return kScale;
	}
	void ConstructNodeContext(float scale) {
		Emit(0x00478B90u, 0x004E1758u, kStack, {});
		contextScale = scale;
	}
	bool Call00480820(std::uint32_t model) {
		Emit(0x00480820u, 0x004E1783u, kAny, {model});
		return LowByte(Next(in.r480820, hits480820));
	}
	void LogScale(std::uint32_t (&log)[3]) const {
		for (std::uint32_t& bits : log) {
			bits = FloatBits(contextScale);
		}
	}
	// 0x004430C0 and the NiPointer assign 0x0075FA90 behind one port (the
	// logic header's contract), the assign incrementing what it stores.
	std::uint32_t Create004430C0(std::uint32_t model) {
		Emit(0x004430C0u, 0x004E179Bu, kTES, {model, kStack});
		LogScale(createScale);
		Emit(0x0075FA90u, 0x004E17A5u, kStack, {kCreated});
		++refCount;
		return kCreated;
	}
	std::uint32_t Clone00700610(std::uint32_t model) {
		Emit(0x00700610u, 0x004E17B7u, model, {kStack});
		LogScale(cloneScale);
		return kCloned;
	}
	bool Call00471B80(std::uint32_t node) {
		Emit(0x00471B80u, 0x004E17BFu, kAny, {node});
		return LowByte(Next(in.r471B80, hits471B80));
	}
	// The engine port supplies the name 0x00A30968 itself (the logic passes none).
	std::uint32_t Find004D96F0(std::uint32_t parent) {
		Emit(0x004D96F0u, 0x004E17D5u, kThis, {parent, kBip01Name});
		return Next(in.find, findHits);
	}
	std::uint32_t FindByName(std::uint32_t object, const char* name) {
		Emit(kVfn58, 0x004E17EDu, object, {std::strcmp(name, "SkinAttachment") == 0 ? kSkinAttachmentName : 0u});
		return Next(in.vfn58, vfn58Hits);
	}
	std::uint32_t ObjectAt1C(std::uint32_t object) {
		if (object != kTarget) {
			faults.push_back("+0x1C of an unmapped object");
			return 0;
		}
		return kAt1C;
	}
	void AttachVfn84(std::uint32_t receiver, std::uint32_t node) { Emit(kVfn84, 0x004E1809u, receiver, {node, 1u}); }
	void Call00478EC0(std::uint32_t parent, std::uint32_t node) { Emit(0x00478EC0u, 0x004E1816u, kAny, {parent, node, 0u}); }
	void Call00479140(std::uint32_t parent, std::uint32_t node) {
		Emit(0x00479140u, 0x004E182Eu, kAny, {parent, node, 0u, 0u, 0xFFFFFFFFu, 0u});
	}
	long DecrementRefCount(std::uint32_t object) {
		Emit(kInterlockedDecrement, 0x004E184Bu, kAny, {object + 4});
		deleteSite = 0x004E1859u;
		if (object != kCreated) {
			faults.push_back("refcount of an unmapped object");
			return 1;
		}
		return --refCount;
	}
	void DeleteObject(std::uint32_t object) { Emit(kVfn0, deleteSite, object, {1u}); }
	std::uint32_t NodeContextSlot(int index) {
		deleteSite = index == 0 ? 0x004E1874u : 0x004E1884u;
		return index == 0 ? in.slot0 : in.slot4;
	}
};

std::string Hex(std::uint32_t value) {
	char text[16];
	std::snprintf(text, sizeof text, "%08X", value);
	return text;
}

std::string Describe(const TracedCall& call) {
	std::string text = Hex(call.callee) + " from " + Hex(call.from) + " ecx " + Hex(call.ecx) + " (";
	for (std::uint32_t arg : call.args) {
		text += " " + Hex(arg);
	}
	return text + " )";
}

bool SameCall(const TracedCall& original, const TracedCall& rebuild) {
	return original.callee == rebuild.callee && original.from == rebuild.from &&
		   (original.ecx == kAny || original.ecx == rebuild.ecx) && original.args == rebuild.args;
}

// The emulator's trace of the original, transcribed from
// tools/difftest/expected/0x004E1580.json; the inputs are the case file's.
const EmulatorCase kCases[] = {
	{
		"a1 other type 0x10, vfn[0xFC] returns 1",
		{.type = 0x10, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x1u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfnFC, 0x004E18BFu, kForm, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"a2 other type 0x10, vfn[0xFC] returns 0",
		{.type = 0x10, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfnFC, 0x004E18BFu, kForm, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		0,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"b1 type 0x23, vfn[0x168] returns 1: 0x00526DB0",
		{.type = 0x23, .otherFormType = 0x24, .vfn168 = 0x1u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x00526DB0u, 0x004E18ABu, kForm, {kThis}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"b2 type 0x23, vfn[0x168] returns 0: no 0x00526DB0",
		{.type = 0x23, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"b3 type 0x23, vfn[0x168] returns 0x100 (low byte 0): the whole dword is tested",
		{.type = 0x23, .otherFormType = 0x24, .vfn168 = 0x100u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x00526DB0u, 0x004E18ABu, kForm, {kThis}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"c type 0x24, niNode null: nothing built",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = 0x0u, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"d type 0x24, vfn[0x190] 0, probe bit clear: 0x0051E240(this, 1, 1, 0), empty list",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"e1 lookup flags bit 20: 0x0051E240(this, 0, 1, 0)",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x1u, .lookupFlags = 0x100000u, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x005E0380u, 0x004E163Du, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x0u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"e2 lookup flags bit 21: 0x0051E240(this, 1, 0, 0)",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x1u, .lookupFlags = 0x200000u, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x005E0380u, 0x004E163Du, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x0u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"e3 lookup flags bits 20 and 21: 0x0051E240(this, 0, 0, 0)",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x1u, .lookupFlags = 0x300000u, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x005E0380u, 0x004E163Du, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x0u, 0x0u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"e4 lookup returns null: 0x0051E240(this, 1, 1, 0)",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x1u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x005E0380u, 0x004E163Du, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"f1 probe bit set, 0x004533F0 returns 0x08000000: 0x0051D460",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x2u, .probe = 0x8000000u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x004533F0u, 0x004E1677u, kAny, {kThis, 0x1u}},
			{0x0051D460u, 0x004E1686u, kForm, {kThis}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"f2 probe bit set, 0x004533F0 returns 0xF7FFFFFF, lookup bit 20: 0x0051E240(this, 0, 1, 0)",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x1u, .lookupFlags = 0x100000u, .createdBaseObjListFlags = 0x2u, .probe = 0xF7FFFFFFu, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x005E0380u, 0x004E163Du, kThis, {}},
			{0x004533F0u, 0x004E1677u, kAny, {kThis, 0x1u}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x0u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"g two entries: create + SkinAttachment found by name + delete at 0; clone + plain attach",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0, 1}, .loads = {kModel, kModel2}, .r480820 = {0x1u, 0x0u}, .r471B80 = {0x1u, 0x0u}, .find = {kTarget, kTarget}, .vfn58 = {kNamed}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x004430C0u, 0x004E179Bu, kTES, {kModel, kStack}},
			{0x0075FA90u, 0x004E17A5u, kStack, {kCreated}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCreated}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{kVfn58, 0x004E17EDu, kTarget, {kSkinAttachmentName}},
			{kVfn84, 0x004E1809u, kNamed, {kCreated, 0x1u}},
			{0x00478EC0u, 0x004E1816u, kAny, {kNiNode, kCreated, 0x0u}},
			{kInterlockedDecrement, 0x004E184Bu, kAny, {kCreated + 4}},
			{kVfn0, 0x004E1859u, kCreated, {0x1u}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel2}},
			{0x00700610u, 0x004E17B7u, kModel2, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNiNode, kCloned, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif", "meshes\\creatures\\wolf\\wolfbody.nif"},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"h vfn[0x58] returns null: vfn[0x84] on the object at target+0x1C",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x1u}, .find = {kTarget}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x00700610u, 0x004E17B7u, kModel, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{kVfn58, 0x004E17EDu, kTarget, {kSkinAttachmentName}},
			{kVfn84, 0x004E1809u, kAt1C, {kCloned, 0x1u}},
			{0x00478EC0u, 0x004E1816u, kAny, {kNiNode, kCloned, 0x0u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif"},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"i first load returns null: the second entry is loaded",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0, 1}, .loads = {0x0u, kModel2}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel2}},
			{0x00700610u, 0x004E17B7u, kModel2, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNiNode, kCloned, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif", "meshes\\creatures\\wolf\\wolfbody.nif"},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"j vfn[0x164] returns an object: 0x004728C0 on it",
		{.type = 0x10, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = kHelper, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x004728C0u, 0x004E15F3u, kHelper, {}},
			{kVfnFC, 0x004E18BFu, kForm, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		0,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"k context slots non-null: both get vfn[0](1)",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = kSlot0, .slot4 = kSlot4, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x00700610u, 0x004E17B7u, kModel, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNiNode, kCloned, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{kVfn0, 0x004E1874u, kSlot0, {0x1u}},
			{kVfn0, 0x004E1884u, kSlot4, {0x1u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif"},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"n NiPointer decrement leaves 1: no delete",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0}, .loads = {kModel}, .r480820 = {0x1u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 1, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x004430C0u, 0x004E179Bu, kTES, {kModel, kStack}},
			{0x0075FA90u, 0x004E17A5u, kStack, {kCreated}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCreated}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNiNode, kCreated, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{kInterlockedDecrement, 0x004E184Bu, kAny, {kCreated + 4}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif"},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		1,
	},
	{
		"o second node has data 0 and next 0: the walk ends after the first",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0, -1}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x00700610u, 0x004E17B7u, kModel, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNiNode, kCloned, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif"},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"p head node has data 0 and next 0: no entry visited",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {-1}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"q 0x00471B80 true, 0x004D96F0 null: plain attach",
		{.type = 0x24, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = 0x0u, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x1u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = 0x0u, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x00700610u, 0x004E17B7u, kModel, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNiNode, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNiNode, kCloned, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif"},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"l 0x004728C0 changes this+0x1C (type 0x24) and this+0x3C: type and node re-read, builder/list/path on the cached form",
		{.type = 0x10, .otherFormType = 0x24, .vfn168 = 0x0u, .vfn164 = kHelper, .niNode = 0x0u, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = true, .names = {0}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = kOtherForm, .niNodeAfter004728C0 = kNode2},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x004728C0u, 0x004E15F3u, kHelper, {}},
			{kVfn190, 0x004E1632u, kThis, {}},
			{0x0051E240u, 0x004E169Cu, kForm, {kThis, 0x1u, 0x1u, 0x0u}},
			{0x006899C0u, 0x004E16A7u, kForm + 0xEC, {}},
			{kVfn14, 0x004E16E4u, kForm + 0xAC, {}},
			{0x00983FD0u, 0x004E1702u, kAny, {kStack, 0x5Cu}},
			{0x00439EB0u, 0x004E1734u, kLoader, {kStack, 0x0u, 0x3u, 0x1u}},
			{kVfnEC, 0x004E174Bu, kThis, {}},
			{0x00478B90u, 0x004E1758u, kStack, {}},
			{0x00480820u, 0x004E1783u, kAny, {kModel}},
			{0x00700610u, 0x004E17B7u, kModel, {kStack}},
			{0x00471B80u, 0x004E17BFu, kAny, {kCloned}},
			{0x004D96F0u, 0x004E17D5u, kThis, {kNode2, kBip01Name}},
			{0x00479140u, 0x004E182Eu, kAny, {kNode2, kCloned, 0x0u, 0x0u, 0xFFFFFFFFu, 0x0u}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{"meshes\\creatures\\wolf\\wolfhead.nif"},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x3F400000u, 0x3F400000u, 0x3F400000u},
		0,
	},
	{
		"r 0x004728C0 changes this+0x1C (type 0x23): 0x00526DB0 on the form read before vfn[0x168]",
		{.type = 0x10, .otherFormType = 0x23, .vfn168 = 0x1u, .vfn164 = kHelper, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x0u, .formAfter004728C0 = kOtherForm, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x004728C0u, 0x004E15F3u, kHelper, {}},
			{0x00526DB0u, 0x004E18ABu, kForm, {kThis}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
	{
		"m 0x004728C0 changes this+0x1C (type 0x10): vfn[0xFC] on the re-read form",
		{.type = 0x24, .otherFormType = 0x10, .vfn168 = 0x0u, .vfn164 = kHelper, .niNode = kNiNode, .vfn190 = 0x0u, .lookupFlags = std::nullopt, .createdBaseObjListFlags = 0x0u, .probe = 0x0u, .list = false, .names = {}, .loads = {kModel}, .r480820 = {0x0u}, .r471B80 = {0x0u}, .find = {0x0u}, .vfn58 = {0x0u}, .refCount = 0, .slot0 = 0x0u, .slot4 = 0x0u, .vfnFC = 0x1u, .formAfter004728C0 = kOtherForm, .niNodeAfter004728C0 = 0x0u},
		{
			{kVfn168, 0x004E15CFu, kThis, {}},
			{0x0047D120u, 0x004E15DBu, kTimeInfo, {}},
			{kVfn164, 0x004E15E8u, kThis, {}},
			{0x004728C0u, 0x004E15F3u, kHelper, {}},
			{kVfnFC, 0x004E18BFu, kOtherForm, {}},
			{0x0047D0F0u, 0x004E18CBu, kTimeInfo, {}},
			{0x009811E2u, 0x004E18EEu, kAny, {}},
		},
		1,
		{},
		{0x00000000u, 0x00000000u, 0x00000000u},
		{0x00000000u, 0x00000000u, 0x00000000u},
		0,
	},
};

void Compare(const EmulatorCase& expected) {
	Section(expected.name);
	EmulatorPorts ports(expected.input);
	bool result = logic::LoadBaseFormModels(ports);

	// The original's calls without the two the rebuild has no port for.
	std::vector<TracedCall> original;
	bool strrchrPlaced = true;
	for (std::size_t i = 0; i < expected.calls.size(); ++i) {
		const TracedCall& call = expected.calls[i];
		if (call.callee == kStrrchr) {
			strrchrPlaced = strrchrPlaced && i > 0 && i + 1 < expected.calls.size() && expected.calls[i - 1].callee == kVfn14 &&
							expected.calls[i + 1].callee == 0x00439EB0u &&
							call.args == std::vector<std::uint32_t>{kStack, 0x5Cu};
			continue;
		}
		if (call.callee == kSecurityCheckCookie) {
			continue;
		}
		original.push_back(call);
	}
	const TracedCall& last = expected.calls.back();
	Check(last.callee == kSecurityCheckCookie && expected.calls[expected.calls.size() - 2].callee == 0x0047D0F0u,
		"original: the /GS check is the last call, right after 0x0047D0F0 (not compared: no port)");
	Check(strrchrPlaced, "original: each _strrchr runs between vfn 0x14 and 0x00439EB0 on (buffer, '\\') (not compared: std::strrchr)");

	Check(ports.faults.empty(), "the rebuild only read what the case's memory holds");
	Check(ports.refusals.empty(), "the rebuild refused no entry (the original ran every one)");
	std::size_t common = std::min(original.size(), ports.calls.size());
	bool same = original.size() == ports.calls.size();
	for (std::size_t i = 0; i < common; ++i) {
		if (!SameCall(original[i], ports.calls[i])) {
			same = false;
			std::printf("    call %zu: original %s\n             rebuild  %s\n", i, Describe(original[i]).c_str(),
				Describe(ports.calls[i]).c_str());
		}
	}
	for (std::size_t i = common; i < original.size(); ++i) {
		std::printf("    call %zu: original %s, rebuild none\n", i, Describe(original[i]).c_str());
	}
	for (std::size_t i = common; i < ports.calls.size(); ++i) {
		std::printf("    call %zu: original none, rebuild %s\n", i, Describe(ports.calls[i]).c_str());
	}
	std::string calls = "the same " + std::to_string(original.size()) +
						" calls in the same order: callee, return address, ECX, arguments";
	Check(same, calls.c_str());
	Check(result == (expected.al != 0), expected.al != 0 ? "returns 1 (the original's AL)" : "returns 0 (the original's AL)");
	Check(ports.paths == expected.paths, "the same paths reach 0x00439EB0");
	Check(std::equal(std::begin(ports.createScale), std::end(ports.createScale), std::begin(expected.createScale)) &&
			  std::equal(std::begin(ports.cloneScale), std::end(ports.cloneScale), std::begin(expected.cloneScale)),
		"the context holds vfn 0xEC's float three times when 0x004430C0 / 0x00700610 run (bit patterns)");
	Check(ports.refCount == expected.refCount, "the created node's refcount ends where the original's does");
}

} // namespace

int main() {
	for (const EmulatorCase& emulatorCase : kCases) {
		Compare(emulatorCase);
	}
	return Failures();
}
