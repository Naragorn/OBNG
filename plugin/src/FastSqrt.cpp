// The CRT square root's dispatch in the game: see FastSqrt.h.

#include "FastSqrt.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "FastSqrtLogic.h"
#include "Memory.h"
#include "ShadowRecorder.h"

namespace obng::plugin {
namespace {

constexpr const char* kName = "_CIsqrt";

// The route: the cell the generated code jumps through for the cases it
// leaves to the original (the detour's trampoline).
void* g_routeOriginal = nullptr;

// The shadow: the trampoline, the generated dispatch (its cell is the same
// trampoline), and the thunk's bookkeeping.
void* g_shadowOriginal = nullptr;
void* g_shadowDispatch = nullptr;

// What one side of the shadow's comparison leaves: ST0 as FSTP tbyte stores
// it, then the control word and the status word.
struct Outcome {
	UInt8 result[10];
	UInt16 controlWord;
	UInt16 statusWord;
};

// Runs `code` from the x87 state in `area` (an FXSAVE image: the argument is
// ST0 there) and stores what it leaves into *out. cdecl: [esp+4] code,
// [esp+8] area, [esp+12] out. EAX, ECX, EDX are the caller's to lose, as
// with the original.
__declspec(naked) void __cdecl RunFromState(const void* /*code*/, const void* /*area*/, Outcome* /*out*/) {
	__asm {
		mov eax, [esp + 8]
		fxrstor [eax]
		mov eax, [esp + 4]
		call eax
		mov eax, [esp + 12]
		fstp tbyte ptr [eax]
		fnstcw word ptr [eax + 10]
		fnstsw word ptr [eax + 12]
		ret
	}
}

void Hex(const UInt8* bytes, UInt32 count, char* out) {
	for (UInt32 i = 0; i < count; ++i) {
		std::snprintf(out + 2 * i, 3, "%02X", bytes[count - 1 - i]);  // most significant first
	}
}

// The comparison, called by the thunk with the FXSAVE image of the state at
// the entry; the thunk restores that state afterwards and continues in the
// original.
void __cdecl ShadowCompare(const UInt8* area) {
	Outcome original{};
	Outcome rebuild{};
	RunFromState(g_shadowOriginal, area, &original);
	RunFromState(g_shadowDispatch, area, &rebuild);
	const bool mismatch = std::memcmp(original.result, rebuild.result, sizeof(original.result)) != 0 ||
	                      original.controlWord != rebuild.controlWord ||
	                      (original.statusWord & 0x3F) != (rebuild.statusWord & 0x3F);
	if (!mismatch) {
		RecordCall(Shadowed::FastSqrt, false, "", "");
		return;
	}
	char argument[24];
	char a[24];
	char b[24];
	Hex(area + 32, 10, argument);  // ST0 of the FXSAVE image
	Hex(original.result, 10, a);
	Hex(rebuild.result, 10, b);
	char inputs[80];
	char detail[200];
	UInt16 entryWord;
	std::memcpy(&entryWord, area, 2);
	std::snprintf(inputs, sizeof(inputs), "st0=%s cw=%04X", argument, entryWord);
	std::snprintf(detail, sizeof(detail), " original st0=%s cw=%04X sw=%04X; rebuild st0=%s cw=%04X sw=%04X", a,
	              original.controlWord, original.statusWord, b, rebuild.controlWord, rebuild.statusWord);
	RecordCall(Shadowed::FastSqrt, true, detail, inputs);
}

// The shadow's replacement for the entry: ST0 holds the argument, [esp] the
// return address. Saves the registers the comparison may change and the x87
// state, compares, restores both, and continues in the original.
__declspec(naked) void ShadowThunk() {
	__asm {
		push eax
		push ecx
		push edx
		sub esp, 528
		lea eax, [esp + 15]
		and eax, -16
		fxsave [eax]
		push eax
		call ShadowCompare
		pop eax
		fxrstor [eax]
		add esp, 528
		pop edx
		pop ecx
		pop eax
		jmp dword ptr [g_shadowOriginal]
	}
}

// The generated dispatch in executable memory, its cell at *cell.
void* BuildDispatch(void** cell, LogFn log, const char* who) {
	constexpr UInt32 capacity = 256;
	auto* code = static_cast<UInt8*>(AllocExecutable(capacity));
	if (code == nullptr) {
		log("%s %s: no executable memory for the dispatch; not made", who, kName);
		return nullptr;
	}
	if (fast_sqrt::BuildFastSqrt(code, capacity, reinterpret_cast<UInt32>(code), reinterpret_cast<UInt32>(cell)) !=
	    fast_sqrt::kCodeSize) {
		log("%s %s: the dispatch did not fit; not made", who, kName);
		return nullptr;
	}
	return code;
}

}  // namespace

UInt32 InstallRouteFastSqrt(const RouteSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.sqrt) {
		return 0;
	}
	s_done = true;
	void* dispatch = BuildDispatch(&g_routeOriginal, log, "Route");
	if (dispatch == nullptr) {
		return 0;
	}
	if (!InstallEntryDetour(kName, fast_sqrt::kCIsqrtAddress, fast_sqrt::kCIsqrtEntryBytes,
	                        sizeof(fast_sqrt::kCIsqrtEntryBytes), dispatch, &g_routeOriginal, log, DetourRole::Route)) {
		return 0;
	}
	log("Route %s: a finite, non-negative argument under a masked PC 24/53 word takes FSQRT alone; the rest continues in "
	    "the original (FastSqrtLogic.h)",
	    kName);
	return 1;
}

UInt32 InstallShadowFastSqrt(const ShadowSettings& settings, LogFn log) {
	static bool s_done = false;
	if (s_done || !settings.sqrt) {
		return 0;
	}
	s_done = true;
	g_shadowDispatch = BuildDispatch(&g_shadowOriginal, log, "Shadow");
	if (g_shadowDispatch == nullptr) {
		return 0;
	}
	if (!InstallEntryDetour(kName, fast_sqrt::kCIsqrtAddress, fast_sqrt::kCIsqrtEntryBytes,
	                        sizeof(fast_sqrt::kCIsqrtEntryBytes), reinterpret_cast<const void*>(&ShadowThunk),
	                        &g_shadowOriginal, log, DetourRole::Shadow)) {
		return 0;
	}
	return 1;
}

}  // namespace obng::plugin
