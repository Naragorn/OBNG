#pragma once

// The CRT square root's dispatch, rebuilt: what _CIsqrt 0x00982C30 decides
// around its one FSQRT, and the machine code that makes the same decisions
// without the exception-reporting path the game's control word sends every
// inexact result down.
//
// What the original does (read in re/export/frame-update/0x00982C30,
// 0x00982C4D, 0x00991B15, 0x00991B88, 0x00991BAB, 0x00991AB7 and the
// decompilates of __87except 0x0099E2B9, __handle_exc 0x00992CE0,
// __ctrlfp 0x009930D8, _set_statfp 0x009930FF, __set_errno_from_matherr
// 0x00992EC1; knowledge/render-thread-outside-the-pass.md):
//
//   1. SUB ESP,12; FST qword [ESP]: the argument ST0 stored as a double for
//      the tests below; the arithmetic itself runs on the register.
//   2. Exponent 0x7FF in the stored high dword (inf or NaN) or the sign bit
//      set: the special cases (0x00982CA9 / 0x00982C8B: -0 takes the square
//      root, -inf and negatives return the indefinite NaN 0x00B319B0 with
//      DOMAIN, NaNs pass through 0x00991B2C).
//   3. FSTCW; when the word is not 0x027F, FLDCW (word & 0x0300) | 0x007F:
//      the precision control kept, round to nearest, every exception masked.
//   4. FSQRT.
//   5. With the dword 0x00BA9DDC zero (it is never written in this build:
//      every one of its eleven references in re/export/rea-all is a read, and
//      it lies in .data's zero-filled tail), JMP 0x00991BAB: word 0x027F ->
//      return as is; the word's PM bit (0x20) clear -> FLDCW word, return;
//      FSTSW and the PE flag (0x20) clear -> FLDCW word, return; otherwise
//      __87except through 0x00991AB7: the result goes through a double store
//      and load (exact below precision control 64), __handle_exc(0x10)
//      re-raises the already set PE flag (_set_statfp(0x20): FLDPI; FSTP
//      qword), __ctrlfp(word, 0xFFFF) puts the caller's word back,
//      __set_errno_from_matherr(8) touches nothing for INEXACT (only types
//      1..3 set errno), and 0x00991AB7 loads the word again.
//
// So for a finite, non-negative argument under a control word with every
// exception masked at precision control 24 or 53, the original's observable
// effect is: ST0 = FSQRT under (word & 0x0300) | 0x007F, the control word as
// it was, the status word's exception flags as FSQRT left them (PE for an
// inexact result, DE for a denormal argument). The exception path adds a
// deliberately inexact FSTP whose only trace is the already set PE flag; its
// cost is 1.7 % of the render thread's out-of-pass samples and 1.2 % of the
// in-pass samples (measured, knowledge/render-thread-outside-the-pass.md).
//
// The replacement below makes exactly that: the same store, the same tests
// on the stored high dword, FSQRT under the same word, and the caller's word
// back - and nothing else. Everything the fast path does not cover (special
// or negative argument, a control word with an exception unmasked, precision
// control 64 or the reserved encoding) jumps to the original through the
// detour's trampoline, so those cases keep the original's behaviour to the
// byte, errno and the indefinite NaN included. Registers: EAX is clobbered
// (the original clobbers EAX and ECX on every path and preserves EDX; the
// replacement preserves ECX and EDX), the x87 stack takes one argument and
// returns one result, and the stack below ESP is scratch as in the original.
//
// The precision control matters: at PC 24 the hardware's FSQRT rounds the
// register result to 24 significant bits (the game's words are 0x007F and
// 0x0C7F, measured in knowledge/frame-clock.md), and that is what every
// caller gets today. The replacement runs the same instruction under the
// same word, so it inherits that rounding from the hardware instead of
// modelling it. FastSqrtOracleTest runs the original's own bytes beside
// this code on the host's x87.
//
// The decisions are plain functions over the stored high dword and the
// control word, so a test can walk every flow; the code generation is a
// function of two addresses, so a test can check the bytes.

#include "CodeWriter.h"
#include "Types.h"

