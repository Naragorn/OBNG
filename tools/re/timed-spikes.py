#!/usr/bin/env python3
"""A harness run's [Timed] lines summed over the run, and its spike lines between two marks of the script,
for the update steps that one timed function holds.
Usage: timed-spikes.py [--from MARK] [--to MARK] [--holder NAME] [--min MS] [--prefix P,...] <run folder>...
  --holder  the timed function whose spikes are listed (default update:TES-0x00444FB0), --min its least ms (5)
  --prefix  which timed functions the totals list (default grid:,purge:,update:)"""
import collections, os, re, sys

args = sys.argv[1:]


def opt(name, default):
    if name in args:
        i = args.index(name)
        value = args[i + 1]
        del args[i:i + 2]
        return value
    return default


start, end = opt('--from', 'ride'), opt('--to', 'done')
holder, least = opt('--holder', 'update:TES-0x00444FB0'), float(opt('--min', '5'))
prefixes = tuple(opt('--prefix', 'grid:,purge:,update:').split(','))
TIMED = re.compile(r'Timed (\S+) (0x[0-9A-F]+) - (\d+) calls, ([\d.]+) ms in all \(([\d.]+) us each\), ([\d.]+) ms without the timed '
                   r'functions inside, longest ([\d.]+) ms')
SPIKE = re.compile(r'Timed spike: frame:UpdateStep ([\d.]+) ms ending at tick (\d+) - (.*)')
MARK = re.compile(r'HandScript: mark (\S+) \(.*tick (\d+)\)')
for run in args:
    marks = {m.group(1): int(m.group(2)) for m in (MARK.search(line) for line in open(os.path.join(run, 'OBVR.log'), errors='replace')) if m}
    lo, hi = marks.get(start, 0), marks.get(end, 1 << 62)
    totals, spikes = collections.OrderedDict(), []
    for line in open(os.path.join(run, 'OBNG.log'), errors='replace'):
        m = TIMED.match(line)
        if m:
            t = totals.setdefault(m.group(1), [0, 0.0, 0.0, 0.0])
            t[0] += int(m.group(3))
            t[1] += float(m.group(4))
            t[2] += float(m.group(6))
            t[3] = max(t[3], float(m.group(7)))
            continue
        m = SPIKE.match(line)
        if m and lo <= int(m.group(2)) <= hi:
            parts = {k: float(v) for k, v in re.findall(r'(\S+) ([\d.]+) ms \(', m.group(3))}
            if parts.get(holder, 0) >= least:
                spikes.append((float(m.group(1)), parts))
    print(f'== {run} (totals over the whole run; spikes between "{start}" and "{end}")')
    for name, (calls, incl, excl, longest) in totals.items():
        if name.startswith(prefixes):
            print(f'  {name:42s} calls {calls:7d} incl {incl:8.1f} excl {excl:8.1f} longest {longest:6.1f}')
    print(f'  update steps held by {holder} (>= {least} ms): {len(spikes)}')
    for ms, parts in spikes:
        shown = '; '.join(f'{k.split(":", 1)[-1]} {v:.1f}' for k, v in sorted(parts.items(), key=lambda kv: -kv[1]) if v >= 0.5)
        print(f'   {ms:5.1f} ms | {shown}')
