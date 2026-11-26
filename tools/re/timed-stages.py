#!/usr/bin/env python3
"""The [Timed] probe's functions per frame between two marks of a hand script: OBNG.log's "Timed <name> 0x<address>
- N calls, X ms in all (...), Z ms without the timed functions inside, longest L ms" lines, one set a summary window,
cut by the summary's clock ("Log: summary at tick N", OBNG builds since 2026-11-26) against OBVR's marks. Only the
windows wholly between the two marks count. A frame is a call of the first timed function (F1, the frame's step).

Per function: ms a frame with the timed functions inside it (inclusive) and without them (its own), calls a frame
and the longest call; in the order of the INI. With several runs the per-frame values are given run by run.

Usage: timed-stages.py <run folder>... --from MARK --to MARK"""
import csv, os, re, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


FROM, TO = opt('--from', 'still'), opt('--to', 'done')
TICK = re.compile(r'Log: summary at tick (\d+)')
TIMED = re.compile(r'^Timed (\S+) 0x[0-9A-F]{8} - (\d+) calls, ([\d.]+) ms in all \([\d.]+ us each\), ([\d.]+) ms without the timed '
                   r'functions inside, longest ([\d.]+) ms')
MARK = re.compile(r'HandScript: mark (\S+) .*?tick (\d+)\)')


def read(run):
    marks = {m: int(t) for m, t in MARK.findall(open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    a, b = marks[FROM], marks[TO]
    ticks, order, sums, windows = [], [], {}, 0
    current = None
    for line in open(os.path.join(run, 'OBNG.log'), errors='ignore'):
        t = TICK.search(line)
        if t:
            ticks.append(int(t[1]))
            current = (ticks[-2], ticks[-1]) if len(ticks) >= 2 else None
            counted = False
            continue
        m = TIMED.match(line)
        if not m or current is None or not (a <= current[0] and current[1] <= b):
            continue
        name = m[1]
        if name not in sums:
            order.append(name)
            sums[name] = [0, 0.0, 0.0, 0.0]
        s = sums[name]
        s[0] += int(m[2])
        s[1] += float(m[3])
        s[2] += float(m[4])
        s[3] = max(s[3], float(m[5]))
        if not counted:
            windows += 1
            counted = True
    return order, sums, windows, (b - a) / 1e7, waited(run, a, b)


def waited(run, a, b):
    """The mean of OBVR's pose waits a frame between the marks (its capture, when the stretch lies in it): the wait
    sits inside the render frame (OBVR's late pose wait at the scene render's entry), so the render frame's own time
    holds it."""
    path = os.path.join(run, 'OBVR-performance-1-5', 'cpu_events.csv')
    if not os.path.isfile(path):
        return None
    w = [(int(r['start_tick']), int(r['end_tick'])) for r in csv.DictReader(open(path)) if r['event_type'] == 'poses_wait']
    w = [(s, e) for s, e in w if a <= e <= b]
    return sum(e - s for s, e in w) / len(w) / 1e4 if w else None


runs = [r for r in sys.argv[1:] if os.path.isdir(r)]
data = [read(r) for r in runs]
order = []
for o, _, _, _, _ in data:
    order += [n for n in o if n not in order]
print(f"{FROM} - {TO}; ms a frame with what it calls (inclusive) / its own, calls a frame, the longest call")
for r, (o, s, w, length, wait) in zip(runs, data):
    frames = s[o[0]][0] if o else 0
    print(f"  {os.path.basename(os.path.normpath(r))}: {w} windows of the stretch's {length:.1f} s, {frames} frames ({o[0] if o else '-'} calls)"
          + (f"; OBVR's pose wait {wait:.3f} ms a frame (inside the render frame)" if wait is not None else ''))
for name in order:
    cells = []
    for o, s, w, _, _ in data:
        frames = s[o[0]][0] if o else 0
        if name not in s or frames == 0:
            cells.append(f"{'-':>27s}")
            continue
        calls, inc, exc, longest = s[name]
        cells.append(f"{inc / frames:6.3f} / {exc / frames:6.3f} {calls / frames:5.1f} {longest:6.1f}")
    print(f"{name:42s} " + ' | '.join(cells))
