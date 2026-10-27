#include "EntryDetour.h"

#include "CodeWriter.h"

namespace obng::plugin {

namespace {
// A rel32 jump is five bytes, and the patch cannot be shorter than one.
constexpr UInt32 kJumpSize = 5;
}  // namespace

UInt32 BuildEntryTrampoline(UInt8* buffer, UInt32 capacity, UInt32 trampolineAddress,
                            UInt32 entryAddress, const UInt8* entryBytes,
                            UInt32 entryLength) {
	if (entryLength < kJumpSize) {
		return 0;
	}

	CodeWriter code(buffer, capacity, trampolineAddress);
	code.Bytes(entryBytes, entryLength);
	code.JumpRelative(entryAddress + entryLength);

	if (code.Overflowed()) {
		return 0;
	}
	return code.Size();
}

UInt32 BuildEntryPatch(UInt8* buffer, UInt32 capacity, UInt32 entryAddress,
                       UInt32 replacementAddress, UInt32 entryLength) {
	if (entryLength < kJumpSize) {
		return 0;
	}

	CodeWriter code(buffer, capacity, entryAddress);
	code.JumpRelative(replacementAddress);
	code.Nop(entryLength - kJumpSize);

	if (code.Overflowed() || code.Size() != entryLength) {
		return 0;
	}
	return code.Size();
}

}  // namespace obng::plugin
