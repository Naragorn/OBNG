#!/usr/bin/env bash
# One measuring run of the tutorial's fight held still (tools/hand-scripts/obng-profile-prison-fight-hold.txt, save
# OBNGPrisonFight) as Nadi plays it: the Windows mod list, the routes of the Windows OBNG.ini, Oblivion.ini's
# [Display] at the Windows values for the run. Waits first until no game and no harness run has been going for
# 30 seconds (it does not interrupt one), runs exclusive, and notes every other game seen beside the run.
#
#   tools/measure-fight.sh <name> [Section.Key=Value ...]        the keys go over the as-played OBNG.ini
#
#   PERF=1                 record the game with perf from the mark h01 (tools/perf-record-run.sh); the recording is
#                          put into the run's folder as perf/, for tools/re/perf-pass.py
#   OBNG_DLL=<file>        run with this OBNG.dll instead of the game's (put back afterwards; no deploy)
#   SPECULAR=0             bDoSpecularPass for the run (1)
#   GAME_INI="Section:Key=Value ..."   more keys of Oblivion.ini for the run (Display:bDoCanopyShadowPass=0)
#   RECORD=<seconds>       film the right eye for that long from every mark (the harness's --record); the run is
#                          for its pictures then, not for its times
#   SCENARIO=, SAVE=, TIMEOUT=   another hand script, its save, the run's timeout in seconds
#
# Prints the run's folder as its last line. Announce the run to the other sessions before and send FREE after.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root" || exit 2
name="${1:?usage: $0 <name> [Section.Key=Value ...]}"; shift
scenario="${SCENARIO:-tools/hand-scripts/obng-profile-prison-fight-hold.txt}"
save="${SAVE:-OBNGPrisonFight}"
work="artifacts/shadow/_measure/$name"; mkdir -p "$work"
ini="$work/OBNG-test.ini"
python3 tools/as-played-ini.py "$ini" Log.SummaryTimer=1 Log.SummaryEverySeconds=5 "$@" > /dev/null || exit 2

quiet=0; waited=0
while (( quiet < 30 )); do
	if tools/machine-busy.py > /dev/null; then quiet=0; else quiet=$((quiet + 5)); fi
	sleep 5; waited=$((waited + 5))
	(( waited > 9000 )) && { echo "measure: gave up after 150 minutes of waiting"; exit 3; }
done
echo "measure: machine free after $waited s, $name starts $(date +%H:%M:%S)"

(while true; do tools/machine-busy.py --others | sed "s/^/$(date +%H:%M:%S) /"; sleep 5; done) > "$work/beside.txt" 2>&1 &
watch=$!
perf=""
if [[ -n "${PERF:-}" ]]; then
	rm -rf "$work/perf"
	tools/perf-record-run.sh "$work/perf" 42 h01 > "$work/perf.log" 2>&1 &
	perf=$!
fi
run=(python3 tools/run-with-windows-mods.py -- tools/shadow-run.sh --ini "$ini" "$scenario" --save "$save" --run-timeout "${TIMEOUT:-420}" --no-steamvr --exclusive)
[[ -n "${RECORD:-}" ]] && run+=(--record "$RECORD")
[[ -n "${OBNG_DLL:-}" ]] && run=(tools/with-plugin-dll.sh OBNG.dll "$OBNG_DLL" -- "${run[@]}")
read -r -a more <<< "${GAME_INI:-}"
tools/with-game-ini.sh Display:fSpecularLOD1=500.0000 Display:fSpecularLOD2=800.0000 Display:iActorShadowCountInt=4 "Display:bDoSpecularPass=${SPECULAR:-1}" "${more[@]}" -- \
	"${run[@]}" > "$work/run.log" 2>&1
status=$?
kill "$watch" 2>/dev/null
[[ -n "$perf" ]] && wait "$perf"
folder="$(ls -d "artifacts/shadow/$(basename "$scenario" .txt)"/*/ | tail -1)"
echo "measure: $name exit $status, $(grep -E 'PASS|FAIL' "$work/run.log" | head -1 | cut -c1-120)"
[[ -s "$work/beside.txt" ]] && echo "measure: OTHER GAMES RAN BESIDE IT: $(awk '{print $2}' "$work/beside.txt" | sort | uniq -c | tr '\n' ' ')"
if [[ -n "$perf" ]]; then
	cat "$work/perf.log"
	[[ -f "$work/perf/perf.data" ]] && rm -rf "${folder}perf" && mv "$work/perf" "${folder}perf"
fi
[[ -n "${OBNG_DLL:-}" ]] && cp "$(dirname "$OBNG_DLL")/OBNG.map" "${folder}OBNG.map" 2>/dev/null
echo "$folder"
