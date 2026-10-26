#pragma once

// The engine-facing shape of 0x004E1580 (proposed
// TESObjectREFR::LoadBaseFormModels): the layouts it reads, its calling
// convention and its callees' as called here. The decisions and the call
// sequence live in TESObjectREFR_LoadBaseFormModelsLogic.h.
//
// Evidence: re/functions/0x004E1580.md, re/export/frame-clock/0x004E1580/asm.s.
// Names are proposals until the frame-clock slice's refutation round.
//
// The structs below are views: they cover the bytes this function touches,
// not the whole engine object. Their size is the end of the last field read
// here, not the object's size (unverified for every one of them except
// NodeContext and ModelListNode). They belong in a shared reference/form
// header that does not exist yet (layout request in the worker report).
//
// Only addresses with two sources are constants here (AGENTS.md):
//   0x00B333A0 g_TES                - ECX of 0x004430C0 (0x004E178A) and
//                                     xOBSE obse/GameData.cpp:218 (re/symbols/xobse.csv:1533)
//   0x00B33A1C g_modelLoader        - ECX of 0x00439EB0 (0x004E171E) and
//                                     xOBSE obse/GameTasks.cpp:8 (xobse.csv:1550)
//   0x00B33B00 g_createdBaseObjList - flags read at 0x004E165F and
//                                     xOBSE obse/GameAPI.cpp:197 (xobse.csv:1553)
//   0x0075FA90 kNiPointerAssign     - the assign after 0x004430C0 (0x004E17A0) and
//                                     OBVR GameAddresses.h:1151 (re/symbols/obvr.csv:92)
//   0x00B33E90 g_timeInfo           - TimeInfo.h's kTimeInfoAddress
// The function itself and its other callees have one source (this body) and
// stay in comments until a second one agrees.

#include "engine/time/TESObjectREFR_LoadBaseFormModelsLogic.h"
#include "engine/time/TimeInfo.h"

#include <cstddef>
#include <cstdint>

