#!/usr/bin/env python3
"""A run with [Probe] PassMaskAlternate (or another alternation that logs the same switch lines): the phases the plugin switched through (OBNG.log's switch lines and OBVR's
capture carry the same counter's ticks), a line a phase - the mask, the records a frame of the summary that ended
the phase, the scene pass and the frame's own work from OBVR's capture - and both sides summed up.
Usage: alternate-phases.py <run folder> [--from <mark>] [--to <mark>] [--guard 1.0] [--period 13.89]
  --from / --to  only the phases that lie between these marks of the hand script (whole phases)
  --guard        seconds after a switch left out of the timing (every pass list builds anew there)
  --period       the frame interval in ms: the share of frames whose own work is over it"""
import collections, csv, os, re, statistics as st, sys

run = sys.argv[1]


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


guard, period = float(opt('--guard', '1.0')), float(opt('--period', '13.89'))
marks = {}
for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
    m = re.search(r'HandScript: mark (\S+) \(.*tick (\d+)\)', line)
    if m:
        marks[m.group(1)] = int(m.group(2))
lo = marks[opt('--from', '')] if '--from' in sys.argv else 0
hi = marks[opt('--to', '')] if '--to' in sys.argv else 1 << 62

# The phases: [start tick, end tick, side, mask, records a frame]. A summary writes its switch line before its
# record count (measured: the count after a switch line is the window's that the switch ended).
phases, start, side, mask, ended = [], None, None, None, None
for line in open(os.path.join(run, 'OBNG.log'), errors='replace'):
    m = re.search(r'draw instancing \(\w+\) - ([\d.]+) records a frame in pass lists', line)
    if m and ended is not None:
        ended[4], ended = float(m.group(1)), None
    m = re.search(r"alternate - switch \d+ at tick (\d+): the (?:mask|carry's reads) (\w+) from here \((flipped|the game's)\)", line)
    if m:
        tick = int(m.group(1))
        if start is not None:
            ended = [start, tick, side, mask, float('nan')]
            phases.append(ended)
        start, side, mask = tick, 'flipped' if m.group(3) == 'flipped' else 'game', m.group(2)
phases = [p for p in phases if p[0] >= lo and p[1] <= hi]
if not phases:
    sys.exit(f'{run}: no whole phase between the marks (switch lines in OBNG.log: is the alternation on?)')

caps = [d for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report')]
cap = os.path.join(run, caps[0])
rows = [r for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick']]
frames = sorted(((int(r['present_id']), int(r['start_tick'])) for r in rows), key=lambda f: f[1])
events = collections.defaultdict(lambda: collections.defaultdict(float))
for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'), newline='')):
    events[int(r['present_id'])][r['event_type']] += (int(r['end_tick']) - int(r['start_tick'])) / 1e4
captured = (frames[0][1], frames[-1][1])


def measure(a, b):
    """The scene pass and the own work of the frames that start in [a, b)."""
    scene, work = [], []
    for (pid, s), (_, e) in zip(frames, frames[1:]):
        if a <= s < b and (e - s) / 1e4 < 60:
            scene.append(events[pid]['scene_pass'])
            work.append((e - s) / 1e4 - events[pid]['poses_wait'] - events[pid]['pacing_vsync'])
    return scene, work


def q(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))] if v else float('nan')


print(f'{run}: {len(phases)} whole phases, guard {guard} s, period {period} ms')
by = {'game': [[], [], []], 'flipped': [[], [], []]}
for a, b, side, mask, records in phases:
    if a + guard * 1e7 < captured[0] or b > captured[1]:
        print(f'  {side:7s} {mask:>6s}  {(b - a) / 1e7:4.1f} s  records {records}  (outside the capture)')
        continue
    scene, work = measure(a + guard * 1e7, b)
    by[side][0] += scene
    by[side][1] += work
    if records == records:  # a phase whose summary's count was not found in the log has none
        by[side][2].append(records)
    print(f'  {side:7s} {mask:>6s}  {(b - a) / 1e7:4.1f} s  records {records:7.1f}  {len(scene):4d} frames  scene pass {st.mean(scene):5.2f} ms'
          f'  own work mean {st.mean(work):5.2f} p90 {q(work, .9):5.2f}')
def mean(values):
    return st.mean(values) if values else float('nan')


for side, (scene, work, records) in by.items():
    if scene:
        print(f"{side:7s}: {len(scene)} frames, records a frame {mean(records):.0f} ({len(records)} phases counted); scene pass mean {st.mean(scene):.2f} ms, median {st.median(scene):.2f}, "
              f"p90 {q(scene, .9):.2f}; own work mean {st.mean(work):.2f}, median {st.median(work):.2f}, p90 {q(work, .9):.2f}, over {period} ms {100 * sum(w > period for w in work) / len(work):.1f} %")
if by['game'][0] and by['flipped'][0]:
    g, f = by['game'], by['flipped']
    print(f"flipped against the game's: records {mean(f[2]) - mean(g[2]):+.0f} ({100 * (mean(f[2]) / mean(g[2]) - 1):+.1f} %), "
          f"scene pass {st.mean(f[0]) - st.mean(g[0]):+.2f} ms ({100 * (st.mean(f[0]) / st.mean(g[0]) - 1):+.1f} %), own work {st.mean(f[1]) - st.mean(g[1]):+.2f} ms")
