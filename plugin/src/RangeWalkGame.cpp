#include "RangeWalkGame.h"

namespace obng::plugin::range_walk_game {

// 0x00982C30 takes its argument in ST0 and returns in ST0 (read, C14 of
// re/functions/0x007D2EA0.md); a cdecl function returns a double in ST0 as
// well. So: load the float, tail jump; its RET comes back to our caller with
// the result where it expects it. The game's own routine runs, under the
// game's x87 control word, so neither the comparison nor the route rests on
// a model of its rounding.
__declspec(naked) double __cdecl GameSquareRoot(float /*s*/) {
	__asm {
		fld dword ptr [esp + 4]
		mov eax, 0x00982C30
		jmp eax
	}
}

}  // namespace obng::plugin::range_walk_game
