#!/usr/bin/env python3
"""One block per OBVR profiler capture, for the backlog's tables of places: the
frames in time, the frames SteamVR showed again, the frame's own work by
frame length class, the scene pass and the compositor's GPU time before
submit - over the world's frames only. A frame delivered behind a menu
(delivered_mode live_menu) is a paused world drawn: it is left out and its
share printed; a capture that is mostly menu frames measured no play (the
rides and the Gold Road of 2026-11-16, behind two mods' boxes).
Usage: capture-row.py [--period 13.33] <capture folder>..."""
import collections, csv, os, statistics as st, sys

args = sys.argv[1:]
period = 13.33
if '--period' in args:
    i = args.index('--period')
    period = float(args[i + 1])
    del args[i:i + 2]


def quantile(values, p):
    values = sorted(values)
    return values[min(len(values) - 1, int(p * len(values)))] if values else float('nan')


def mean(values):
    return sum(values) / len(values) if values else float('nan')


for cap in args:
    rows = [r for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick']]
    frames = sorted(((int(r['present_id']), int(r['start_tick'])) for r in rows), key=lambda f: f[1])
    world = {int(r['present_id']) for r in rows if r['delivered_mode'].startswith('world')}
    menu = sum(1 for r in rows if r['delivered_mode'] == 'live_menu')
    length = {pid: (e - s) / 10000.0 for (pid, s), (_, e) in zip(frames, frames[1:]) if pid in world}
    if not length:
        print(f"{cap.rstrip('/')}: no world frames - {100 * menu / max(len(rows), 1):.1f} % of {len(rows)} frames were drawn behind a menu; void")
        continue
    events = collections.defaultdict(lambda: collections.defaultdict(float))
    for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'))):
        events[int(r['present_id'])][r['event_type']] += (int(r['end_tick']) - int(r['start_tick'])) / 10000.0
    classes = [('in time', 0, period + 0.5), ('near', period + 0.5, period + 1.9), ('slow', period + 1.9, 60), ('long', 60, 1e12)]
    comp = [r for r in csv.DictReader(open(os.path.join(cap, 'compositor.csv'))) if int(r['present_id']) in world]
    pre = [float(r['pre_submit_gpu_ms']) for r in comp]
    again = sum(1 for r in comp if int(r['num_presents']) > 1)
    print(f"{cap.rstrip('/')}: {len(length)} world frames, median {st.median(length.values()):.2f} ms; behind a menu {100 * menu / len(rows):.1f} % of the capture's {len(rows)} (left out)")
    for name, lo, hi in classes:
        ids = [pid for pid, ms in length.items() if lo <= ms < hi]
        if not ids:
            continue
        # busy: the frame less the wait for poses and the running start's hold, as capture-frames.py has it
        busy = mean([length[p] - events[p]['poses_wait'] - events[p]['pacing_vsync'] for p in ids])
        less_wait = mean([length[p] - events[p]['poses_wait'] for p in ids])
        scene = mean([events[p]['scene_pass'] for p in ids])
        print(f"  {name:8s} {len(ids):5d} frames ({100 * len(ids) / len(length):5.1f} %), mean {mean([length[p] for p in ids]):6.1f} ms,"
              f" busy {busy:5.1f}, less the poses wait {less_wait:5.1f}, scene pass {scene:4.1f}, poses wait {mean([events[p]['poses_wait'] for p in ids]):4.1f}")
    busy_all = sorted(length[p] - events[p]['poses_wait'] - events[p]['pacing_vsync'] for p in length if length[p] < 60)
    print(f"  own work (the frame less the poses wait and the running start), frames under 60 ms: median {quantile(busy_all, 0.5):.1f}, p90 {quantile(busy_all, 0.9):.1f},"
          f" p95 {quantile(busy_all, 0.95):.1f}, p99 {quantile(busy_all, 0.99):.1f} ms")
    print(f"  SteamVR: shown again {100 * again / len(comp):.1f} % of {len(comp)}; GPU before submit median {st.median(pre):.1f}, p95 {quantile(pre, 0.95):.1f} ms;"
          f" scene pass over all frames {mean([events[p]['scene_pass'] for p in length]):.1f} ms")
