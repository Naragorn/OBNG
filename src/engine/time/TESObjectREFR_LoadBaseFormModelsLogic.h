#pragma once

// The decisions of 0x004E1580 (proposed TESObjectREFR::LoadBaseFormModels),
// as pure functions over plain values, and the body's call sequence replayed
// over injected ports. Evidence: re/functions/0x004E1580.md and the export
// re/export/frame-clock/0x004E1580/asm.s; every address below is an
// instruction address in that asm.s (read) unless marked otherwise.
//
// What the body does (read): it brackets everything with the TimeInfo pair
// 0x0047D120 / 0x0047D0F0 on g_timeInfo, and in between dispatches on the base
// form's type byte. Type 0x23 runs 0x00526DB0, type 0x24 builds the base model
// and attaches one extra model per entry of a list on the base form, any other
// type returns the base form's vfn[0xFC].
//
// Where the rebuild deliberately differs from the original (derived, each
// from the instructions named): the original faults on three inputs, the
// rebuild refuses the list entry, reports it through RefusePath and goes on
// with the next entry, as on a failed load (B15):
//   - a node with next set and data 0: data is dereferenced at 0x004E1710;
//   - a base model path without '\': _strrchr returns 0 and the name is
//     written from address 1 (LEA EDX,[EAX+1] at 0x004E1707);
//   - a path longer than the 0x104-byte buffer at [ESP+0x40]: the copies at
//     0x004E16E8 and 0x004E1710 have no bound and reach the /GS cookie.
// A null base path (vfn 0x14 of base+0xAC returning 0) is refused for the same
// reason; whether it can return 0 is unverified.
//
// Not modelled: the unwind the EH handler 0x009B5B96 runs for a throw inside
// the loop (try-states 0 and 1 at 0x004E176A/0x004E1776). Its unwind map is not
// in this export (unverified), so the sequence below is the normal path only;
// like the original, a throw skips 0x0047D0F0 (no try-state covers it, read:
// the state is -1 outside 0x004E176A..0x004E185F).
//
// Names are proposals until the frame-clock slice's refutation round.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace obng::engine::time::load_base_form_models {

// ---- B3/B4/B22: the dispatch on the base form's type byte (+0x04) ----------

inline constexpr std::uint8_t kType23 = 0x23;  // SUB EAX,0x23 / JZ at 0x004E15FA/0x004E1604
inline constexpr std::uint8_t kType24 = 0x24;  // SUB EAX,1 / JNZ at 0x004E160A/0x004E160D

enum class BaseFormPath : std::uint8_t {
	Type23,  // 0x004E189F
	Type24,  // 0x004E1613
	Other,   // 0x004E18AF
};

constexpr BaseFormPath ClassifyBaseForm(std::uint8_t typeId) {
	if (typeId == kType23) {
		return BaseFormPath::Type23;
	}
	if (typeId == kType24) {
		return BaseFormPath::Type24;
	}
	return BaseFormPath::Other;
}

// The function's result (AL = BL at 0x004E18CB): 1 on both character paths
// (0x004E189B, 0x004E18AB), vfn[0xFC] of the base form on the other path
// (0x004E18BF).
constexpr bool ResultFor(BaseFormPath path, bool vfnFCResult) {
	return path == BaseFormPath::Other ? vfnFCResult : true;
}

// ---- B3a: the type 0x23 action -------------------------------------------------

// vfn[0x168]'s result, kept in ESI from 0x004E15D4, gates 0x00526DB0
// (TEST ESI,ESI / JZ at 0x004E189F/0x004E18A1).
constexpr bool RunsType23Action(std::uint32_t vfn168Result) {
	return vfn168Result != 0;
}

// ---- B6..B9: the two byte arguments of 0x0051E240 ---------------------------------

// Both start at 1 (0x004E1626, 0x004E162B).
struct BuildFlags {
	bool arg2;  // [ESP+0x18], second push at 0x004E1693
	bool arg3;  // [ESP+0x14], third push at 0x004E1692
};

