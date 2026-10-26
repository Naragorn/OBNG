// Apply seed names from a CSV to the open program.
//
// Script argument: the CSV path. Columns (header required):
//   address,name,kind,confidence,conflict,source[,...]
//
// Rules:
//   - confidence "named": the name is applied as USER_DEFINED. If the address
//     is a function entry, the function is renamed; otherwise a label is set.
//     "A::B::C" becomes name C in namespace A::B.
//   - confidence "inferred": not applied as a name; left as a plate comment
//     "seed (inferred, <source>): <name>" so a reader sees it.
//   - conflict "1": not applied as a name, written as a plate comment listing
//     the candidate, so a conflicting address never gets a silently chosen name.
//   - An address that already carries a USER_DEFINED name is left alone and
//     reported, so a second CSV never overwrites a first.
//
// Run through tools/ghidra/apply-symbols.sh.
//@category OBNG

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ApplySymbols extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length != 1) {
			printerr("usage: ApplySymbols <csv>");
			return;
		}
		List<Map<String, String>> rows = readCsv(args[0]);
		int renamed = 0, labelled = 0, commented = 0, skippedExisting = 0, skippedBad = 0;
		SymbolTable symbols = currentProgram.getSymbolTable();

		for (Map<String, String> row : rows) {
			Address addr;
			try {
				addr = toAddr(Long.parseLong(row.get("address").replace("0x", ""), 16));
			}
			catch (NumberFormatException e) {
				skippedBad++;
				printerr("bad address: " + row);
				continue;
			}
			if (!currentProgram.getMemory().contains(addr)) {
				skippedBad++;
				printerr("address outside the image: " + row.get("address") + " " + row.get("name"));
				continue;
			}
			String name = row.get("name");
			boolean named = "named".equals(row.get("confidence"));
			boolean conflict = "1".equals(row.getOrDefault("conflict", "0"));

			if (!named || conflict) {
				String why = conflict ? "conflict" : "inferred";
				appendPlateComment(addr, "seed (" + why + ", " + row.get("source") + "): " + name);
				commented++;
				continue;
			}

			Symbol primary = symbols.getPrimarySymbol(addr);
			if (primary != null && primary.getSource() == SourceType.USER_DEFINED) {
				skippedExisting++;
				println("kept existing " + primary.getName(true) + " at " + addr + " (wanted " + name + ")");
				continue;
			}

			String[] parts = name.split("::");
			Namespace ns = currentProgram.getGlobalNamespace();
			for (int i = 0; i < parts.length - 1; i++) {
				ns = symbols.getOrCreateNameSpace(ns, parts[i], SourceType.USER_DEFINED);
			}
			String leaf = parts[parts.length - 1];

			Function fn = getFunctionAt(addr);
			if (fn != null) {
				fn.setParentNamespace(ns);
				fn.setName(leaf, SourceType.USER_DEFINED);
				renamed++;
			}
			else {
				symbols.createLabel(addr, leaf, ns, SourceType.USER_DEFINED);
				labelled++;
			}
			appendPlateComment(addr, "seed (" + row.get("source") + ")");
		}
		println(String.format(
			"ApplySymbols: %d rows, %d functions renamed, %d labels, %d comments only, %d kept existing, %d bad",
			rows.size(), renamed, labelled, commented, skippedExisting, skippedBad));
	}

	private void appendPlateComment(Address addr, String text) {
		String old = getPlateComment(addr);
		if (old != null && old.contains(text)) {
			return;
		}
		setPlateComment(addr, old == null ? text : old + "\n" + text);
	}

	private static List<Map<String, String>> readCsv(String path) throws Exception {
		List<Map<String, String>> rows = new ArrayList<>();
		try (BufferedReader r = new BufferedReader(new FileReader(path))) {
			String header = r.readLine();
			if (header == null) {
				return rows;
			}
			List<String> cols = Arrays.asList(header.split(","));
			String line;
			while ((line = r.readLine()) != null) {
				if (line.isBlank()) {
					continue;
				}
				String[] cells = line.split(",", -1);
				Map<String, String> row = new HashMap<>();
				for (int i = 0; i < cols.size() && i < cells.length; i++) {
					row.put(cols.get(i).trim(), cells[i].trim());
				}
				rows.add(row);
			}
		}
		return rows;
	}
}
