#pragma once

#include "Types.h"

namespace obng::plugin {

// Assembles x86 machine code. Deliberately tiny: the plugin needs a handful of
// opcodes, not an assembler.
//
// Emitting the bytes here instead of using __declspec(naked) with an __asm
// block is intentional. MSVC inline assembly ties the project to MSVC, while
// this route builds with MSVC, clang-cl and clang-cross alike - and it can be
// tested without a running Oblivion.
//
// Deliberately free of Windows dependencies.
class CodeWriter {
public:
	// baseAddress is the address at which the code will later execute. It is
	// passed in rather than derived from the buffer, so that byte generation
	// can be checked outside the game process.
	CodeWriter(UInt8* buffer, UInt32 capacity, UInt32 baseAddress);

	void Byte(UInt8 value);
	void Bytes(const UInt8* data, UInt32 size);
	void DWord(UInt32 value);

	// pushad / popad save all eight general purpose registers.
	void PushAllRegisters();
	void PopAllRegisters();
	void PushFlags();
	void PopFlags();

	// push dword ptr [esp + offset]
	void PushStackValue(UInt8 offset);

	// add esp, amount
	void AddStackPointer(UInt8 amount);

	// call / jmp with a 32-bit relative target, from the current position.
	void CallRelative(UInt32 target);
	void JumpRelative(UInt32 target);

	// ja (jump if above) with a 32-bit relative target.
	void JumpAboveRelative(UInt32 target);

	// push ecx, to hand a thiscall's `this` to a cdecl callback.
	void PushEcx();
	void PushEax();
	void PushEdx();

	// Small stack-relative moves used by call-site stubs.  The offsets are
	// deliberately byte-sized: the generated frames are fixed and local.
	void MoveEcxFromStack(UInt8 offset);
	void MoveEdxFromStack(UInt8 offset);
	void MoveStackFromEax(UInt8 offset);
	void LoadEffectiveAddressEax(UInt8 offset);

	// sub esp, amount
	void SubStackPointer(UInt8 amount);

	// movzx ecx, word ptr [ebp + offset] - a 16-bit argument or local.
	void MoveZeroExtendEcxFromEbpWord(UInt8 offset);

	// mov edx, eax - a callback's result where the patched code expects it.
	void MoveEdxFromEax();

	// xor ecx, ecx
	void ClearEcx();

	// nop, to pad out a partially overwritten instruction.
	void Nop(UInt32 count);

	// pop dword ptr [address] - takes the top of the stack into memory.
	void PopToMemory(UInt32 address);

	// push dword ptr [address] - puts a memory value on the stack.
	void PushFromMemory(UInt32 address);

	// mov dword ptr [address], esp - records where the stack stands.
	void StoreStackPointer(UInt32 address);

	// The square root dispatch's instructions (FastSqrtLogic.h): the x87
	// store, control-word and square-root instructions with an ESP-relative
	// operand, the EAX masks and compares, the conditional jumps and the
	// indirect jump through a memory cell.
	void StoreDoubleToStack(UInt8 offset);             // fst qword ptr [esp + offset]
	void StoreControlWordToStack(UInt8 offset);        // fnstcw word ptr [esp + offset]
	void LoadControlWordFromStack(UInt8 offset);       // fldcw word ptr [esp + offset]
	void SquareRoot();                                 // fsqrt
	void MoveEaxFromStack(UInt8 offset);               // mov eax, dword ptr [esp + offset]
	void MoveZeroExtendEaxFromStackWord(UInt8 offset); // movzx eax, word ptr [esp + offset]
	void MoveStackWordFromAx(UInt8 offset);            // mov word ptr [esp + offset], ax
	void AndEax(UInt32 mask);                          // and eax, imm32
	void OrEax(UInt32 bits);                           // or eax, imm32
	void CompareEax(UInt32 value);                     // cmp eax, imm32
	void TestEax(UInt32 mask);                         // test eax, imm32
	void JumpIfEqualRelative(UInt32 target);           // je rel32
	void JumpIfNotEqualRelative(UInt32 target);        // jne rel32 (also jnz)
	void JumpIndirect(UInt32 cellAddress);             // jmp dword ptr [cellAddress]

	// ret, with nothing popped beyond the return address.
	void Return();
	void ReturnAndPop(UInt16 bytes);

	UInt32 Size() const { return m_size; }
	bool Overflowed() const { return m_overflowed; }

	// The address this code would have at the given position.
	UInt32 AddressAt(UInt32 offset) const { return m_baseAddress + offset; }

private:
	UInt8* m_buffer;
	UInt32 m_capacity;
	UInt32 m_baseAddress;
	UInt32 m_size;
	bool m_overflowed;
};

}  // namespace obng::plugin
