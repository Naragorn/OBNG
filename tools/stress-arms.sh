#!/usr/bin/env bash
# The stress flight's arms in turn, one run each a round, so that every arm flies in the same hour: the flight's
# crash rate changes from hour to hour (0 of 11 one evening, 10 of 11 the next morning; docs/performance-backlog.md),
# and a series of one arm after another compares hours, not arms. Every run without OblivionWalkers.dll (measured
# not to be the cause, 2026-11-13), so the arms differ in the data and the routes only.
#   tools/stress-arms.sh <rounds> <arm>...        the arms: save windows vanilla plain
#     save     the Windows mod list, the save OBVRHarnessMOO, the routes as played (the runs of 2026-11-14 morning)
#     windows  the Windows mod list, from the main menu, the routes as played
#     vanilla  Oblivion.esm alone, from the main menu, the routes as played
#     plain    the Windows mod list, from the main menu, every route off but the stereo draw
# A file ${TMPDIR:-/tmp}/obng-stress-stop holds the series before its next run. Lines go to stdout and to
# artifacts/shadow/stress-arms.log.
set -u
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root" || exit 2
rounds="$1"; shift
log="artifacts/shadow/stress-arms.log"
played="tools/hand-scripts/obng-measure-ride-checks.ini"
export OBNG_STRESS_WITHOUT_DLL="OblivionWalkers.dll"
for ((r = 1; r <= rounds; r++)); do
	for arm in "$@"; do
		[[ -e "${TMPDIR:-/tmp}/obng-stress-stop" ]] && { echo "held before round $r arm $arm" | tee -a "$log"; exit 0; }
		ini="$played"; mods="windows"; script="obng-stress-ride-menu.txt"; save="continue"
		case "$arm" in
			save) script="obng-stress-ride.txt"; save="OBVRHarnessMOO" ;;
			windows) ;;
			vanilla) mods="only:Oblivion.esm" ;;
			plain) ini="tools/hand-scripts/obng-measure-ride-plain.ini" ;;
			*) echo "no arm $arm" >&2; exit 2 ;;
		esac
		line="$(OBNG_STRESS_MODS="$mods" OBNG_STRESS_SCRIPT="$script" OBNG_STRESS_SAVE="$save" tools/stress-ride.sh "$ini" 1 | tail -1)"
		echo "$(date +%H:%M) round $r $arm: $line" | tee -a "$log"
	done
done
