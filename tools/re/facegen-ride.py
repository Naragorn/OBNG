#!/usr/bin/env python3
"""A ride's FaceGen numbers, one block a run: the frames over --over ms between the marks ride and ride-120 (OBVR's
capture), the main thread's waits on FaceGen's cache lock 0x00B39C00 ([Probe] LockCalls' per-step lines), the pump's
ends of queued characters ([Probe] TaskEnds), and the three routes' own summary lines (FaceGen loads, body texture
premake, animation lazy append).
Usage: facegen-ride.py <run folder>... [--over 14.5]"""
import csv, os, re, sys

args, over, skip = [], 14.5, False
for i, a in enumerate(sys.argv[1:]):
    if skip:
        skip = False
    elif a == '--over':
        over, skip = float(sys.argv[i + 2]), True
    else:
        args.append(a)

CHARACTER = '00A36DDC'
LOCK = '00B39C00'

def marks(run):
    out = {}
    for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
        m = re.search(r'HandScript: mark (\S+) \(t=[0-9.]+ s, line \d+, scene call \d+, tick (\d+)\)', line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out

def frames(run, start, end):
    """The presents' end ticks between two ticks (cpu_events.csv of OBVR's capture)."""
    capture = next(d for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report'))
    ticks = []
    for row in csv.DictReader(open(os.path.join(run, capture, 'cpu_events.csv'), newline='')):
        if row['event_type'] == 'present' and start <= int(row['end_tick']) <= end:
            ticks.append(int(row['end_tick']))
    return sorted(ticks)

for run in args:
    log = open(os.path.join(run, 'OBNG.log'), errors='replace').read().splitlines()
    mark = marks(run)
    line = [os.path.basename(run.rstrip('/'))]
    if 'ride' in mark and 'ride-120' in mark:
        per_ms = (mark['ride-120'] - mark['ride']) / 120000.0
        presents = frames(run, mark['ride'], mark['ride-120'])
        gaps = [(b - a) / per_ms for a, b in zip(presents, presents[1:])]
        line.append('frames %d, over %.1f ms %d, over 20 ms %d, longest %.1f' % (len(gaps), over, sum(g > over for g in gaps),
                                                                            sum(g > 20 for g in gaps), max(gaps) if gaps else 0))
        in_ride = lambda tick: mark['ride'] <= tick <= mark['ride-120']
    else:
        line.append('no ride marks')
        in_ride = lambda tick: True
    waits = []
    for l in log:
        m = re.match(r'Probe: main-thread lock waits - the step ending at tick (\d+)', l)
        if m and in_ride(int(m.group(1))):
            waits += [float(x) for x in re.findall(LOCK + r' \d+ / ([0-9.]+) / [0-9.]+ at [0-9A-F]+', l)]
    long_waits = [w for w in waits if w >= 1.0]
    line.append("main-thread steps waiting on FaceGen's lock: %d, %.1f ms in all; of 1 ms or more %d (%s)"
                % (len(waits), sum(waits), len(long_waits), ' '.join('%.1f' % w for w in sorted(long_waits, reverse=True)[:8])))
    ends = ms = 0
    longest = 0.0
    for l in log:
        m = re.match(r'Probe: task ends - vtable ' + CHARACTER + r': (\d+) ends, ([0-9.]+) ms in all, the longest ([0-9.]+) ms', l)
        if m:
            ends, ms, longest = ends + int(m.group(1)), ms + float(m.group(2)), max(longest, float(m.group(3)))
    long_ends = sorted((float(m.group(1)) for l in log for m in [re.match(r'Probe: task end - vtable ' + CHARACTER + r' ([0-9.]+) ms', l)] if m), reverse=True)
    line.append('queued characters ended (whole run): %d in %.0f ms, longest %.1f; of 8 ms or more %d (%s)'
                % (ends, ms, longest, sum(e >= 8 for e in long_ends), ' '.join('%.1f' % e for e in long_ends[:8])))
    for key in ('Route: FaceGen loads - since', 'Route: body texture premake - since', 'Route: animation lazy append - since'):
        last = [l for l in log if l.startswith(key)]
        if last:
            line.append(last[-1][:400])
    print('\n  '.join(line))
