#!/usr/bin/env python3
"""The long frames of a harness run, one line each: when (seconds after a mark), how long, the render sampler's samples
inside it by engine function (inclusive: eip and stack words, each function once a sample), and the [Timed] spike line
of the update step that ended inside it.
Usage: long-frame-split.py <run folder> [--over 30] [--mark ride] [--top 10] [--export re/export/rea-all/functions]"""
import bisect, collections, csv, os, re, sys

run = sys.argv[1]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
over, mark, top = float(opt('--over', '30')), opt('--mark', 'ride'), int(opt('--top', '10'))
export = opt('--export', 're/export/rea-all/functions')
cap = os.path.join(run, 'OBVR-performance-1-5')
marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
zero = marks.get(mark, 0)
spikes = []
for line in open(os.path.join(run, 'OBNG.log'), errors='ignore'):
    m = re.match(r'Timed spike: frame:UpdateStep ([\d.]+) ms ending at tick (\d+) - (.*)', line)
    if m:
        spikes.append((int(m[2]), float(m[1]), m[3].strip()))
spikes.sort()
ticks = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick'])
spans = [(a, b) for a, b in zip(ticks, ticks[1:]) if (b - a) / 1e4 > over]
functions = sorted(int(x[2:-2], 16) for x in os.listdir(export) if x.startswith('0x') and x.endswith('.c'))
def function(address):
    i = bisect.bisect_right(functions, address) - 1
    return f'{functions[i]:08X}' if 0x401000 <= address < 0xA00000 and i >= 0 else None
samples = list(csv.DictReader(open(os.path.join(cap, 'samples.csv'))))
sample_ticks = [int(r['tick']) for r in samples]
for a, b in spans:
    counts, n = collections.Counter(), 0
    for r in samples[bisect.bisect_left(sample_ticks, a):bisect.bisect_left(sample_ticks, b)]:
        n += 1
        seen = set()
        for key in ['eip'] + [f'w{j}' for j in range(8)]:
            g = function(int(r[key], 16)) if r.get(key) else None
            if g and g not in seen:
                seen.add(g)
                counts[g] += 1
    j = bisect.bisect_left(spikes, (a,))
    inside = [s for s in spikes[j:j + 3] if a <= s[0] <= b + 200000]
    spike = f' | step {inside[0][1]:.1f} ms: {inside[0][2][:160]}' if inside else ' | no step spike'
    print(f'{(a - zero) / 1e7:7.1f} s {(b - a) / 1e4:6.1f} ms, {n} samples: ' + ' '.join(f'{g}:{c}' for g, c in counts.most_common(top)) + spike)
