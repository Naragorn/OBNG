#!/usr/bin/env bash
# Runs the stress flight (tools/hand-scripts/obng-stress-ride.txt) N times with one OBNG INI and prints a line a
# run: crashed or not, where, and how long the process manager kept being called (the flight's length).
#   tools/stress-ride.sh <ini> <runs>
# A file ${TMPDIR:-/tmp}/obng-stress-stop holds the series before its next run (for another session's turn at the game).
set -u
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ini="$1"; runs="${2:-3}"
cd "$root" || exit 2
stop="${TMPDIR:-/tmp}/obng-stress-stop"
for ((i = 1; i <= runs; i++)); do
	[[ -e "$stop" ]] && { echo "held before run $i ($stop exists)"; break; }
	python3 tools/run-with-windows-mods.py -- tools/shadow-run.sh --ini "$ini" tools/hand-scripts/obng-stress-ride.txt \
		--save OBVRHarnessMOO --run-timeout 600 > /dev/null 2>&1
	code=$?
	run="$(ls -dt artifacts/shadow/obng-stress-ride/* | head -1)"
	# Oblivion Walkers flushes its log line by line: its last lines say what it did before the game ended.
	walkers="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}/OblivionWalkers.log"
	[[ -f "$walkers" ]] && cp -p "$walkers" "$run/OblivionWalkers.log"
	crash="$(grep -m1 -o 'access violation.*thread [0-9]*' "$run/OBVR.log" || echo none)"
	calls="$(grep 'Probe: process manager' "$run/OBNG.log" | sed -E 's/.* - ([0-9]+) calls.*/\1/' | awk '{c += $1} END {print c + 0}')"
	west="$(grep -o 'the feet at [-0-9.]*' "$run/OBVR.log" | awk '{if ($4 < w) w = $4} END {printf "%d", w}')"
	echo "$(basename "$ini") run $i: exit $code, $run, westmost x $west, process manager calls $calls, crash: $crash"
done
