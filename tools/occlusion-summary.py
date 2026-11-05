#!/usr/bin/env python3
"""Sums the occlusion probe's lines of one run per place.

    tools/occlusion-summary.py artifacts/shadow/obng-occlusion/<run>

OBNG.log carries the probe's lines ("Probe: occlusion - ... qpc N" followed by
one line per draw kind, the counts since the last head line); OBVR.log
carries the marks with the same clock ("HandScript: mark <name> (... tick
N)"). A place is the span from a mark <name> to the mark <name>-done; a
block of probe lines belongs to it when the block's head line and the head
line before it both lie inside the span, so every draw it counts was drawn
at the place.

Prints one table per place and one line per place for the shares.
"""

import re
import sys
from pathlib import Path

HEAD = re.compile(
    r"Probe: occlusion - (\d+) eye passes sampled \(1 in (\d+)\), (\d+) draws in them, (\d+) beyond the \d+ a pass holds, "
    r"(\d+) in passes inside them, (\d+) batches lost, (\d+) draws unsampled; qpc (\d+)"
)
KIND = re.compile(
    r"Probe: occlusion (.+?) - (\d+) draws: (\d+) testable \(near plane (\d+), outside the frustum (\d+), no bound (\d+), "
    r"viewport (\d+)\); no pixel drawn (\d+); bound hidden at the draw (\d+), at the pass's end (\d+) of (\d+) tested "
    r"\((\d+) of them drew no pixel\); contradictions (\d+), failed controls (\d+); draw time ([\d.]+) (ms|ticks), ([\d.]+) (?:ms|ticks) hidden at the end, "
    r"([\d.]+) (?:ms|ticks) without a pixel"
)
TIME = r"([\d.]+) (?:ms|ticks)"
CLASSES = re.compile(
    r"Probe: occlusion no-pixel (.+?) - bound hidden (\d+) \(" + TIME + r"\), bound seen (\d+) \(" + TIME + r"\), near plane (\d+) \(" + TIME
    + r"\), no bound (\d+) \(" + TIME + r"\), outside the frustum (\d+) \(" + TIME + r"\), viewport (\d+) \(" + TIME + r"\)"
)
CLASS_NAMES = ("bound hidden", "bound seen", "near plane", "no bound", "outside the frustum", "viewport")
PASS = re.compile(
    r"Probe: occlusion pass (0x[0-9A-F]+) - (\d+) draws, (\d+) without a pixel \((\d+) with the bound seen\), (\d+) hidden at the end, time "
    + TIME + ", " + TIME + " without a pixel"
)
TALLY = r"(\d+) draws, (\d+) without a pixel, time " + TIME + ", " + TIME + " without a pixel"
OWNER = re.compile(r"Probe: occlusion owner (.+?), (.+?) - " + TALLY)
NAME = re.compile(r'Probe: occlusion name "(.*?)" \((.+?), (.+?)\) - ' + TALLY)
RADIUS = re.compile(r"Probe: occlusion radius - (.*)")
RADIUS_PART = re.compile(r"(below \d+|\d+ and above): (\d+) draws, (\d+) without a pixel \(" + TIME + " of " + TIME + r"\)")
MARK = re.compile(r"HandScript: mark (\S+) \(.*tick (\d+)\)")
KIND_FIELDS = (
    "draws", "testable", "near", "outside", "no_bound", "viewport", "no_pixel", "hidden_at_draw", "hidden_at_end", "end_tested",
    "hidden_end_no_pixel", "contradictions", "control_failed",
)
HEAD_FIELDS = ("passes", "draws", "beyond", "inner", "lost", "unsampled")


def read_marks(log):
    marks = {}
    for line in log.read_text(errors="replace").splitlines():
        m = MARK.search(line)
        if m:
            marks[m.group(1)] = int(m.group(2))
    return marks