inline constexpr std::uint32_t kClearArg2Bit = 1u << 20;  // SHR ECX,0x14 / TEST CL,1 at 0x004E1646
inline constexpr std::uint32_t kClearArg3Bit = 1u << 21;  // SHR EAX,0x15 / TEST AL,1 at 0x004E1653

// B7..B9: lookupFlags is the dword at +0x1C of 0x005E0380's result, or none
// when the lookup was skipped (B6) or returned null (JZ at 0x004E163F).
constexpr BuildFlags BuildFlagsFor(std::optional<std::uint32_t> lookupFlags) {
	BuildFlags flags{true, true};
	if (!lookupFlags) {
		return flags;
	}
	if ((*lookupFlags & kClearArg2Bit) != 0) {
		flags.arg2 = false;
	}
	if ((*lookupFlags & kClearArg3Bit) != 0) {
		flags.arg3 = false;
	}
	return flags;
}

// ---- B10/B11: which builder runs ----------------------------------------------------

inline constexpr std::uint32_t kProbeBit = 1u << 1;         // [g_createdBaseObjList]+0x18, SHR 1 / TEST DL,1 at 0x004E1668
inline constexpr std::uint32_t kBuilder51D460Bit = 0x08000000u;  // TEST EAX,0x8000000 at 0x004E1677

enum class ModelBuilder : std::uint8_t {
	Build0051E240,  // thiscall(base; ref, arg2, arg3, 0), 0x004E1697
	Build0051D460,  // thiscall(base; ref), 0x004E1681
};

// B10: 0x004533F0 is only called when bit 1 is set (JZ at 0x004E166D).
constexpr bool Probes004533F0(std::uint32_t createdBaseObjListFlags) {
	return (createdBaseObjListFlags & kProbeBit) != 0;
}

// B11: probeResult is 0x004533F0's return, or none when it was not called.
constexpr ModelBuilder BuilderFor(std::optional<std::uint32_t> probeResult) {
	if (probeResult && (*probeResult & kBuilder51D460Bit) != 0) {
		return ModelBuilder::Build0051D460;
	}
	return ModelBuilder::Build0051E240;
}

// ---- B13/B21: the list walk ----------------------------------------------------------

// B13: a node is visited when next != 0 (JNZ at 0x004E16C8) or data != 0
// (JZ at 0x004E16CD). B21 is the walk itself: after a node, the next one is
// taken while next != 0 (JNZ at 0x004E1895).
constexpr bool VisitsListNode(bool hasNext, bool hasData) {
	return hasNext || hasData;
}

// ---- B14: the sibling model path ------------------------------------------------------

// The buffer at [ESP+0x40] runs to the /GS cookie at [ESP+0x144] (derived:
// the cookie store at 0x004E159B is [ESP+0x130] before five pushes).
inline constexpr std::size_t kModelPathBufferSize = 0x104;
using ModelPathBuffer = std::array<char, kModelPathBufferSize>;

inline constexpr char kPathSeparator = '\\';  // PUSH 0x5C at 0x004E16FA

enum class PathStatus : std::uint8_t {
	Composed,
	NoBasePath,     // refusal: vfn 0x14 returned null (the original reads it at 0x004E16E8)
	NoFileName,     // refusal: node data null (the original reads it at 0x004E1710)
	NoSeparator,    // refusal: no '\' (the original writes from address 1, 0x004E1707)
	BaseTooLong,    // refusal: the first copy (0x004E16E8) would pass the buffer
	ResultTooLong,  // refusal: the second copy (0x004E1710) would pass the buffer
};

