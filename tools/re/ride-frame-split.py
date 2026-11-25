#!/usr/bin/env python3
"""A run's beats from OBVR's capture (cpu_events.csv), counted at OBVR's pose wait, between two marks of its hand
script - by default a ride's (obng-profile-ride-anvil*.txt: "ride" to "ride-120", the crossings at 6.35 s + 11.79 s k
after "ride"; the crossings are attributed only then).

The headset's beat is the pose wait's end: the wait returns once a period. Between two waits a frame does its render
(from the wait's end to the present's end) and the next frame its update (from there to the next wait). A beat is
missed only when the two together run past the next beat - then the two wait ends are two periods apart. A present
that comes late within the period is no missed beat: the next frame's present comes as much earlier (the beats stay
regular), so counting the presents' intervals (as this tool did before 2026-11-25, 15:40) overstates the misses.

The harness takes a picture of the game's window at every mark (a gamescope screenshot when headless): about 1.4 s
after the mark the game loses one or two beats to it (measured 2026-11-25 in the rides and the fight's hold, every
mark). --skip-pictures leaves out the beats from 0.8 to 2.6 s after every mark - the instrument's, not the game's.

Usage: ride-frame-split.py <run folder>... [--from MARK] [--to MARK] [--skip-pictures] [--lines N] [--samples <dir>]
  --from, --to  the stretch (default ride, ride-120); e.g. --from arrived --to done for the fight's hold
  --lines    how many of the missed beats to list per run (default: all)
  --samples  name what the render sampler caught in each missed beat's render and update, Oblivion.exe by the
             export's function starts, other code by module"""
import bisect, collections, csv, glob, os, re, statistics as st, sys


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


CROSSINGS = [6.35 + 11.79 * k for k in range(10)]


FROM, TO = opt('--from', 'ride'), opt('--to', 'ride-120')
RIDE = FROM == 'ride'
SKIP = '--skip-pictures' in sys.argv


def at_crossing(t):
    return RIDE and any(-0.2 <= t - at <= 0.8 for at in CROSSINGS)


def read(run):
    cap = os.path.join(run, 'OBVR-performance-1-5')
    marks = {m: int(t) for m, t in re.findall(r'HandScript: mark (\S+) .*?tick (\d+)\)', open(os.path.join(run, 'OBVR.log'), errors='ignore').read())}
    events = collections.defaultdict(list)
    for row in csv.DictReader(open(os.path.join(cap, 'cpu_events.csv'))):
        events[row['event_type']].append((int(row['start_tick']), int(row['end_tick'])))
    waits, presents = sorted(events['poses_wait']), sorted(events['present'])
    present_starts = [s for s, _ in presents]
    beats, skipped = [], 0
    for (_, end0), (start1, end1) in zip(waits, waits[1:]):
        if not marks[FROM] <= end0 <= marks[TO]:
            continue
        if SKIP and any(0.8e7 <= end0 - m <= 2.6e7 for m in marks.values()):
            skipped += 1
            continue
        i = bisect.bisect_left(present_starts, end0)
        if i >= len(presents) or presents[i][0] >= start1:
            continue
        done = presents[i][1]
        beats.append({'t': (end0 - marks[FROM]) / 1e7, 'beat': (end1 - end0) / 1e4, 'render': (done - end0) / 1e4,
                      'update': (start1 - done) / 1e4, 'ticks': (end0, done, start1)})
    return cap, beats, skipped


def sampler(cap, export):
    starts = sorted(int(os.path.basename(p)[2:-2], 16) for p in glob.glob(os.path.join(export, '0x*.c')))
    pages = {int(r['page'], 16): r['module'] for r in csv.DictReader(open(os.path.join(cap, 'samples-modules.csv')))}
    samples = sorted((int(r['tick']), int(r['eip'], 16), r['in_pass'] == '1') for r in csv.DictReader(open(os.path.join(cap, 'samples.csv'))))
    ticks = [s[0] for s in samples]

    def name(eip):
        if 0x00400000 <= eip < 0x00A30000:
            i = bisect.bisect_right(starts, eip) - 1
            return f"0x{starts[i]:08X}" if i >= 0 else f"0x{eip:08X}"
        return pages.get(eip >> 16, f"page {eip >> 16:04X}")

    def caught(a, b):
        got = collections.Counter(name(eip) + ('*' if in_pass else '') for _, eip, in_pass in samples[bisect.bisect_left(ticks, a):bisect.bisect_left(ticks, b)])
        return ', '.join(f"{k} {v}" for k, v in got.most_common(4))
    return caught


export = opt('--samples', '')
for run in [a for a in sys.argv[1:] if os.path.isdir(a) and a != export]:
    cap, beats, skipped = read(run)
    period = st.median(b['beat'] for b in beats)
    work = sorted(b['render'] + b['update'] for b in beats)
    missed = [b for b in beats if b['beat'] > period * 1.5]
    print(f"{os.path.basename(os.path.normpath(run))}: {len(beats)} beats of {period:.2f} ms in the capture{f' ({skipped} left out after the pictures)' if SKIP else ''}; between two waits a render and an "
          f"update of {st.median(work):.2f} ms (median), p99 {work[int(len(work) * 0.99)]:.2f}, the longest {work[-1]:.1f}")
    at_crossings = f", {sum(1 for b in missed if at_crossing(b['t']))} at the crossings" if RIDE else ''
    print(f"  missed beats: {len(missed)} ({len(missed) * 100.0 / len(beats):.2f} %){at_crossings}; "
          f"the render over 5 ms in {sum(1 for b in missed if b['render'] > 5)}, the update in {sum(1 for b in missed if b['update'] > 5)}")
    moves = [max((b for b in beats if -0.2 <= b['t'] - at <= 0.6), key=lambda b: b['update'], default=None) for at in CROSSINGS] if RIDE else []
    moves = sorted(b['update'] for b in moves if b)
    if moves:
        print(f"  the crossings' longest updates: {len(moves)}, {moves[0]:.1f} - {moves[-1]:.1f} ms (median {st.median(moves):.1f})")
    caught = sampler(cap, export) if export else None
    for b in missed[:int(opt('--lines', '1000'))]:
        line = f"    {b['t']:7.2f} s {b['beat']:5.1f} ms: render {b['render']:5.1f}, update {b['update']:5.1f}{'  (crossing)' if at_crossing(b['t']) else ''}"
        if caught:
            end0, done, start1 = b['ticks']
            line += f"  render [{caught(end0, done)}] update [{caught(done, start1)}]"
        print(line)
