#!/usr/bin/env python3
"""Two runs of a still scenario (eye pictures dumped at marks, e.g. obng-prison-tutorial-alternate.txt) compared mark
by mark: the share of pixels that differ and the mean difference - between the runs, and inside each run from one
mark to the next (the scene's own noise). Optionally one mark's pictures side by side with their difference.

Usage: run-pictures.py <run A> <run B> [--eye Left] [--threshold 8] [--sheet <out.png> [--mark d05] [--width 1500]]
                       [--region <name>=<x0>,<y0>,<x1>,<y1> ...] [--marks <prefix>] [--alternate]
  --marks      only the marks whose names begin so: one view of a scenario that looks two ways (obng-lighting-road-
               still.txt: d0 the marks looking east, e0 the ones with the head turned), so that a region is one surface
  --region     a part of the picture (fractions of its width and height): its mean colour in A and in B, the median
               over the marks, and B's brightness against A's - what a change does to one surface
  --alternate  both runs had [Probe] PassMaskAlternate on: the marks are split by the side their run was on (the
               game's mask or the flipped one, a mark within a second after a switch left out) and each side is
               compared by itself - a lighting pass taken out of both runs at the same marks"""
import os, re, statistics as st, sys
from PIL import Image, ImageChops, ImageDraw

a_run, b_run = sys.argv[1], sys.argv[2]


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


eye, threshold = opt('--eye', 'Left'), int(opt('--threshold', '8'))


def pictures(run):
    """{mark: path} of a run's dumped eye pictures, the frame files of EyeDumpFrames first."""
    found = {}
    for name in sorted(os.listdir(run)):
        m = re.fullmatch(rf'OBVR-Eye-{eye}-(\w+?)(-f00)?\.png', name)
        if m and (m.group(2) or m.group(1) not in found):
            found[m.group(1)] = os.path.join(run, name)
    return found


