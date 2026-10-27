// Print what the listing holds from an address on: instructions, defined
// data or undefined bytes, with the function each lies in. For looking at
// a gap Ghidra's analysis left before deciding how to define it.
//
//   tools/ghidra/headless.sh Listing.java 0x004060AC 24 0x00406169 12
//@category OBNG

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;

public class Listing extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		for (int i = 0; i + 1 < args.length; i += 2) {
			Address at = toAddr(Long.parseLong(args[i].replace("0x", "").replace("0X", ""), 16));
			int count = Integer.parseInt(args[i + 1]);
			println("listing from " + at + ":");
			for (int n = 0; n < count && at != null; n++) {
				CodeUnit unit = currentProgram.getListing().getCodeUnitContaining(at);
				if (unit == null) {
					println("  " + at + "  (no code unit)");
					break;
				}
				Function fn = getFunctionContaining(unit.getMinAddress());
				StringBuilder bytes = new StringBuilder();
				for (byte b : unit.getBytes()) {
					bytes.append(String.format("%02x ", b));
				}
				println(String.format("  %s  %-28s %s%s", unit.getMinAddress(), bytes.toString().trim(), unit.toString(),
					fn != null ? "   [" + fn.getEntryPoint() + " " + fn.getName() + "]" : "   [no function]"));
				at = unit.getMaxAddress().add(1);
			}
		}
	}
}
