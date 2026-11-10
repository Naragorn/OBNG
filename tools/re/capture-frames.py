#!/usr/bin/env python3
"""An OBVR profiler capture by frame length: how many frames and how much of
the time fall into each length class, what the timed spans are in each, and
for the classes named with --samples where the render sampler's samples of
those frames fall (Oblivion.exe functions by the export's function starts,
other code by module).
Usage: capture-frames.py <capture folder> <export functions dir> [--period 11.11] [--samples long|near|13-60|all] [--top 25] [--pass in|out]"""
import bisect, collections, csv, glob, os, statistics as st, sys
cap, export = sys.argv[1], sys.argv[2]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
period = float(opt('--period', '11.11'))
which = opt('--samples', 'long')
top = int(opt('--top', '25'))
only_pass = opt('--pass', '')
frames = [(int(r['present_id']), int(r['start_tick'])) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick']]
frames.sort(key=lambda f: f[1])
length = {}
span = {}
for (pid, s), (_, e) in zip(frames, frames[1:]):
    length[pid] = (e - s) / 10000.0
    span[pid] = (s, e)
events = collections.defaultdict(lambda: collections.defaultdict(float))
for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'))):
    events[int(r['present_id'])][r['event_type']] += (int(r['end_tick']) - int(r['start_tick'])) / 10000.0
classes = [('in time', 0, period + 0.5), ('near', period + 0.5, period + 1.9), ('13-60', period + 1.9, 60), ('long', 60, 1e12)]
total = sum(length.values())
keys = ['scene_pass', 'poses_wait', 'pacing_vsync', 'present', 'interop_held', 'submit_hold']
print(f"{os.path.basename(cap.rstrip('/'))}: {len(length)} frames, {total / 1000:.1f} s, median {st.median(length.values()):.2f} ms")
member = {}
for name, lo, hi in classes:
    ids = [p for p, v in length.items() if lo < v <= hi]
    for p in ids:
        member[p] = name
    if not ids:
        continue
    t = sum(length[p] for p in ids)
    waits = st.mean(events[p].get('poses_wait', 0) + events[p].get('pacing_vsync', 0) for p in ids)
    row = ' '.join(f"{k} {st.mean(events[p].get(k, 0) for p in ids):.1f}" for k in keys)
    print(f"  {name:8s} {len(ids):6d} frames ({100 * len(ids) / len(length):5.1f} %), {100 * t / total:5.1f} % of the time, mean {t / len(ids):7.1f} ms, busy {t / len(ids) - waits:7.1f} | {row}")
sample_file = os.path.join(cap, 'samples.csv')
if which == 'none' or not os.path.exists(sample_file):
    sys.exit(0)
starts = sorted(int(os.path.basename(f)[2:-2], 16) for f in glob.glob(os.path.join(export, '0x*.c')))
modules = sorted((int(r['page'], 16), r['module']) for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv'))))
def where(eip):
    if 0x00401000 <= eip < 0x00A28000:
        return f"0x{starts[bisect.bisect_right(starts, eip) - 1]:08X}"
    page = eip >> 16
    i = bisect.bisect_right(modules, (page, '\xff')) - 1
    return modules[i][1] if i >= 0 and page - modules[i][0] < 0x400 else 'other'
order = sorted(span.items(), key=lambda kv: kv[1][0])
begin = [v[0] for _, v in order]
here = collections.Counter()
callers = collections.defaultdict(collections.Counter)
inpass = collections.Counter()
n = 0
for r in csv.DictReader(open(sample_file)):
    t = int(r['tick'])
    i = bisect.bisect_right(begin, t) - 1
    if i < 0 or t >= order[i][1][1]:
        continue
    if which != 'all' and member.get(order[i][0]) != which:
        continue
    if only_pass and (r['in_pass'] == '1') != (only_pass == 'in'):
        continue
    n += 1
    f = where(int(r['eip'], 16))
    here[f] += 1
    inpass[r['in_pass']] += 1
    seen = set()
    for k in ('w0', 'w1', 'w2', 'w3', 'w4', 'w5', 'w6', 'w7'):
        if r[k]:
            c = where(int(r[k], 16))
            if c.startswith('0x') and c != f and c not in seen:
                seen.add(c)
                callers[f][c] += 1
print(f"  samples in the '{which}' frames{' (' + only_pass + ' the scene pass)' if only_pass else ''}: {n} (in the scene pass: {inpass.get('1', 0)})")
engine = sum(c for f, c in here.items() if f.startswith('0x'))
print(f"    Oblivion.exe in all: {100.0 * engine / max(n, 1):.1f} %")
for f, c in here.most_common(top):
    print(f"    {100.0 * c / n:5.1f} %  {f:14s} <- " + ', '.join(f"{a} {b}" for a, b in callers[f].most_common(4)))
