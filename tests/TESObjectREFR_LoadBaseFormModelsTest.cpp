// Flows of 0x004E1580 (proposed TESObjectREFR::LoadBaseFormModels), one
// section per branch of the mapped control flow in re/functions/0x004E1580.md
// (B1..B23), plus the refusal paths the rebuild adds where the original
// faults (B13 null name, B14 no separator and buffer overrun).
//
// The pure decisions are checked directly; the sequence is replayed through
// LoadBaseFormModels over a fake port set that records every call, so call
// order (the clock bracket) and which base-form read feeds which call are
// checked too.

#include "Check.h"
#include "engine/time/TESObjectREFR_LoadBaseFormModels.h"
#include "engine/time/TESObjectREFR_LoadBaseFormModelsLogic.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace logic = obng::engine::time::load_base_form_models;
using logic::AttachMode;
using logic::BaseFormPath;
using logic::ModelBuilder;
using logic::ModelPathBuffer;
using logic::NodeSource;
using logic::PathStatus;
using logic::SkinAttachReceiver;

namespace {

// An engine object as the fake sees it: a name for the trace and, for forms,
// the type byte at +0x04.
struct Obj {
	const char* name;
	std::uint8_t typeId = 0;
};
using H = const Obj*;

struct ListNode {
	const char* data;
	const ListNode* next;
};

struct FakePorts {
	// What the engine answers.
	Obj form{"form", 0x24};
	Obj otherForm{"otherForm", 0x24};
	Obj node{"niNode"};
	Obj otherNode{"otherNiNode"};
	Obj helper{"vfn164Object"};
	Obj model{"model"};
	Obj created{"created"};
	Obj cloned{"cloned"};
	Obj target{"target"};
	Obj named{"named"};
	Obj at1C{"at1C"};
	Obj slot0{"slot0"};
	Obj slot4{"slot4"};

	H baseForm = &form;
	H baseFormAfter004728C0 = nullptr;  // models 0x004728C0 changing this->baseForm
	std::uint32_t vfn168 = 1;
	H vfn164 = nullptr;
	H niNode = &node;
	std::optional<H> niNodeAfter004728C0;  // models 0x004728C0 changing this->niNode
	bool vfn190 = false;
	std::optional<std::uint32_t> lookupFlags;
	std::uint32_t createdBaseObjListFlags = 0;
	std::uint32_t probeResult = 0;
	const ListNode* listHead = nullptr;
	const char* basePath = "meshes\\creatures\\wolf\\skeleton.nif";
	std::vector<std::string> failingLoads;
	float scale = 1.5f;
	bool result00480820 = false;
	bool result00471B80 = false;
	H attachTarget = nullptr;
	H foundByName = nullptr;
	bool at1CIsNull = false;  // [target+0x1C] null: the original faults at 0x004E17FD
	long refCountAfterDecrement = 1;
	H contextSlot0 = nullptr;
	H contextSlot4 = nullptr;
	bool vfnFC = false;

	// What the fake saw.
	std::vector<std::string> trace;
	std::vector<std::string> loadedPaths;
	std::vector<float> contextScales;
	std::vector<std::string> namesLookedUp;
	std::vector<PathStatus> refusals;
	bool arg2 = false;
	bool arg3 = false;

	void Log(const std::string& entry) { trace.push_back(entry); }
	static std::string Name(H h) { return h != nullptr ? h->name : "null"; }

