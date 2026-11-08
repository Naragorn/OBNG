#!/usr/bin/env python3
"""Sums the occlusion culler's summary windows of one run per place.

    tools/cull-summary.py artifacts/shadow/obng-cull/<run> [<run> ...]

OBNG.log carries the culler's lines ("Route: occlusion cull ... ; totals qpc N
passes P draws D skipped S tests T hidden H visible V checkDrawn C checkVisible X
passMs A cullMs B", the counts since the last line); OBVR.log carries the marks
with the same clock ("HandScript: mark <name> (... tick N)"). A place is the
span from a mark <name> to the mark <name>-done; a window belongs to it when
its qpc and the previous window's qpc both lie inside the span. Per place:
eye passes, draws and skips per pass, the eye pass's milliseconds per pass
(the culler's own work included and alone), the share of the tests found
hidden, and in check mode the share of the skipped draws that put pixels down.
"""

import re
import sys
from pathlib import Path

TOTALS = re.compile(
    r"Route: occlusion cull.*; totals qpc (\d+) passes (\d+) draws (\d+) skipped (\d+) tests (\d+) hidden (\d+) visible (\d+) "
    r"checkDrawn (\d+) checkVisible (\d+) passMs ([\d.]+) cullMs ([\d.]+)"
)
FIELDS = ("qpc", "passes", "draws", "skipped", "tests", "hidden", "visible", "check_drawn", "check_visible", "pass_ms", "cull_ms")
MARK = re.compile(r"HandScript: mark (\S+) \(.*tick (\d+)\)")


def read_marks(log):
    marks = {}
    for line in log.read_text(errors="replace").splitlines():
        m = MARK.search(line)
        if m:
            marks[m.group(1)] = int(m.group(2))
    return marks


def read_windows(log):
    windows = []
    for line in log.read_text(errors="replace").splitlines():
        m = TOTALS.search(line)
        if m:
            values = dict(zip(FIELDS, (float(x) if "." in x else int(x) for x in m.groups())))
            windows.append(values)
    return windows


def summarise(run):
    run = Path(run)
    marks = read_marks(run / "OBVR.log")
    windows = read_windows(run / "OBNG.log")
    if not windows:
        print(f"{run}: no culler lines (the culler off, or the DLL without it)")
        return
    places = sorted((name for name in marks if f"{name}-done" in marks), key=lambda n: marks[n])
    print(f"# {run.name}: {len(windows)} windows, places {', '.join(places)}")
    print("| place | eye passes | draws/pass | skipped/pass | skipped | eye pass ms | culler ms | tests hidden | check: skips visible |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for name in places:
        start, end = marks[name], marks[f"{name}-done"]
        total = {f: 0 for f in FIELDS}
        for previous, window in zip(windows, windows[1:]):
            if start <= previous["qpc"] and window["qpc"] <= end:
                for f in FIELDS[1:]:
                    total[f] += window[f]
        passes = total["passes"] or 1
        tests = total["hidden"] + total["visible"]
        check = f"{total['check_visible'] * 100 / total['check_drawn']:.1f} % of {total['check_drawn']}" if total["check_drawn"] else "-"
        print(
            f"| {name} | {total['passes']} | {total['draws'] / passes:.0f} | {total['skipped'] / passes:.0f} | "
            f"{total['skipped'] * 100 / (total['draws'] or 1):.0f} % | {total['pass_ms'] / passes:.3f} | {total['cull_ms'] / passes:.3f} | "
            f"{total['hidden'] * 100 / (tests or 1):.0f} % of {tests} | {check} |"
        )


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for run in argv[1:]:
        summarise(run)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
