#!/usr/bin/env python3
"""Where run B differs from run A in a way that follows the geometry, for a still scenario's eye pictures: per
pixel the median over the marks of log(B / A), less its own blur (a flame's flicker or the bloom changes whole areas
smoothly; a surface drawn otherwise has edges). Prints the share of the lit pixels whose change stands out from
their surroundings by more than 3 %, and writes a map: grey the same, red brighter, blue darker, black too dark to
compare. Two runs of the same build give the floor of the measure (2.9 % in obng-lighting-still.txt's room).

Usage: geometry-changes.py <run A> <run B> <out.png> [--marks d] [--span 10] [--eye Left]
  --marks  the marks whose names begin so (d: d01 .. d12)
  --span   the change that is full colour in the map, in percent"""
import os, re, sys
import numpy as np
from PIL import Image


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


a_run, b_run, out = sys.argv[1], sys.argv[2], sys.argv[3]
prefix, span, eye = opt('--marks', 'd'), float(opt('--span', '10')), opt('--eye', 'Left')


def pictures(run):
    """{mark: path} of a run's dumped eye pictures, the frame files of EyeDumpFrames first."""
    found = {}
    for name in sorted(os.listdir(run)):
        m = re.fullmatch(rf'OBVR-Eye-{eye}-(\w+?)(-f00)?\.png', name)
        if m and (m.group(2) or m.group(1) not in found):
            found[m.group(1)] = os.path.join(run, name)
    return {k: v for k, v in found.items() if k.startswith(prefix)}


def brightness(path):
    """The picture at a sixth of its size, the sum of its three channels."""
    picture = Image.open(path).convert('RGB')
    picture = picture.resize((picture.size[0] // 6, picture.size[1] // 6), Image.BOX)
    return np.asarray(picture, dtype=np.float32).sum(axis=2)


def box(values, radius):
    """The mean over a square of 2 x radius + 1 around each pixel (edges repeated)."""
    padded = np.pad(values, radius, mode='edge')
    sums = np.pad(padded.cumsum(axis=0).cumsum(axis=1), ((1, 0), (1, 0)))
    n = 2 * radius + 1
    return (sums[n:, n:] - sums[:-n, n:] - sums[n:, :-n] + sums[:-n, :-n]) / (n * n)


pa, pb = pictures(a_run), pictures(b_run)
marks = sorted(set(pa) & set(pb))
if not marks:
    raise SystemExit('no marks in both runs')
ratios = np.stack([np.log((brightness(pb[m]) + 3.0) / (brightness(pa[m]) + 3.0)) for m in marks])
median = np.median(ratios, axis=0)
lit = np.stack([brightness(pa[m]) for m in marks]).mean(axis=0) >= 9
change = (np.exp(median - box(median, 24)) - 1) * 100
change[~lit] = 0
share = (np.abs(change[lit]) > 3).mean() * 100 if lit.any() else 0.0
print(f'{os.path.basename(os.path.normpath(a_run))} -> {os.path.basename(os.path.normpath(b_run))}: {len(marks)} marks; '
      f'lit pixels whose change stands out from their surroundings by more than 3 %: {share:.2f} %')
v = np.clip(change / span, -1, 1)
rgb = np.zeros(v.shape + (3,), dtype=np.uint8)
rgb[..., 0] = (110 + 145 * np.clip(v, 0, 1)).astype(np.uint8)
rgb[..., 1] = (110 * (1 - np.abs(v))).astype(np.uint8)
rgb[..., 2] = (110 + 145 * np.clip(-v, 0, 1)).astype(np.uint8)
rgb[~lit] = 0
Image.fromarray(rgb).resize((rgb.shape[1] * 2, rgb.shape[0] * 2), Image.NEAREST).save(out)
