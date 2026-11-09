#!/usr/bin/env python3
"""Where the render thread's samples fall, per phase of a hand script: OBVR's
render sampler (samples.csv of a profiler capture) cut by the marks in
OBVR.log, the eips binned to Oblivion.exe functions (the export's function
starts) or to the module (samples-modules.csv). Per phase: samples per frame
in and out of the scene pass, and the functions that gained most against the
first phase.
Usage: sampler-phases.py <run folder> <export functions dir> mark1 mark2 [...]"""
import bisect, collections, csv, glob, os, re, sys
run, export = sys.argv[1], sys.argv[2]
marks_wanted = sys.argv[3:]
marks = {}
for line in open(os.path.join(run, 'OBVR.log'), errors='replace'):
    m = re.search(r'HandScript: mark (\S+) \(.*tick (\d+)\)', line)
    if m:
        marks[m.group(1)] = int(m.group(2))
cap = sorted(glob.glob(os.path.join(run, 'OBVR-performance-1-*', 'samples.csv')))[0]
capdir = os.path.dirname(cap)
starts = sorted(int(os.path.basename(f)[2:-2], 16) for f in glob.glob(os.path.join(export, '0x*.c')))
modules = []
for r in csv.DictReader(open(os.path.join(capdir, 'samples-modules.csv'))):
    modules.append((int(r['page'], 16), r['module']))
modules.sort()
def where(eip):
    if 0x00401000 <= eip < 0x00A28000:
        i = bisect.bisect_right(starts, eip) - 1
        return f"0x{starts[i]:08X}"
    page = eip >> 16
    i = bisect.bisect_right(modules, (page, '\xff')) - 1
    if i >= 0 and page - modules[i][0] < 0x400:
        return modules[i][1]
    return 'other'
frames = [int(r['start_tick']) for r in csv.DictReader(open(os.path.join(capdir, 'frames.csv'))) if r['start_tick']]
phases = []
for a, b in zip(marks_wanted, marks_wanted[1:]):
    lo, hi = marks[a], marks[b]
    phases.append((a, lo, hi, sum(1 for t in frames if lo <= t < hi), collections.Counter(), collections.Counter()))
for r in csv.DictReader(open(cap)):
    t = int(r['tick'])
    for name, lo, hi, n, inpass, outpass in phases:
        if lo <= t < hi:
            (inpass if r['in_pass'] == '1' else outpass)[where(int(r['eip'], 16))] += 1
            break
base = phases[0]
for name, lo, hi, n, inpass, outpass in phases:
    print(f"== {name}: {n} frames, in-pass {sum(inpass.values())/max(n,1):.2f} samples a frame, out-of-pass {sum(outpass.values())/max(n,1):.2f}")
last = phases[-1]
for label, idx in (('in the pass', 4), ('outside the pass', 5)):
    gain = {}
    for k in set(last[idx]) | set(base[idx]):
        gain[k] = last[idx][k] / max(last[3], 1) - base[idx][k] / max(base[3], 1)
    print(f"-- gained most {label}, samples a frame ({base[0]} -> {last[0]}):")
    for k, v in sorted(gain.items(), key=lambda kv: -kv[1])[:22]:
        print(f"   {k:14s} {base[idx][k]/max(base[3],1):6.3f} -> {last[idx][k]/max(last[3],1):6.3f}  (+{v:.3f})")
