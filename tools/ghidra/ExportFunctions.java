// Export functions of the open program to files, one directory per function.
//
// Script arguments: <output directory> <address or @listfile> [<address> ...]
//   A list file holds one address per line (0x-prefixed hex); '#' starts a
//   comment. An address inside a function selects that function.
//
// Per function <out>/<entry>/:
//   pseudo.c   Ghidra decompiler output (a reading aid, not proof)
//   asm.s      every instruction of the body, "address  bytes  mnemonic operands"
//   meta.json  name, entry, body ranges, callers, callees, referenced strings,
//              referenced data labels, decompiler status
// And <out>/index.json listing what was exported.
//
// Run through tools/ghidra/export.sh.
//@category OBNG

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.FileWriter;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ExportFunctions extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 2) {
			printerr("usage: ExportFunctions <outdir> <address|@listfile> [...]");
			return;
		}
		File out = new File(args[0]);
		out.mkdirs();
		List<Address> wanted = new ArrayList<>();
		for (int i = 1; i < args.length; i++) {
			if (args[i].startsWith("@")) {
				for (String line : Files.readAllLines(new File(args[i].substring(1)).toPath())) {
					String t = line.split("#", 2)[0].trim();
					if (!t.isEmpty()) {
						wanted.add(parse(t));
					}
				}
			}
			else {
				wanted.add(parse(args[i]));
			}
		}

		DecompInterface decomp = new DecompInterface();
		decomp.openProgram(currentProgram);
		StringBuilder index = new StringBuilder("[\n");
		int done = 0;
		try {
			for (Address a : wanted) {
				Function fn = getFunctionContaining(a);
				if (fn == null) {
					printerr("no function contains " + a);
					index.append(String.format("  {\"requested\": \"%s\", \"error\": \"no function\"},\n", a));
					continue;
				}
				exportOne(fn, decomp, out);
				index.append(String.format("  {\"requested\": \"%s\", \"entry\": \"%s\", \"name\": \"%s\"},\n",
					a, fn.getEntryPoint(), json(fn.getName(true))));
				done++;
			}
		}
		finally {
			decomp.dispose();
		}
		if (index.length() > 3) {
			index.setLength(index.length() - 2);
			index.append("\n");
		}
		index.append("]\n");
		write(new File(out, "index.json"), index.toString());
		println("ExportFunctions: " + done + " of " + wanted.size() + " exported to " + out);
	}

	private void exportOne(Function fn, DecompInterface decomp, File out) throws Exception {
		File dir = new File(out, "0x" + fn.getEntryPoint().toString().toUpperCase());
		dir.mkdirs();

		DecompileResults res = decomp.decompileFunction(fn, 120, monitor);
		String status;
		if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
			write(new File(dir, "pseudo.c"), res.getDecompiledFunction().getC());
			status = "ok";
		}
		else {
			write(new File(dir, "pseudo.c"), "/* decompile failed: " + res.getErrorMessage() + " */\n");
			status = "failed: " + res.getErrorMessage();
		}

		StringBuilder asm = new StringBuilder();
		Set<String> strings = new LinkedHashSet<>();
		Set<String> dataRefs = new LinkedHashSet<>();
		for (Instruction ins : currentProgram.getListing().getInstructions(fn.getBody(), true)) {
			asm.append(String.format("%-10s  %-20s  %s\n", ins.getMinAddress(), hex(ins.getBytes()), ins));
			for (Reference ref : ins.getReferencesFrom()) {
				Address to = ref.getToAddress();
				if (!to.isMemoryAddress() || ref.getReferenceType().isFlow()) {
					continue;
				}
				Data d = getDataContaining(to);
				if (d != null && d.hasStringValue()) {
					strings.add(String.format("{\"at\": \"%s\", \"ref\": \"%s\", \"value\": \"%s\"}",
						ins.getMinAddress(), to, json(StringDataInstance.getStringDataInstance(d).getStringValue())));
					continue;
				}
				Symbol s = getSymbolAt(to);
				dataRefs.add(String.format("{\"at\": \"%s\", \"ref\": \"%s\", \"name\": \"%s\"}",
					ins.getMinAddress(), to, s == null ? "" : json(s.getName(true))));
			}
		}
		write(new File(dir, "asm.s"), asm.toString());

		StringBuilder meta = new StringBuilder("{\n");
		meta.append("  \"name\": \"").append(json(fn.getName(true))).append("\",\n");
		meta.append("  \"entry\": \"").append(fn.getEntryPoint()).append("\",\n");
		meta.append("  \"signature\": \"").append(json(fn.getPrototypeString(false, false))).append("\",\n");
		meta.append("  \"calling_convention\": \"").append(json(String.valueOf(fn.getCallingConventionName()))).append("\",\n");
		meta.append("  \"decompile\": \"").append(json(status)).append("\",\n");
		meta.append("  \"body\": [");
		List<String> ranges = new ArrayList<>();
		for (AddressRange r : fn.getBody()) {
			ranges.add(String.format("{\"from\": \"%s\", \"to\": \"%s\"}", r.getMinAddress(), r.getMaxAddress()));
		}
		meta.append(String.join(", ", ranges)).append("],\n");
		meta.append("  \"callers\": [").append(functions(fn.getCallingFunctions(monitor))).append("],\n");
		meta.append("  \"callees\": [").append(functions(fn.getCalledFunctions(monitor))).append("],\n");
		meta.append("  \"strings\": [").append(String.join(", ", strings)).append("],\n");
		meta.append("  \"data_refs\": [").append(String.join(", ", dataRefs)).append("]\n");
		meta.append("}\n");
		write(new File(dir, "meta.json"), meta.toString());
	}

	private static String functions(Set<Function> fns) {
		List<String> items = new ArrayList<>();
		for (Function f : fns) {
			items.add(String.format("{\"entry\": \"%s\", \"name\": \"%s\"}", f.getEntryPoint(), json(f.getName(true))));
		}
		return String.join(", ", items);
	}

	private Address parse(String text) {
		return toAddr(Long.parseLong(text.replace("0x", "").replace("0X", ""), 16));
	}

	private static String hex(byte[] bytes) {
		StringBuilder b = new StringBuilder();
		for (byte x : bytes) {
			b.append(String.format("%02x ", x));
		}
		return b.toString().trim();
	}

	private static String json(String s) {
		if (s == null) {
			return "";
		}
		// Keep every byte of a string visible: a CR in the binary is a fact a
		// rebuild must reproduce (the LoadingMenu warning ends in CR LF).
		return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t");
	}

	private static void write(File f, String text) throws Exception {
		try (FileWriter w = new FileWriter(f)) {
			w.write(text);
		}
	}
}