def read_blocks(log):
    """Each head line with the kind lines that follow it."""
    blocks = []
    for line in log.read_text(errors="replace").splitlines():
        m = HEAD.search(line)
        if m:
            g = [int(x) for x in m.groups()]
            blocks.append({"head": dict(zip(HEAD_FIELDS, (g[0], g[2], g[3], g[4], g[5], g[6]))), "qpc": g[7], "kinds": {}})
            continue
        m = KIND.search(line)
        if m and blocks:
            values = dict(zip(KIND_FIELDS, (int(x) for x in m.groups()[1:14])))
            values["unit"] = m.group(16)
            values["time"] = float(m.group(15))
            values["time_hidden"] = float(m.group(17))
            values["time_no_pixel"] = float(m.group(18))
            blocks[-1]["kinds"][m.group(1)] = values
            continue
        if not blocks:
            continue
        extra = blocks[-1].setdefault("extra", {})
        m = CLASSES.search(line)
        if m:
            g = m.groups()
            for i, name in enumerate(CLASS_NAMES):
                add(extra, ("class", g[0], name), (int(g[1 + 2 * i]), float(g[2 + 2 * i])))
            continue
        m = PASS.search(line)
        if m:
            g = m.groups()
            add(extra, ("pass", g[0]), (int(g[1]), int(g[2]), int(g[3]), int(g[4]), float(g[5]), float(g[6])))
            continue
        m = OWNER.search(line)
        if m:
            g = m.groups()
            add(extra, ("owner", g[1], g[0]), (int(g[2]), int(g[3]), float(g[4]), float(g[5])))
            continue
        m = NAME.search(line)
        if m:
            g = m.groups()
            add(extra, ("name", g[0], g[1], g[2]), (int(g[3]), int(g[4]), float(g[5]), float(g[6])))
            continue
        m = RADIUS.search(line)
        if m:
            for part in RADIUS_PART.finditer(m.group(1)):
                g = part.groups()
                add(extra, ("radius", g[0]), (int(g[1]), int(g[2]), float(g[3]), float(g[4])))
    return blocks


def add(table, key, values):
    """Adds a tuple of numbers to the tuple under key."""
    old = table.get(key)
    table[key] = values if old is None else tuple(a + b for a, b in zip(old, values))


def places(marks):
    """(name, from tick, to tick) for every mark that has a -done partner, in time order."""
    spans = [(name, tick, marks[name + "-done"]) for name, tick in marks.items() if name + "-done" in marks]
    return sorted(spans, key=lambda s: s[1])


def sum_place(blocks, start, end):
    head = dict.fromkeys(HEAD_FIELDS, 0)
    kinds = {}
    extra = {}
    used = 0
    for before, block in zip([None] + blocks[:-1], blocks):
        if before is None or before["qpc"] < start or block["qpc"] > end:
            continue
        used += 1
        for key in HEAD_FIELDS:
            head[key] += block["head"][key]
        for name, values in block["kinds"].items():
            total = kinds.setdefault(name, dict.fromkeys(KIND_FIELDS + ("time", "time_hidden", "time_no_pixel"), 0))
            for key in total:
                total[key] += values[key]
            total["unit"] = values["unit"]
        for key, values in block.get("extra", {}).items():
            add(extra, key, values)
    head["extra"] = extra
    return head, kinds, used


