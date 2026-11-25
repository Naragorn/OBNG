#!/usr/bin/env python3
"""A ride's frames split at OBVR's pose wait (obng-profile-ride-anvil*.txt: the crossings at 6.35 s + 11.79 s k after
the mark "ride"): for each frame the work before the wait, the wait, and what comes after it - the render, whose
scene pass is OBVR's scene_pass span - from OBVR's capture (cpu_events.csv). With OBVR's late pose wait the update
runs before the wait and the render after it, so a frame misses its beat either because the update ran past the
wait's end or because the render after the wait ran past the present's time.

Usage: ride-frame-split.py <run folder>... [--over MS] [--lines N]
  --over   a frame longer than this missed its beat (default: the median frame + 0.6 ms)
  --lines  how many of the long frames away from the crossings to list (default 20)"""
import bisect, csv, os, re, statistics as st, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def frames(run):
    marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    events = {}
    for row in csv.DictReader(open(os.path.join(run, 'OBVR-performance-1-5', 'cpu_events.csv'))):
        events.setdefault(row['event_type'], []).append((int(row['start_tick']), int(row['end_tick'])))
    presents = sorted(e for _, e in events.get('present', []))
    scenes, waits = sorted(events.get('scene_pass', [])), sorted(events.get('poses_wait', []))
    scene_starts, wait_starts = [s for s, _ in scenes], [s for s, _ in waits]
    out = []
    for a, b in zip(presents, presents[1:]):
        if not marks['ride'] <= a <= marks['ride-120']:
            continue
        scene = sum((e - s) / 1e4 for s, e in scenes[bisect.bisect_left(scene_starts, a):bisect.bisect_left(scene_starts, b)])
        wait = waits[bisect.bisect_left(wait_starts, a):bisect.bisect_left(wait_starts, b)]
        if len(wait) != 1:
            continue
        out.append({'t': (a - marks['ride']) / 1e7, 'ms': (b - a) / 1e4, 'scene': scene, 'before': (wait[0][0] - a) / 1e4,
                    'wait_end': (wait[0][1] - a) / 1e4, 'after': (b - wait[0][1]) / 1e4})
    return out


def text(f):
    return f"{f['t']:6.2f} s {f['ms']:5.1f} ms (before the wait {f['before']:4.1f}, after it {f['after']:4.1f}, scene pass {f['scene']:4.1f})"


for run in [a for a in sys.argv[1:] if os.path.isdir(a)]:
    rows = frames(run)
    period = st.median(f['ms'] for f in rows)
    over = float(opt('--over', period + 0.6))
    normal = [f for f in rows if f['ms'] <= over]
    late = [f for f in rows if f['ms'] > over]
    by_update = [f for f in late if f['before'] > st.median(g['wait_end'] for g in normal) - 0.3]
    print(f"{os.path.basename(os.path.normpath(run))}: {len(rows)} frames, period {period:.2f} ms; a normal frame {st.median(f['before'] for f in normal):.2f} ms "
          f"before the wait, the wait to +{st.median(f['wait_end'] for f in normal):.2f}, {st.median(f['after'] for f in normal):.2f} after it (scene pass "
          f"{st.median(f['scene'] for f in normal):.2f}, p90 {sorted(f['scene'] for f in normal)[int(len(normal) * 0.9)]:.2f})")
    print(f"  over {over:.1f} ms: {len(late)} frames ({len(late) * 100.0 / len(rows):.1f} %) - the update past the wait's end in {len(by_update)}, "
          f"the render after it in {len(late) - len(by_update)}; over {period * 1.5:.1f} ms: {sum(1 for f in late if f['ms'] > period * 1.5)}")
    moves = []
    for k in range(10):
        at = 6.35 + 11.79 * k
        window = [f for f in rows if -0.2 <= f['t'] - at <= 0.6]
        if window:
            moves.append(max(window, key=lambda f: f['before']))
        near = [f for f in late if -0.2 <= f['t'] - at <= 0.6]
        if near:
            print(f"  crossing {k + 1:2d} at {at:6.2f} s: " + '; '.join(text(f) for f in near))
    if moves:
        befores = sorted(f['before'] for f in moves)
        print(f"  the crossings' move frames: {len(moves)}, work before the wait {befores[0]:.1f} - {befores[-1]:.1f} ms (median "
              f"{st.median(befores):.1f}), over the beat {sum(1 for f in moves if f['ms'] > over)}; every frame over the beat within a "
              f"crossing's window: {sum(1 for f in late if any(-0.2 <= f['t'] - (6.35 + 11.79 * k) <= 0.6 for k in range(10)))}")
    away = [f for f in late if all(not (-0.2 <= f['t'] - (6.35 + 11.79 * k) <= 0.6) for k in range(10))]
    worst = sorted(away, key=lambda f: -f['ms'])[:int(opt('--lines', '20'))]
    print(f"  away from the crossings: {len(away)}; the longest:")
    for f in sorted(worst, key=lambda f: f['t']):
        print(f"    {text(f)}")
