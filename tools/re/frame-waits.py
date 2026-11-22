#!/usr/bin/env python3
"""A capture's frames (present to present) split into what OBVR's capture names inside them - the pose wait, the
vsync pacing, the submits, the scene passes - for the frames near the median and the late ones (more than --over ms
above it), and the frame-length histogram: says whether a late frame is work or a wait for the next interval. The work a frame
(its length less the pose wait) is the continuous measure; the count of late frames is a cliff at the interval.
Usage: frame-waits.py <run folder>... [--over 3]"""
import bisect, collections, csv, json, os, statistics as st, sys

args = [a for a in sys.argv[1:] if not a.startswith('--')]
over = float(sys.argv[sys.argv.index('--over') + 1]) if '--over' in sys.argv else 3.0
args = [a for a in args if a != str(over) and a != sys.argv[sys.argv.index('--over') + 1]] if '--over' in sys.argv else args
for run in args:
    cap = next(os.path.join(run, d) for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report'))
    hz = float(json.load(open(os.path.join(cap, 'manifest.json'))).get('qpc_frequency', 10_000_000))
    events = list(csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'), newline='')))
    ends = sorted(int(e['end_tick']) for e in events if e['event_type'] == 'present')
    frames = [collections.Counter() for _ in ends]
    for e in events:
        i = bisect.bisect_left(ends, int(e['end_tick']))
        if 0 < i < len(ends):
            frames[i][e['event_type']] += (int(e['end_tick']) - int(e['start_tick'])) * 1000.0 / hz
    length = [(ends[i] - ends[i - 1]) * 1000.0 / hz for i in range(1, len(ends))]
    frames = frames[1:]
    median = st.median(length)
    near = [f for f, l in zip(frames, length) if abs(l - median) <= 1.0]
    late = [f for f, l in zip(frames, length) if l > median + over]
    hist = collections.Counter(round(l / median * 2) / 2 for l in length)
    print(f'== {run}\n  {len(length)} frames, median {median:.2f} ms; near it {len(near)}, late {len(late)}')
    work = sorted(l - f['poses_wait'] for f, l in zip(frames, length))
    q = lambda p: work[min(len(work) - 1, int(len(work) * p))]
    print(f'  work a frame (length less the pose wait): median {q(0.5):.2f} ms, p90 {q(0.9):.2f}, p99 {q(0.99):.2f}, '
          f'mean {st.mean(work):.2f}; over the interval {sum(w > median for w in work)} frames')
    print('  frame lengths in medians: ' + ', '.join(f'{k:g}x {v}' for k, v in sorted(hist.items())))
    names = sorted({k for f in frames for k in f}, key=lambda k: -st.mean(f[k] for f in late) if late else 0)
    for k in names:
        a = st.mean(f[k] for f in near) if near else 0.0
        b = st.mean(f[k] for f in late) if late else 0.0
        if max(a, b) >= 0.05:
            print(f'  {k:20s} near the median {a:6.2f} ms   late {b:6.2f} ms   {b - a:+6.2f}')