	H BaseForm() {
		Log("baseForm");
		return baseForm;
	}
	std::uint32_t Vfn168() {
		Log("vfn168");
		return vfn168;
	}
	void ClockEnter() { Log("clockEnter"); }
	void ClockLeave() { Log("clockLeave"); }
	H Vfn164() {
		Log("vfn164");
		return vfn164;
	}
	void Call004728C0(H object) {
		Log("004728C0 " + Name(object));
		if (baseFormAfter004728C0 != nullptr) {
			baseForm = baseFormAfter004728C0;
		}
		if (niNodeAfter004728C0) {
			niNode = *niNodeAfter004728C0;
		}
	}
	std::uint8_t TypeId(H form) { return form->typeId; }
	H NiNode() { return niNode; }
	void Call00526DB0(H form) { Log("00526DB0 " + Name(form)); }
	bool VfnFC(H form) {
		Log("vfnFC " + Name(form));
		return vfnFC;
	}
	bool Vfn190() {
		Log("vfn190");
		return vfn190;
	}
	std::optional<std::uint32_t> Lookup005E0380Flags() {
		Log("005E0380");
		return lookupFlags;
	}
	std::uint32_t CreatedBaseObjListFlags() { return createdBaseObjListFlags; }
	std::uint32_t Call004533F0() {
		Log("004533F0");
		return probeResult;
	}
	void Call0051D460(H form) { Log("0051D460 " + Name(form)); }
	void Call0051E240(H form, bool a2, bool a3) {
		Log("0051E240 " + Name(form));
		arg2 = a2;
		arg3 = a3;
	}
	const ListNode* ModelListHead(H form) {
		Log("listHead " + Name(form));
		return listHead;
	}
	const ListNode* ListNext(const ListNode* entry) { return entry->next; }
	const char* ListData(const ListNode* entry) { return entry->data; }
	const char* BaseModelPath(H form) {
		Log("basePath " + Name(form));
		return basePath;
	}
	void RefusePath(PathStatus status, const char* /*fileName*/) {
		Log("refuse");
		refusals.push_back(status);
	}
	H LoadModel(const char* path) {
		Log("load");
		loadedPaths.emplace_back(path);
		for (const std::string& failing : failingLoads) {
			if (failing == path) {
				return nullptr;
			}
		}
		return &model;
	}
	float Vfn0EC() {
		Log("vfn0EC");
		return scale;
	}
	void ConstructNodeContext(float contextScale) {
		Log("context");
		contextScales.push_back(contextScale);
	}
	bool Call00480820(H loaded) {
		Log("00480820 " + Name(loaded));
		return result00480820;
	}
	H Create004430C0(H loaded) {
		Log("004430C0 " + Name(loaded));
		return &created;
	}
	H Clone00700610(H loaded) {
		Log("00700610 " + Name(loaded));
		return &cloned;
	}
	bool Call00471B80(H attached) {
		Log("00471B80 " + Name(attached));
		return result00471B80;
	}
	H Find004D96F0(H parent) {
		Log("004D96F0 " + Name(parent));
		return attachTarget;
	}
	H FindByName(H object, const char* name) {
		Log("vfn58 " + Name(object));
		namesLookedUp.emplace_back(name);
		return foundByName;
	}
	H ObjectAt1C(H object) {
		Log("at1C " + Name(object));
		return at1CIsNull ? nullptr : &at1C;
	}
	void AttachVfn84(H receiver, H attached) { Log("vfn84 " + Name(receiver) + " " + Name(attached)); }
	void Call00478EC0(H parent, H attached) { Log("00478EC0 " + Name(parent) + " " + Name(attached)); }
	void Call00479140(H parent, H attached) { Log("00479140 " + Name(parent) + " " + Name(attached)); }
	long DecrementRefCount(H object) {
		Log("decrement " + Name(object));
		return refCountAfterDecrement;
	}
	void DeleteObject(H object) { Log("delete " + Name(object)); }
	H NodeContextSlot(int index) { return index == 0 ? contextSlot0 : contextSlot4; }
};

int Count(const std::vector<std::string>& trace, const std::string& entry) {
	int count = 0;
	for (const std::string& seen : trace) {
		count += seen == entry ? 1 : 0;
	}
	return count;
}

bool Has(const std::vector<std::string>& trace, const std::string& entry) {
	return Count(trace, entry) > 0;
}

int IndexOf(const std::vector<std::string>& trace, const std::string& entry) {
	for (std::size_t i = 0; i < trace.size(); ++i) {
		if (trace[i] == entry) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

bool HasPrefix(const std::vector<std::string>& trace, const std::string& prefix) {
	for (const std::string& seen : trace) {
		if (seen.rfind(prefix, 0) == 0) {
			return true;
		}
	}
	return false;
}

// A type-0x24 reference with one list entry that loads and attaches.
FakePorts OneEntryCreature(const ListNode& entry) {
	FakePorts ports;
	ports.listHead = &entry;
	return ports;
}

// A buffer whose every byte is non-zero, so a missing terminator shows
// instead of being supplied by zero-initialisation.
constexpr char kGarbage = 0x7F;
ModelPathBuffer GarbageBuffer() {
	ModelPathBuffer buffer;
	buffer.fill(kGarbage);
	return buffer;
}

// The buffer's string up to its first NUL, or "<unterminated>" when the
// buffer holds none.
std::string Terminated(const ModelPathBuffer& buffer) {
	auto terminator = std::find(buffer.begin(), buffer.end(), '\0');
	return terminator != buffer.end() ? std::string(buffer.begin(), terminator) : std::string("<unterminated>");
}

std::string Composed(const char* base, const char* name) {
	ModelPathBuffer buffer = GarbageBuffer();
	PathStatus status = logic::ComposeSiblingModelPath(base, name, buffer);
	return status == PathStatus::Composed ? Terminated(buffer) : std::string("<refused>");
}

} // namespace

int main() {
	Section("B1 clock bracket entry: vfn[0x168] runs first, then 0x0047D120, before any branch");
	{
		FakePorts ports;
		ports.form.typeId = 0x10;
		logic::LoadBaseFormModels(ports);
		Check(IndexOf(ports.trace, "vfn168") >= 0 && IndexOf(ports.trace, "vfn168") < IndexOf(ports.trace, "clockEnter"),
			"vfn[0x168] (0x004E15CD) before the enter call (0x004E15D6)");
		Check(IndexOf(ports.trace, "clockEnter") < IndexOf(ports.trace, "vfn164"),
			"the enter call precedes vfn[0x164] (0x004E15E6)");
		Check(Count(ports.trace, "clockEnter") == 1, "entered exactly once");
	}

	Section("B2 vfn[0x164] result: nonzero is passed to 0x004728C0, zero skips it");
	{
		FakePorts with;
		with.form.typeId = 0x10;
		with.vfn164 = &with.helper;
		logic::LoadBaseFormModels(with);
		Check(Has(with.trace, "004728C0 vfn164Object"), "0x004728C0 called on the vfn[0x164] object");
		FakePorts without;
		without.form.typeId = 0x10;
		logic::LoadBaseFormModels(without);
		Check(!HasPrefix(without.trace, "004728C0"), "no 0x004728C0 call for a null result (JZ 0x004E15EA)");
	}

	Section("B3 type 0x23 takes the 0x23 path");
	{
		Check(logic::ClassifyBaseForm(0x23) == BaseFormPath::Type23, "0x23 -> Type23 (JZ 0x004E1604)");
		FakePorts ports;
		ports.form.typeId = 0x23;
		ports.vfn168 = 0;
		bool result = logic::LoadBaseFormModels(ports);
		Check(result, "returns 1 on the 0x23 path (0x004E18AB)");
		Check(!Has(ports.trace, "vfn190") && !HasPrefix(ports.trace, "vfnFC"), "neither the 0x24 nor the other path runs");
	}

	Section("B3a type 0x23 action: vfn[0x168] nonzero calls 0x00526DB0, zero skips");
	{
		Check(logic::RunsType23Action(7), "nonzero runs the action");
		Check(!logic::RunsType23Action(0), "zero skips it (JZ 0x004E18A1)");
		FakePorts ports;
		ports.form.typeId = 0x23;
		ports.vfn168 = 1;
		Check(logic::LoadBaseFormModels(ports), "returns 1");
		Check(Has(ports.trace, "00526DB0 form"), "0x00526DB0 runs on the base form");
		FakePorts skipped;
		skipped.form.typeId = 0x23;
		skipped.vfn168 = 0;
		logic::LoadBaseFormModels(skipped);
		Check(!HasPrefix(skipped.trace, "00526DB0"), "zero gate: no 0x00526DB0 call");
	}

	Section("B3a uses the base form read before vfn[0x168]; the type byte comes from the re-read");
	{
		FakePorts ports;
		ports.form.typeId = 0x24;           // the cached EBX form
		ports.otherForm.typeId = 0x23;      // what this+0x1C holds after 0x004728C0
		ports.vfn164 = &ports.helper;
		ports.baseFormAfter004728C0 = &ports.otherForm;
		logic::LoadBaseFormModels(ports);
		Check(Has(ports.trace, "00526DB0 form"),
			"type 0x23 read at 0x004E15F3, action on EBX cached at 0x004E15C0");
	}

	Section("B4 type 0x24 takes the 0x24 path, anything else the other path");
	{
		Check(logic::ClassifyBaseForm(0x24) == BaseFormPath::Type24, "0x24 -> Type24");
		Check(logic::ClassifyBaseForm(0x22) == BaseFormPath::Other, "0x22 -> Other");
		Check(logic::ClassifyBaseForm(0x25) == BaseFormPath::Other, "0x25 -> Other (JNZ 0x004E160D)");
		Check(logic::ClassifyBaseForm(0x00) == BaseFormPath::Other, "0x00 -> Other");
		FakePorts ports;
		logic::LoadBaseFormModels(ports);
		Check(Has(ports.trace, "vfn190") && !HasPrefix(ports.trace, "vfnFC"), "0x24 runs the build, not vfn[0xFC]");
	}

	Section("B5 no node: type 0x24 with this+0x3C null exits with 1 and builds nothing");
	{
		FakePorts ports;
		ports.niNode = nullptr;
		bool result = logic::LoadBaseFormModels(ports);
		Check(result, "returns 1 (0x004E189B)");
		Check(!Has(ports.trace, "vfn190") && !HasPrefix(ports.trace, "0051E240") && !HasPrefix(ports.trace, "listHead"),
			"no lookup, no builder, no list walk (JZ 0x004E1615)");
	}

	Section("B4 type 0x24 uses the cached base form for builder, list and model path");
	{
		ListNode entry{"a.nif", nullptr};
		FakePorts built = OneEntryCreature(entry);
		built.vfn164 = &built.helper;
		built.baseFormAfter004728C0 = &built.otherForm;  // also type 0x24
		logic::LoadBaseFormModels(built);
		Check(Has(built.trace, "0051E240 form"), "0x0051E240 on EBX (MOV ECX,EBX at 0x004E1695)");
		Check(Has(built.trace, "listHead form"), "the list on EBX+0xEC (0x004E169C)");
		Check(Has(built.trace, "basePath form"), "the model path on EBX+0xAC (0x004E16D3)");
		Check(!HasPrefix(built.trace, "0051E240 otherForm") && !HasPrefix(built.trace, "listHead otherForm") &&
				!HasPrefix(built.trace, "basePath otherForm"),
			"nothing on the this+0x1C 0x004728C0 left behind");
		FakePorts alt = OneEntryCreature(entry);
		alt.vfn164 = &alt.helper;
		alt.baseFormAfter004728C0 = &alt.otherForm;
		alt.createdBaseObjListFlags = 2;
		alt.probeResult = 0x08000000u;
		logic::LoadBaseFormModels(alt);
		Check(Has(alt.trace, "0051D460 form") && !HasPrefix(alt.trace, "0051D460 otherForm"),
			"0x0051D460 on EBX (MOV ECX,EBX at 0x004E167F)");
	}

	Section("B5/B21 this+0x3C is read after 0x004728C0 (0x004E15FD after 0x004E15EE)");
	{
		ListNode entry{"a.nif", nullptr};
		FakePorts moved = OneEntryCreature(entry);
		moved.vfn164 = &moved.helper;
		moved.niNodeAfter004728C0 = &moved.otherNode;
		logic::LoadBaseFormModels(moved);
		Check(Has(moved.trace, "004D96F0 otherNiNode") && Has(moved.trace, "00479140 otherNiNode cloned"),
			"0x004D96F0 and the attach get the node 0x004728C0 left behind");
		Check(!HasPrefix(moved.trace, "004D96F0 niNode") && !HasPrefix(moved.trace, "00479140 niNode"),
			"never the node from before 0x004728C0");
		FakePorts appeared = OneEntryCreature(entry);
		appeared.vfn164 = &appeared.helper;
		appeared.niNode = nullptr;
		appeared.niNodeAfter004728C0 = &appeared.otherNode;
		logic::LoadBaseFormModels(appeared);
		Check(Has(appeared.trace, "vfn190") && Has(appeared.trace, "00479140 otherNiNode cloned"),
			"the node gate (0x004E1615) tests the read after 0x004728C0");
		FakePorts vanished = OneEntryCreature(entry);
		vanished.vfn164 = &vanished.helper;
		vanished.niNodeAfter004728C0 = std::optional<H>(nullptr);
		Check(logic::LoadBaseFormModels(vanished), "returns 1 (0x004E189B)");
		Check(!Has(vanished.trace, "vfn190") && !HasPrefix(vanished.trace, "listHead"),
			"a node cleared by 0x004728C0 refuses the build");
	}

	Section("B6 vfn[0x190] false skips the 0x005E0380 lookup; both build args stay 1");
	{
		FakePorts ports;
		ports.vfn190 = false;
		ports.lookupFlags = 0xFFFFFFFFu;
		logic::LoadBaseFormModels(ports);
		Check(!Has(ports.trace, "005E0380"), "0x005E0380 not called (JZ 0x004E1634)");
		Check(ports.arg2 && ports.arg3, "0x0051E240 gets (1, 1)");
	}

	Section("B7 0x005E0380 null skips the flag test; both build args stay 1");
	{
		logic::BuildFlags flags = logic::BuildFlagsFor(std::nullopt);
		Check(flags.arg2 && flags.arg3, "no lookup result -> (1, 1) (JZ 0x004E163F)");
		FakePorts ports;
		ports.vfn190 = true;
		ports.lookupFlags = std::nullopt;
		logic::LoadBaseFormModels(ports);
		Check(Has(ports.trace, "005E0380"), "0x005E0380 called");
		Check(ports.arg2 && ports.arg3, "0x0051E240 gets (1, 1)");
	}

	Section("B8 flags bit 20 clears arg 2 only");
	{
		logic::BuildFlags flags = logic::BuildFlagsFor(1u << 20);
		Check(!flags.arg2 && flags.arg3, "bit 20 -> (0, 1) (0x004E164C/0x004E164E)");
		logic::BuildFlags clear = logic::BuildFlagsFor(0u);
		Check(clear.arg2 && clear.arg3, "no bit -> (1, 1)");
		logic::BuildFlags neighbours = logic::BuildFlagsFor(~((1u << 20) | (1u << 21)));
		Check(neighbours.arg2 && neighbours.arg3, "every other bit set -> (1, 1)");
		FakePorts ports;
		ports.vfn190 = true;
		ports.lookupFlags = 1u << 20;
		logic::LoadBaseFormModels(ports);
		Check(!ports.arg2 && ports.arg3, "0x0051E240 gets (0, 1)");
	}

	Section("B9 flags bit 21 clears arg 3 only; both bits clear both");
	{
		logic::BuildFlags flags = logic::BuildFlagsFor(1u << 21);
		Check(flags.arg2 && !flags.arg3, "bit 21 -> (1, 0) (0x004E1658/0x004E165A)");
		logic::BuildFlags both = logic::BuildFlagsFor((1u << 20) | (1u << 21));
		Check(!both.arg2 && !both.arg3, "bits 20 and 21 -> (0, 0)");
		FakePorts ports;
		ports.vfn190 = true;
		ports.lookupFlags = 1u << 21;
		logic::LoadBaseFormModels(ports);
		Check(ports.arg2 && !ports.arg3, "0x0051E240 gets (1, 0)");
	}

	Section("B10 g_createdBaseObjList bit 1 clear: no 0x004533F0 probe, builder 0x0051E240");
	{
		Check(!logic::Probes004533F0(0u), "bit 1 clear -> no probe (JZ 0x004E166D)");
		Check(!logic::Probes004533F0(~2u), "every other bit set -> no probe");
		Check(logic::Probes004533F0(2u), "bit 1 set -> probe");
		Check(logic::BuilderFor(std::nullopt) == ModelBuilder::Build0051E240, "no probe -> 0x0051E240");
		FakePorts ports;
		ports.createdBaseObjListFlags = 0;
		ports.probeResult = 0x08000000u;
		logic::LoadBaseFormModels(ports);
		Check(!Has(ports.trace, "004533F0"), "0x004533F0 not called");
		Check(Has(ports.trace, "0051E240 form") && !HasPrefix(ports.trace, "0051D460"), "0x0051E240 on the cached form");
	}

	Section("B11 0x004533F0 bit 27: set -> 0x0051D460, clear -> 0x0051E240");
	{
		Check(logic::BuilderFor(0x08000000u) == ModelBuilder::Build0051D460, "bit 27 -> 0x0051D460");
		Check(logic::BuilderFor(~0x08000000u) == ModelBuilder::Build0051E240, "all but bit 27 -> 0x0051E240");
		ListNode entry{"a.nif", nullptr};
		FakePorts set = OneEntryCreature(entry);
		set.createdBaseObjListFlags = 2;
		set.probeResult = 0x08000000u;
		logic::LoadBaseFormModels(set);
		Check(Has(set.trace, "004533F0") && Has(set.trace, "0051D460 form") && !HasPrefix(set.trace, "0051E240"),
			"probe, then 0x0051D460 only");
		Check(IndexOf(set.trace, "0051D460 form") < IndexOf(set.trace, "listHead form"),
			"the list is walked after 0x0051D460 (JMP 0x004E169C at 0x004E1686)");
		Check(set.loadedPaths.size() == 1 && set.loadedPaths[0] == "meshes\\creatures\\wolf\\a.nif" &&
				Has(set.trace, "00479140 niNode cloned"),
			"the entry loads and attaches");
		FakePorts clear;
		clear.createdBaseObjListFlags = 2;
		clear.probeResult = 0;
		clear.vfn190 = true;
		clear.lookupFlags = 1u << 20;
		logic::LoadBaseFormModels(clear);
		Check(Has(clear.trace, "004533F0") && Has(clear.trace, "0051E240 form"), "probe, then 0x0051E240");
		Check(!clear.arg2 && clear.arg3, "the lookup's args reach 0x0051E240 after the probe (join 0x004E1688)");
	}

	Section("B12 empty list: 0x006899C0 null exits with 1 after the builder");
	{
		FakePorts ports;
		ports.listHead = nullptr;
		bool result = logic::LoadBaseFormModels(ports);
		Check(result, "returns 1 (0x004E16AF -> 0x004E189B)");
		Check(Has(ports.trace, "listHead form"), "the list comes from the cached form");
		Check(!Has(ports.trace, "load") && !HasPrefix(ports.trace, "basePath"), "no entry is visited");
	}

	Section("B13 loop head: next != 0 or data != 0 enters the body");
	{
		Check(!logic::VisitsListNode(false, false), "next 0, data 0 -> exit (0x004E16CD)");
		Check(logic::VisitsListNode(false, true), "next 0, data set -> body");
		Check(logic::VisitsListNode(true, false), "next set, data 0 -> body (0x004E16C8)");
		Check(logic::VisitsListNode(true, true), "next set, data set -> body");
		ListNode sentinel{nullptr, nullptr};
		FakePorts empty = OneEntryCreature(sentinel);
		Check(logic::LoadBaseFormModels(empty), "sentinel head returns 1");
		Check(!Has(empty.trace, "load") && !Has(empty.trace, "refuse"), "the sentinel is not visited");
	}

	Section("B13 refusal: a node with next set and data 0 is refused, the walk goes on");
	{
		ListNode last{"horns.nif", nullptr};
		ListNode hole{nullptr, &last};
		FakePorts ports = OneEntryCreature(hole);
		Check(logic::LoadBaseFormModels(ports), "returns 1");
		Check(ports.refusals.size() == 1 && ports.refusals[0] == PathStatus::NoFileName,
			"null name refused (the original dereferences it at 0x004E1710)");
		Check(ports.loadedPaths.size() == 1 && ports.loadedPaths[0] == "meshes\\creatures\\wolf\\horns.nif",
			"the next entry still loads");
	}

	Section("B14 path copy: the base path's folder plus the entry's name");
	{
		Check(Composed("meshes\\a\\b.nif", "c.nif") == "meshes\\a\\c.nif", "name replaces the file after the last '\\'");
		Check(Composed("a\\b\\", "c.nif") == "a\\b\\c.nif", "trailing separator keeps the folder");
		Check(Composed("a\\b.nif", "") == "a\\", "an empty name leaves the folder");
		Check(Composed("a/b\\c/d.nif", "e.nif") == "a/b\\e.nif", "only '\\' (0x5C, 0x004E16FA) separates");
		ListNode entry{"tail.nif", nullptr};
		FakePorts ports = OneEntryCreature(entry);
		logic::LoadBaseFormModels(ports);
		Check(ports.loadedPaths.size() == 1 && ports.loadedPaths[0] == "meshes\\creatures\\wolf\\tail.nif",
			"0x00439EB0 gets the composed path");
		Check(Has(ports.trace, "basePath form"), "the base path is read from the cached form (EBX+0xAC)");
	}

	Section("B14 refusals: no separator, null base path, buffer overrun");
	{
		ModelPathBuffer buffer{};
		Check(logic::ComposeSiblingModelPath("b.nif", "c.nif", buffer) == PathStatus::NoSeparator,
			"no '\\' refused (the original writes through address 1, 0x004E1707)");
		Check(logic::ComposeSiblingModelPath(nullptr, "c.nif", buffer) == PathStatus::NoBasePath, "null base path refused");
		Check(logic::ComposeSiblingModelPath("a\\b.nif", nullptr, buffer) == PathStatus::NoFileName, "null name refused");

		std::string fits(logic::kModelPathBufferSize - 1, 'x');
		fits[0] = '\\';
		buffer = GarbageBuffer();
		Check(logic::ComposeSiblingModelPath(fits.c_str(), "", buffer) == PathStatus::Composed,
			"a base path of 0x103 chars plus NUL fills the 0x104 buffer exactly");
		Check(Terminated(buffer) == "\\", "and leaves its folder, NUL-terminated");
		std::string tooLong(logic::kModelPathBufferSize, 'x');
		tooLong[0] = '\\';
		Check(logic::ComposeSiblingModelPath(tooLong.c_str(), "", buffer) == PathStatus::BaseTooLong,
			"0x104 chars plus NUL overrun the first copy (0x004E16E8)");

		std::string folder = "a\\";
		std::string nameFits(logic::kModelPathBufferSize - folder.size() - 1, 'n');
		buffer = GarbageBuffer();
		Check(logic::ComposeSiblingModelPath((folder + "b.nif").c_str(), nameFits.c_str(), buffer) == PathStatus::Composed,
			"folder plus name plus NUL of exactly 0x104 fits");
		Check(buffer[logic::kModelPathBufferSize - 1] == '\0' &&
				std::strlen(buffer.data()) == logic::kModelPathBufferSize - 1,
			"and is NUL-terminated at 0x103 (the copy loop 0x004E1710..0x004E171C ends on the NUL)");
		Check(Terminated(buffer) == folder + nameFits, "and holds folder plus name");
		std::string nameTooLong = nameFits + "n";
		Check(logic::ComposeSiblingModelPath((folder + "b.nif").c_str(), nameTooLong.c_str(), buffer) ==
				PathStatus::ResultTooLong,
			"one more byte overruns the second copy (0x004E1710)");

		ListNode last{"ok.nif", nullptr};
		ListNode first{"x.nif", &last};
		FakePorts ports = OneEntryCreature(first);
		ports.basePath = "noseparator.nif";
		logic::LoadBaseFormModels(ports);
		Check(ports.refusals.size() == 2 && ports.refusals[0] == PathStatus::NoSeparator,
			"each entry refused and reported, none loaded");
		Check(ports.loadedPaths.empty(), "nothing reaches 0x00439EB0");
	}

	Section("B15 load failed: 0x00439EB0 null goes to the next entry");
	{
		ListNode second{"b.nif", nullptr};
		ListNode first{"a.nif", &second};
		FakePorts ports = OneEntryCreature(first);
		ports.failingLoads.push_back("meshes\\creatures\\wolf\\a.nif");
		Check(logic::LoadBaseFormModels(ports), "returns 1 regardless of load success");
		Check(ports.loadedPaths.size() == 2, "both entries tried");
		Check(ports.contextScales.size() == 1, "vfn[0xEC] and the context only for the loaded one (JZ 0x004E1738)");
		Check(Count(ports.trace, "00479140 niNode cloned") == 1, "one attach");
	}

	Section("B16 node source: 0x00480820 true -> 0x004430C0 held in a NiPointer, false -> 0x00700610");
	{
		Check(logic::NodeSourceFor(true) == NodeSource::Create004430C0, "true -> 0x004430C0 (0x004E1788)");
		Check(logic::NodeSourceFor(false) == NodeSource::Clone00700610, "false -> 0x00700610");
		ListNode entry{"a.nif", nullptr};
		FakePorts created = OneEntryCreature(entry);
		created.result00480820 = true;
		logic::LoadBaseFormModels(created);
		Check(Has(created.trace, "004430C0 model") && !HasPrefix(created.trace, "00700610"), "created via 0x004430C0");
		Check(Has(created.trace, "00479140 niNode created"), "the created node is attached");
		Check(created.contextScales.size() == 1 && created.contextScales[0] == 1.5f, "context carries vfn[0xEC]");
		FakePorts cloned = OneEntryCreature(entry);
		logic::LoadBaseFormModels(cloned);
		Check(Has(cloned.trace, "00700610 model") && !HasPrefix(cloned.trace, "004430C0"), "cloned via 0x00700610");
		Check(Has(cloned.trace, "00479140 niNode cloned"), "the cloned node is attached");
	}

	Section("B17 attach mode: 0x00471B80 true and a 0x004D96F0 target -> SkinAttachment, else 0x00479140");
	{
		Check(logic::AttachModeFor(true, true) == AttachMode::SkinAttachment, "true, target -> skin");
		Check(logic::AttachModeFor(true, false) == AttachMode::Plain00479140, "true, no target -> plain (0x004E17DD)");
		Check(logic::AttachModeFor(false, true) == AttachMode::Plain00479140, "false, target -> plain (0x004E17D9)");
		Check(logic::AttachModeFor(false, false) == AttachMode::Plain00479140, "false, no target -> plain");
		ListNode entry{"a.nif", nullptr};
		FakePorts plain = OneEntryCreature(entry);
		plain.result00471B80 = true;
		plain.attachTarget = nullptr;
		logic::LoadBaseFormModels(plain);
		Check(Has(plain.trace, "004D96F0 niNode"), "0x004D96F0 runs when its own result is null");
		Check(IndexOf(plain.trace, "00471B80 cloned") < IndexOf(plain.trace, "004D96F0 niNode"),
			"0x00471B80 (0x004E17BA) before 0x004D96F0 (0x004E17D0)");
		Check(Has(plain.trace, "00479140 niNode cloned") && !HasPrefix(plain.trace, "00478EC0"), "plain attach");
		FakePorts noSkin = OneEntryCreature(entry);
		noSkin.result00471B80 = false;
		noSkin.attachTarget = &noSkin.target;
		logic::LoadBaseFormModels(noSkin);
		Check(Has(noSkin.trace, "004D96F0 niNode"),
			"0x004D96F0 runs even when 0x00471B80 returned false (CALL 0x004E17D0 before TEST BL,BL 0x004E17D5)");
		Check(IndexOf(noSkin.trace, "00471B80 cloned") < IndexOf(noSkin.trace, "004D96F0 niNode"),
			"0x00471B80 first on this path too");
		Check(Has(noSkin.trace, "00479140 niNode cloned") && !HasPrefix(noSkin.trace, "vfn58"), "plain attach");
	}

	Section("B18 SkinAttachment lookup: found -> its vfn[0x84], not found -> vfn[0x84] of target+0x1C");
	{
		Check(logic::ReceiverFor(true) == SkinAttachReceiver::NamedObject, "found -> named object");
		Check(logic::ReceiverFor(false) == SkinAttachReceiver::ObjectAt1C, "null -> object at +0x1C (0x004E17FA)");
		Check(std::strcmp(logic::kSkinAttachmentName, "SkinAttachment") == 0, "the name pushed at 0x004E17E4");
		ListNode entry{"a.nif", nullptr};
		FakePorts found = OneEntryCreature(entry);
		found.result00471B80 = true;
		found.attachTarget = &found.target;
		found.foundByName = &found.named;
		logic::LoadBaseFormModels(found);
		Check(found.namesLookedUp.size() == 1 && found.namesLookedUp[0] == "SkinAttachment", "vfn[0x58](\"SkinAttachment\")");
		Check(Has(found.trace, "vfn84 named cloned") && !HasPrefix(found.trace, "at1C"), "named object receives the node");
		Check(IndexOf(found.trace, "vfn84 named cloned") < IndexOf(found.trace, "00478EC0 niNode cloned"),
			"then 0x00478EC0 (0x004E1811)");
		Check(!HasPrefix(found.trace, "00479140"), "no plain attach");
		FakePorts fallback = OneEntryCreature(entry);
		fallback.result00471B80 = true;
		fallback.attachTarget = &fallback.target;
		logic::LoadBaseFormModels(fallback);
		Check(Has(fallback.trace, "at1C target") && Has(fallback.trace, "vfn84 at1C cloned"), "fallback to target+0x1C");
		Check(Has(fallback.trace, "00478EC0 niNode cloned"), "then 0x00478EC0");
		FakePorts nullAt1C = OneEntryCreature(entry);
		nullAt1C.result00471B80 = true;
		nullAt1C.attachTarget = &nullAt1C.target;
		nullAt1C.at1CIsNull = true;
		logic::LoadBaseFormModels(nullAt1C);
		Check(Has(nullAt1C.trace, "vfn84 null cloned"),
			"not modelled: a null [target+0x1C] reaches the attach port unchanged (the original faults at 0x004E17FD)");
	}

	Section("B19 NiPointer release: non-null is decremented, deleted only at zero");
	{
		Check(logic::DeletesAfterDecrement(0), "zero deletes (JNZ 0x004E184D not taken)");
		Check(!logic::DeletesAfterDecrement(1), "one keeps");
		Check(!logic::DeletesAfterDecrement(-1), "negative keeps (only zero deletes)");
		ListNode entry{"a.nif", nullptr};
		FakePorts last = OneEntryCreature(entry);
		last.result00480820 = true;
		last.refCountAfterDecrement = 0;
		logic::LoadBaseFormModels(last);
		Check(Has(last.trace, "decrement created") && Has(last.trace, "delete created"), "last reference deleted");
		Check(IndexOf(last.trace, "00479140 niNode created") < IndexOf(last.trace, "decrement created"),
			"released after the attach");
		FakePorts shared = OneEntryCreature(entry);
		shared.result00480820 = true;
		shared.refCountAfterDecrement = 2;
		logic::LoadBaseFormModels(shared);
		Check(Has(shared.trace, "decrement created") && !Has(shared.trace, "delete created"), "shared reference kept");
		FakePorts cloned = OneEntryCreature(entry);
		logic::LoadBaseFormModels(cloned);
		Check(!HasPrefix(cloned.trace, "decrement"), "the clone path holds no NiPointer (JZ 0x004E183F)");
	}

	Section("B20 local object release: each non-null context slot gets vfn[0](1)");
	{
		ListNode entry{"a.nif", nullptr};
		FakePorts none = OneEntryCreature(entry);
		logic::LoadBaseFormModels(none);
		Check(!HasPrefix(none.trace, "delete"), "both null: nothing deleted (JZ 0x004E186A, 0x004E187A)");
		FakePorts both = OneEntryCreature(entry);
		both.result00480820 = true;
		both.refCountAfterDecrement = 0;
		both.contextSlot0 = &both.slot0;
		both.contextSlot4 = &both.slot4;
		logic::LoadBaseFormModels(both);
		int pointer = IndexOf(both.trace, "delete created");
		int first = IndexOf(both.trace, "delete slot0");
		int second = IndexOf(both.trace, "delete slot4");
		Check(pointer >= 0 && pointer < first && first < second, "NiPointer, then +0x00, then +0x04");
		FakePorts onlySecond = OneEntryCreature(entry);
		onlySecond.contextSlot4 = &onlySecond.slot4;
		logic::LoadBaseFormModels(onlySecond);
		Check(!Has(onlySecond.trace, "delete slot0") && Has(onlySecond.trace, "delete slot4"), "slots decided apart");
	}

	Section("B21 loop continue: every entry up to next == 0 is visited, in order");
	{
		ListNode third{"c.nif", nullptr};
		ListNode second{"b.nif", &third};
		ListNode first{"a.nif", &second};
		FakePorts ports = OneEntryCreature(first);
		Check(logic::LoadBaseFormModels(ports), "returns 1");
		Check(ports.loadedPaths.size() == 3 && ports.loadedPaths[0] == "meshes\\creatures\\wolf\\a.nif" &&
				ports.loadedPaths[2] == "meshes\\creatures\\wolf\\c.nif",
			"a, b, c in list order");
		Check(Count(ports.trace, "basePath form") == 3, "the base path is fetched per entry (0x004E16E2)");
		ListNode tail{nullptr, nullptr};
		ListNode head{"a.nif", &tail};
		FakePorts sentinelTail = OneEntryCreature(head);
		logic::LoadBaseFormModels(sentinelTail);
		Check(sentinelTail.loadedPaths.size() == 1 && sentinelTail.refusals.empty(),
			"a trailing empty node ends the walk without a refusal");
	}

	Section("B21 the attach parent is this+0x3C read once at 0x004E15FD");
	{
		ListNode second{"b.nif", nullptr};
		ListNode first{"a.nif", &second};
		FakePorts ports = OneEntryCreature(first);
		logic::LoadBaseFormModels(ports);
		Check(Count(ports.trace, "00479140 niNode cloned") == 2, "both entries attach to the same node");
	}

	Section("B22 other type: the result is vfn[0xFC] of the base form re-read at 0x004E18AF");
	{
		Check(logic::ResultFor(BaseFormPath::Other, true), "other, true -> true");
		Check(!logic::ResultFor(BaseFormPath::Other, false), "other, false -> false");
		Check(logic::ResultFor(BaseFormPath::Type23, false), "0x23 -> true (0x004E18AB)");
		Check(logic::ResultFor(BaseFormPath::Type24, false), "0x24 -> true (0x004E189B)");
		FakePorts yes;
		yes.form.typeId = 0x10;
		yes.vfnFC = true;
		Check(logic::LoadBaseFormModels(yes), "vfn[0xFC] true -> returns true");
		FakePorts no;
		no.form.typeId = 0x10;
		no.vfnFC = false;
		Check(!logic::LoadBaseFormModels(no), "vfn[0xFC] false -> returns false");
		FakePorts reread;
		reread.form.typeId = 0x24;
		reread.otherForm.typeId = 0x10;
		reread.vfn164 = &reread.helper;
		reread.baseFormAfter004728C0 = &reread.otherForm;
		logic::LoadBaseFormModels(reread);
		Check(Has(reread.trace, "vfnFC otherForm"), "vfn[0xFC] on the current this+0x1C, not the cached EBX");
	}

	Section("B23 clock bracket exit: 0x0047D0F0 is the last call on every path");
	{
		auto leavesLast = [](FakePorts& ports) {
			logic::LoadBaseFormModels(ports);
			return !ports.trace.empty() && ports.trace.back() == "clockLeave" && Count(ports.trace, "clockLeave") == 1;
		};
		FakePorts type23;
		type23.form.typeId = 0x23;
		Check(leavesLast(type23), "type 0x23 path");
		FakePorts other;
		other.form.typeId = 0x10;
		Check(leavesLast(other), "other path");
		FakePorts noNode;
		noNode.niNode = nullptr;
		Check(leavesLast(noNode), "type 0x24, no node");
		FakePorts emptyList;
		Check(leavesLast(emptyList), "type 0x24, empty list");
		ListNode entry{"a.nif", nullptr};
		FakePorts walked = OneEntryCreature(entry);
		walked.result00480820 = true;
		walked.refCountAfterDecrement = 0;
		Check(leavesLast(walked), "type 0x24, walked list");
		FakePorts refused = OneEntryCreature(entry);
		refused.basePath = "flat.nif";
		Check(leavesLast(refused), "type 0x24, refused path");
	}

	Section("Engine-facing layout: offsets the function reads");
	{
		namespace engine = obng::engine::time::load_base_form_models::engine;
		Check(engine::kVfnGate23 == 0x168 && engine::kVfn164 == 0x164 && engine::kVfn190 == 0x190 &&
				engine::kVfnScale == 0x0EC && engine::kFormVfnFC == 0x0FC,
			"vtable slots 0x168, 0x164, 0x190, 0xEC and form 0xFC");
		Check(engine::kModelLoaderAddress == 0x00B33A1C && engine::kTESAddress == 0x00B333A0 &&
				engine::kCreatedBaseObjListAddress == 0x00B33B00,
			"globals agree with xOBSE (re/symbols/xobse.csv)");
	}

	return Failures();
}
