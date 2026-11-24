#!/usr/bin/env python3
"""A perf recording of the game taken during a harness run, read for the render thread's scene pass: where the
cycles go by module, and what AMD's IBS samples say there about instruction fetch (cache and TLB misses, fetch
latency) and about the tagged operations (loads and stores that miss the data cache and their latency, branches
mispredicted, how long an operation waits from tag to retire).

The recording (tools: perf record -p <game> -k raw -R -d -W -e cycles:u -e ibs_op/swfilt=1/u -e ibs_fetch/swfilt=1/u):
its clock is CLOCK_MONOTONIC_RAW, which is Wine's QueryPerformanceCounter in 100 ns ticks - the ticks of OBVR's
capture, so a sample is inside the scene pass when its time lies in one of cpu_events.csv's scene_pass rows.
Modules by OBVR's samples-modules.csv (64 KiB pages), then by the process's maps.

Usage: perf-pass.py <run folder> <perf folder> [--map <link map>] [--module OBNG.dll] [--function <name part>] [--top 12]
  <perf folder>  holds perf.data and maps.txt (/proc/<pid>/maps at the recording)
  --module       the module whose functions and instructions are listed (the most expensive by each measure); its link map with --map
  --function     also the instructions of the functions with this in their name, by the cycles"""
import bisect, collections, csv, os, re, subprocess, sys

run, perf = sys.argv[1], sys.argv[2]


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


top, focus, mapfile = int(opt('--top', '12')), opt('--module', 'OBNG.dll'), opt('--map', '')

# ---- the capture: the scene pass's intervals and the modules ----
caps = [d for d in sorted(os.listdir(run)) if d.startswith('OBVR-performance') and not d.endswith('report')]
cap = os.path.join(run, caps[0])
starts, ends = [], []
for r in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'), newline='')):
    if r['event_type'] == 'scene_pass':
        starts.append(int(r['start_tick']))
        ends.append(int(r['end_tick']))
order = sorted(range(len(starts)), key=lambda i: starts[i])
starts, ends = [starts[i] for i in order], [ends[i] for i in order]
pages, bases = {}, {}
modules_csv = os.path.join(cap, 'samples-modules.csv')
if os.path.exists(modules_csv):
    for r in csv.DictReader(open(modules_csv)):
        pages[int(r['page'], 16)] = r['module']
        bases.setdefault(r['module'].lower(), int(r['base'], 16))
ranges = []
maps = os.path.join(perf, 'maps.txt')
if os.path.exists(maps):
    for line in open(maps, errors='replace'):
        m = re.match(r'([0-9a-f]+)-([0-9a-f]+) \S+ \S+ \S+ \S+\s*(.*)', line)
        if m:
            ranges.append((int(m.group(1), 16), int(m.group(2), 16), os.path.basename(m.group(3)) or '[anonymous]'))
ranges.sort()
range_starts = [r[0] for r in ranges]


def module_of(ip):
    name = pages.get(ip >> 16)
    if name:
        return name
    i = bisect.bisect_right(range_starts, ip) - 1
    return ranges[i][2] if i >= 0 and ip < ranges[i][1] else '?'


def in_pass(tick):
    i = bisect.bisect_right(starts, tick) - 1
    return i >= 0 and tick <= ends[i]


# ---- the recording ----
class Module:
    def __init__(self):
        self.cycles = 0          # cycles:u, summed periods
        self.fetch = collections.Counter()  # samples, IcMiss, L2Miss, FetchL3Miss, L1TlbMiss, L2TlbMiss, lat (summed)
        self.op = collections.Counter()     # samples, load, store, loadMiss, loadL2Miss, storeMiss, missLat, branch, mispredict, tagToRet, microcode


mods = collections.defaultdict(Module)
by_ip = {'cycles': collections.Counter(), 'fetchLat': collections.Counter(), 'fetchMiss': collections.Counter(), 'missLat': collections.Counter(),
         'tagToRet': collections.Counter(), 'mispredict': collections.Counter(), 'op': collections.Counter()}
