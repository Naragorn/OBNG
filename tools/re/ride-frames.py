#!/usr/bin/env python3
"""A harness ride's frames between two marks, one line a run: frames over 20/30/40/50 ms, the longest, mean and p99
(OBVR's capture), the main thread's slow texture loads ([Probe] SlowCalls, thread of the first UpdateStep spike is not
known, so the main thread is the one most texture lines of the loading menu ran on: caller 00591481), the main thread's
loader waits ([Probe] LoaderWaits) and the address space at the end.
Usage: ride-frames.py <run folder>... [--from ride] [--to ride-120]"""
import collections, csv, os, re, sys

args, skip = [], False
for a in sys.argv[1:]:
    if skip:
        skip = False
    elif a.startswith('--'):
        skip = True
    else:
        args.append(a)
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
first, last = opt('--from', 'ride'), opt('--to', 'ride-120')
for run in args:
    log = open(os.path.join(run, 'OBNG.log'), errors='ignore').read()
    marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    a, b = marks[first], marks[last]
    ticks = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(run, 'OBVR-performance-1-5', 'frames.csv'))) if r['start_tick'])
    frames = sorted((y - x) / 1e4 for x, y in zip(ticks, ticks[1:]) if a <= x <= b)
    menu = collections.Counter(t for t in re.findall(r'slow call - source texture [\d.]+ ms "[^"]*", caller 00591481, thread (\d+)', log))
    main = menu.most_common(1)[0][0] if menu else '432'
    loads = [(float(ms), path) for ms, path, thread, tick in
             re.findall(r'slow call - source texture ([\d.]+) ms "([^"]*)", caller \w+, thread (\d+), ending at tick (\d+)', log)
             if thread == main and a <= int(tick) <= b]
    waits = [float(ms) for ms, thread, tick in re.findall(r'loader wait - .*? ([\d.]+) ms, caller \w+, thread (\d+), .*?ending at tick (\d+)', log)
             if thread == main and a <= int(tick) <= b]
    space = re.findall(r'address space (\d+) of 4095 MB used \(\d+ %, peak (\d+) MB\)', log)
    over = lambda ms: sum(f > ms for f in frames)
    print(f'{os.path.basename(run)}: frames >20/30/40/50 ms {over(20)}/{over(30)}/{over(40)}/{over(50)}, longest {frames[-1]:.1f}, '
          f'mean {sum(frames) / len(frames):.2f}, p99 {frames[int(len(frames) * 0.99)]:.1f} | main-thread textures {len(loads)} '
          f'({sum(m for m, _ in loads):.1f} ms) | loader waits {len(waits)} ({sum(waits):.1f} ms) | address space end {space[-1][0] if space else "?"} '
          f'(peak {space[-1][1] if space else "?"}) MB')
    for ms, path in sorted(loads, reverse=True)[:4]:
        print(f'    {ms:6.1f} ms {path}')
