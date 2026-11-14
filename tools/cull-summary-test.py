#!/usr/bin/env python3
"""Checks for tools/cull-summary.py: the windows of a place summed, acting and
test-only windows apart ([Route] OcclusionCullAct), and logs of before the key.
Run with: python3 tools/cull-summary-test.py"""
import importlib.util
import sys
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("cull_summary", here / "cull-summary.py")
cs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cs)

failures = 0


def check(condition, what):
    global failures
    print("  %-5s %s" % ("ok" if condition else "FAIL", what))
    if not condition:
        failures += 1


def line(qpc, passes, draws, skipped, pass_ms, tail=""):
    return (f"Route: occlusion cull - {passes} eye passes; totals qpc {qpc} passes {passes} draws {draws} skipped {skipped} "
            f"tests 100 hidden 40 visible 60 skipsConfirmed {skipped} skipsUnconfirmed 0 passMs {pass_ms:.3f} cullMs 1.000{tail}")


print("the windows read")
with tempfile.TemporaryDirectory() as folder:
    log = Path(folder) / "OBNG.log"
    log.write_text("\n".join([
        line(100, 10, 1000, 300, 20.0),                              # a log of before the key
        line(200, 10, 1000, 300, 20.0, " heldBack 0 acted 1"),
        line(300, 10, 1000, 0, 30.0, " heldBack 310 acted 0"),
        "Route: something else",
    ]))
    windows = cs.read_windows(log)
check(len(windows) == 3, "three culler lines, the others left out")
check(windows[0]["acted"] and windows[0]["held_back"] == 0, "a line without the two fields: an acting window, nothing held back")
check(windows[1]["acted"] and not windows[2]["acted"] and windows[2]["held_back"] == 310, "acted 1 and acted 0 with what was held back")
check(windows[2]["pass_ms"] == 30.0 and windows[2]["qpc"] == 300, "the fields before them as ever")

print("a place's windows, the two kinds apart")
windows = [
    {"qpc": 100, "acted": True},
    dict(qpc=200, passes=10, draws=1000, skipped=300, tests=100, hidden=40, visible=60, skips_confirmed=300, skips_unconfirmed=0,
         pass_ms=20.0, cull_ms=1.0, held_back=0, acted=True),
    dict(qpc=300, passes=10, draws=1000, skipped=0, tests=100, hidden=40, visible=60, skips_confirmed=0, skips_unconfirmed=0,
         pass_ms=30.0, cull_ms=1.0, held_back=310, acted=False),
    dict(qpc=400, passes=20, draws=2000, skipped=600, tests=200, hidden=80, visible=120, skips_confirmed=600, skips_unconfirmed=0,
         pass_ms=42.0, cull_ms=2.0, held_back=0, acted=True),
    dict(qpc=900, passes=10, draws=1000, skipped=0, tests=100, hidden=40, visible=60, skips_confirmed=0, skips_unconfirmed=0,
         pass_ms=99.0, cull_ms=1.0, held_back=1, acted=False),
]
totals = cs.place_totals(windows, 100, 450)
check(set(totals) == {True, False}, "both kinds of window inside the place")
check(totals[True]["passes"] == 30 and totals[True]["pass_ms"] == 62.0 and totals[True]["skipped"] == 900, "the acting windows summed")
check(totals[False]["passes"] == 10 and totals[False]["held_back"] == 310, "the test-only window alone; the one past the place's end left out")
check(cs.place_totals(windows, 150, 450).get(True, {}).get("passes") == 20, "a window that began before the place: left out")
check(cs.place_totals(windows, 1000, 2000) == {}, "no window inside: nothing")
gain = cs.skips_gain(totals)
check(gain is not None and abs(gain[0] - 3.0) < 1e-9 and abs(gain[1] - 62.0 / 30) < 1e-9 and abs(gain[2] - (3.0 - 62.0 / 30)) < 1e-9,
      "the pass without the skips, with them, and the difference")
check(cs.skips_gain({True: totals[True]}) is None and cs.skips_gain({False: totals[False]}) is None, "one kind of window only: no difference to give")

print("the rows")
acting = cs.row("market (acting)", totals[True], True)
check(acting.startswith("| market (acting) | 30 | 100 | 30 | 30 % | 2.067 |"), "an acting row: the skips a pass, the share, the pass ms")
test_only = cs.row("market (test only)", totals[False], False)
check(test_only.startswith("| market (test only) | 10 | 100 | 31 | 31 % | 3.000 |"), "a test-only row: what was held back in the skips' place")

if failures:
    print("%d check(s) failed" % failures)
    sys.exit(1)
print("all checks passed")
