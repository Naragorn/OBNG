#!/usr/bin/env python3
"""The horizontal shift between the two eye pictures of a run, per band of rows.

  tools/eye-shift.py <run> [<run> ...] [--marks a,b,...] [--bands 8]

For every mark with OBVR-Eye-Left-<mark>.png and OBVR-Eye-Right-<mark>.png
(OBVR's [Debug] EyeDump) the picture is cut into bands of rows, and for each
band the shift of the right picture against the left that correlates best is
printed with its correlation. The far background has the smallest shift
(the two eyes' texture bounds apart); nearer content has more. A layer drawn
in the second eye with the first eye's matrix shows the background's shift
at a near band - compare a single-pass run with a dual-pass run of the same
scenario, mark by mark.
"""
import argparse
import glob
import os

import numpy as np
from PIL import Image


def best_shift(left, right, lo, hi):
    best = (-2.0, 0)
    width = left.shape[1]
    for dx in range(lo, hi):
        a = left[:, dx + 20:width - 20]
        b = right[:, 20:width - 20 - dx]
        a = a - a.mean()
        b = b - b.mean()
        denom = np.sqrt((a * a).sum() * (b * b).sum())
        c = float((a * b).sum() / denom) if denom > 0 else -1.0
        if c > best[0]:
            best = (c, dx)
    return best


def main():
    p = argparse.ArgumentParser()
    p.add_argument("runs", nargs="+")
    p.add_argument("--marks", default="")
    p.add_argument("--bands", type=int, default=8)
    p.add_argument("--lo", type=int, default=60)
    p.add_argument("--hi", type=int, default=180)
    args = p.parse_args()
    for run in args.runs:
        print(run)
        marks = [m for m in args.marks.split(",") if m] or sorted(
            os.path.basename(f)[len("OBVR-Eye-Left-"):-4] for f in glob.glob(os.path.join(run, "OBVR-Eye-Left-*.png")))
        for mark in marks:
            lp = os.path.join(run, f"OBVR-Eye-Left-{mark}.png")
            rp = os.path.join(run, f"OBVR-Eye-Right-{mark}.png")
            if not (os.path.exists(lp) and os.path.exists(rp)):
                print(f"  {mark}: no pair")
                continue
            left = np.asarray(Image.open(lp).convert("L")).astype(float)
            right = np.asarray(Image.open(rp).convert("L")).astype(float)
            rows = left.shape[0]
            cells = []
            for i in range(args.bands):
                a, b = rows * i // args.bands, rows * (i + 1) // args.bands
                c, dx = best_shift(left[a:b], right[a:b], args.lo, args.hi)
                cells.append(f"{dx:3d}({c:.2f})")
            print(f"  {mark:12s} " + " ".join(cells))


if __name__ == "__main__":
    main()
