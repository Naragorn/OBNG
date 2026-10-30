#!/usr/bin/env bash
# One shadow-mode run in the game: OBNG.dll deployed, every shadow and probe
# on through OBNG-test.ini, the scenario tools/hand-scripts/obng-shadow.txt
# driven by OBVR's harness (headless under gamescope), then OBNG.log read
# for the verdict. The player's OBNG.ini is untouched; OBNG-test.ini is
# removed afterwards whatever happened.
#
#   tools/shadow-run.sh [scenario] [extra runner args...]
#
# Needs: Steam and SteamVR running, Oblivion not running, OBVR's repo at
# ../OBVR (OBNG_OBVR_DIR otherwise), OBNG.dll deployed (tools/build-plugin.sh).
# Exit 0 when the harness passed and OBNG.log shows a summary with zero
# mismatches for every shadowed function; 1 otherwise.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
obvr="${OBNG_OBVR_DIR:-$root/../OBVR}"
game="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}"
plugins="$game/Data/OBSE/Plugins"
scenario="$(readlink -f "${1:-$root/tools/hand-scripts/obng-shadow.txt}")"  # absolute: the runner runs from OBVR's directory
shift || true
[[ -f "$scenario" ]] || { echo "no scenario at $scenario" >&2; exit 2; }
artifacts="$root/artifacts/shadow"

[[ -f "$obvr/tools/hand-script-run.py" ]] || { echo "no OBVR harness at $obvr" >&2; exit 2; }
[[ -f "$plugins/OBNG.dll" ]] || { echo "OBNG.dll is not deployed in $plugins" >&2; exit 2; }

cat > "$plugins/OBNG-test.ini" <<'EOF'
[Shadow]
TimeInfoUpdate=1
TimeInfoInit=1
TimeInfoFreeze=1
TimeInfoUnfreeze=1
LoadingMenuTick=1
LoadingMenuCtor=1
[Probe]
FpuControlWord=1
HavokPlanner=1
CallCounters=1
Memory=1
[Log]
MismatchLines=20
SummaryEverySeconds=10
EOF
trap 'rm -f "$plugins/OBNG-test.ini"' EXIT

stamp="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$artifacts"
( cd "$obvr" && python3 tools/hand-script-run.py "$scenario" --headless --run-timeout 300 \
	--artifact-root "$artifacts" "$@" )
harness=$?

run_dir="$(ls -dt "$artifacts"/*/* 2>/dev/null | head -1)"
if [[ -f "$game/OBNG.log" && -n "$run_dir" ]]; then
	cp "$game/OBNG.log" "$run_dir/OBNG.log"
fi
log="${run_dir:-/dev/null}/OBNG.log"
echo "== harness exit $harness; run $run_dir"
echo "== OBNG.log (shadow lines)"
grep -E 'Shadow|Probe|Settings' "$log" 2>/dev/null | tail -25

verdict=0
[[ $harness -eq 0 ]] || verdict=1
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