pairs = re.compile(r'(\w+)\s+(\S+)')
sample = re.compile(r'^\d+ (\d+) 0x[0-9a-f]+ \[0x[0-9a-f]+\]: PERF_RECORD_SAMPLE\(IP, 0x[0-9a-f]+\): (\d+)/(\d+): (0x[0-9a-f]+) period: (\d+)')
kind, fields, seen, kept, first_tick, last_tick = 'cycles', {}, 0, 0, None, None
proc = subprocess.Popen(['perf', 'script', '-i', os.path.join(perf, 'perf.data'), '-D'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors='replace')
for line in proc.stdout:
    if line.startswith('ibs_fetch_ctl:'):
        kind, fields = 'fetch', dict(pairs.findall(line.split('\t', 1)[1].split(' ', 1)[1]))
    elif line.startswith('ibs_op_ctl:'):
        kind, fields = 'op', {}
    elif line.startswith('ibs_op_data') and kind == 'op':
        fields.update(pairs.findall(line.split('\t', 1)[1].split(' ', 1)[1].replace(' bytes', '')))
    elif 'PERF_RECORD_SAMPLE' in line:
        m = sample.match(line)
        k, f = kind, fields
        kind, fields = 'cycles', {}
        if not m:
            continue
        seen += 1
        tick, pid, tid, ip, period = int(m.group(1)) // 100, int(m.group(2)), int(m.group(3)), int(m.group(4), 16), int(m.group(5))
        if tid != pid or not in_pass(tick):
            continue
        kept += 1
        first_tick = tick if first_tick is None else min(first_tick, tick)
        last_tick = tick if last_tick is None else max(last_tick, tick)
        mod = mods[module_of(ip)]
        focused = module_of(ip).lower() == focus.lower()
        if k == 'cycles':
            mod.cycles += period
            if focused:
                by_ip['cycles'][ip] += period
        elif k == 'fetch':
            n = lambda key: int(f.get(key, '0')) if f.get(key, '0').isdigit() else 0
            lat = n('Lat')
            mod.fetch.update(samples=1, IcMiss=n('IcMiss'), L2Miss=n('L2Miss'), L3Miss=n('FetchL3Miss'), L1TlbMiss=n('L1TlbMiss'), L2TlbMiss=n('L2TlbMiss'), lat=lat)
            if focused:
                by_ip['fetchLat'][ip] += lat
                by_ip['fetchMiss'][ip] += n('IcMiss')
        else:
            n = lambda key: int(f.get(key, '0')) if f.get(key, '0').isdigit() else 0
            load, store, miss, lat = n('LdOp'), n('StOp'), n('DcMiss'), n('DcMissLat')
            mod.op.update(samples=1, load=load, store=store, loadMiss=load * miss, loadL2Miss=load * n('L2Miss'), storeMiss=store * miss, missLat=lat,
                          branch=n('BrnRet'), mispredict=n('OpBrnMisp'), tagToRet=n('TagToRetCtr'), own=max(n('TagToRetCtr') - n('CompToRetCtr'), 0), microcode=n('Microcode'))
            if focused:
                by_ip['missLat'][ip] += lat
                by_ip['tagToRet'][ip] += max(n('TagToRetCtr') - n('CompToRetCtr'), 0)
                by_ip['mispredict'][ip] += n('OpBrnMisp')
                by_ip['op'][ip] += 1
proc.wait()
if not kept:
    sys.exit(f'{seen} samples read, none of the render thread inside a scene pass (the clocks do not match, or the recording lies outside the capture)')

passes = sum(1 for s in starts if first_tick <= s <= last_tick)
pass_ms = sum((e - s) / 1e4 for s, e in zip(starts, ends) if first_tick <= s <= last_tick) / max(passes, 1)
total = sum(m.cycles for m in mods.values()) or 1
print(f'{run}: {seen} samples, {kept} of the render thread inside {passes} scene passes of {pass_ms:.2f} ms ({(last_tick - first_tick) / 1e7:.1f} s)')
print('\nby module, inside the scene pass. ms: a frame, by the cycles. fetch: samples; of 1 000, those missing the instruction cache, L2, the first and')
print('second instruction TLB; lat: mean fetch latency in cycles; lat %: the module\'s share of all fetch latency. ops: tagged operations; of 1 000 loads,')
print('those missing the data cache and L2; of 1 000 stores, those missing; missLat: data cache miss latency a tagged operation, cycles; brMisp: of 1 000')
print('branches; own: cycles from an operation\'s tag to its completion, mean')
print(f"  {'module':22s} {'ms':>6s} | {'fetch':>6s} {'IcMiss':>6s} {'L2':>5s} {'iTLB1':>5s} {'iTLB2':>5s} {'lat':>5s} {'lat %':>6s} | {'ops':>6s} {'ldMiss':>6s} {'ldL2':>5s} {'stMiss':>6s} {'missLat':>7s} {'brMisp':>6s} {'own':>6s}")
all_fetch_lat = sum(m.fetch['lat'] for m in mods.values()) or 1
rows = sorted(mods.items(), key=lambda kv: -kv[1].cycles)
for name, m in rows[:top]:
    f, o = m.fetch, m.op
    fs, os_ = max(f['samples'], 1), max(o['samples'], 1)
    print(f"  {name[:22]:22s} {pass_ms * m.cycles / total:6.2f} | {f['samples']:6d} {1000 * f['IcMiss'] / fs:6.0f} {1000 * f['L2Miss'] / fs:5.0f} "
          f"{1000 * f['L1TlbMiss'] / fs:5.0f} {1000 * f['L2TlbMiss'] / fs:5.0f} {f['lat'] / fs:5.1f} {100 * f['lat'] / all_fetch_lat:6.1f} | {o['samples']:6d} {1000 * o['loadMiss'] / max(o['load'], 1):6.0f} "
          f"{1000 * o['loadL2Miss'] / max(o['load'], 1):5.0f} {1000 * o['storeMiss'] / max(o['store'], 1):6.0f} {o['missLat'] / os_:7.1f} {1000 * o['mispredict'] / max(o['branch'], 1):6.0f} {o['own'] / os_:6.1f}")
allf, allo = collections.Counter(), collections.Counter()
for m in mods.values():
    allf.update(m.fetch)
    allo.update(m.op)
print(f"  all: fetch latency {allf['lat'] / max(allf['samples'], 1):.1f} cycles a fetch sample, {1000 * allf['IcMiss'] / max(allf['samples'], 1):.0f} of 1 000 miss the instruction cache; "
      f"a tagged operation takes {allo['own'] / max(allo['samples'], 1):.1f} cycles from tag to completion, with {allo['missLat'] / max(allo['samples'], 1):.1f} of data cache miss latency; "
      f"loads missing {1000 * allo['loadMiss'] / max(allo['load'], 1):.0f}, branches mispredicted {1000 * allo['mispredict'] / max(allo['branch'], 1):.0f} of 1 000")

# ---- the focused module: its functions, then its instructions ----
symbols, preferred = [], 0
if mapfile:
    for line in open(mapfile, errors='replace'):
        m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
        if m:
            preferred = int(m.group(1), 16)
        m = re.match(r'\s*0001:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s', line)
        if m:
            symbols.append((int(m.group(2), 16) - preferred, m.group(1)))
    symbols.sort()
rvas = [s[0] for s in symbols]
base = bases.get(focus.lower(), 0)
names = {}


def symbol_of(ip):
    """The index of the function that holds ip, or -1."""
    return bisect.bisect_right(rvas, ip - base) - 1 if symbols and base else -1


def name_of(i):
    raw = symbols[i][1]
    if raw not in names:
        out = subprocess.run(['llvm-undname', raw], capture_output=True, text=True).stdout.strip().split('\n')
        names[raw] = out[-1] if len(out) > 1 and not out[-1].startswith('error') else raw
    return re.sub(r'\(.*', '', re.sub(r'<.*>', '<>', names[raw])).split(' ')[-1]


def where(ip):
    i = symbol_of(ip)
    if i < 0:
        return f'{ip:08X}'
    return f'{preferred + ip - base:08X} {name_of(i)[-60:]}+{ip - base - symbols[i][0]:#x}'


if symbols and base:
    by_function = {key: collections.Counter() for key in by_ip}
    for key, c in by_ip.items():
        for ip, v in c.items():
            by_function[key][symbol_of(ip)] += v
    module_ms = pass_ms * sum(by_ip['cycles'].values()) / total
    whole = {key: sum(c.values()) or 1 for key, c in by_ip.items()}
    print(f"\n{focus}: its functions inside the scene pass, {module_ms:.2f} ms a frame in all (ms by the cycles; the shares of the module's fetch latency, of its")
    print("operations' cycles from tag to completion and of its data cache miss latency)")
    print(f"  {'ms':>6s} {'fetch':>6s} {'own':>6s} {'miss':>6s}  function")
    for i, v in by_function['cycles'].most_common(top):
        print(f"  {pass_ms * v / total:6.3f} {100 * by_function['fetchLat'][i] / whole['fetchLat']:5.1f}% {100 * by_function['tagToRet'][i] / whole['tagToRet']:5.1f}% "
              f"{100 * by_function['missLat'][i] / whole['missLat']:5.1f}%  {name_of(i)[-90:] if i >= 0 else '(no symbol)'}")

part = opt('--function', '')
if part and symbols and base:
    chosen = [i for i in {symbol_of(ip) for ip in by_ip['cycles']} if i >= 0 and (part in symbols[i][1] or part in name_of(i))]
    for i in sorted(chosen, key=lambda k: -sum(v for ip, v in by_ip['cycles'].items() if symbol_of(ip) == k))[:3]:
        inside = {ip: v for ip, v in by_ip['cycles'].items() if symbol_of(ip) == i}
        mine = sum(inside.values())
        print(f"\n{name_of(i)[-100:]}: {pass_ms * mine / total:.3f} ms a frame; its instructions by the cycles (the address is the one after the slow instruction)")
        for ip, v in sorted(inside.items(), key=lambda kv: -kv[1])[:top]:
            print(f'  {100 * v / mine:5.1f} %  {preferred + ip - base:08X}  +{ip - base - symbols[i][0]:#x}')

for key, title in (('cycles', 'cycles'), ('tagToRet', 'cycles from tag to completion, summed over the tagged operations'), ('missLat', 'data cache miss latency, summed'),
                   ('fetchLat', 'fetch latency, summed'), ('mispredict', 'mispredicted branches')):
    c = by_ip[key]
    amount = sum(c.values())
    if not amount:
        continue
    print(f'\n{focus}: the instructions with the most {title} ({amount} in all)')
    for ip, v in c.most_common(top):
        print(f'  {100 * v / amount:5.1f} %  {where(ip)}')
