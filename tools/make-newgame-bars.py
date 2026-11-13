#!/usr/bin/env python3
"""Writes the hand script of the new game's prison bars test to stdout.

The way into the prison is tools/hand-scripts/obng-cull-newgame-bars.txt's (New, Yes, the movie, the name, the
race menu's Done, Yes), taken from that file up to its `mark prison`. Then the player steps back towards the cell's
door, Valen Dreth's scene is started from the console (stage 6 of CharacterGen) and the head looks through the bars
at the cell opposite while it sways sideways and leans, both eyes dumped at every mark: the inmate opposite and,
later, the guards, behind the bars - what OBNG's occlusion cull got wrong on Windows (Nadi, 2026-11-11 and -12:
blue floor and missing body parts behind the bars only).

  tools/make-newgame-bars.py [--skip-intro 0|1] [--back SECONDS] [--rounds N] > script.txt

--skip-intro 1 (the default) presses Escape four seconds into the intro movie instead of after twenty-five.
--inside UNITS: the player placed that far inside the cell from its door by the console (exact; the stick is not).
--guards SECONDS: wait that long at the door before the sway, for the Emperor and the Blades to come and stand there.
--forward / --back: how long the left stick is held forwards (away from the door) or backwards from where the game
puts the player, which is at the door.
"""
import argparse
import sys
from pathlib import Path

HEAD = """# Written by tools/make-newgame-bars.py (skip-intro {skip}, back {back} s, forward {forward} s, {rounds} rounds) - do not edit by hand.
# Run with the Windows mod list:
#   python3 tools/run-with-windows-mods.py -- tools/shadow-run.sh --ini <an OBNG INI> <this file> \\
#     --save continue --run-timeout 480
"""

# The synthetic head's places in tracking space (metres: x right, y up, z back), the standing eye at 0 1.6 0.
SWAY = [(0.0, 1.6, 0.0), (0.04, 1.6, 0.0), (0.08, 1.6, 0.0), (0.04, 1.6, -0.05), (0.0, 1.6, -0.10), (-0.04, 1.6, -0.05),
        (-0.08, 1.6, 0.0), (-0.04, 1.58, 0.0), (0.0, 1.56, 0.0), (0.02, 1.6, 0.0)]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--skip-intro", type=int, default=1)
    p.add_argument("--back", type=float, default=0.0)
    p.add_argument("--forward", type=float, default=0.1)
    p.add_argument("--rounds", type=int, default=6)
    p.add_argument("--guards", type=float, default=0.0,
                   help="seconds to wait at the door for the Emperor and the Blades before the sway (a mark every ten seconds)")
    p.add_argument("--inside", type=float, default=-1.0,
                   help="place the player this many units inside the cell from its door with player.setpos (the stick is then not used)")
    a = p.parse_args()
    base = (Path(__file__).parent / "hand-scripts" / "obng-cull-newgame-bars.txt").read_text().splitlines()
    start = next(i for i, l in enumerate(base) if l.startswith("start main-menu"))
    end = next(i for i, l in enumerate(base) if l.strip() == "mark prison")
    way = [l.replace("EyeDumpFrames=12", "EyeDumpFrames=2") for l in base[start:end + 1] if not l.startswith("console-at t") and not l.startswith("console-at at-door")]
    if a.skip_intro:
        i = next(i for i, l in enumerate(way) if l.strip() == "mark movie")
        j = next(i for i, l in enumerate(way) if l.strip() == "key 0x1B 1")
        way[i - 1:j] = ["wait 4", "mark movie"]  # Escape right after: the lines from `key 0x1B 1` on stay
    out = [HEAD.format(skip=a.skip_intro, back=a.back, forward=a.forward, rounds=a.rounds)]
    out += way
    # The game puts the player at the cell's door (358 -109, measured 2026-11-13: three seconds backwards move three
    # units); with the head turned to the bars the eyes stand in them. So a step forward, away from the door, first.
    # The door's spot and the way into the cell (measured 2026-11-13: the player at the door 358.3 -108.6 -38.7,
    # deep in the cell 567.9 -53.6 -108.0; the heading 76.2 degrees looks into the cell). Placed a little above the
    # door's floor: the cell's floor falls away behind the door and the player drops onto it.
    if a.inside >= 0:
        dx, dy = 0.971, 0.239
        x, y, z = 358.3 + a.inside * dx, -108.6 + a.inside * dy, -30.0
        a.back = a.forward = 0
        place = [f"console-at prison player.setpos x {x:.1f}", f"console-at prison player.setpos y {y:.1f}", f"console-at prison player.setpos z {z:.1f}"]
    else:
        place = []
    if a.back > 0:
        out += ["left stick 0 -1", f"wait {a.back}", "left stick 0 0", "wait 1"]
    if a.forward > 0:
        out += ["left stick 0 1", f"wait {a.forward}", "left stick 0 0", "wait 1"]
    out += ["wait 2", "mark at-door", "head yaw 180", "wait 1", "mark bars-before"]
    consoles = place + ["console-at at-door setstage charactergen 6"]
    # The Emperor and the Blades come down the hall once Valen Dreth has spoken (stage 9 of CharacterGen; in the
    # run of 2026-11-11 they stood at the door sixty seconds after stage 6) and wait there: the pictures from then
    # on show four actors in armour through the bars.
    waited = 0
    while waited < a.guards:
        waited += 10
        out += ["wait 9.5", f"mark w{waited:03d}", "wait 0.5"]
        consoles.append(f"console-at w{waited:03d} CloseAllMenus")
    for r in range(a.rounds):
        for k, (x, y, z) in enumerate(SWAY):
            name = f"r{r}-s{k}"
            out += [f"head pos {x} {y} {z}", "wait 0.4", f"mark {name}", "wait 0.6"]
        consoles.append(f"console-at r{r}-s0 CloseAllMenus")
    out += ["mark done"]
    text = "\n".join(out)
    # The runner's lines go with the others at the top (after the first console-at).
    text = text.replace("console-at prison CloseAllMenus", "console-at prison CloseAllMenus\n" + "\n".join(consoles), 1)
    sys.stdout.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
