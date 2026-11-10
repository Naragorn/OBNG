#!/usr/bin/env python3
"""Two harness runs' dumped eye pictures side by side: per mark and eye the
share of pixels that differ by more than a threshold (after a 4x reduction,
so that a pixel of noise does not count) and the mean difference. Two runs of
the same scenario with the AI off show the same town; a culler that leaves
out something visible shows as a block of differing pixels.
Usage: eye-compare.py <run a> <run b> [--frame f00] [--threshold 24]"""
import glob, os, sys
from PIL import Image, ImageChops
a, b = sys.argv[1], sys.argv[2]
def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
frame = opt('--frame', 'f00')
threshold = int(opt('--threshold', '24'))
rows = []
for path in sorted(glob.glob(os.path.join(a, f'OBVR-Eye-*-{frame}.png'))):
    name = os.path.basename(path)
    other = os.path.join(b, name)
    if not os.path.exists(other):
        continue
    ia = Image.open(path).convert('RGB')
    ib = Image.open(other).convert('RGB')
    if ia.size != ib.size:
        print(f"{name}: sizes differ {ia.size} {ib.size}")
        continue
    small = (ia.size[0] // 4, ia.size[1] // 4)
    d = ImageChops.difference(ia.resize(small, Image.BOX), ib.resize(small, Image.BOX)).convert('L')
    hist = d.histogram()
    total = sum(hist)
    over = sum(hist[threshold:])
    mean = sum(i * h for i, h in enumerate(hist)) / total
    rows.append((100.0 * over / total, mean, name))
rows.sort(reverse=True)
print(f"{len(rows)} pictures compared (frame {frame}, threshold {threshold} of 255 after a 4x reduction)")
for share, mean, name in rows:
    print(f"  {share:6.2f} % of the pixels differ, mean difference {mean:5.2f}  {name}")
