#!/usr/bin/env python3
"""Where the render thread's samples fall inside the actors' loop: OBVR's
sampler capture (samples.csv) cut by the loop's clock readings OBNG logs with
[Route] ActorBatch=1 and ActorBatchTicks=1 ("Probe: actor loop ticks"). With
the batch on, a loop's first span is the actors' updates without their
skeleton updates and the second span the skeleton updates. The eips are
binned to Oblivion.exe functions (the export's function starts) or to the
module; w0 (the sampler's first return-address-like stack word) is listed as
the likeliest caller for the top functions.
Usage: sampler-actor-loop.py <run folder> <export functions dir> [min deferred] [max deferred]"""
import bisect, collections, csv, glob, os, re, sys
run, export = sys.argv[1], sys.argv[2]
lo_def = int(sys.argv[3]) if len(sys.argv) > 3 else 0
hi_def = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
loops = []
for line in open(os.path.join(run, 'OBNG.log'), errors='replace'):
    if 'Probe: actor loop ticks' in line:
        for m in re.finditer(r' (\d+)\+(\d+)\+(\d+):(\d+)', line):
            s, a, b, d = (int(x) for x in m.groups())
            if lo_def <= d <= hi_def:
                loops.append((s, s + a, s + a + b))
loops.sort()
starts_l = [l[0] for l in loops]
cap = sorted(glob.glob(os.path.join(run, 'OBVR-performance-1-*', 'samples.csv')))[0]
capdir = os.path.dirname(cap)
starts = sorted(int(os.path.basename(f)[2:-2], 16) for f in glob.glob(os.path.join(export, '0x*.c')))
modules = sorted((int(r['page'], 16), r['module']) for r in csv.DictReader(open(os.path.join(capdir, 'samples-modules.csv'))))
def where(eip):
    if 0x00401000 <= eip < 0x00A28000:
        return f"0x{starts[bisect.bisect_right(starts, eip) - 1]:08X}"
    page = eip >> 16
    i = bisect.bisect_right(modules, (page, '\xff')) - 1
    if i >= 0 and page - modules[i][0] < 0x400:
        return modules[i][1]
    return 'other'
spans = [collections.Counter(), collections.Counter()]
callers = [collections.defaultdict(collections.Counter), collections.defaultdict(collections.Counter)]
for r in csv.DictReader(open(cap)):
    t = int(r['tick'])
    i = bisect.bisect_right(starts_l, t) - 1
    if i < 0:
        continue
    s, m, e = loops[i]
    if t >= e:
        continue
    k = 0 if t < m else 1
    f = where(int(r['eip'], 16))
    spans[k][f] += 1
    # the first stack word that is a return address into the EXE and not into f itself
    for w in (r['w0'], r['w1'], r['w2'], r['w3']):
        if w:
            c = where(int(w, 16))
            if c != f and c.startswith('0x'):
                callers[k][f][c] += 1
                break
n = len(loops)
ticks = [sum(m - s for s, m, e in loops), sum(e - m for s, m, e in loops)]
print(f"{n} loops with {lo_def}..{hi_def} bodies deferred")
for k, name in enumerate(("the loop (actor updates without the skeleton updates)", "the run after it (skeleton updates)")):
    total = sum(spans[k].values())
    ms = ticks[k] / n / 10000.0 if n else 0.0
    print(f"\n== {name}: {ms:.3f} ms a loop, {total} samples ({total / n if n else 0:.2f} a loop)")
    for f, c in spans[k].most_common(int(os.environ.get("TOP", "40"))):
        top = ', '.join(f"{a} {b}" for a, b in callers[k][f].most_common(3))
        print(f"  {100.0 * c / total:5.1f} %  {ms * c / total:6.3f} ms  {f}   <- {top}")