namespace obng::plugin::fast_sqrt {

// _CIsqrt and its first two instructions: SUB ESP,0xC (83 EC 0C) and FST
// qword ptr [ESP] (DD 14 24) - whole instructions, nothing relative, six
// bytes for the five the entry patch needs (read, re/export/frame-update/
// 0x00982C30/asm.s).
inline constexpr UInt32 kCIsqrtAddress = 0x00982C30;
inline constexpr UInt8 kCIsqrtEntryBytes[] = {0x83, 0xEC, 0x0C, 0xDD, 0x14, 0x24};

// The stored double's high dword: sign bit set (negative, or -0) or the
// exponent all ones (inf or NaN) - the cases 0x00982C4D hands to its
// special-case code, and the replacement hands to the original.
constexpr bool ImageIsSpecialOrNegative(UInt32 highDword) {
	return (highDword & 0x80000000u) != 0 || (highDword & 0x7FF00000u) == 0x7FF00000u;
}

// A control word the fast path serves: all six exception masks set (bits
// 0..5) and precision control 24 or 53 (bit 8 clear: PC is bits 8..9, 00 =
// 24, 10 = 53, 11 = 64, 01 reserved). With an exception unmasked the
// original's FLDCW sequence can raise, and at precision control 64 its
// exception path rounds the result through a double store; both stay with
// the original.
constexpr bool ControlWordIsFast(UInt16 controlWord) {
	return (controlWord & 0x013Fu) == 0x003Fu;
}

// The word the original loads for its FSQRT: precision control kept, round
// to nearest, every exception masked (0x00991B15, read).
constexpr UInt16 SqrtControlWord(UInt16 controlWord) {
	return static_cast<UInt16>((controlWord & 0x0300u) | 0x007Fu);
}

// Whether a fast word differs from the word FSQRT needs - only then are the
// two FLDCW worth their cost. 0x007F and 0x027F are already the square
// root's word; 0x0C7F (truncate) is not.
constexpr bool ControlWordNeedsSwap(UInt16 controlWord) {
	return (controlWord & 0xFCFFu) != 0x007Fu;
}

enum class Path : UInt8 {
	Original,   // through the trampoline: the original decides
	FastPlain,  // FSQRT under the caller's own word
	FastSwap,   // FLDCW the square root's word, FSQRT, FLDCW the caller's
};

constexpr Path Decide(UInt32 highDword, UInt16 controlWord) {
	if (ImageIsSpecialOrNegative(highDword) || !ControlWordIsFast(controlWord)) {
		return Path::Original;
	}
	return ControlWordNeedsSwap(controlWord) ? Path::FastSwap : Path::FastPlain;
}

// The stack frame the code keeps below the return address, as the original's:
// the stored double at +0 (low dword) and +4 (high dword), the caller's
// control word at +8, the square root's word at +10.
inline constexpr UInt8 kFrameBytes = 12;
inline constexpr UInt8 kDoubleLowOffset = 0;
inline constexpr UInt8 kDoubleHighOffset = 4;
inline constexpr UInt8 kControlWordOffset = 8;
inline constexpr UInt8 kSqrtControlWordOffset = 10;

inline constexpr UInt32 kCodeSize = 133;  // what BuildFastSqrt writes; the test pins the bytes

// Writes the replacement for _CIsqrt into buffer, to run at baseAddress.
// originalCell is the address of a dword that holds where the original
// continues (the detour's trampoline; filled by the installer before the
// entry is patched). Returns the bytes written, 0 when they did not fit.
inline UInt32 BuildFastSqrt(UInt8* buffer, UInt32 capacity, UInt32 baseAddress, UInt32 originalCell) {
	// Two passes: the first learns where the two labels fall, the second
	// writes the jumps to them. The code has no other dependency on its
	// own layout.
	UInt32 sqrtLabel = baseAddress;
	UInt32 originalLabel = baseAddress;
	UInt32 written = 0;
	for (int pass = 0; pass < 2; ++pass) {
		CodeWriter code(buffer, capacity, baseAddress);
		code.SubStackPointer(kFrameBytes);
		code.StoreDoubleToStack(kDoubleLowOffset);
		code.MoveEaxFromStack(kDoubleHighOffset);
		code.TestEax(0x80000000u);               // the sign
		code.JumpIfNotEqualRelative(originalLabel);
		code.AndEax(0x7FF00000u);                // the exponent
		code.CompareEax(0x7FF00000u);
		code.JumpIfEqualRelative(originalLabel);  // inf or NaN
		code.StoreControlWordToStack(kControlWordOffset);
		code.MoveZeroExtendEaxFromStackWord(kControlWordOffset);
		code.AndEax(0x0000013Fu);                // the masks and PC's bit 8
		code.CompareEax(0x0000003Fu);
		code.JumpIfNotEqualRelative(originalLabel);  // an exception unmasked, or PC 64 / reserved
		code.MoveZeroExtendEaxFromStackWord(kControlWordOffset);
		code.AndEax(0x0000FCFFu);                // everything but PC
		code.CompareEax(0x0000007Fu);
		code.JumpIfEqualRelative(sqrtLabel);     // already the square root's word
		code.MoveZeroExtendEaxFromStackWord(kControlWordOffset);
		code.AndEax(0x00000300u);
		code.OrEax(0x0000007Fu);
		code.MoveStackWordFromAx(kSqrtControlWordOffset);
		code.LoadControlWordFromStack(kSqrtControlWordOffset);
		code.SquareRoot();
		code.LoadControlWordFromStack(kControlWordOffset);
		code.AddStackPointer(kFrameBytes);
		code.Return();
		sqrtLabel = code.AddressAt(code.Size());
		code.SquareRoot();
		code.AddStackPointer(kFrameBytes);
		code.Return();
		originalLabel = code.AddressAt(code.Size());
		code.AddStackPointer(kFrameBytes);
		code.JumpIndirect(originalCell);
		if (code.Overflowed()) {
			return 0;
		}
		written = code.Size();
	}
	return written;
}

}  // namespace obng::plugin::fast_sqrt
