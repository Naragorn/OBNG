#include "RangeWalkGame.h"

namespace obng::plugin::range_walk_game {

// 0x00982C30 takes its argument in ST0 and returns in ST0 (read, C14 of
// re/functions/0x007D2EA0.md). Read with objdump in Oblivion.exe 1.2.0.416:
// it stores the argument (FST qword), checks it (0x00991B88), and in
// 0x00982C4D runs FSQRT in place - under the caller's precision control
// (0x00991B15 keeps the caller's PC bits and masks every exception when the
// control word is not 0x27F) - then restores the control word. It needs no
// x87 slot beyond the one its caller pushed.
//
// Why the guard (FXSAVE / FNINIT / FLDCW / FXRSTOR): the rebuild calls it
// from SSE code at a point where the original walk would have balanced its
// own x87 use, but nothing guarantees the caller of the walk left free slots;
// a push onto a full stack gives the indefinite NaN, which would make the
// range compare false. Resetting the stack for the call and restoring it
// after makes the result independent of the caller's x87 stack, and the
// control word taken from the saved image keeps its numbers: the game's own
// word at that moment (measured at the walk's entry: 0x0C7F, 24-bit,
// truncate; knowledge/frame-clock.md). 0x00982C30 saves that word (FSTCW
// 0x00982C4E), runs its FSQRT under (word & 0x300) | 0x7F - precision kept,
// round to nearest, exceptions masked (0x00991B15..0x00991B27) - and loads
// the saved word back before it returns (FLDCW 0x00991BA6 or 0x00991BD0);
// read with objdump in the reference Oblivion.exe, md5 cdd2f0c5...: the same
// root the original gets, and the FSTP dword below rounds under the game's
// word as the original's store at 0x007D2FB0 does. Per square root rather
// than per walk: the walk
// calls game code that uses the x87 itself (0x0049FFC0's FLDZ/FCOMP; the
// route's AddGeometry), which keeps running in the caller's environment, as
// under the original.
//
// The result is stored as a float (FSTP dword), the one rounding the original
// makes at 0x007D2FB0 (FSTP float right after the call, read): exact in every
// precision mode, where a double store followed by an SSE narrowing would
// round twice outside 24-bit precision.
//
// cdecl: [esp+4] s, [esp+8] out, [esp+12] area (16-aligned). EAX, ECX, EDX
// are the caller's to lose (0x00982C30 and its callees use them).
__declspec(naked) void __cdecl GameSquareRootGuarded(float /*s*/, float* /*out*/, void* /*area*/) {
	__asm {
		mov eax, [esp + 12]
		fxsave [eax]
		fninit
		fldcw word ptr [eax]
		fld dword ptr [esp + 4]
		mov eax, 0x00982C30
		call eax
		mov eax, [esp + 8]
		fstp dword ptr [eax]
		mov eax, [esp + 12]
		fxrstor [eax]
		ret
	}
}

// cdecl, no argument: the word in AX, zero-extended into EAX. FNSTCW is the
// no-wait form: it neither waits for nor raises a pending x87 exception, and
// changes no x87 state.
__declspec(naked) std::uint16_t __cdecl CaptureControlWord() {
	__asm {
		push eax
		fnstcw word ptr [esp]
		pop eax
		movzx eax, ax
		ret
	}
}

__declspec(naked) void __cdecl CaptureX87(void* /*area*/) {
	__asm {
		mov eax, [esp + 4]
		fxsave [eax]
		ret
	}
}

}  // namespace obng::plugin::range_walk_game
