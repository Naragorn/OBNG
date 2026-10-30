#!/usr/bin/env python3
"""Summarise a measurement series: for every run directory given, cut OBVR's
capture by marks (../OBVR/tools/perf-phases.py) and print the Walkers phase's
frame mean, p95, share over 20 ms and scene_pass mean / p95, then the median
of each column per side. Runs are grouped by their scenario directory name
(obng-range-walk-original, obng-range-walk-routed).

    python3 tools/measure-summary.py artifacts/shadow/obng-range-walk-*/2026*
"""
import re
import statistics
import subprocess
import sys
from pathlib import Path

OBVR = Path(__file__).resolve().parents[2] / 'OBVR'
PHASE = 'people-placed to people-watched'


def phases(run):
    out = subprocess.run(['python3', 'tools/perf-phases.py', str(Path(run).resolve()), 'prison', 'empty-watched',
                          'people-placed', 'people-watched', '--events', 'scene_pass'],
                         cwd=OBVR, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith('| ' + PHASE):
            cells = [c.strip() for c in line.strip('|').split('|')]
            frames = int(cells[1])
            mean, p95 = float(cells[2]), float(cells[4])
            over = float(re.search(r'\(([\d.]+)%\)', cells[7]).group(1))
            sp_mean, sp_p95 = (float(x) for x in cells[11].split('/'))
            return frames, mean, p95, over, sp_mean, sp_p95
    return None


def main(argv):
    sides = {}
    for run in argv[1:]:
        side = Path(run).parent.name.replace('obng-range-walk-', '')
        row = phases(run)
        if row is None:
            print('%-9s %s: no capture' % (side, Path(run).name))
            continue
        sides.setdefault(side, []).append((Path(run).name, row))
    print('%-9s %-16s %6s %9s %8s %7s %10s %9s' % ('side', 'run', 'frames', 'frame ms', 'p95', '>20ms%', 'scene ms', 'sc p95'))
    for side, rows in sides.items():
        for name, r in rows:
            print('%-9s %-16s %6d %9.2f %8.2f %7.1f %10.2f %9.2f' % ((side, name) + r))
        if len(rows) > 1:
            med = tuple(statistics.median(r[i] for _, r in rows) for i in range(6))
            print('%-9s %-16s %6d %9.2f %8.2f %7.1f %10.2f %9.2f' % ((side, 'MEDIAN') + med))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
