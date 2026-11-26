#!/usr/bin/env python3
"""The GPU's time a frame from OBNG's [Probe] GpuPasses summaries in a run's OBNG.log ("Probe: GPU passes - N frames
read back ..., ms a frame: from the first stamp to the last X; actor shadows Y; image space Z; ..."): one value a
summary window (every five seconds with tools/measure-fight.sh).

The stretch:
  --from MARK --to MARK   the windows wholly between two marks of the hand script. Needs a log with the summary's
                          clock ("Log: summary at tick N", OBNG builds since 2026-11-26): the same counter as OBVR's
                          marks. A window runs from one summary's tick to the next one's.
  otherwise               by position, for the logs without the clock: the windows with frames read back, less the
                          first --skip-first and the last --skip-last of them (for obng-profile-prison-fight-quiet.txt
                          the defaults leave the hold: the walk in before it, the end after it).

Usage: gpu-summaries.py <run folder>... [--from MARK --to MARK] [--skip-first 3] [--skip-last 2]"""
import os, re, statistics as st, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


TICK = re.compile(r'Log: summary at tick (\d+)')
LINE = re.compile(r'Probe: GPU passes - (\d+) frames read back .*?from the first stamp to the last ([\d.]+); actor shadows ([\d.]+); '
                  r'image space ([\d.]+)')
MARK = re.compile(r'HandScript: mark (\S+) .*?tick (\d+)\)')
first, last = int(opt('--skip-first', '3')), int(opt('--skip-last', '2'))
FROM, TO = opt('--from', ''), opt('--to', '')


def windows(run):
    """(start tick, end tick, frames, frame ms, actor shadows ms, image space ms) a window; the ticks None without
    the summary's clock. The GPU line comes after its summary's tick line (written by the render thread a frame
    later), so it belongs to the latest tick before it."""
    rows, ticks = [], []
    for line in open(os.path.join(run, 'OBNG.log'), errors='ignore'):
        t = TICK.search(line)
        if t:
            ticks.append(int(t[1]))
            continue
        m = LINE.search(line)
        if m:
            start, end = (ticks[-2], ticks[-1]) if len(ticks) >= 2 else (None, None)
            rows.append((start, end, int(m[1]), float(m[2]), float(m[3]), float(m[4])))
    return rows


def stretch(run, rows):
    if not FROM:
        rows = [r for r in rows if r[2] > 0]
        return rows[first:len(rows) - last] if len(rows) > first + last else [], f"{len(rows)} windows with frames read back"
    marks = {m: int(t) for m, t in MARK.findall(open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    if FROM not in marks or TO not in marks:
        return [], f"no mark {FROM if FROM not in marks else TO}"
    if not any(r[0] is not None for r in rows):
        return [], "no summary clock in OBNG.log (a build before 2026-11-26)"
    inside = [r for r in rows if r[0] is not None and marks[FROM] <= r[0] and r[1] <= marks[TO] and r[2] > 0]
    return inside, f"{FROM} - {TO}, {(marks[TO] - marks[FROM]) / 1e7:.1f} s"


for run in [a for a in sys.argv[1:] if os.path.isdir(a)]:
    name = os.path.basename(os.path.normpath(run))
    used, what = stretch(run, windows(run))
    if not used:
        print(f"{name}: {what} - too few windows")
        continue
    frame = sorted(r[3] for r in used)
    print(f"{name}: {len(used)} windows ({what}), {sum(r[2] for r in used)} frames read back; the GPU a frame (first stamp to "
          f"last) median {st.median(frame):.2f} ms, the windows {frame[0]:.2f} - {frame[-1]:.2f}; actor shadows "
          f"{st.median(r[4] for r in used):.2f}, image space {st.median(r[5] for r in used):.2f}")
