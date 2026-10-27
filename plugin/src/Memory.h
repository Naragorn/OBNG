#pragma once

#include "Types.h"

namespace obng::plugin {

// Writes size bytes of code into the game's image, lifting the page
// protection for the moment of the write. False when the protection could
// not be changed; nothing is written then.
bool SafeWrite(UInt32 address, const void* data, UInt32 size);

// Whether the bytes at address are exactly the expected ones - the game's
// own, as read from Oblivion.exe 1.2.416. A patch is made only on a match:
// another plugin's detour, or another game version, leaves the engine alone.
bool Verify(UInt32 address, const UInt8* expected, UInt32 size);

// Executable memory for a trampoline; null when the system refuses.
void* AllocExecutable(UInt32 size);

// The first eight bytes at address as hex, for the log line that says why a
// patch was not made.
void DescribeBytes(UInt32 address, char* out, UInt32 capacity);

}  // namespace obng::plugin
