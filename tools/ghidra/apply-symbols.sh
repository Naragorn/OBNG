#!/usr/bin/env bash
# Apply the seed CSVs under re/symbols/ to the Ghidra project, in priority
# order: xOBSE's explicit names first, OBVR's hook sites second. A later CSV
# never overwrites a name an earlier one set (ApplySymbols reports "kept").
#
#   tools/ghidra/apply-symbols.sh [csv ...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
if [ $# -eq 0 ]; then
	set -- "$root/re/symbols/xobse.csv" "$root/re/symbols/obvr.csv"
fi
for csv in "$@"; do
	[ -f "$csv" ] || { echo "missing $csv" >&2; exit 2; }
	"$here/headless.sh" ApplySymbols.java "$csv"
done
