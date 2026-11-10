#!/usr/bin/env python3
"""The render sampler's samples inside one DLL by that DLL's own functions,
from its link map (the plugin build writes build-plugin/OBNG.map).
Usage: dll-samples.py <capture folder> <module name> <link map> [--pass in|out] [--top 40]"""
import bisect, collections, csv, os, re, subprocess, sys
cap, module, mapfile = sys.argv[1:4]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
only = opt('--pass', '')
top = int(opt('--top', '40'))
preferred = 0
symbols = []
for line in open(mapfile, errors='replace'):
    m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
    if m:
        preferred = int(m.group(1), 16)
    m = re.match(r'\s*0001:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s', line)
    if m:
        symbols.append((int(m.group(2), 16) - preferred, m.group(1)))
symbols.sort()
rvas = [s[0] for s in symbols]
base = None
for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv'))):
    if r['module'].lower() == module.lower():
        base = int(r['base'], 16)
counts = collections.Counter()
total = inside = 0
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    if only and (r['in_pass'] == '1') != (only == 'in'):
        continue
    total += 1
    eip = int(r['eip'], 16)
    page = eip - base
    if 0 <= page < 0x200000:
        i = bisect.bisect_right(rvas, page) - 1
        if i >= 0:
            inside += 1
            counts[symbols[i][1]] += 1
names = [n for n, _ in counts.most_common(top)]
try:
    if not names:
        raise OSError
    demangled = subprocess.run(['llvm-undname'] + names, capture_output=True, text=True).stdout
    pretty = dict(re.findall(r'^(\S+)\n(.*)$', demangled, re.M))
except OSError:
    pretty = {}
print(f"{inside} of {total} samples in {module} ({100.0 * inside / max(total, 1):.1f} %)")
for n, c in counts.most_common(top):
    print(f"  {100.0 * c / max(total, 1):5.2f} % of all, {100.0 * c / max(inside, 1):5.1f} % of the module  {pretty.get(n, n)[:150]}")