// The original copies basePath into the buffer, finds its last '\' with
// _strrchr (0x004E16FD) and copies fileName with its NUL after it. On
// Composed, out holds that string; on a refusal out is unspecified.
inline PathStatus ComposeSiblingModelPath(const char* basePath, const char* fileName, ModelPathBuffer& out) {
	if (basePath == nullptr) {
		return PathStatus::NoBasePath;
	}
	if (fileName == nullptr) {
		return PathStatus::NoFileName;
	}
	std::size_t baseLength = std::strlen(basePath);
	if (baseLength + 1 > kModelPathBufferSize) {
		return PathStatus::BaseTooLong;
	}
	const char* separator = std::strrchr(basePath, kPathSeparator);
	if (separator == nullptr) {
		return PathStatus::NoSeparator;
	}
	std::size_t folderLength = static_cast<std::size_t>(separator - basePath) + 1;
	std::size_t nameLength = std::strlen(fileName);
	if (folderLength + nameLength + 1 > kModelPathBufferSize) {
		return PathStatus::ResultTooLong;
	}
	std::memcpy(out.data(), basePath, folderLength);
	std::memcpy(out.data() + folderLength, fileName, nameLength + 1);
	return PathStatus::Composed;
}

// ---- B15..B18: one list entry ---------------------------------------------------------

enum class NodeSource : std::uint8_t {
	Create004430C0,  // thiscall(g_TES; model, &context), held in a NiPointer (0x0075FA90 at 0x004E17A0)
	Clone00700610,   // thiscall(model; &context), 0x004E17B2
};

// B16: 0x00480820(model) (JZ at 0x004E1788).
constexpr NodeSource NodeSourceFor(bool result00480820) {
	return result00480820 ? NodeSource::Create004430C0 : NodeSource::Clone00700610;
}

enum class AttachMode : std::uint8_t {
	SkinAttachment,  // 0x004E17DF..0x004E1816
	Plain00479140,   // 0x004E181B..0x004E182E
};

// B17: 0x00471B80(node) true (JZ at 0x004E17D9) and 0x004D96F0's result
// non-null (JZ at 0x004E17DD). Both calls always run, 0x00471B80 first (read:
// CALL 0x00471B80 at 0x004E17BA, its AL kept in BL at 0x004E17CE, CALL
// 0x004D96F0 at 0x004E17D0, and only then TEST BL,BL at 0x004E17D5).
constexpr AttachMode AttachModeFor(bool result00471B80, bool targetNonNull) {
	return result00471B80 && targetNonNull ? AttachMode::SkinAttachment : AttachMode::Plain00479140;
}

// The string at 0x00A3D01C, pushed at 0x004E17E4 (read: meta.json strings).
inline constexpr const char* kSkinAttachmentName = "SkinAttachment";

enum class SkinAttachReceiver : std::uint8_t {
	NamedObject,  // target->vfn[0x58]("SkinAttachment"), 0x004E17F4
	ObjectAt1C,   // the object at target+0x1C, 0x004E17FA
};

// B18: JZ at 0x004E17F2. On ObjectAt1C the original loads [target+0x1C]
// (0x004E17FA) and dereferences it for its vtable at 0x004E17FD without a
// null test (read). Not modelled, the original faults: the sequence passes
// that object on to AttachVfn84 unchanged, so the real port faults where the
// original does. Whether [target+0x1C] can be null is unverified.
constexpr SkinAttachReceiver ReceiverFor(bool foundByName) {
	return foundByName ? SkinAttachReceiver::NamedObject : SkinAttachReceiver::ObjectAt1C;
}

// ---- B19: the NiPointer release after each attached entry -----------------------------

// The NiPointer at [ESP+0x14] is decremented when non-null (JZ at 0x004E183F,
// in the sequence below) and deleted through vfn[0](1) when
// InterlockedDecrement returned 0 (JNZ at 0x004E184D).
constexpr bool DeletesAfterDecrement(long countAfterDecrement) {
	return countAfterDecrement == 0;
}

// ---- The sequence ---------------------------------------------------------------------

