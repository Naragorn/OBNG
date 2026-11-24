#!/usr/bin/env python3
"""Two time windows of one capture side by side: the scene pass's samples by module and Oblivion.exe function,
in ms a frame (share of the window's in-pass samples times the window's mean scene pass).
Usage: window-shares.py <capture> <export functions dir> a0 a1 b0 b1 [--top 40] (seconds from the capture's first frame)"""
import bisect, collections, csv, glob, json, os, re, sys
cap, export = sys.argv[1], sys.argv[2]
a0, a1, b0, b1 = map(float, sys.argv[3:7])
top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 40
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fn = sorted(int(x[:-2], 16) for x in os.listdir(export) if x.endswith('.c'))
names = {}
for path in glob.glob(os.path.join(root, 're/symbols/*.csv')):
    for row in csv.reader(open(path, errors='replace')):
        if len(row) > 1 and re.fullmatch(r'0x[0-9A-Fa-f]{8}', row[0]):
            names.setdefault(int(row[0], 16), row[1])
page = {int(r['page'], 16): r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv')))}
fr = sorted((int(r['start_tick']), int(r['present_id']), r['delivered_mode']) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick'])
t0 = fr[0][0]
def win(t):
    s = (t - t0) / 1e7
    return 0 if a0 <= s < a1 else 1 if b0 <= s < b1 else None
passms = [[], []]
pid_win = {}
for t, pid, mode in fr:
    w = win(t)
    if w is not None and mode.startswith('world'):
        pid_win[pid] = w
sp = collections.defaultdict(float)
for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'))):
    if r['event_type'] == 'scene_pass':
        sp[int(r['present_id'])] += (int(r['end_tick']) - int(r['start_tick'])) / 1e4
for pid, w in pid_win.items():
    passms[w].append(sp.get(pid, 0))
mod = [collections.Counter(), collections.Counter()]
fun = [collections.Counter(), collections.Counter()]
n = [0, 0]
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    if r['in_pass'] != '1':
        continue
    w = win(int(r['tick']))
    if w is None:
        continue
    e = int(r['eip'], 16)
    n[w] += 1
    if 0x400000 <= e < 0xC00000:
        mod[w]['Oblivion.exe'] += 1
        f = fn[bisect.bisect_right(fn, e) - 1]
        fun[w][f] += 1
    else:
        mod[w][page.get(e >> 16, '?')] += 1
mean = [sum(p) / len(p) for p in passms]
print(f'window A {a0}-{a1} s: {len(passms[0])} world frames, scene pass {mean[0]:.2f} ms, {n[0]} samples')
print(f'window B {b0}-{b1} s: {len(passms[1])} world frames, scene pass {mean[1]:.2f} ms, {n[1]} samples')
def ms(c, w, k):
    return c[w][k] / n[w] * mean[w]
print('module                       A ms    B ms   B-A')
for k in sorted(set(mod[0]) | set(mod[1]), key=lambda k: -ms(mod, 1, k))[:14]:
    print(f'{k:26s} {ms(mod,0,k):6.2f} {ms(mod,1,k):6.2f} {ms(mod,1,k)-ms(mod,0,k):+6.2f}')
print('Oblivion.exe function        A ms    B ms   B-A')
for k in sorted(set(fun[0]) | set(fun[1]), key=lambda k: -(ms(fun, 1, k) - ms(fun, 0, k)))[:top]:
    print(f'0x{k:08X} {names.get(k,"")[:44]:44s} {ms(fun,0,k):6.3f} {ms(fun,1,k):6.3f} {ms(fun,1,k)-ms(fun,0,k):+6.3f}')
