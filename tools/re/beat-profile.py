#!/usr/bin/env python3
"""Where the main thread's time goes between two marks, split as tools/re/ride-frame-split.py splits a beat: the
render (the pose wait's end to the present's end), the update (from there to the next pose wait's start) and the
wait itself. OBVR's render sampler (samples.csv, about one sample a millisecond on the render thread) binned to
Oblivion.exe's functions (the export's function starts) or to the module (samples-modules.csv), per part: ms a
beat (the part's samples a beat times the sampler's interval) and the functions with the most.

Usage: beat-profile.py <run folder> <export functions dir> --from MARK --to MARK [--top 25]"""
import bisect, collections, csv, glob, os, re, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


run, export = sys.argv[1], sys.argv[2]
FROM, TO, TOP = opt('--from', 'still'), opt('--to', 'done'), int(opt('--top', '25'))
cap = os.path.join(run, 'OBVR-performance-1-5')
marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
events = collections.defaultdict(list)
for row in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'))):
    events[row['event_type']].append((int(row['start_tick']), int(row['end_tick'])))
waits, presents = sorted(events['poses_wait']), sorted(events['present'])
present_starts = [s for s, _ in presents]
spans = []  # (start, end, part)
for (w0s, w0e), (w1s, _) in zip(waits, waits[1:]):
    if not marks[FROM] <= w0e <= marks[TO]:
        continue
    i = bisect.bisect_left(present_starts, w0e)
    if i >= len(presents) or presents[i][0] >= w1s:
        continue
    spans += [(w0s, w0e, 'wait'), (w0e, presents[i][1], 'render'), (presents[i][1], w1s, 'update')]
spans.sort()
starts = sorted(int(os.path.basename(p)[2:-2], 16) for p in glob.glob(os.path.join(export, '0x*.c')))
pages = {int(r['page'], 16): r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv')))}


def name(eip):
    if 0x00400000 <= eip < 0x00A30000:
        i = bisect.bisect_right(starts, eip) - 1
        return f"0x{starts[i]:08X}" if i >= 0 else f"0x{eip:08X}"
    return pages.get(eip >> 16, f"page {eip >> 16:04X}")


ticks = []
counts = {p: collections.Counter() for p in ('wait', 'render', 'update')}
span_starts = [s[0] for s in spans]
samples = sorted((int(r['tick']), int(r['eip'], 16)) for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))))
inside = [t for t, _ in samples if marks[FROM] <= t <= marks[TO]]
interval = (inside[-1] - inside[0]) / max(1, len(inside) - 1) / 1e4  # ms
beats = len(spans) // 3
for t, eip in samples:
    j = bisect.bisect_right(span_starts, t) - 1
    if j < 0 or t > spans[j][1]:
        continue
    counts[spans[j][2]][name(eip)] += 1
print(f"{os.path.basename(os.path.normpath(run))}, {FROM} - {TO}: {beats} beats, a sample every {interval:.2f} ms")
for part in ('render', 'update', 'wait'):
    total = sum(counts[part].values())
    print(f"== the {part}: {total * interval / beats:.2f} ms a beat")
    if part == 'wait':
        continue
    for k, v in counts[part].most_common(TOP):
        print(f"   {k:28s} {v * interval / beats:6.3f} ms a beat  ({v * 100.0 / total:4.1f} %)")
