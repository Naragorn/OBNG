// Print every reference to the given addresses: the referencing instruction,
// the function it lies in, and the reference type. Used by the architect to
// find the functions of a slice (who reads a global, who calls a function).
//
//   tools/ghidra/headless.sh ReferencesTo.java 0x00B33E90 0x0047D170
//@category OBNG

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class ReferencesTo extends GhidraScript {

	@Override
	protected void run() throws Exception {
		for (String arg : getScriptArgs()) {
			Address target = toAddr(Long.parseLong(arg.replace("0x", "").replace("0X", ""), 16));
			println("references to " + target + ":");
			int count = 0;
			for (Reference ref : getReferencesTo(target)) {
				Address from = ref.getFromAddress();
				Function fn = getFunctionContaining(from);
				println(String.format("  %s  in %s (%s)  %s", from,
					fn == null ? "<no function>" : fn.getEntryPoint(),
					fn == null ? "" : fn.getName(true), ref.getReferenceType()));
				count++;
			}
			println("  " + count + " references");
		}
	}
}
