#!/usr/bin/env python3
"""An OBVR profiler capture's long frames by call chain: every render-sampler sample that falls into a frame
longer than --over ms (default 20), its eip and stack words resolved to engine functions (the export's function
starts), the most common chains first. The stack words can be stale (knowledge/render-thread-outside-the-pass.md).
Usage: long-frame-chains.py <capture folder> <export functions dir> [--over 20] [--top 25] [--depth 7]"""
import bisect, collections, csv, os, sys
cap, export = sys.argv[1], sys.argv[2]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
over, top, depth = float(opt('--over', '20')), int(opt('--top', '25')), int(opt('--depth', '7'))
frames = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick'])
spans = [(a, b) for a, b in zip(frames, frames[1:]) if (b - a) / 1e4 > over]
starts = [s for s, _ in spans]
functions = sorted(int(x[:-2], 16) for x in os.listdir(export) if x.endswith('.c'))
def function(address):
    i = bisect.bisect_right(functions, address) - 1
    return f'{functions[i]:08X}' if 0x401000 <= address < 0xA00000 and i >= 0 else None
chains, n = collections.Counter(), 0
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    t = int(r['tick'])
    i = bisect.bisect_right(starts, t) - 1
    if i < 0 or t >= spans[i][1]:
        continue
    n += 1
    chain = []
    for key in ['eip'] + [f'w{j}' for j in range(8)] + [f'x{j}' for j in range(4)]:
        g = function(int(r[key], 16)) if r.get(key) else None
        if g and (not chain or chain[-1] != g):
            chain.append(g)
    chains[' < '.join(chain[:depth]) or '(no engine function)'] += 1
print(f"{len(spans)} frames over {over:g} ms, {sum((e - s) / 1e4 for s, e in spans):.0f} ms in all, {n} samples")
for k, v in chains.most_common(top):
    print(f'{v:5d} {100 * v / max(n, 1):5.1f} %  {k}')
