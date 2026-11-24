#!/usr/bin/env python3
"""Two runs of a still scenario (eye pictures dumped at marks, e.g. obng-prison-tutorial-alternate.txt) compared mark
by mark: the share of pixels that differ and the mean difference - between the runs, and inside each run from one
mark to the next (the scene's own noise). Optionally one mark's pictures side by side with their difference.

Usage: run-pictures.py <run A> <run B> [--eye Left] [--threshold 8] [--sheet <out.png> [--mark d05] [--width 1500]]"""
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


a, b = pictures(a_run), pictures(b_run)
common = sorted(set(a) & set(b))
print(f'A {a_run}: {len(a)} pictures; B {b_run}: {len(b)}; {len(common)} marks in both; eye {eye}, threshold {threshold} of 255 after a 4x reduction')
between = []
for mark in common:
    share, mean = differ(load(a[mark]), load(b[mark]))
    between.append((share, mean))
    print(f'  {mark}  A against B  {share:6.2f} % differ, mean {mean:5.2f}')
print(line('inside A, mark to mark (the noise)', inside(a)))
print(line('inside B, mark to mark (the noise)', inside(b)))
print(line('A against B at the same mark', between))
if '--sheet' in sys.argv and common:
    mark = opt('--mark', common[len(common) // 2])
    sheet(opt('--sheet', ''), mark, int(opt('--width', '1500')), a, b)
    print(f'sheet of {mark}: {opt("--sheet", "")}')
