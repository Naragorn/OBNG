#!/usr/bin/env python3
"""The render sampler's samples inside the [Timed] spikes of a run: every "Timed spike: ... N ms ending at tick T"
line of the run's OBNG.log is a span of the performance counter; the capture's samples in the spans are resolved to
engine functions (the export's function starts) and counted by call chain and by the function of the eip. With
--part NAME only the spikes whose line gives NAME more than half of the spike's time are taken. The stack words can
be stale (knowledge/render-thread-outside-the-pass.md).
Usage: spike-samples.py <run folder> <export functions dir> [--part NAME] [--top 20] [--depth 6]"""
import bisect, collections, csv, glob, json, os, re, sys
run, export = sys.argv[1], sys.argv[2]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
part, top, depth = opt('--part', ''), int(opt('--top', '20')), int(opt('--depth', '6'))
cap = next(d for d in sorted(glob.glob(os.path.join(run, 'OBVR-performance-*'))) if os.path.isfile(os.path.join(d, 'samples.csv')))
frequency = json.load(open(os.path.join(cap, 'manifest.json')))['qpc_frequency']
spans, who = [], collections.Counter()
for line in open(os.path.join(run, 'OBNG.log'), errors='replace'):
    m = re.match(r'Timed spike: (\S+) ([\d.]+) ms ending at tick (\d+) - (.*)', line)
    if not m:
        continue
    ms, end, rest = float(m[2]), int(m[3]), m[4]
    if part:
        p = re.search(re.escape(part) + r'\S* ([\d.]+) ms \(\d+ calls?, longest [\d.]+(?: by (\w+) of (\w+))?', rest)
        if not p or float(p[1]) < 0.5 * ms:
            continue
    for by in re.findall(r'by (\w+ of \w+)', rest)[:1]:
        who[by] += 1
    spans.append((end - int(ms * frequency / 1000), end))
spans.sort()
starts = [s for s, _ in spans]
functions = sorted(int(x[:-2], 16) for x in os.listdir(export) if x.endswith('.c'))
pages = {int(r['page'], 16): r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv')))}
def name(address):
    if 0x401000 <= address < 0xA00000:
        i = bisect.bisect_right(functions, address) - 1
        return f'{functions[i]:08X}' if i >= 0 else None
    return None
keys = ['eip'] + [f'w{j}' for j in range(8)] + [f'x{j}' for j in range(4)]
chains, leaves, n = collections.Counter(), collections.Counter(), 0
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    t = int(r['tick'])
    i = bisect.bisect_right(starts, t) - 1
    if i < 0 or t > spans[i][1]:
        continue
    n += 1
    eip = int(r['eip'], 16)
    leaves[name(eip) or pages.get(eip >> 16, '?')] += 1
    chain = []
    for key in keys:
        g = name(int(r[key], 16)) if r.get(key) else None
        if g and (not chain or chain[-1] != g):
            chain.append(g)
    chains[' < '.join(chain[:depth]) or '(no engine function)'] += 1
print(f"{len(spans)} spikes{' with ' + part + ' over half' if part else ''}, {sum(e - s for s, e in spans) * 1000 / frequency:.0f} ms in all, {n} samples in them")
if who:
    print('the longest call by: ' + ', '.join(f'{k} x{v}' for k, v in who.most_common(8)))
print('eip in: ' + ', '.join(f'{k} {100 * v / max(n, 1):.1f} %' for k, v in leaves.most_common(top)))
for k, v in chains.most_common(top):
    print(f'{v:5d} {100 * v / max(n, 1):5.1f} %  {k}')
