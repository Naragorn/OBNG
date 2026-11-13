#!/usr/bin/env bash
# Runs the stress flight (tools/hand-scripts/obng-stress-ride.txt) N times with one OBNG INI and prints a line a
# run: crashed or not, where, and how long the process manager kept being called (the flight's length).
#   tools/stress-ride.sh <ini> <runs>
set -u
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ini="$1"; runs="${2:-3}"
cd "$root" || exit 2
for ((i = 1; i <= runs; i++)); do
	python3 tools/run-with-windows-mods.py -- tools/shadow-run.sh --ini "$ini" tools/hand-scripts/obng-stress-ride.txt \
		--save OBVRHarnessMOO --run-timeout 600 > /dev/null 2>&1
	code=$?
	run="$(ls -dt artifacts/shadow/obng-stress-ride/* | head -1)"
	crash="$(grep -m1 -o 'access violation.*thread [0-9]*' "$run/OBVR.log" || echo none)"
	calls="$(grep 'Probe: process manager' "$run/OBNG.log" | sed -E 's/.* - ([0-9]+) calls.*/\1/' | awk '{c += $1} END {print c + 0}')"
	marks="$(grep -c 'HandScript: mark leg' "$run/OBVR.log")"
	echo "$(basename "$ini") run $i: exit $code, $run, legs begun $marks, process manager calls $calls, crash: $crash"
done
