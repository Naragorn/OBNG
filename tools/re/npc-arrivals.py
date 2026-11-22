#!/usr/bin/env python3
"""The arrivals of obng-npc-arrivals.txt, run against run: for each of the thirty marks a01 ... a30 the longest frame
in the three seconds after it and the frames more than --over ms above that window's median frame (OBVR's capture,
present to present; the scene fills with the thirty NPCs, so a fixed limit would count the crowd, not the arrival), then per run
the sums, the queued characters' ends ([Probe] TaskEnds) and the routes' own summary lines. The same NPC arrives at
the same mark in every run, so the columns compare arrival by arrival.
Usage: npc-arrivals.py <run folder>... [--over 3] [--rows]"""
import csv, os, re, sys

args, over, rows, skip = [], 3.0, False, False
for i, a in enumerate(sys.argv[1:]):
    if skip:
        skip = False
    elif a == '--over':
        over, skip = float(sys.argv[i + 2]), True
    elif a == '--rows':
        rows = True
    else:
        args.append(a)
CHARACTER = '00A36DDC'

def marks(run):
    out = {}
    for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
        m = re.search(r'HandScript: mark (\S+) \(t=[0-9.]+ s, line \d+, scene call \d+, tick (\d+)\)', line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out

def presents(run):
    capture = next(d for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report'))
    return sorted(int(r['end_tick']) for r in csv.DictReader(open(os.path.join(run, capture, 'cpu_events.csv'), newline=''))
                  if r['event_type'] == 'present')

table = []
for run in args:
    mark = marks(run)
    if 'a01' not in mark or 'done' not in mark:
        print(os.path.basename(run.rstrip('/')), 'has no a01 / done marks')
        continue
    per_ms = (mark['done'] - mark['a01']) / 90000.0
    ticks = presents(run)
    gaps = [(a, (b - a) / per_ms) for a, b in zip(ticks, ticks[1:])]
    per = []
    for n in range(1, 31):
        start = mark.get('a%02d' % n)
        inside = [g for t, g in gaps if start is not None and start <= t < start + 3000 * per_ms]
        base = sorted(inside)[len(inside) // 2] if inside else 0.0
        per.append((max(inside) - base if inside else 0.0, sum(g > base + over for g in inside), sum(g - base for g in inside if g > base + over), base))
    table.append(per)
    log = open(os.path.join(run, 'OBNG.log'), errors='replace').read().splitlines()
    ends = [float(m.group(1)) for l in log for m in [re.match(r'Probe: task end - vtable ' + CHARACTER + r' ([0-9.]+) ms', l)] if m]
    total = [(int(m.group(1)), float(m.group(2)), float(m.group(3))) for l in log
             for m in [re.match(r'Probe: task ends - vtable ' + CHARACTER + r': (\d+) ends, ([0-9.]+) ms in all, the longest ([0-9.]+) ms', l)] if m]
    line = [os.path.basename(run.rstrip('/')),
            'arrivals: frames more than %.1f ms over their window\'s median %d, their excess %.0f ms; an arrival\'s longest frame over the median %.1f ms (mean), %.1f (worst); the median frame %.1f ms at a01, %.1f at a30'
            % (over, sum(p[1] for p in per), sum(p[2] for p in per), sum(p[0] for p in per) / 30, max(p[0] for p in per), per[0][3], per[29][3]),
            'queued characters ended (whole run): %d in %.0f ms, longest %.1f; listed long ends %d (%s)'
            % (sum(t[0] for t in total), sum(t[1] for t in total), max([t[2] for t in total] or [0]), len(ends),
               ' '.join('%.1f' % e for e in sorted(ends, reverse=True)[:8]))]
    for key in ('Route: FaceGen loads - since', 'Route: body texture premake - since', 'Route: animation name arena', 'Route: sound file cache'):
        last = [l for l in log if l.startswith(key)]
        if last:
            line.append(last[-1][:420])
    print('\n  '.join(line))
if rows and table:
    print('arrival: the longest frame over the median in ms / frames more than %.1f ms over it, a column a run' % over)
    for n in range(30):
        print('  a%02d  ' % (n + 1) + '   '.join('%5.1f / %d' % (per[n][0], per[n][1]) for per in table))
