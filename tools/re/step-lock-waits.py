#!/usr/bin/env python3
"""A run's long update steps beside the critical sections its main thread waited on in them: each [Timed] spike line
of frame:UpdateStep (its parts over --part ms) joined by tick with [Probe] LockCalls' "main-thread lock waits" line of
the same step (written at the next step's start, so its tick is at or just after the spike's).
Usage: step-lock-waits.py <run folder> [--from ride] [--to ride-120] [--part 2] [--only NAME]"""
import bisect, os, re, sys

run = sys.argv[1]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
first, last, part, only = opt('--from', 'ride'), opt('--to', 'ride-120'), float(opt('--part', '2')), opt('--only', '')
marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
spikes, waits = [], []
for line in open(os.path.join(run, 'OBNG.log'), errors='ignore'):
    m = re.match(r'Timed spike: frame:UpdateStep ([\d.]+) ms ending at tick (\d+) - (.*)', line)
    if m:
        spikes.append((int(m[2]), float(m[1]), m[3]))
        continue
    m = re.match(r'Probe: main-thread lock waits - the step ending at tick (\d+): ([\d.]+) ms; [^:]*:(.*)', line)
    if m:
        waits.append((int(m[1]), float(m[2]), m[3].strip()))
waits.sort()
ticks = [w[0] for w in waits]
a, b = marks[first], marks[last]
joined = unjoined = 0
for tick, ms, text in sorted(spikes):
    if not a <= tick <= b or (only and only not in text):
        continue
    parts = re.findall(r'([\w:\-]+) ([\d.]+) ms \((\d+) calls?', text)
    shown = ', '.join(f'{n.split("-0x")[0]} {float(v):.1f}' for n, v, _ in parts if float(v) >= part)
    i = bisect.bisect_left(ticks, tick - 2000)
    found = waits[i] if i < len(waits) and waits[i][0] - tick < 30000 else None  # within 3 ms after the step's end
    joined += found is not None
    unjoined += found is None
    print(f'{(tick - a) / 1e7:6.1f} s step {ms:5.1f} ms | {shown} || main-thread lock waits: ' + (f'{found[1]:.2f} ms: {found[2]}' if found else 'under 2 ms'))
print(f'{joined} steps with waits of 2 ms or more, {unjoined} without; {len(waits)} wait lines in the run')
