// Create a function at each given entry address when Ghidra's analysis left
// the code there undefined (a cut-off after a call it took for no-return, or
// code only reached through a table). The entry must be known from an
// independent source - name it in re/slices/<slice>/functions.txt.
//
//   tools/ghidra/headless.sh CreateFunction.java 0x0040D800
//@category OBNG

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class CreateFunction extends GhidraScript {

	@Override
	protected void run() throws Exception {
		for (String arg : getScriptArgs()) {
			Address entry = toAddr(Long.parseLong(arg.replace("0x", "").replace("0X", ""), 16));
			Function existing = getFunctionAt(entry);
			if (existing != null) {
				println("exists: " + entry + " " + existing.getName(true));
				continue;
			}
			Function containing = getFunctionContaining(entry);
			if (containing != null) {
				println("inside another function: " + entry + " lies in " + containing.getEntryPoint() +
					" " + containing.getName(true) + "; not created");
				continue;
			}
			if (getInstructionAt(entry) == null) {
				disassemble(entry);
			}
			Function created = createFunction(entry, null);
			if (created == null) {
				printerr("could not create a function at " + entry);
				continue;
			}
			println("created: " + entry + " " + created.getName(true) + " body " + created.getBody().getMinAddress() +
				"-" + created.getBody().getMaxAddress());
		}
	}
}
