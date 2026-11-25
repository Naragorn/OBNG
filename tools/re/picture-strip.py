#!/usr/bin/env python3
"""Pictures of several runs of a still scenario side by side, for an evidence page: the same mark's eye picture of
each run (or a part of it) under its label - and, with --ratio, a map of where the second run is brighter or darker
than the first, the mean over the marks so that a flame's flicker averages out.

Usage: picture-strip.py --out <file> [--mark d05] [--eye Left] [--crop x0,y0,x1,y1] [--height 700] <label>=<run> ...
       picture-strip.py --out <file> --ratio [--marks d] [--eye Left] [--crop ...] [--height 700] [--span 20] <label>=<run A> <label>=<run B>
  --crop    the part to show, fractions of the picture's width and height
  --ratio   B against A: grey where they are the same, red where B is brighter, blue where it is darker, full
            colour at --span percent; pixels darker than 4 of 255 in both stay black (nothing to compare)
  --marks   the marks averaged for --ratio: those whose names begin so (d: d01 .. d12)"""
import os, re, sys
from PIL import Image, ImageChops, ImageDraw


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


eye, height = opt('--eye', 'Left'), int(opt('--height', '700'))
runs = [arg.split('=', 1) for arg in sys.argv[1:] if '=' in arg and not arg.startswith('--') and os.path.isdir(arg.split('=', 1)[1])]


def pictures(run):
    """{mark: path} of a run's dumped eye pictures, the frame files of EyeDumpFrames first."""
    found = {}
    for name in sorted(os.listdir(run)):
        m = re.fullmatch(rf'OBVR-Eye-{eye}-(\w+?)(-f00)?\.png', name)
        if m and (m.group(2) or m.group(1) not in found):
            found[m.group(1)] = os.path.join(run, name)
    return found


def part(path):
    picture = Image.open(path).convert('RGB')
    if '--crop' in sys.argv:
        x0, y0, x1, y1 = (float(v) for v in opt('--crop', '').split(','))
        w, h = picture.size
        picture = picture.crop((int(w * x0), int(h * y0), int(w * x1), int(h * y1)))
    return picture


def scaled(picture):
    return picture.resize((picture.size[0] * height // picture.size[1], height), Image.LANCZOS)


def mean_of(run, prefix):
    """The mean picture over the run's marks that begin with the prefix, as three lists of floats and a size."""
    paths = [p for mark, p in sorted(pictures(run).items()) if mark.startswith(prefix)]
    total, size = None, None
    for path in paths:
        picture = part(path)
        picture = picture.resize((max(1, picture.size[0] // 4), max(1, picture.size[1] // 4)), Image.BOX)
        data, size = picture.tobytes(), picture.size
        total = [v + d for v, d in zip(total, data)] if total else list(data)
    return [v / len(paths) for v in total], size


def ratio_map(a_run, b_run, prefix, span):
    a, size = mean_of(a_run, prefix)
    b, _ = mean_of(b_run, prefix)
    out = bytearray(len(a))
    for i in range(0, len(a), 3):
        sa, sb = a[i] + a[i + 1] + a[i + 2], b[i] + b[i + 1] + b[i + 2]
        if sa < 12 and sb < 12:
            continue
        change = max(-1.0, min(1.0, ((sb + 1.0) / (sa + 1.0) - 1.0) * 100.0 / span))
        grey = 110
        out[i] = int(grey + (255 - grey) * max(0.0, change))
        out[i + 1] = int(grey * (1.0 - abs(change)))
        out[i + 2] = int(grey + (255 - grey) * max(0.0, -change))
    return Image.frombytes('RGB', size, bytes(out))


def strip(panels):
    """The panels side by side at one height, a line of text above each."""
    panels = [(label, scaled(picture)) for label, picture in panels]
    width = sum(p.size[0] for _, p in panels) + 6 * (len(panels) - 1)
    out = Image.new('RGB', (width, height + 24), (16, 16, 16))
    x = 0
    for label, picture in panels:
        out.paste(picture, (x, 24))
        ImageDraw.Draw(out).text((x + 6, 6), label, fill=(232, 232, 232))
        x += picture.size[0] + 6
    return out


if len(runs) < 2:
    raise SystemExit(__doc__)
if '--ratio' in sys.argv:
    span = float(opt('--span', '20'))
    (la, ra), (lb, rb) = runs[0], runs[1]
    mark = sorted(m for m in pictures(ra) if m.startswith(opt('--marks', 'd')))[0]
    panels = [(la, part(pictures(ra)[mark])), (lb, part(pictures(rb)[mark])),
              (f'{lb} against {la}: red brighter, blue darker, full colour at {span:g} %', ratio_map(ra, rb, opt('--marks', 'd'), span))]
else:
    mark = opt('--mark', 'd05')
    panels = [(f'{label}  ({mark})', part(pictures(run)[mark])) for label, run in runs]
out = strip(panels)
out.save(opt('--out', 'strip.jpg'), quality=90)
print(f"{opt('--out', 'strip.jpg')}: {out.size[0]}x{out.size[1]}, {len(panels)} panels")