def share(part, whole):
    return f"{100.0 * part / whole:.1f} %" if whole else "-"


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    run = Path(sys.argv[1])
    obng, obvr = run / "OBNG.log", run / "OBVR.log"
    if not obng.is_file() or not obvr.is_file():
        print(f"no OBNG.log or OBVR.log in {run}")
        return 2
    marks = read_marks(obvr)
    blocks = read_blocks(obng)
    spans = places(marks)
    if not blocks or not spans:
        print(f"{len(blocks)} probe blocks, {len(spans)} places with a -done mark: nothing to sum")
        return 1
    order = ("shapes", "skinned shapes", "direct shapes", "direct strips")
    for name, start, end in spans:
        head, kinds, used = sum_place(blocks, start, end)
        print(f"\n## {name}: {used} probe blocks, {head['passes']} eye passes sampled, {head['draws']} draws in them "
              f"({head['beyond']} beyond capacity, {head['inner']} in inner passes, {head['lost']} batches lost)\n")
        if not kinds:
            print("no draws sampled")
            continue
        print("| kind | draws | testable | near plane | outside frustum | no bound | no pixel drawn | bound hidden at the draw | "
              "bound hidden at the end (of tested) | contradictions | failed controls | draw time | time hidden at the end |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
        total = dict.fromkeys(KIND_FIELDS + ("time", "time_hidden", "time_no_pixel"), 0)
        unit = "ticks"
        rows = [k for k in order if k in kinds] + [k for k in kinds if k not in order]
        for kind in rows:
            v = kinds[kind]
            unit = v["unit"]
            for key in total:
                total[key] += v[key]
            print(row(kind, v, unit))
        print(row("**all**", total, unit))
        passes = head["passes"] or 1
        print(f"\n{name}: per sampled pass {total['draws'] / passes:.0f} draws; bound hidden at the pass's end "
              f"{share(total['hidden_at_end'], total['draws'])} of all draws ({share(total['hidden_at_end'], total['end_tested'])} of the tested), "
              f"{share(total['time_hidden'], total['time'])} of the draw time; no pixel at the draw {share(total['no_pixel'], total['draws'])}; "
              f"contradictions {total['contradictions']} of {total['hidden_at_draw']} bounds hidden at the draw, "
              f"failed controls {total['control_failed']}")
        print_extra(head["extra"], passes)
    return 0


def print_extra(extra, passes):
    """The classification of the draws without a pixel, per sampled pass."""
    if not extra:
        return
    classes = {k: v for k, v in extra.items() if k[0] == "class"}
    if classes:
        print("\nDraws without a pixel by class (draws / ms, per sampled pass):\n")
        print("| kind | " + " | ".join(CLASS_NAMES) + " |")
        print("|---|" + "---:|" * len(CLASS_NAMES))
        for kind in sorted({k[1] for k in classes}):
            cells = [classes.get(("class", kind, name), (0, 0.0)) for name in CLASS_NAMES]
            print(f"| {kind} | " + " | ".join(f"{c[0] / passes:.1f} / {c[1] / passes:.3f}" for c in cells) + " |")
    owners = {k: v for k, v in extra.items() if k[0] == "owner"}
    if owners:
        print("\nBy owner (per sampled pass):\n")
        print("| kind | owner | draws | without a pixel | time ms | ms without a pixel |")
        print("|---|---|---:|---:|---:|---:|")
        for key in sorted(owners):
            v = owners[key]
            print(f"| {key[1]} | {key[2]} | {v[0] / passes:.1f} | {v[1] / passes:.1f} | {v[2] / passes:.3f} | {v[3] / passes:.3f} |")
    radius = {k: v for k, v in extra.items() if k[0] == "radius"}
    if radius:
        print("\nBy the bound's radius (per sampled pass):\n")
        print("| radius | draws | without a pixel | time ms | ms without a pixel |")
        print("|---|---:|---:|---:|---:|")
        for name in ("below 64", "below 256", "below 1024", "below 4096", "4096 and above"):
            v = radius.get(("radius", name), (0, 0, 0.0, 0.0))
            print(f"| {name} | {v[0] / passes:.1f} | {v[1] / passes:.1f} | {v[3] / passes:.3f} | {v[2] / passes:.3f} |")
    rows = {k: v for k, v in extra.items() if k[0] == "pass"}
    if rows:
        print("\nBy shader pass id (per sampled pass; the passes with the most time without a pixel first):\n")
        print("| pass | draws | without a pixel | of those with the bound seen | bound hidden at the end | time ms | ms without a pixel |")
        print("|---|---:|---:|---:|---:|---:|---:|")
        for key, v in sorted(rows.items(), key=lambda item: -item[1][5])[:24]:
            print(f"| {key[1]} | {v[0] / passes:.1f} | {v[1] / passes:.1f} | {v[2] / passes:.1f} | {v[3] / passes:.1f} | {v[4] / passes:.3f} | "
                  f"{v[5] / passes:.3f} |")
    names = {k: v for k, v in extra.items() if k[0] == "name"}
    if names:
        print("\nBy geometry name (only the names the probe listed, its top 16 a block; per sampled pass):\n")
        print("| name | kind | owner | draws | without a pixel | ms without a pixel |")
        print("|---|---|---|---:|---:|---:|")
        for key, v in sorted(names.items(), key=lambda item: -item[1][3])[:24]:
            print(f"| {key[1] or '(none)'} | {key[2]} | {key[3]} | {v[0] / passes:.1f} | {v[1] / passes:.1f} | {v[3] / passes:.3f} |")


def row(kind, v, unit):
    return (f"| {kind} | {v['draws']} | {v['testable']} | {v['near']} | {v['outside']} | {v['no_bound']} | "
            f"{v['no_pixel']} ({share(v['no_pixel'], v['draws'])}) | {v['hidden_at_draw']} | "
            f"{v['hidden_at_end']} of {v['end_tested']} ({share(v['hidden_at_end'], v['end_tested'])}) | {v['contradictions']} | {v['control_failed']} | "
            f"{v['time']:.1f} {unit} | {v['time_hidden']:.1f} {unit} ({share(v['time_hidden'], v['time'])}) |")


if __name__ == "__main__":
    sys.exit(main())
