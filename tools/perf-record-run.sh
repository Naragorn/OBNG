#!/usr/bin/env bash
# Records the game of a harness run with perf, for tools/re/perf-pass.py: waits for Oblivion to appear, then for a
# mark of the run's hand script in the game's live OBVR.log, and records the process for the given seconds -
# cycles, AMD's IBS operation and fetch samples (user mode, no root: perf_event_paranoid 2 is enough) and the
# render thread's pipeline counters. Started before the run, in the background; it ends by itself.
#
#   tools/perf-record-run.sh <out folder> [seconds=42] [mark=h01]
#
# The recording's clock is CLOCK_MONOTONIC_RAW (-k raw): Wine's QueryPerformanceCounter, so the samples carry the
# ticks of OBVR's capture. Keep the folder with the run (artifacts/shadow/<scenario>/<run>/perf): a scratch folder
# is gone with a reboot (2026-11-24).
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: $0 <out folder> [seconds] [mark]}"; seconds="${2:-42}"; mark="${3:-h01}"
log="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}/OBVR.log"
mkdir -p "$out"
pid=""
for _ in $(seq 1 18000); do
	pid="$("$root/tools/machine-busy.py" | awk '$1=="oblivion"{print $2; exit}')"
	[[ -n "$pid" ]] && break
	sleep 0.5
done
[[ -n "$pid" ]] || { echo "perf: no game appeared"; exit 2; }
echo "perf: game $pid at $(date +%H:%M:%S)"
sleep 20  # the plugin has started its own log by now; the one before the run may hold the mark too
for _ in $(seq 1 1200); do
	grep -q "HandScript: mark $mark " "$log" 2>/dev/null && break
	[[ -d "/proc/$pid" ]] || { echo "perf: the game ended before the mark $mark"; exit 3; }
	sleep 0.25
done
grep -q "HandScript: mark $mark " "$log" 2>/dev/null || { echo "perf: the mark $mark was not seen"; exit 4; }
cp "/proc/$pid/maps" "$out/maps.txt"
for t in /proc/"$pid"/task/*; do echo "${t##*/} $(cat "$t/comm" 2>/dev/null)"; done > "$out/threads.txt"
echo "perf: recording $pid for $seconds s from $(date +%H:%M:%S)"
# The main thread is the render thread (its id is the process's).
perf stat -t "$pid" -M PipelineL1,PipelineL2 -o "$out/stat-pipeline.txt" -- sleep "$seconds" 2> "$out/stat-pipeline.err" &
perf stat -t "$pid" -e instructions:u,cycles:u,branch-misses:u,branches:u,L1-icache-load-misses:u,iTLB-load-misses:u,dTLB-load-misses:u,cache-misses:u,ls_bad_status2.stli_other:u,ic_tag_hit_miss.instruction_cache_miss:u,l2_cache_req_stat.ic_fill_miss:u,l2_cache_req_stat.ic_dc_miss_in_l2:u,bp_l1_tlb_miss_l2_tlb_miss.all:u \
	-o "$out/stat-events.txt" -- sleep "$seconds" 2> "$out/stat-events.err" &
perf record -q -p "$pid" -k raw -R -d -W -e cycles:u -e ibs_op/swfilt=1/u -e ibs_fetch/swfilt=1/u -F 5000 -o "$out/perf.data" -- sleep "$seconds" 2> "$out/record.err"
wait
echo "perf: done $(date +%H:%M:%S), perf.data $(du -h "$out/perf.data" | cut -f1)"