def differ(a, b):
    """(percent of pixels differing by more than the threshold, mean difference of 255) after a 4x reduction."""
    d = ImageChops.difference(a, b).convert('L')
    d = d.resize((d.size[0] // 4, d.size[1] // 4), Image.BOX)
    h = d.histogram()
    n = sum(h)
    return sum(h[threshold + 1:]) * 100.0 / n, sum(i * c for i, c in enumerate(h)) / n


def load(path):
    return Image.open(path).convert('RGB')


def inside(found):
    marks = sorted(found)
    return [differ(load(found[x]), load(found[y])) for x, y in zip(marks, marks[1:])]


def line(name, rows):
    if not rows:
        return f'{name}: no pairs'
    return (f'{name}: {len(rows)} pairs, pixels differing median {st.median(r[0] for r in rows):.2f} % (max {max(r[0] for r in rows):.2f}), '
            f'mean difference median {st.median(r[1] for r in rows):.2f} (max {max(r[1] for r in rows):.2f})')


def sheet(path, mark, width, a, b):
    """A | B | their difference four times as bright, each at a third of the width, with a line of names."""
    pa, pb = load(a[mark]), load(b[mark])
    diff = ImageChops.difference(pa, pb).point(lambda v: min(255, v * 4))
    w = width // 3
    h = pa.size[1] * w // pa.size[0]
    out = Image.new('RGB', (w * 3, h + 22), (16, 16, 16))
    for i, (pic, label) in enumerate(((pa, 'A  ' + os.path.basename(os.path.normpath(a_run))),
                                       (pb, 'B  ' + os.path.basename(os.path.normpath(b_run))), (diff, 'difference x4'))):
        out.paste(pic.resize((w, h), Image.LANCZOS), (i * w, 22))
        ImageDraw.Draw(out).text((i * w + 6, 5), f'{label}  ({mark}, {eye} eye)', fill=(230, 230, 230))
    out.save(path)


def regions():
    """[(name, (x0, y0, x1, y1))] of the --region arguments."""
    found = []
    for i, arg in enumerate(sys.argv):
        if arg == '--region' and i + 1 < len(sys.argv):
            name, box = sys.argv[i + 1].split('=', 1)
            found.append((name, tuple(float(v) for v in box.split(','))))
    return found


def mean_colour(picture, box):
    w, h = picture.size
    part = picture.crop((int(w * box[0]), int(h * box[1]), int(w * box[2]), int(h * box[3]))).resize((32, 32), Image.BOX)
    pixels = part.tobytes()
    return tuple(sum(pixels[c::3]) / (len(pixels) // 3) for c in range(3))


def region_lines(common, a, b):
    """A line a region: the median over the marks of the mean colour in A and in B, and B's brightness against A's."""
    lines = []
    for name, box in regions():
        of_a = [mean_colour(load(a[m]), box) for m in common]
        of_b = [mean_colour(load(b[m]), box) for m in common]
        med_a = tuple(st.median(c[i] for c in of_a) for i in range(3))
        med_b = tuple(st.median(c[i] for c in of_b) for i in range(3))
        ratios = [sum(y) / sum(x) for x, y in zip(of_a, of_b) if sum(x) > 0]
        ratio = f'{(st.median(ratios) - 1) * 100:+.1f} % (from {(min(ratios) - 1) * 100:+.1f} to {(max(ratios) - 1) * 100:+.1f})' if ratios else 'black in A'
        lines.append(f'  region {name:22s} A ({med_a[0]:5.1f}, {med_a[1]:5.1f}, {med_a[2]:5.1f})  B ({med_b[0]:5.1f}, {med_b[1]:5.1f}, {med_b[2]:5.1f})  B against A {ratio}')
    return lines


def sides(run):
    """{mark: 'game' | 'flipped'} of a run with [Probe] PassMaskAlternate, by the last switch before the mark."""
    marks, switches = {}, []
    for text in open(os.path.join(run, 'OBVR.log'), errors='replace'):
        m = re.search(r'HandScript: mark (\S+) \(.*tick (\d+)\)', text)
        if m:
            marks[m.group(1)] = int(m.group(2))
    for text in open(os.path.join(run, 'OBNG.log'), errors='replace'):
        m = re.search(r"pass mask alternate - switch \d+ at tick (\d+): the mask \w+ from here \((flipped|the game's)\)", text)
        if m:
            switches.append((int(m.group(1)), 'flipped' if m.group(2) == 'flipped' else 'game'))
    found = {}
    for mark, tick in marks.items():
        side, since = 'game', 1e9
        for at, to in switches:
            if at <= tick:
                side, since = to, (tick - at) / 1e7
        if since >= 1.0:
            found[mark] = side
    return found


def report(marks, a, b, label):
    between = []
    for mark in marks:
        share, mean = differ(load(a[mark]), load(b[mark]))
        between.append((share, mean))
        print(f'  {mark}  A against B  {share:6.2f} % differ, mean {mean:5.2f}')
    print(line(f'A against B at the same mark{label}', between))
    print('\n'.join(region_lines(marks, a, b)))


prefix = opt('--marks', '')
a, b = ({mark: path for mark, path in pictures(run).items() if mark.startswith(prefix)} for run in (a_run, b_run))
common = sorted(set(a) & set(b))
print(f'A {a_run}: {len(a)} pictures; B {b_run}: {len(b)}; {len(common)} marks in both; eye {eye}, threshold {threshold} of 255 after a 4x reduction')
if '--alternate' in sys.argv:
    of_a, of_b = sides(a_run), sides(b_run)
    for side in ('game', 'flipped'):
        marks = [m for m in common if of_a.get(m) == side and of_b.get(m) == side]
        print(f"-- both runs on the {'mask flipped' if side == 'flipped' else 'the game\'s mask'}: {len(marks)} marks ({' '.join(marks)})")
        report(marks, a, b, f', {side}')
else:
    print(line('inside A, mark to mark (the noise)', inside(a)))
    print(line('inside B, mark to mark (the noise)', inside(b)))
    report(common, a, b, '')
if '--sheet' in sys.argv and common:
    mark = opt('--mark', common[len(common) // 2])
    sheet(opt('--sheet', ''), mark, int(opt('--width', '1500')), a, b)
    print(f'sheet of {mark}: {opt("--sheet", "")}')
