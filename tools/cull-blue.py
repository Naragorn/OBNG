#!/usr/bin/env python3
"""The pure-blue share of every dumped eye picture of a harness run.

The occlusion culler's blue geometry (docs/subsystems/occlusion-cull.md,
2026-11-09): geometry drawn without its textures comes out as (0, 0, 255).
For each run folder given, prints per mark series (OBVR's [Debug] EyeDump /
EyeDumpFrames pictures OBVR-Eye-Right-<mark>[-fNN].png) the share of pixels
that are pure blue, one number per frame. 0.0 everywhere is the pass mark.

    python3 tools/cull-blue.py artifacts/shadow/obng-cull-texture-turn/<run> ...
"""
import glob
import os
import sys

import numpy as np
from PIL import Image


def main(runs):
    for run in runs:
        print("===", run)
        series = {}
        for path in sorted(glob.glob(os.path.join(run, "OBVR-Eye-Right-*.png"))):
            name = os.path.basename(path)[len("OBVR-Eye-Right-"):-len(".png")]
            key = name.split("-f")[0] if "-f" in name else name
            picture = np.asarray(Image.open(path).convert("RGB"))
            blue = ((picture[..., 2] > 200) & (picture[..., 0] < 40) & (picture[..., 1] < 40)).mean() * 100
            series.setdefault(key, []).append(blue)
        for key, values in series.items():
            print(f"{key:16s} " + " ".join(f"{v:.1f}" for v in values))


if __name__ == "__main__":
    main(sys.argv[1:])
