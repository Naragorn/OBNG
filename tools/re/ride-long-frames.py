#!/usr/bin/env python3
"""A ride's frames over --over ms (default 20) by where they fall: within 1.5 s after a half-cell line (the cell
buffer's evictions), at a cell crossing, in the first 6 s, elsewhere - for obng-profile-ride-anvil.txt's timeline
(crossings at 6.35 s + 11.79 s k after the mark "ride", half-cell lines 5.9 s later). One line a run, with the unloads
that share a long update step ([Timed] spike lines of unload:Cell) and OBNG's task end budget line.
Usage: ride-long-frames.py <run folder>... [--over 20]"""
import csv, os, re, sys

runs, skip, over = [], False, 20.0
for i, a in enumerate(sys.argv[1:]):
    if skip:
        skip = False
    elif a == '--over':
        over, skip = float(sys.argv[i + 2]), True
    else:
        runs.append(a)
for run in runs:
    marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    ticks = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(run, 'OBVR-performance-1-5', 'frames.csv'))) if r['start_tick'])
    frames = [((a - marks['ride']) / 1e7, (b - a) / 1e4) for a, b in zip(ticks, ticks[1:]) if marks['ride'] <= a <= marks['ride-120']]
    long = [f for f in frames if f[1] > over]
    evictions = [f for f in long if any(-0.2 <= f[0] - (12.25 + 11.79 * k) <= 1.5 for k in range(10))]
    crossings = [f for f in long if any(-0.3 <= f[0] - (6.35 + 11.79 * k) <= 0.5 for k in range(10))]
    first = [f for f in long if f[0] < 6 and f not in evictions and f not in crossings]
    log = open(os.path.join(run, 'OBNG.log'), errors='ignore').read()
    shared = [int(c) for c in re.findall(r'unload:Cell-0x00447BA0 [\d.]+ ms \((\d+) call', log)]
    unloads = re.findall(r'Timed unload:Cell-0x00447BA0 0x00447BA0 - (\d+) calls, ([\d.]+) ms in all', log)
    budget = re.search(r'Route: task end budget - (off|the pump ends finished background tasks for (\d+) ms)', log)
    label = 'no line' if not budget else ('game' if budget[1] == 'off' else budget[2] + ' ms')
    ordered = sorted(f[1] for f in frames)
    print(f'{os.path.basename(run)} budget {label:7s}: over {over:g} ms {len(long):3d} | evictions {len(evictions):2d} | crossings {len(crossings):2d} | '
          f'first 6 s {len(first):2d} | elsewhere {len(long) - len(evictions) - len(crossings) - len(first):3d} | mean {sum(ordered) / len(ordered):.2f} '
          f'p99 {ordered[int(len(ordered) * 0.99)]:.1f} | unloads {sum(int(a) for a, _ in unloads)} in {sum(float(b) for _, b in unloads):.0f} ms, '
          f'per long step {shared}')
