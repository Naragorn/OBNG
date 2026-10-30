#!/usr/bin/env bash
# One run of OBNG.dll in the game: OBNG.dll deployed, its settings through
# OBNG-test.ini, a scenario driven by OBVR's harness (headless under
# gamescope), then OBNG.log read for the verdict. The player's OBNG.ini is
# untouched; OBNG-test.ini is removed afterwards whatever happened.
#
#   tools/shadow-run.sh [scenario] [extra runner args...]
#       every shadow and probe on (the INI below), the scenario
#       tools/hand-scripts/obng-shadow.txt by default
#   tools/shadow-run.sh --ini <file> <scenario> [extra runner args...]
#       <file> written as OBNG-test.ini instead (the measurement runs:
#       tools/hand-scripts/obng-measure-original.ini / -routed.ini), the
#       runner's timeout 420 s (perf-walkers-in-view.txt's, the same timeline)
#
# Needs: Steam and SteamVR running, Oblivion not running, OBVR's repo at
# ../OBVR (OBNG_OBVR_DIR otherwise), OBNG.dll deployed (tools/build-plugin.sh).
# Exit 0 when the harness passed and OBNG.log shows:
#   - with a shadow on in the INI: a summary with zero mismatches for every
#     shadowed function;
#   - with every shadow off: no MISMATCH line, and the summary lines of what
#     the INI has on - "Route: range walk N walks routed" with N > 0 for
#     [Route] RangeWalk=1 (and its detour made), "Probe: memory" for
#     [Probe] Memory=1;
# 1 otherwise; 2 when something it needs is missing.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
obvr="${OBNG_OBVR_DIR:-$root/../OBVR}"
game="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}"
plugins="$game/Data/OBSE/Plugins"