namespace obng::engine::time::load_base_form_models::engine {

inline constexpr std::uint32_t kTESAddress = 0x00B333A0;
inline constexpr std::uint32_t kModelLoaderAddress = 0x00B33A1C;
inline constexpr std::uint32_t kCreatedBaseObjListAddress = 0x00B33B00;
inline constexpr std::uint32_t kNiPointerAssignAddress = 0x0075FA90;
inline constexpr std::uint32_t kClockAddress = kTimeInfoAddress;  // 0x00B33E90, ECX at 0x004E15CF and 0x004E18C1

// Vtable slot offsets as called here (read).
inline constexpr std::uint32_t kVfnGate23 = 0x168;  // this, 0x004E15C3; result gates type 0x23
inline constexpr std::uint32_t kVfn164 = 0x164;     // this, 0x004E15DE
inline constexpr std::uint32_t kVfn190 = 0x190;     // this, 0x004E161E; result tested as AL
inline constexpr std::uint32_t kVfnScale = 0x0EC;   // this, 0x004E1741; result in ST0 (float, fstp at 0x004E174B)
inline constexpr std::uint32_t kFormVfnFC = 0x0FC;  // base form, 0x004E18B5; result tested as AL
inline constexpr std::uint32_t kModelVfnPath = 0x14;  // base+0xAC component, 0x004E16D9
inline constexpr std::uint32_t kVfnFindByName = 0x58;  // 0x004D96F0's result, 0x004E17E1
inline constexpr std::uint32_t kVfnAttach = 0x84;      // receiver, 0x004E1801
inline constexpr std::uint32_t kVfnDelete = 0x00;      // called with 1 (0x004E1851, 0x004E186E, 0x004E187E)

using VTable = void* const*;

// this: base form at +0x1C (0x004E15C0), node at +0x3C (0x004E15FD) (read
// accesses; the names agree with xOBSE GameObjects.h as cited in OBVR
// GameAddresses.h:1437-1441, derived).
struct TESObjectREFRView {
	VTable vtable;              // +0x00
	std::uint8_t unread04[0x18];
	struct BaseFormView* baseForm;  // +0x1C
	std::uint8_t unread20[0x1C];
	void* niNode;               // +0x3C
};
static_assert(sizeof(void*) == 4, "the views mirror a 32-bit binary");
static_assert(offsetof(TESObjectREFRView, baseForm) == 0x1C, "baseForm at +0x1C");
static_assert(offsetof(TESObjectREFRView, niNode) == 0x3C, "niNode at +0x3C");
static_assert(sizeof(TESObjectREFRView) == 0x40, "the view ends after +0x3C");

// The embedded component at base+0xAC: a vtable whose slot 0x14 returns the
// model path (read: 0x004E16D3..0x004E16E2). TESModel is a guess (unverified).
struct ModelComponentView {
	VTable vtable;  // +0x00
};
static_assert(sizeof(ModelComponentView) == 4, "only the vtable is read");

// The base form: type byte at +0x04 (MOVZX at 0x004E15F6), the model
// component at +0xAC, the list source at +0xEC (LEA ECX,[EBX+0xEC] at
// 0x004E169C, passed as ECX to 0x006899C0; its own layout unverified).
struct BaseFormView {
	VTable vtable;              // +0x00
	std::uint8_t typeId;        // +0x04
	std::uint8_t unread05[0xA7];
	ModelComponentView model;   // +0xAC
	std::uint8_t unreadB0[0x3C];
	std::uint8_t listSource[4];  // +0xEC, only its address is taken
};
static_assert(offsetof(BaseFormView, typeId) == 0x04, "typeId at +0x04");
static_assert(offsetof(BaseFormView, model) == 0xAC, "model component at +0xAC");
static_assert(offsetof(BaseFormView, listSource) == 0xEC, "list source at +0xEC");
static_assert(sizeof(BaseFormView) == 0xF0, "the view ends at the list source's first dword");

// A node of the list 0x006899C0 returns (read: data at 0x004E16CA/0x004E1702,
// next at 0x004E16C4/0x004E188C).
struct ModelListNode {
	const char* data;     // +0x00 file name
	ModelListNode* next;  // +0x04
};
static_assert(sizeof(ModelListNode) == 8, "data and next");
static_assert(offsetof(ModelListNode, next) == 0x04, "next at +0x04");

// 0x005E0380's result: flags at +0x1C (MOV EAX,[EAX+0x1C] at 0x004E1641).
struct Lookup005E0380View {
	std::uint8_t unread00[0x1C];
	std::uint32_t flags;  // +0x1C, bits 20 and 21 (logic: kClearArg2Bit, kClearArg3Bit)
};
static_assert(offsetof(Lookup005E0380View, flags) == 0x1C, "flags at +0x1C");

// [g_createdBaseObjList]: flags at +0x18 (MOV EDX,[ECX+0x18] at 0x004E1665).
struct CreatedBaseObjListView {
	std::uint8_t unread00[0x18];
	std::uint32_t flags;  // +0x18, bit 1 (logic: kProbeBit)
};
static_assert(offsetof(CreatedBaseObjListView, flags) == 0x18, "flags at +0x18");

// The object the NiPointer local holds: refcount at +0x04 (LEA EAX,[ESI+4]
// for InterlockedDecrement at 0x004E1841). NiRefObject is a guess (unverified).
struct RefCountedView {
	VTable vtable;                // +0x00, slot 0 called with 1
	volatile long refCount;       // +0x04
};
static_assert(offsetof(RefCountedView, refCount) == 0x04, "refCount at +0x04");

// The local at [ESP+0x24], constructed by 0x00478B90 (0x004E1753) and passed
// by address to 0x004430C0 / 0x00700610. Slots +0x00 and +0x04 are released
// at 0x004E1859/0x004E1874; +0x10..+0x18 take vfn[0xEC]'s float (0x004E175C..
// 0x004E1764); +0x08/+0x0C are not touched here. Size derived: the path buffer
// starts at [ESP+0x40], 0x1C bytes later. Name is a proposal.
struct NodeContext {
	RefCountedView* slot0;  // +0x00
	RefCountedView* slot4;  // +0x04
	std::uint8_t unread08[0x08];
	float scale[3];         // +0x10, +0x14, +0x18
};
static_assert(sizeof(NodeContext) == 0x1C, "[ESP+0x24]..[ESP+0x3F]");
static_assert(offsetof(NodeContext, scale) == 0x10, "the three floats at +0x10");

// The frame offsets the logic's buffer size rests on (read: LEA EDX,[ESP+0x40]
// at 0x004E16E4; cookie at [ESP+0x130] at 0x004E159B before the five pushes
// 0x004E15A2..0x004E15AD, so [ESP+0x144] in the body).
inline constexpr std::uint32_t kPathBufferFrameOffset = 0x40;
inline constexpr std::uint32_t kCookieFrameOffset = 0x130 + 5 * 4;
static_assert(kCookieFrameOffset - kPathBufferFrameOffset == kModelPathBufferSize,
	"the path buffer runs to the /GS cookie");
static_assert(kPathBufferFrameOffset - 0x24 == sizeof(NodeContext), "NodeContext ends where the buffer starts");

// ---- Calling conventions --------------------------------------------------------

// 0x004E1580 itself: thiscall, ECX = this (MOV EBP,ECX at 0x004E15BB), no
// stack arguments, plain RET (0x004E18F4), returns AL = BL (0x004E18CB).
// Stack arithmetic in re/functions/0x004E1580.md (derived).
using LoadBaseFormModelsFn = bool(__thiscall*)(TESObjectREFRView* self);

// 0x0047D120 and 0x0047D0F0: thiscall on g_timeInfo, nothing pushed
// (0x004E15CF..0x004E15D6, 0x004E18C1..0x004E18C6, read); their own RET N and
// what they touch in TimeInfo are not in this export (unverified).
using ClockBracketFn = void(__thiscall*)(TimeInfo* clock);

// Callees, as called here. "Pops N" is derived from the stack offsets the
// body reads right after the call (re/functions/0x004E1580.md).
// 0x00526DB0: thiscall(base; this) (0x004E18A3..0x004E18A6); pops 4 unverified (only path to exit).
using Type23ActionFn = void(__thiscall*)(BaseFormView* base, TESObjectREFRView* self);
// 0x0051D460: thiscall(base; this), pops 4 (derived: join at 0x004E169C reads [ESP+0x18]
// at 0x004E16AB like the 0x0051E240 path).
using Build0051D460Fn = void(__thiscall*)(BaseFormView* base, TESObjectREFRView* self);
// 0x0051E240: thiscall(base; this, arg2, arg3, 0), pops 16 (derived as above). arg2/arg3 are
// dwords whose low byte only is set (MOV BYTE at 0x004E1626/0x004E162B), so the callee
// reads them as bytes (derived).
using Build0051E240Fn = void(__thiscall*)(BaseFormView* base, TESObjectREFRView* self, bool arg2, bool arg3,
	std::uint32_t zero);
// 0x004533F0: (this, 1), callee pops 8 (derived). ECX holds [0x00B33B00] at the call
// (0x004E165F, read); whether the callee uses it - thiscall on g_createdBaseObjList - or is
// stdcall is unverified. Declared stdcall until its body is read.
using Probe004533F0Fn = std::uint32_t(__stdcall*)(TESObjectREFRView* self, std::uint32_t one);
// 0x00439EB0: thiscall(g_modelLoader; path, 0, 3, 1) (0x004E171E..0x004E172F); pops 16
// (derived: [ESP+0x14] at 0x004E174B is the same slot as at 0x004E1771).
using LoadModelFn = void*(__thiscall*)(void* modelLoader, const char* path, std::uint32_t zero, std::uint32_t three,
	std::uint32_t one);
// 0x00480820, 0x00471B80: cdecl, one argument (ADD ESP,4 at 0x004E1783 / 0x004E17C3, read).
using Test00480820Fn = bool(__cdecl*)(void* model);
using Test00471B80Fn = bool(__cdecl*)(void* node);
// 0x004430C0: thiscall(g_TES; model, &context), pops 8 (derived: 0x004E17A5 reads the NiPointer
// at [ESP+0x14] after kNiPointerAssign's push).
// Its result is pushed to kNiPointerAssign (CALL 0x0075FA90 at 0x004E17A0, ECX = &[ESP+0x14]),
// and B19 later decrements and may delete what [ESP+0x14] holds (read). The logic's
// Create004430C0 port therefore stands for both calls and must perform the assign with its
// refcount increment; that the assign increments is unverified (0x0075FA90's body is not in
// this export).
using Create004430C0Fn = void*(__thiscall*)(void* tes, void* model, NodeContext* context);
// 0x00700610: thiscall(model; &context), pops 4 (derived: [ESP+0x20] at 0x004E17BF is the node).
using Clone00700610Fn = void*(__thiscall*)(void* model, NodeContext* context);
// 0x004D96F0: thiscall(this; niNode, 0x00A30968), pops 8 (derived: [ESP+0x1C] at 0x004E1809).
// The string at 0x00A30968 is not in this export (unverified).
using Find004D96F0Fn = void*(__thiscall*)(TESObjectREFRView* self, void* niNode, const char* name);
// 0x00478EC0: cdecl(niNode, node, 0) (ADD ESP,0xC at 0x004E1816, read).
using Attach00478EC0Fn = void(__cdecl*)(void* niNode, void* node, std::uint32_t zero);
// 0x00479140: cdecl(niNode, node, 0, 0, -1, 0) (ADD ESP,0x18 at 0x004E182E, read).
using Attach00479140Fn = void(__cdecl*)(void* niNode, void* node, std::uint32_t a3, std::uint32_t a4, std::int32_t a5,
	std::uint32_t a6);
// _strrchr 0x00983FD0: cdecl (ADD ESP,8 at 0x004E1704, read); the rebuild uses std::strrchr.

} // namespace obng::engine::time::load_base_form_models::engine
