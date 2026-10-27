// Find every aligned and unaligned occurrence of a 32-bit little-endian
// value in the image's initialised memory, and say what lies there (a
// defined data item, a function, nothing). Used to find a function's
// address inside a table - a CRT initialiser entry, a vtable slot.
//
//   tools/ghidra/headless.sh FindDword.java 0x009DC2A0
//@category OBNG

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class FindDword extends GhidraScript {

	@Override
	protected void run() throws Exception {
		for (String arg : getScriptArgs()) {
			long value = Long.parseLong(arg.replace("0x", "").replace("0X", ""), 16);
			byte[] pattern = {(byte) value, (byte) (value >> 8), (byte) (value >> 16), (byte) (value >> 24)};
			println("occurrences of " + String.format("%08X", value) + ":");
			int count = 0;
			for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
				if (!block.isInitialized()) {
					continue;
				}
				Address at = block.getStart();
				while (at != null && at.compareTo(block.getEnd()) < 0) {
					Address found = currentProgram.getMemory().findBytes(at, block.getEnd(), pattern, null, true, monitor);
					if (found == null) {
						break;
					}
					Data data = getDataContaining(found);
					Function fn = getFunctionContaining(found);
					Symbol sym = getSymbolAt(found);
					println(String.format("  %s in %s%s%s%s", found, block.getName(),
						sym != null ? " symbol " + sym.getName(true) : "",
						data != null ? " data " + data.getDataType().getName() + " at " + data.getMinAddress() : "",
						fn != null ? " inside function " + fn.getEntryPoint() : ""));
					count++;
					at = found.add(1);
				}
			}
			println("  " + count + " occurrences");
		}
	}
}
