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
    r"\((\d+) of them drew no pixel\); contradictions (\d+); draw time ([\d.]+) (ms|ticks), ([\d.]+) (?:ms|ticks) hidden at the end, "
    r"([\d.]+) (?:ms|ticks) without a pixel"
)
MARK = re.compile(r"HandScript: mark (\S+) \(.*tick (\d+)\)")
KIND_FIELDS = (
    "draws", "testable", "near", "outside", "no_bound", "viewport", "no_pixel", "hidden_at_draw", "hidden_at_end", "end_tested",
    "hidden_end_no_pixel", "contradictions",
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
            values = dict(zip(KIND_FIELDS, (int(x) for x in m.groups()[1:13])))
            values["unit"] = m.group(15)
            values["time"] = float(m.group(14))
            values["time_hidden"] = float(m.group(16))
            values["time_no_pixel"] = float(m.group(17))
            blocks[-1]["kinds"][m.group(1)] = values
    return blocks


def places(marks):
    """(name, from tick, to tick) for every mark that has a -done partner, in time order."""
    spans = [(name, tick, marks[name + "-done"]) for name, tick in marks.items() if name + "-done" in marks]
    return sorted(spans, key=lambda s: s[1])


def sum_place(blocks, start, end):
    head = dict.fromkeys(HEAD_FIELDS, 0)
    kinds = {}
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
              "bound hidden at the end (of tested) | contradictions | draw time | time hidden at the end |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
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
              f"contradictions {total['contradictions']} of {total['hidden_at_draw']} bounds hidden at the draw")
    return 0


def row(kind, v, unit):
    return (f"| {kind} | {v['draws']} | {v['testable']} | {v['near']} | {v['outside']} | {v['no_bound']} | "
            f"{v['no_pixel']} ({share(v['no_pixel'], v['draws'])}) | {v['hidden_at_draw']} | "
            f"{v['hidden_at_end']} of {v['end_tested']} ({share(v['hidden_at_end'], v['end_tested'])}) | {v['contradictions']} | "
            f"{v['time']:.1f} {unit} | {v['time_hidden']:.1f} {unit} ({share(v['time_hidden'], v['time'])}) |")


if __name__ == "__main__":
    sys.exit(main())
