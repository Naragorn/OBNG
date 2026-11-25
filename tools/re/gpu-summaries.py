#!/usr/bin/env python3
"""The GPU's time a frame from OBNG's [Probe] GpuPasses summaries in a run's OBNG.log ("Probe: GPU passes - N frames
read back ..., ms a frame: from the first stamp to the last X; actor shadows Y; image space Z; ..."): one value a
summary window (every five seconds with tools/measure-fight.sh). OBNG.log has no clock, so the stretch is taken by
position: the windows with frames read back, less the first --skip-first and the last --skip-last of them (for
obng-profile-prison-fight-quiet.txt the defaults leave the hold: the walk in before it, the end after it).

Usage: gpu-summaries.py <run folder>... [--skip-first 3] [--skip-last 2]"""
import os, re, statistics as st, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


LINE = re.compile(r'Probe: GPU passes - (\d+) frames read back .*?from the first stamp to the last ([\d.]+); actor shadows ([\d.]+); '
                  r'image space ([\d.]+)')
first, last = int(opt('--skip-first', '3')), int(opt('--skip-last', '2'))
for run in [a for a in sys.argv[1:] if os.path.isdir(a)]:
    rows = [(int(m[1]), float(m[2]), float(m[3]), float(m[4])) for m in LINE.finditer(open(os.path.join(run, 'OBNG.log'), errors='ignore').read())]
    rows = [r for r in rows if r[0] > 0]
    used = rows[first:len(rows) - last] if len(rows) > first + last else []
    if not used:
        print(f"{os.path.basename(os.path.normpath(run))}: {len(rows)} windows with frames read back - too few")
        continue
    frame = sorted(r[1] for r in used)
    print(f"{os.path.basename(os.path.normpath(run))}: {len(used)} of {len(rows)} windows, {sum(r[0] for r in used)} frames read back; the GPU a "
          f"frame (first stamp to last) median {st.median(frame):.2f} ms, the windows {frame[0]:.2f} - {frame[-1]:.2f}; actor shadows "
          f"{st.median(r[2] for r in used):.2f}, image space {st.median(r[3] for r in used):.2f}")
