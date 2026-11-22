#!/usr/bin/env python3
"""pass-shares.py for one phase of a hand script: the render sampler's samples inside the scene pass between two
marks of OBVR.log, by module and - inside Oblivion.exe - by function (exclusive, the eip) and by the stack's first
words that lie in a named set of functions (inclusive, as far as the sampler's stack words go; they can be stale).
Usage: pass-phase.py <run folder> <export functions dir> <from mark> <to mark> [--top 30] [--out]"""
import bisect, collections, csv, glob, os, re, sys
run, export, m0, m1 = sys.argv[1:5]
top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 30
want = '0' if '--out' in sys.argv else '1'
cap = next(os.path.join(run, d) for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report'))
marks = {}
for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
    m = re.search(r'HandScript: mark (\S+) \(.*tick (\d+)\)', line)
    if m:
        marks[m.group(1)] = int(m.group(2))
lo, hi = marks[m0], marks[m1]
starts = sorted(int(os.path.basename(f)[2:-2], 16) for f in glob.glob(os.path.join(export, '0x*.c')))
names = {}
for f in glob.glob('re/symbols/*.csv'):
    for row in csv.reader(open(f, errors='replace')):
        if len(row) >= 2:
            for a, b in ((row[0], row[1]), (row[1], row[0])):
                try:
                    names.setdefault(int(a, 16), b)
                except ValueError:
                    pass
pages = {r['page']: r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv'), newline=''))}
events = [e for e in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'), newline='')) if lo <= int(e['start_tick']) < hi]
passes = [(int(e['end_tick']) - int(e['start_tick'])) / 1e4 for e in events if e['event_type'] == 'scene_pass']
frames = sum(e['event_type'] == 'present' for e in events)
mods, funcs, incl, n, total = collections.Counter(), collections.Counter(), collections.Counter(), 0, 0
def func(eip):
    i = bisect.bisect_right(starts, eip) - 1
    return starts[i] if i >= 0 else 0
for s in csv.DictReader(open(os.path.join(cap, 'samples.csv'), newline='')):
    if not lo <= int(s['tick']) < hi:
        continue
    total += 1
    if s['in_pass'] != want:
        continue
    n += 1
    module = pages.get(s['eip'][:4], '?')
    mods[module] += 1
    if module == 'Oblivion.exe':
        funcs[func(int(s['eip'], 16))] += 1
    seen = set()
    for w in [s['eip']] + [s[f'w{i}'] for i in range(8)]:
        if w and pages.get(w[:4]) == 'Oblivion.exe':
            seen.add(func(int(w, 16)))
    for f in seen:
        incl[f] += 1
where = 'outside the pass' if want == '0' else 'inside the pass'
span = (sum(passes) / len(passes)) if want == '1' else ((hi - lo) / 1e4 / max(frames, 1) - sum(passes) / max(len(passes), 1))
print(f'{m0} -> {m1}: {frames} frames, {(hi - lo) / 1e4 / max(frames, 1):.1f} ms a frame, the scene pass {sum(passes) / max(len(passes), 1):.1f} ms; '
      f'{n} of {total} samples {where}')
ms = lambda c: c / n * span
print('by module:')
for k, c in mods.most_common(12):
    print(f'  {c / n * 100:5.1f} %  {ms(c):5.2f} ms  {k}')
print('Oblivion.exe by function (exclusive):')
for k, c in funcs.most_common(top):
    print(f'  {c / n * 100:5.1f} %  {ms(c):5.2f} ms  0x{k:08X}  {names.get(k, "")}')
print('Oblivion.exe by function (on the stack words, inclusive):')
for k, c in incl.most_common(top):
    print(f'  {c / n * 100:5.1f} %  {ms(c):5.2f} ms  0x{k:08X}  {names.get(k, "")}')
