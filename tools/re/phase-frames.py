#!/usr/bin/env python3
"""One phase of a hand script, run by run: the frames between two marks of OBVR.log from OBVR's capture - their
length, the scene pass, the pose wait and the work (length less the pose wait) - and the eye pass's records a frame
from [Probe] GpuPasses's last summary in the phase, when the run has it.
Usage: phase-frames.py <from mark> <to mark> <run folder>..."""
import csv, os, re, statistics as st, sys
m0, m1, runs = sys.argv[1], sys.argv[2], sys.argv[3:]
for run in runs:
    marks = {}
    for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
        m = re.search(r'HandScript: mark (\S+) \(.*tick (\d+)\)', line)
        if m:
            marks[m.group(1)] = int(m.group(2))
    lo, hi = marks[m0], marks[m1]
    cap = next(os.path.join(run, d) for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report'))
    ev = [e for e in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'), newline='')) if lo <= int(e['start_tick']) < hi]
    ms = lambda kind: [(int(e['end_tick']) - int(e['start_tick'])) / 1e4 for e in ev if e['event_type'] == kind]
    ends = sorted(int(e['end_tick']) for e in ev if e['event_type'] == 'present')
    length = [(b - a) / 1e4 for a, b in zip(ends, ends[1:])]
    scene, wait = ms('scene_pass'), ms('poses_wait')
    work = st.mean(length) - sum(wait) / max(len(length), 1)
    records = ''
    for line in open(os.path.join(run, 'OBNG.log'), errors='replace'):
        if 'Probe: GPU passes -' in line and 'frames read back' in line:
            found = [(float(r), l) for l, r in re.findall(r'\(([\d.]+) lists, ([\d.]+) records, target \d{4,}x\d+\)', line)]
            if found:
                r, l = max(found)
                records = f'; eye pass {r:.0f} records in {l} lists (last summary of the run)'
    over = sum(l > 1.5 * st.median(length) for l in length) if length else 0
    print(f'{os.path.basename(run.rstrip("/"))}: {len(length)} frames, {st.mean(length):.1f} ms a frame (median {st.median(length):.1f}), '
          f'scene pass {st.mean(scene):.1f} (p90 {sorted(scene)[int(len(scene) * .9)]:.1f}), pose wait {st.mean(wait):.1f}, work {work:.1f}{records}')
