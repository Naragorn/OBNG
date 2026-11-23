#!/usr/bin/env python3
"""A run of obng-prison-tutorial-alternate.txt: the eye pictures dumped at the marks d01, d02, ... put into the
phases [Route] FogPassFoldAlternate switched through (OBNG.log's switch lines and OBVR.log's marks carry the same
counter's ticks), each picture compared with the one before it - inside one phase that is the scene's own noise,
across a switch the route's change - and the scene pass's time per phase from OBVR's capture.
Usage: fold-pictures.py <run folder> [--threshold 8] [--eye Left] [--guard 1.0]
  --guard  seconds after a switch in which a picture counts for neither side (the lists build anew)"""
import csv, os, re, statistics as st, sys
from PIL import Image, ImageChops
run = sys.argv[1]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
threshold, eye, guard = int(opt('--threshold', '8')), opt('--eye', 'Left'), float(opt('--guard', '1.0'))
marks = []
for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
    m = re.search(r'HandScript: mark (d\d+) \(.*tick (\d+)\)', line)
    if m:
        marks.append((m.group(1), int(m.group(2))))
switches = []
for line in open(os.path.join(run, 'OBNG.log'), errors='replace'):
    m = re.search(r"fog pass fold - switch \d+ at tick (\d+): the (route|game)'s ending", line)
    if m:
        switches.append((int(m.group(1)), m.group(2)))
def phase(tick):
    """(side, index of the phase, seconds since its switch); the run starts with the game's ending."""
    side, index, since = 'game', 0, 1e9
    for i, (at, to) in enumerate(switches):
        if at <= tick:
            side, index, since = to, i + 1, (tick - at) / 1e7
    return side, index, since
def picture(mark):
    path = os.path.join(run, f'OBVR-Eye-{eye}-{mark}-f00.png')
    return Image.open(path).convert('RGB') if os.path.exists(path) else None
def differ(a, b):
    d = ImageChops.difference(a, b).convert('L')
    d = d.resize((d.size[0] // 4, d.size[1] // 4), Image.BOX)
    h = d.histogram()
    n = sum(h)
    return sum(h[threshold + 1:]) * 100.0 / n, sum(i * c for i, c in enumerate(h)) / n
inside, across, rows = [], [], []
prev = None
for mark, tick in marks:
    pic = picture(mark)
    side, index, since = phase(tick)
    if pic is None:
        continue
    if prev is not None and min(since, prev[4]) >= guard:
        share, mean = differ(prev[0], pic)
        kind = 'inside' if prev[2] == index else 'across' if prev[1] != side else 'skipped a phase'
        (inside if kind == 'inside' else across if kind == 'across' else []).append((share, mean))
        rows.append(f'  {prev[3]} -> {mark}  {kind:7s} ({prev[1]} -> {side})  {share:6.2f} % differ, mean {mean:5.2f}')
    prev = (pic, side, index, mark, since)
print(f'{run}: {len(marks)} marks, {len(switches)} switches, eye {eye}, threshold {threshold} of 255 after a 4x reduction')
print('\n'.join(rows))
for name, rowsOf in (('inside a phase (the noise)', inside), ('across a switch', across)):
    if rowsOf:
        print(f'{name}: {len(rowsOf)} pairs, pixels differing median {st.median(r[0] for r in rowsOf):.2f} % (max {max(r[0] for r in rowsOf):.2f}), '
              f'mean difference median {st.median(r[1] for r in rowsOf):.2f} (max {max(r[1] for r in rowsOf):.2f})')
caps = [d for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report')]
if caps:
    by = {'game': [], 'route': []}
    for e in csv.DictReader(open(os.path.join(run, caps[0], 'cpu_events.csv'), newline='')):
        if e['event_type'] == 'scene_pass' and marks and marks[0][1] <= int(e['start_tick']) <= marks[-1][1]:
            side, index, since = phase(int(e['start_tick']))
            if since >= guard:
                by[side].append((int(e['end_tick']) - int(e['start_tick'])) / 1e4)
    for side, v in by.items():
        if v:
            v.sort()
            print(f"scene pass with the {side}'s ending: {len(v)} frames, mean {st.mean(v):.2f} ms, median {st.median(v):.2f}, p90 {v[int(len(v) * .9)]:.2f}")
