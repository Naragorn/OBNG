#!/usr/bin/env python3
"""What the render thread did between a long frame's present and its next pose wait (the update step's own work), by the
sampler's eip only (its stack words can be stale): engine functions by the export's starts, other code by module (the
capture's samples-modules.csv), Wine's ntdll by export name when --ntdll gives the DLL. Over the frames longer than
--over ms between two marks.
Usage: frame-gap-samples.py <run folder> [--over 30] [--from ride] [--to ride-120] [--ntdll path] [--top 8]
       [--capture OBVR-performance-1-5]"""
import bisect, collections, csv, os, re, struct, sys

run = sys.argv[1]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
over, first, last, top = float(opt('--over', '30')), opt('--from', 'ride'), opt('--to', 'ride-120'), int(opt('--top', '8'))
cap = os.path.join(run, opt('--capture', 'OBVR-performance-1-5'))
export = 're/export/rea-all/functions'
starts = sorted(int(x[2:-2], 16) for x in os.listdir(export) if x.startswith('0x') and x.endswith('.c'))
modules = list(csv.DictReader(open(os.path.join(cap, 'samples-modules.csv'))))
pages = {r['page']: (r['module'], int(r['base'], 16)) for r in modules}

def ntdll_names(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count, size = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    sections = []
    for i in range(count):
        at = pe + 24 + size + i * 40
        vsize, va = struct.unpack_from('<II', data, at + 8)
        raw = struct.unpack_from('<I', data, at + 20)[0]
        sections.append((va, max(vsize, 1), raw))
    def offset(rva):
        for va, vsize, raw in sections:
            if va <= rva < va + vsize:
                return rva - va + raw
        raise ValueError(hex(rva))
    table = offset(struct.unpack_from('<I', data, pe + 24 + 96)[0])
    count_names = struct.unpack_from('<I', data, table + 24)[0]
    functions, names, ordinals = struct.unpack_from('<III', data, table + 28)
    found = []
    for i in range(count_names):
        ordinal = struct.unpack_from('<H', data, offset(ordinals) + 2 * i)[0]
        rva = struct.unpack_from('<I', data, offset(functions) + 4 * ordinal)[0]
        at = offset(struct.unpack_from('<I', data, offset(names) + 4 * i)[0])
        found.append((rva, data[at:data.index(b'\0', at)].decode('latin-1')))
    return sorted(found)
ntdll = ntdll_names(opt('--ntdll', '')) if '--ntdll' in sys.argv else []

def name(address):
    if 0x401000 <= address < 0xA00000:
        return f'{starts[bisect.bisect_right(starts, address) - 1]:08X}'
    module, base = pages.get(f'{address >> 16:04X}', (f'?{address >> 16:04X}', 0))
    if module == 'ntdll.dll' and ntdll:
        i = bisect.bisect_right([r for r, _ in ntdll], address - base) - 1
        return 'ntdll!' + ntdll[i][1]
    return module

marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
a, b = marks[first], marks[last]
ticks = sorted(int(r['start_tick']) for r in csv.DictReader(open(os.path.join(cap, 'frames.csv'))) if r['start_tick'])
events = [r for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv')))]
samples = list(csv.DictReader(open(os.path.join(cap, 'samples.csv'))))
sample_ticks = [int(r['tick']) for r in samples]
for x, y in zip(ticks, ticks[1:]):
    if not (a <= x <= b and (y - x) / 1e4 > over):
        continue
    inside = [e for e in events if x <= int(e['start_tick']) < y]
    present_end = max((int(e['end_tick']) for e in inside if e['event_type'] == 'present'), default=x)
    poses = min((int(e['start_tick']) for e in inside if e['event_type'] == 'poses_wait'), default=y)
    counts = collections.Counter(name(int(r['eip'], 16)) for r in samples[bisect.bisect_left(sample_ticks, present_end):bisect.bisect_left(sample_ticks, poses)])
    print(f'{(x - marks[first]) / 1e7:7.2f} s {(y - x) / 1e4:5.1f} ms, gap {(poses - present_end) / 1e4:5.1f} ms, {sum(counts.values())} samples: '
          + ', '.join(f'{k} {v}' for k, v in counts.most_common(top)))
