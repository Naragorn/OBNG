#!/usr/bin/env python3
"""The render sampler's samples inside one function of a DLL, instruction by instruction: the function's
disassembly (llvm-objdump) with each instruction's samples beside it - where inside the function the time goes.
Also lists the samples of the module that fall under no symbol of the link map, by address.
Usage: dll-function-samples.py <capture folder> <module name> <link map> <dll file> <name part> [--pass in|out] [--min 3]
  <name part>  a part of the function's decorated or plain name ("EyeSwitch" finds ...EyeSwitch...Switch); the
               function with the most samples among the matches is shown; "-" shows only the samples without a symbol
  --min        instructions with fewer samples are left out of the listing (their neighbours stay as context)"""
import bisect, collections, csv, os, re, subprocess, sys

cap, module, mapfile, dll, part = sys.argv[1:6]


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


only, least = opt('--pass', ''), int(opt('--min', '3'))
preferred, symbols = 0, []
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
        base, size = int(r['base'], 16), int(r.get('size', '0') or '0', 16) or 0x200000
if base is None:
    sys.exit(f'{module} is not in samples-modules.csv')
by_function, by_rva, total = collections.Counter(), collections.Counter(), 0
for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))):
    if only and (r['in_pass'] == '1') != (only == 'in'):
        continue
    total += 1
    rva = int(r['eip'], 16) - base
    if 0 <= rva < size:
        i = bisect.bisect_right(rvas, rva) - 1
        by_function[i] += 1
        by_rva[rva] += 1
inside = sum(by_function.values())
print(f'{inside} of {total} samples in {module}')
nameless = [(rva, n) for rva, n in sorted(by_rva.items()) if rvas and (rva < rvas[0] or symbols[bisect.bisect_right(rvas, rva) - 1][1] == '')]
if part == '-':
    for rva, n in nameless:
        print(f'  no symbol: {preferred + rva:08X}  {n}')
    sys.exit(0)
matches = [i for i in by_function if i >= 0 and part in symbols[i][1]]
if not matches:
    pretty = subprocess.run(['llvm-undname'] + [symbols[i][1] for i in by_function if i >= 0], capture_output=True, text=True).stdout
    names = dict(re.findall(r'^(\S+)\n(.*)$', pretty, re.M))
    matches = [i for i in by_function if i >= 0 and part in names.get(symbols[i][1], '')]
if not matches:
    sys.exit(f'no sampled function with "{part}" in its name')
i = max(matches, key=lambda k: by_function[k])
start = symbols[i][0]
end = symbols[i + 1][0] if i + 1 < len(symbols) else start + 0x4000
name = subprocess.run(['llvm-undname', symbols[i][1]], capture_output=True, text=True).stdout.strip().split('\n')[-1]
print(f'{by_function[i]} samples ({100.0 * by_function[i] / max(inside, 1):.1f} % of the module) in {name[:160]}')
print(f'  {preferred + start:08X} - {preferred + end:08X}, {end - start} bytes')
dis = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '-M', 'intel', f'--start-address={preferred + start:#x}',
                      f'--stop-address={preferred + end:#x}', dll], capture_output=True, text=True).stdout
lines = []
for line in dis.split('\n'):
    m = re.match(r'\s*([0-9a-f]+):\s+(.*)$', line)
    if m:
        lines.append((int(m.group(1), 16) - preferred, m.group(2).strip()))
# a sample's address is the instruction about to run or running: count it on the instruction that holds it
counts = [0] * len(lines)
addresses = [a for a, _ in lines]
for rva, n in by_rva.items():
    if start <= rva < end:
        k = bisect.bisect_right(addresses, rva) - 1
        if k >= 0:
            counts[k] += n
shown = set()
for k, n in enumerate(counts):
    if n >= least:
        shown.update(range(max(0, k - 2), min(len(lines), k + 2)))
last = -1
for k in sorted(shown):
    if last >= 0 and k != last + 1:
        print('        ...')
    print(f'  {counts[k]:5d}  {preferred + lines[k][0]:08X}  {lines[k][1]}')
    last = k