// The body's calls over ports, in the original's order. Handles are
// pointer-like (null tests by conversion to bool). The ports:
//   BaseForm()                      - reads this+0x1C (0x004E15C0, 0x004E15F3, 0x004E18AF)
//   Vfn168() -> uint32              - this->vfn[0x168]() (0x004E15CD)
//   ClockEnter() / ClockLeave()     - 0x0047D120 / 0x0047D0F0 on g_timeInfo
//   Vfn164() / Call004728C0(h)      - 0x004E15E6 / 0x004E15EE
//   TypeId(form) -> uint8           - form+0x04 (0x004E15F6)
//   NiNode()                        - reads this+0x3C (0x004E15FD)
//   Call00526DB0(form)              - thiscall(form; this) (0x004E18A6)
//   VfnFC(form) -> bool             - form->vfn[0xFC]() (0x004E18BD)
//   Vfn190() -> bool                - this->vfn[0x190]() (0x004E1630)
//   Lookup005E0380Flags() -> optional<uint32>
//                                   - 0x005E0380(this), its +0x1C or none on null
//   CreatedBaseObjListFlags()       - [0x00B33B00]+0x18 (0x004E1665)
//   Call004533F0() -> uint32        - 0x004533F0(this, 1) (0x004E1672)
//   Call0051D460(form) / Call0051E240(form, arg2, arg3)
//   ModelListHead(form)             - 0x006899C0 on form+0xEC (0x004E16A2)
//   ListNext(n) / ListData(n)       - n+0x04 / n+0x00
//   BaseModelPath(form) -> const char*  - vfn 0x14 of form+0xAC (0x004E16E2)
//   RefusePath(status, fileName)    - the rebuild's report of a refused entry
//   LoadModel(path)                 - 0x00439EB0 on g_modelLoader (path, 0, 3, 1)
//   Vfn0EC() -> float               - this->vfn[0xEC]() (0x004E1749)
//   ConstructNodeContext(scale)     - 0x00478B90 on [ESP+0x24], then +0x10/+0x14/+0x18 = scale
//   Call00480820(model) -> bool
//   Create004430C0(model)           - 0x004430C0's result assigned into the
//                                     NiPointer local through 0x0075FA90
//                                     (0x004E17A0). The real port must perform
//                                     that assign, including its refcount
//                                     increment: B19 decrements what it holds.
//                                     The increment is unverified (0x0075FA90's
//                                     body is not in this export; the name
//                                     kNiPointerAssign rests on OBVR).
//   Clone00700610(model)
//   Call00471B80(node) -> bool
//   Find004D96F0(niNode)            - thiscall(this; niNode, 0x00A30968)
//   FindByName(target, name)        - target->vfn[0x58](name)
//   ObjectAt1C(target)              - [target+0x1C] (0x004E17FA), not null-tested (B18)
//   AttachVfn84(receiver, node)     - receiver->vfn[0x84](node, 1)
//   Call00478EC0(niNode, node)      - (niNode, node, 0)
//   Call00479140(niNode, node)      - (niNode, node, 0, 0, -1, 0)
//   DecrementRefCount(h) -> long    - InterlockedDecrement(h+0x04)
//   DeleteObject(h)                 - h->vfn[0](1)
//   NodeContextSlot(0|1)            - [ESP+0x24] / [ESP+0x28]

