#!/usr/bin/env python3
"""What moving OBVR's pose wait behind the grid's move would gain at the cell crossings of obng-profile-ride-anvil.txt
(crossings at 6.35 s + 11.79 s k after the mark "ride"). From OBVR's capture (cpu_events.csv: present, poses_wait) a
frame is the work before the pose wait, the wait (it ends at a fixed beat) and the work after it. The frames over
--over ms (default 14.5: a missed beat) within -0.1 .. +0.3 s of a crossing are sorted into
  late   the wait ended on the beat and the work after it ran long (the grid's move): the present comes late
  next   the frame after a late one: it starts late, misses its beat and waits for the next; gone when "late" is
  before the work before the wait was over the slack (ends of background tasks): not touched by the move
A late frame "would fit" when its work before the wait plus the move is at most the normal wait end less --margin ms
(default 1.0), so the wait would absorb the move if it came after it, and what is left of the extra work after the
wait (over a normal frame's; the render of the new cells) is at most the normal frame's submit hold. The move's time
is the step's spike line in OBNG.log where there is one (steps over SpikeMs), else the whole extra.
Usage: crossing-slack.py <run folder>... [--over MS] [--margin MS] [--lines]"""
import bisect, csv, os, re, statistics as st, sys

args, opts, runs = sys.argv[1:], {'--over': 14.5, '--margin': 1.0}, []
i = 0
while i < len(args):
    if args[i] in opts:
        opts[args[i]] = float(args[i + 1]); i += 2
    else:
        runs.append(args[i]); i += 1
lines = '--lines' in runs
runs = [r for r in runs if r != '--lines']
tally = {'late': 0, 'fits': 0, 'next': 0, 'next gone': 0, 'before': 0, 'crossings': 0, 'clean now': 0, 'clean then': 0}
for run in runs:
    marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    presents, waits, holds = [], [], []
    for r in csv.DictReader(open(os.path.join(run, 'OBVR-performance-1-5', 'cpu_events.csv'))):
        if r['event_type'] == 'present':
            presents.append(int(r['end_tick']))
        elif r['event_type'] == 'poses_wait':
            waits.append((int(r['start_tick']), int(r['end_tick'])))
        elif r['event_type'] == 'submit_hold' and marks['ride'] <= int(r['start_tick']) <= marks['ride-120']:
            holds.append((int(r['end_tick']) - int(r['start_tick'])) / 1e4)
    presents.sort(); waits.sort()
    starts = [w[0] for w in waits]
    moves = {int(t): float(ms) for t, ms in re.findall(r'Timed spike: frame:UpdateStep [\d.]+ ms ending at tick (\d+) - .*?update:GridMove-0x00444FB0 ([\d.]+) ms', open(os.path.join(run, 'OBNG.log'), errors='ignore').read())}
    frames = []  # [time after the mark in s, length, before the wait, the wait's end, after the wait, the grid move's ms or None], ms
    for a, b in zip(presents, presents[1:]):
        if not marks['ride'] <= a <= marks['ride-120']:
            continue
        inside = waits[bisect.bisect_left(starts, a):bisect.bisect_left(starts, b)]
        if len(inside) != 1:
            continue
        (s, e), = inside
        move = next((ms for t, ms in moves.items() if a < t <= b + 20000), None)
        frames.append([(a - marks['ride']) / 1e7, (b - a) / 1e4, (s - a) / 1e4, (e - a) / 1e4, (b - e) / 1e4, move])
    normal = [f for f in frames if f[1] < opts['--over']]
    wait_end, after = st.median(f[3] for f in normal), st.median(f[4] for f in normal)
    room, hold = wait_end - opts['--margin'], st.median(holds)
    count = dict.fromkeys(tally, 0)
    for k in range(10):
        at = 6.35 + 11.79 * k
        idx = [n for n, f in enumerate(frames) if -0.1 <= f[0] - at <= 0.3 and f[1] > opts['--over']]
        if not any(-0.1 <= f[0] - at <= 0.3 for f in frames):
            continue
        count['crossings'] += 1
        left, prev_late, prev_fits = 0, -2, False
        for n in idx:
            f = frames[n]
            extra = f[4] - after
            if f[3] <= wait_end + 0.5 and extra >= 1.0:
                move = f[5] if f[5] is not None else extra
                kind, fits = 'late', f[2] + move <= room and extra - move <= hold
                count['late'] += 1; count['fits'] += fits
                prev_late, prev_fits = n, fits
                left += not fits
                note = (f'extra {extra:4.1f}' + (f' (move {f[5]:.1f})' if f[5] is not None else '') + f', before + move {f[2] + move:4.1f} of {room:.1f}, '
                        f'rest {extra - move:.1f} of {hold:.1f}: {"fits" if fits else "over"}')
            elif n == prev_late + 1 and f[2] <= room:
                kind = 'next'
                count['next'] += 1; count['next gone'] += prev_fits
                left += not prev_fits
                note = 'gone with the late frame before it' if prev_fits else 'stays'
            else:
                kind, note = 'before', f'{f[2]:.1f} ms before the wait'
                count['before'] += 1; left += 1
            if lines:
                print(f'   crossing {k + 1:2d} {f[0]:7.3f} s frame {f[1]:5.1f} = {f[2]:4.1f} before, wait to +{f[3]:4.1f}, {f[4]:5.1f} after | {kind:6s} {note}')
        count['clean now'] += not idx; count['clean then'] += left == 0
    for key in tally:
        tally[key] += count[key]
    print(f'{os.path.basename(os.path.dirname(run))}/{os.path.basename(run)}: normal frame waits to +{wait_end:.1f}, {after:.1f} after, holds {hold:.1f} | crossings {count["crossings"]}, without a missed beat '
          f'{count["clean now"]} -> {count["clean then"]} | late {count["late"]} (would fit {count["fits"]}), next {count["next"]} (gone {count["next gone"]}), before {count["before"]}')
print(f'all: crossings {tally["crossings"]}, without a missed beat {tally["clean now"]} -> {tally["clean then"]} | late {tally["late"]} (would fit {tally["fits"]}), '
      f'next {tally["next"]} (gone {tally["next gone"]}), before {tally["before"]}')
