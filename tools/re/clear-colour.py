#!/usr/bin/env python3
"""Pixels of the clear colour in a run's left-eye dumps (OBVR-Eye-Left-<mark>[-fNN].png), right half: where nothing
was drawn and the eye shows the device's clear colour (18, 27, 27). The occlusion culler's picture gate of the prison
bars (docs/subsystems/occlusion-cull.md, "The prison bars defect"): a culler that takes a see-through surface for cover
leaves out what stands behind it, and the clear colour shows there.
Usage: clear-colour.py <run folder>... [--all]   (--all lists every dump, else only those with clear-colour pixels)"""
import glob, os, sys
from PIL import Image

for run in [a for a in sys.argv[1:] if os.path.isdir(a)]:
    files = sorted(glob.glob(os.path.join(run, 'OBVR-Eye-Left-*.png')))
    total, hit = 0, 0
    for f in files:
        im = Image.open(f).convert('RGB')
        w, h = im.size
        n = sum(1 for p in im.crop((w // 2, 0, w, h)).get_flattened_data() if p == (18, 27, 27)) if hasattr(im, 'get_flattened_data') \
            else sum(1 for p in im.crop((w // 2, 0, w, h)).getdata() if p == (18, 27, 27))
        total += n
        hit += 1 if n else 0
        if n or '--all' in sys.argv:
            print(f"  {os.path.basename(f)}: {n}")
    print(f"{os.path.basename(os.path.normpath(run))}: {len(files)} left-eye dumps, {hit} with clear-colour pixels, {total} in all")