namespace detail {

template <class Ports, class Form, class Node>
void AttachListEntry(Ports& ports, Form form, Node niNode, const char* fileName) {
	ModelPathBuffer path{};
	PathStatus status = ComposeSiblingModelPath(ports.BaseModelPath(form), fileName, path);
	if (status != PathStatus::Composed) {
		ports.RefusePath(status, fileName);
		return;
	}
	auto model = ports.LoadModel(path.data());
	if (!model) {  // B15: JZ at 0x004E1738, next entry
		return;
	}
	ports.ConstructNodeContext(ports.Vfn0EC());

	decltype(ports.Create004430C0(model)) held{};
	decltype(ports.Create004430C0(model)) attached{};
	if (NodeSourceFor(ports.Call00480820(model)) == NodeSource::Create004430C0) {
		held = ports.Create004430C0(model);
		attached = held;
	} else {
		attached = ports.Clone00700610(model);
	}

	bool skinnable = ports.Call00471B80(attached);
	auto target = ports.Find004D96F0(niNode);
	if (AttachModeFor(skinnable, static_cast<bool>(target)) == AttachMode::SkinAttachment) {
		auto named = ports.FindByName(target, kSkinAttachmentName);
		auto receiver = ReceiverFor(static_cast<bool>(named)) == SkinAttachReceiver::NamedObject ? named
																									: ports.ObjectAt1C(target);
		ports.AttachVfn84(receiver, attached);
		ports.Call00478EC0(niNode, attached);
	} else {
		ports.Call00479140(niNode, attached);
	}

	// B19: JZ at 0x004E183F skips a null NiPointer.
	if (held && DeletesAfterDecrement(ports.DecrementRefCount(held))) {
		ports.DeleteObject(held);
	}
	// B20: the context's slots +0x00 and +0x04 ([ESP+0x24], [ESP+0x28]) are
	// each deleted through vfn[0](1) when non-null (JZ at 0x004E186A, 0x004E187A).
	for (int slot = 0; slot < 2; ++slot) {
		auto object = ports.NodeContextSlot(slot);
		if (object) {
			ports.DeleteObject(object);
		}
	}
}

template <class Ports, class Form, class Node>
void BuildType24(Ports& ports, Form form, Node niNode) {
	std::optional<std::uint32_t> lookupFlags;
	if (ports.Vfn190()) {  // B6: false skips the lookup (JZ at 0x004E1634)
		lookupFlags = ports.Lookup005E0380Flags();
	}
	BuildFlags flags = BuildFlagsFor(lookupFlags);

	std::optional<std::uint32_t> probeResult;
	if (Probes004533F0(ports.CreatedBaseObjListFlags())) {
		probeResult = ports.Call004533F0();
	}
	if (BuilderFor(probeResult) == ModelBuilder::Build0051D460) {
		ports.Call0051D460(form);
	} else {
		ports.Call0051E240(form, flags.arg2, flags.arg3);
	}

	for (auto entry = ports.ModelListHead(form);
		entry && VisitsListNode(static_cast<bool>(ports.ListNext(entry)), ports.ListData(entry) != nullptr);
		entry = ports.ListNext(entry)) {
		AttachListEntry(ports, form, niNode, ports.ListData(entry));
	}
}

} // namespace detail

// 0x004E1580's body. The base form read before vfn[0x168] (EBX, 0x004E15C0)
// feeds 0x00526DB0 (MOV ECX,EBX at 0x004E18A4), the builders (0x004E167F,
// 0x004E1695), the list (LEA ECX,[EBX+0xEC] at 0x004E169C) and the model path
// (EBX+0xAC at 0x004E16D3; EBX is restored per entry from its copy at
// [ESP+0x20], stored at 0x004E15C9 and reloaded at 0x004E1884); the type byte
// (0x004E15F3) and the other path's vfn[0xFC] (0x004E18AF) read this+0x1C
// again (read). this+0x3C is read once, at 0x004E15FD after 0x004728C0
// (0x004E15EE), kept at [ESP+0x1C] (0x004E1600) and reloaded from there as the
// attach parent for every entry (0x004E17BF, 0x004E1809, 0x004E181B) (read).
template <class Ports>
bool LoadBaseFormModels(Ports& ports) {
	auto cachedForm = ports.BaseForm();
	std::uint32_t gate23 = ports.Vfn168();
	ports.ClockEnter();

	auto helper = ports.Vfn164();
	if (helper) {  // B2: a nonzero result is ECX of 0x004728C0 (JZ at 0x004E15EA)
		ports.Call004728C0(helper);
	}
	BaseFormPath path = ClassifyBaseForm(ports.TypeId(ports.BaseForm()));
	auto niNode = ports.NiNode();

	bool vfnFCResult = false;
	switch (path) {
	case BaseFormPath::Type23:
		if (RunsType23Action(gate23)) {
			ports.Call00526DB0(cachedForm);
		}
		break;
	case BaseFormPath::Type24:
		if (niNode) {  // B5: JZ at 0x004E1615
			detail::BuildType24(ports, cachedForm, niNode);
		}
		break;
	case BaseFormPath::Other:
		vfnFCResult = ports.VfnFC(ports.BaseForm());
		break;
	}

	ports.ClockLeave();
	return ResultFor(path, vfnFCResult);
}

} // namespace obng::engine::time::load_base_form_models