ini_file=""
run_timeout=300
if [[ "${1:-}" == "--ini" ]]; then
	[[ $# -ge 3 ]] || { echo "usage: $0 --ini <file> <scenario> [runner args...]" >&2; exit 2; }
	ini_file="$(readlink -f "$2")"
	[[ -f "$ini_file" ]] || { echo "no INI at $2" >&2; exit 2; }
	shift 2
	run_timeout=420
fi
scenario="$(readlink -f "${1:-$root/tools/hand-scripts/obng-shadow.txt}")"  # absolute: the runner runs from OBVR's directory
shift || true
[[ -f "$scenario" ]] || { echo "no scenario at $scenario" >&2; exit 2; }
artifacts="$root/artifacts/shadow"

[[ -f "$obvr/tools/hand-script-run.py" ]] || { echo "no OBVR harness at $obvr" >&2; exit 2; }
[[ -f "$plugins/OBNG.dll" ]] || { echo "OBNG.dll is not deployed in $plugins" >&2; exit 2; }

# The value of key in [section] of an INI file (comments and blanks skipped,
# whitespace and CR stripped); empty when absent.
ini_value() {
	awk -F= -v section="[$2]" -v key="$3" '
		{ sub(/\r$/, "") }
		/^[[:space:]]*(;|$)/ { next }
		/^[[:space:]]*\[/ { gsub(/[[:space:]]/, ""); inside = ($0 == section); next }
		inside { k = $1; gsub(/[[:space:]]/, "", k); if (k == key) { v = $2; gsub(/[[:space:]]/, "", v); print v; exit } }
	' "$1"
}

# 0 when any key of [Shadow] is non-zero (OBNG.dll reads them as integers).
any_shadow_on() {
	awk -F= '
		{ sub(/\r$/, "") }
		/^[[:space:]]*(;|$)/ { next }
		/^[[:space:]]*\[/ { gsub(/[[:space:]]/, ""); inside = ($0 == "[Shadow]"); next }
		inside { v = $2; gsub(/[[:space:]]/, "", v); if (v + 0 != 0) { found = 1; exit } }
		END { exit found ? 0 : 1 }
	' "$1"
}

test_ini="$plugins/OBNG-test.ini"
trap 'rm -f "$test_ini"' EXIT
if [[ -n "$ini_file" ]]; then
	cp "$ini_file" "$test_ini"
else
	cat > "$test_ini" <<'EOF'
[Shadow]
TimeInfoUpdate=1
TimeInfoInit=1
TimeInfoFreeze=1
TimeInfoUnfreeze=1
LoadingMenuTick=1
LoadingMenuCtor=1
RangeWalk=1
[Probe]
FpuControlWord=1
HavokPlanner=1
CallCounters=1
Memory=1
[Log]
MismatchLines=20
SummaryEverySeconds=10
EOF
fi
shadow_on=0
any_shadow_on "$test_ini" && shadow_on=1
route_on=0
[[ "$(ini_value "$test_ini" Route RangeWalk)" =~ ^0*[1-9][0-9]*$ ]] && route_on=1
memory_on=1  # OBNG.dll's default when the key is absent
[[ "$(ini_value "$test_ini" Probe Memory)" =~ ^0+$ ]] && memory_on=0

mkdir -p "$artifacts"
( cd "$obvr" && python3 tools/hand-script-run.py "$scenario" --headless --run-timeout "$run_timeout" \
	--artifact-root "$artifacts" "$@" )
harness=$?

run_dir="$(ls -dt "$artifacts"/*/* 2>/dev/null | head -1)"
if [[ -f "$game/OBNG.log" && -n "$run_dir" ]]; then
	cp "$game/OBNG.log" "$run_dir/OBNG.log"
fi
log="${run_dir:-/dev/null}/OBNG.log"
echo "== harness exit $harness; run $run_dir"
echo "== OBNG.log (shadow, route and probe lines)"
grep -E 'Shadow|Route|Probe|Settings|^Log:' "$log" 2>/dev/null | tail -25

verdict=0
[[ $harness -eq 0 ]] || verdict=1
if [[ $shadow_on -eq 1 ]]; then
	summary="$(grep -E '^Shadow: ' "$log" 2>/dev/null | tail -1)"
	if [[ -z "$summary" ]]; then
		echo "FAIL: no shadow summary in OBNG.log"
		verdict=1
	elif grep -qE ' [1-9][0-9]* mismatches| [1-9][0-9]*/[1-9]' <<<"$summary" || grep -q 'MISMATCH' "$log"; then
		echo "FAIL: mismatches in OBNG.log"
		verdict=1
	else
		echo "PASS: $summary"
	fi
	exit $verdict
fi

# Every shadow off: nothing is compared, so the verdict is the harness and
# the summary lines of what the INI has on.
seen=""
[[ $harness -eq 0 ]] || echo "FAIL: the harness did not pass (exit $harness)"
if grep -q 'MISMATCH' "$log" 2>/dev/null; then
	echo "FAIL: a MISMATCH line in OBNG.log with every shadow off"
	verdict=1
fi
if [[ $route_on -eq 1 ]]; then
	route="$(grep -E '^Route: range walk [1-9][0-9]* walks routed' "$log" 2>/dev/null | tail -1)"
	if ! grep -q '^Route: 1 of 1 detours made' "$log" 2>/dev/null; then
		echo "FAIL: the route's detour was not made (see the Route lines above)"
		verdict=1
	elif [[ -z "$route" ]]; then
		echo "FAIL: no route summary with routed walks in OBNG.log"
		verdict=1
	else
		seen+=" | $route"
	fi
fi
if [[ $memory_on -eq 1 ]]; then
	memory="$(grep -E '^Probe: memory' "$log" 2>/dev/null | tail -1)"
	if [[ -z "$memory" ]]; then
		echo "FAIL: no memory summary in OBNG.log (is [Log] SummaryTimer=1?)"
		verdict=1
	else
		seen+=" | $memory"
	fi
fi
if [[ $verdict -eq 0 ]]; then
	echo "PASS: the harness passed, no shadow on$seen"
fi
exit $verdict
