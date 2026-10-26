#!/usr/bin/env bash
# Export the functions of a slice to re/export/<slice>/: pseudocode, assembly
# and metadata per function, from the seeded Ghidra project.
#
#   tools/ghidra/export.sh <slice>            # reads re/slices/<slice>/functions.txt
#   tools/ghidra/export.sh <slice> 0x0047D170 0x0040D7F0 ...
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
slice="$1"; shift
out="$root/re/export/$slice"
mkdir -p "$out"
if [ $# -eq 0 ]; then
	list="$root/re/slices/$slice/functions.txt"
	[ -f "$list" ] || { echo "missing $list" >&2; exit 2; }
	"$here/headless.sh" ExportFunctions.java "$out" "@$list"
else
	"$here/headless.sh" ExportFunctions.java "$out" "$@"
fi
