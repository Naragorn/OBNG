#!/usr/bin/env python3
"""An OBVR profiler capture's scene pass by where the render thread's eip was: the samples taken inside the pass
(in_pass = 1) in world frames, by module and - inside Oblivion.exe - by function (the export's function starts),
named from re/symbols/*.csv and re/functions/*.md where a name exists; in ms a frame from the scene_pass events'
mean. Exclusive only: the sampler's stack words can be stale (knowledge/actor-frame-map.md).
Usage: pass-shares.py <capture folder> <export functions dir> [--top 40] [--out] (--out: the samples outside the pass)"""
import bisect, collections, csv, glob, json, os, re, sys
cap, export = sys.argv[1], sys.argv[2]
top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 40
want = '0' if '--out' in sys.argv else '1'
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
fn = sorted(int(x[:-2], 16) for x in os.listdir(export) if x.endswith('.c'))
names = {}
for path in glob.glob(os.path.join(root, 're/symbols/*.csv')):
    for row in csv.reader(open(path, errors='replace')):
        if len(row) > 1 and re.fullmatch(r'0x[0-9A-Fa-f]{8}', row[0]):
            names.setdefault(int(row[0], 16), row[1])
for path in glob.glob(os.path.join(root, 're/functions/0x*.md')):
    m = re.search(r'(?im)^\W*proposed name\W*[:|]\W*`?([A-Za-z_][\w:~<> ]+)', open(path, errors='replace').read()[:3000])
    if m:
        names[int(os.path.basename(path)[:-3], 16)] = m.group(1).strip()
page = {int(r['page'], 16): r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv')))}
freq = json.load(open(os.path.join(cap, 'manifest.json')))['qpc_frequency'] / 1000.0
frames = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick'] and r['delivered_mode'].startswith('world'))
spans = list(zip(frames, frames[1:]))
starts = [a for a, _ in spans]
pass_ms = collections.defaultdict(float)
for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'))):
    if r['event_type'] == 'scene_pass':
        pass_ms[int(r['present_id'])] += (int(r['end_tick']) - int(r['start_tick'])) / freq
mean_pass = sum(pass_ms.values()) / max(1, len(pass_ms))
mean_frame = sum(b - a for a, b in spans) / freq / max(1, len(spans))
mod = collections.Counter(); func = collections.Counter(); n = 0; total = 0
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    t = int(r['tick']); i = bisect.bisect_right(starts, t) - 1
    if i < 0 or t >= spans[i][1]:
        continue
    total += 1
    if r['in_pass'] != want:
        continue
    n += 1
    e = int(r['eip'], 16)
    if 0x401000 <= e < 0xA20000:
        mod['Oblivion.exe'] += 1
        func[fn[bisect.bisect_right(fn, e) - 1]] += 1
    else:
        mod[page.get(e >> 16, '?')] += 1
part = mean_pass if want == '1' else mean_frame - mean_pass
print('%d world frames, %.1f ms a frame, the scene pass %.1f ms; %d of %d samples %s the pass (%.1f ms a frame by the events)' % (len(spans), mean_frame, mean_pass, n, total, 'inside' if want == '1' else 'outside', part))
print('by module:')
for k, c in mod.most_common(12):
    print('  %5.1f %%  %5.2f ms  %s' % (100 * c / n, part * c / n, k))
print('Oblivion.exe by function:')
for a, c in func.most_common(top):
    print('  %5.1f %%  %5.2f ms  0x%08X  %s' % (100 * c / n, part * c / n, a, names.get(a, '')))
