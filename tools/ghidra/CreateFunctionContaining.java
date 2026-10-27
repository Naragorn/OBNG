// Define the function that contains a given address when Ghidra's analysis
// left that code undefined: walk back to the end of the nearest defined
// function before the address, skip the padding (0xCC, 0x90), disassemble
// from there and create a function. Report whether the new body contains the
// address; when it does not, nothing is left behind.
//
// Ghidra's analysis of Oblivion.exe leaves such gaps where a call it took
// for no-return cut a function short; 0x0055D615 (reads the tick globals)
// and 0x00406169 (reads g_timeInfo) lie in no function (2026-10-27).
//
//   tools/ghidra/headless.sh CreateFunctionContaining.java 0x0055D615
//@category OBNG

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class CreateFunctionContaining extends GhidraScript {

	@Override
	protected void run() throws Exception {
		for (String arg : getScriptArgs()) {
			Address target = toAddr(Long.parseLong(arg.replace("0x", "").replace("0X", ""), 16));
			Function existing = getFunctionContaining(target);
			if (existing != null) {
				println("already inside " + existing.getEntryPoint() + " " + existing.getName(true) + ": " + target);
				continue;
			}
			Function before = getFunctionBefore(target);
			if (before == null) {
				printerr("no function before " + target);
				continue;
			}
			// A gap may hold several functions in a row; each one created is
			// real code and stays, the walk continues from its end.
			Function created = null;
			for (int step = 0; step < 32 && (created == null || !created.getBody().contains(target)); step++) {
				Function last = created == null ? before : created;
				Address entry = last.getBody().getMaxAddress().add(1);
				while (entry.compareTo(target) < 0) {
					int b = getByte(entry) & 0xFF;
					if (b != 0xCC && b != 0x90) {
						break;
					}
					entry = entry.add(1);
				}
				if (entry.compareTo(target) > 0) {
					printerr("walked past " + target + " without a containing function (last " + last.getEntryPoint() + ")");
					created = null;
					break;
				}
				if (getInstructionAt(entry) == null) {
					disassemble(entry);
				}
				created = createFunction(entry, null);
				if (created == null) {
					printerr("could not create a function at " + entry + " for " + target);
					break;
				}
				println("created " + entry + " " + created.getName(true) + " body " + created.getBody().getMinAddress() +
					"-" + created.getBody().getMaxAddress() + (created.getBody().contains(target) ? " containing " + target : ", before " + target));
			}
			if (created != null && created.getBody().contains(target)) {
				println("RESULT " + target + " lies in " + created.getEntryPoint() + " " + created.getName(true));
			}
		}
	}
}
