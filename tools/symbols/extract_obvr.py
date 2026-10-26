#!/usr/bin/env python3
"""Extract the named addresses of OBVR's src/game/GameAddresses.h into a CSV.

Usage: extract_obvr.py <path to GameAddresses.h> <output csv>

Only constants whose value is a full image address (0x00400000..0x00C00000)
are taken; patch sizes and struct offsets are skipped. OBVR's names are hook
sites and function entries alike, so the kind is `site`: the Ghidra script
renames the function when the address is a function entry and places a label
otherwise.
"""
import csv
import re
import subprocess
import sys

CONST = re.compile(
    r"^\s*(?:inline\s+)?(?:static\s+)?const(?:expr)?\s+\w+\s+(k\w+)\s*=\s*(0x[0-9A-Fa-f]{8})\s*;"
)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    header, out = sys.argv[1], sys.argv[2]
    commit = "unknown"
    try:
        commit = subprocess.run(
            ["git", "-C", header.rsplit("/src/", 1)[0], "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, OSError, IndexError):
        pass

    rows = []
    with open(header, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            m = CONST.match(line)
            if not m:
                continue
            name, value = m.group(1), int(m.group(2), 16)
            if not 0x00400000 <= value < 0x00C00000:
                continue
            rows.append({
                "address": f"0x{value:08X}",
                "name": name,
                "kind": "site",
                "confidence": "named",
                "conflict": 0,
                "source": f"OBVR/src/game/GameAddresses.h:{lineno}",
                "obvr_commit": commit,
            })

    seen = {}
    for r in rows:
        seen.setdefault(r["address"], []).append(r)
    for same in seen.values():
        if len(same) > 1:
            for r in same:
                r["conflict"] = 1

    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(sorted(rows, key=lambda r: r["address"]))
    print(f"{len(rows)} rows, {sum(1 for r in rows if r['conflict'])} in conflict")
    return 0


if __name__ == "__main__":
    sys.exit(main())
