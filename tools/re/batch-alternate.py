#!/usr/bin/env python3
"""The actors' batch on against off inside one run ([Route] ActorBatchAlternate=1, with [Timed] lines on the process
manager's update 0x00678510 and the high update 0x00605770): per summary window between two marks the process
manager's ms a call and the high updates a call, by side, and the two sides' means.

Usage: batch-alternate.py <run folder>... [--from MARK] [--to MARK]"""
import os, re, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


FROM, TO = opt('--from', 'still'), opt('--to', 'done')
runs = [a for a in sys.argv[1:] if os.path.isdir(a)]
for run in runs:
    marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    a, b = marks[FROM], marks[TO]
    ticks, window, rows = [], {}, []
    for line in open(os.path.join(run, 'OBNG.log'), errors='ignore'):
        t = re.search(r'Log: summary at tick (\d+)', line)
        if t:
            if len(ticks) >= 2 and a <= ticks[-2] and ticks[-1] <= b and {'side', 'pm', 'high'} <= window.keys():
                rows.append(window)
            ticks.append(int(t[1]))
            window = {}
            continue
        m = re.search(r'Route: actor batch \((on|off) in this window\)', line)
        if m:
            window['side'] = m[1]
        m = re.match(r'Timed actors:ProcessManager \S+ - (\d+) calls, ([\d.]+) ms in all', line)
        if m and int(m[1]):
            window['pm'] = float(m[2]) / int(m[1])
            window['calls'] = int(m[1])
        m = re.match(r'Timed actors:HighUpdate \S+ - (\d+) calls', line)
        if m and window.get('calls'):
            window['high'] = int(m[1]) / window['calls']
    print(f'{os.path.basename(run.rstrip("/"))}: {len(rows)} windows between {FROM} and {TO}')
    for w in rows:
        print(f'  {w["side"]:3s} {w["pm"]:.3f} ms a frame, {w["high"]:.1f} high updates, {1000 * w["pm"] / w["high"]:.1f} us a high update')
    for side in ('on', 'off'):
        s = [w for w in rows if w['side'] == side]
        if s:
            pm = sum(w['pm'] for w in s) / len(s)
            high = sum(w['high'] for w in s) / len(s)
            print(f'  {side:3s} mean: {pm:.3f} ms a frame, {high:.1f} high updates, {1000 * pm / high:.1f} us a high update ({len(s)} windows)')
