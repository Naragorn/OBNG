#!/usr/bin/env python3
"""An OBVR profiler capture's long frames by module: the render sampler's samples in frames over --over ms
(default 20), counted by the module of the eip and by every module anywhere on the eip and the stack words
(samples-modules.csv maps a 64 KB page to its module). The stack words can be stale.
Usage: long-frame-modules.py <capture folder> [--over 20] [--top 14]"""
import bisect, collections, csv, os, sys
cap = sys.argv[1]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
over, top = float(opt('--over', '20')), int(opt('--top', '14'))
page = {int(r['page'], 16): r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv')))}
def module(address):
    return 'Oblivion.exe' if 0x400000 <= address < 0xC00000 else page.get(address >> 16)
frames = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick'])
spans = [(a, b) for a, b in zip(frames, frames[1:]) if (b - a) / 1e4 > over]
starts = [s for s, _ in spans]
keys = ['eip'] + [f'w{j}' for j in range(8)] + [f'x{j}' for j in range(4)]
leaf, anywhere, every, n, total = collections.Counter(), collections.Counter(), collections.Counter(), 0, 0
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    found = [m for m in (module(int(r[k], 16)) for k in keys if r.get(k)) if m]
    total += 1
    for m in set(found):
        every[m] += 1
    t = int(r['tick'])
    i = bisect.bisect_right(starts, t) - 1
    if i < 0 or t >= spans[i][1]:
        continue
    n += 1
    leaf[module(int(r['eip'], 16)) or '?'] += 1
    for m in set(found):
        anywhere[m] += 1
print(f"{len(spans)} frames over {over:g} ms, {n} of {total} samples in them")
print('modules loaded:', ', '.join(sorted(set(page.values()))))
for title, c, of in (('eip in', leaf, n), ('anywhere on the stack', anywhere, n), ('all frames, anywhere on the stack', every, total)):
    print(title + ': ' + ', '.join(f'{k} {100 * v / max(of, 1):.1f} %' for k, v in c.most_common(top)))
