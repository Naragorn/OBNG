#!/usr/bin/env python3
"""Sums the occlusion culler's summary windows of one run per place.

    tools/cull-summary.py artifacts/shadow/obng-cull/<run> [<run> ...]

OBNG.log carries the culler's lines ("Route: occlusion cull ... ; totals qpc N
passes P draws D skipped S tests T hidden H visible V skipsConfirmed C skipsUnconfirmed U
passMs A cullMs B", the counts since the last line); OBVR.log carries the marks
with the same clock ("HandScript: mark <name> (... tick N)"). A place is the
span from a mark <name> to the mark <name>-done; a window belongs to it when
its qpc and the previous window's qpc both lie inside the span. Per place:
eye passes, draws and skips per pass, the eye pass's milliseconds per pass
(the culler's own work included and alone), the share of the tests found
hidden, and the share of the skips the same pass's test left unconfirmed (the
popping's upper bound).

With [Route] OcclusionCullAct=0 or 2 a window's totals end "heldBack N acted
0|1": a place that has test-only windows gets a row for each kind of window
- "acting" and "test only" (its skipped column then what was decided hidden
and drawn all the same) - and a line with the eye pass's time without and
with the skips.
"""

import re
import sys
from pathlib import Path

TOTALS = re.compile(
    r"Route: occlusion cull.*; totals qpc (\d+) passes (\d+) draws (\d+) skipped (\d+) tests (\d+) hidden (\d+) visible (\d+) "
    r"skipsConfirmed (\d+) skipsUnconfirmed (\d+) passMs ([\d.]+) cullMs ([\d.]+)(?: heldBack (\d+) acted (\d))?"
)
FIELDS = ("qpc", "passes", "draws", "skipped", "tests", "hidden", "visible", "skips_confirmed", "skips_unconfirmed", "pass_ms", "cull_ms")
SUMMED = FIELDS[1:] + ("held_back",)
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
            values = dict(zip(FIELDS, (float(x) if "." in x else int(x) for x in m.groups()[: len(FIELDS)])))
            # a log of before OcclusionCullAct has neither: every window acted
            values["held_back"] = int(m.group(len(FIELDS) + 1) or 0)
            values["acted"] = m.group(len(FIELDS) + 2) != "0"
            windows.append(values)
    return windows


def place_totals(windows, start, end):
    """The windows wholly inside start..end summed, the acting ones and the
    test-only ones apart: {True: totals, False: totals} with the kinds that
    have a window."""
    totals = {}
    for previous, window in zip(windows, windows[1:]):
        if start <= previous["qpc"] and window["qpc"] <= end:
            total = totals.setdefault(window["acted"], {f: 0 for f in SUMMED})
            for f in SUMMED:
                total[f] += window[f]
    return totals


def row(name, total, acted):
    passes = total["passes"] or 1
    tests = total["hidden"] + total["visible"]
    skips = total["skips_confirmed"] + total["skips_unconfirmed"]
    unconfirmed = f"{total['skips_unconfirmed'] * 100 / skips:.1f} % of {skips}" if skips else "-"
    left_out = total["skipped"] if acted else total["held_back"]
    return (
        f"| {name} | {total['passes']} | {total['draws'] / passes:.0f} | {left_out / passes:.0f} | "
        f"{left_out * 100 / (total['draws'] or 1):.0f} % | {total['pass_ms'] / passes:.3f} | {total['cull_ms'] / passes:.3f} | "
        f"{total['hidden'] * 100 / (tests or 1):.0f} % of {tests} | {unconfirmed} |"
    )


def skips_gain(totals):
    """(eye pass ms test only, acting, the difference) of a place with both kinds of window, else None."""
    if True not in totals or False not in totals or not totals[True]["passes"] or not totals[False]["passes"]:
        return None
    without = totals[False]["pass_ms"] / totals[False]["passes"]
    with_skips = totals[True]["pass_ms"] / totals[True]["passes"]
    return without, with_skips, without - with_skips


def summarise(run):
    run = Path(run)
    marks = read_marks(run / "OBVR.log")
    windows = read_windows(run / "OBNG.log")
    if not windows:
        print(f"{run}: no culler lines (the culler off, or the DLL without it)")
        return
    places = sorted((name for name in marks if f"{name}-done" in marks), key=lambda n: marks[n])
    print(f"# {run.name}: {len(windows)} windows, places {', '.join(places)}")
    print("| place | eye passes | draws/pass | skipped/pass | skipped | eye pass ms | culler ms | tests hidden | skips unconfirmed |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for name in places:
        totals = place_totals(windows, marks[name], marks[f"{name}-done"])
        if not totals:
            print(f"| {name} | 0 | - | - | - | - | - | - | - |")
            continue
        split = False in totals
        for acted in (True, False):
            if acted in totals:
                print(row(f"{name} ({'acting' if acted else 'test only'})" if split else name, totals[acted], acted))
        gain = skips_gain(totals)
        if gain:
            without, with_skips, saved = gain
            print(f"{name}: the eye pass {without:.3f} ms without the skips, {with_skips:.3f} ms with them - "
                  f"{saved:+.3f} ms saved ({saved * 100 / without if without else 0:.0f} %)")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for run in argv[1:]:
        summarise(run)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
