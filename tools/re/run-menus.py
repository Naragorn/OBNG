#!/usr/bin/env python3
"""When a menu was open in a harness run, against the hand script's marks: OBVR.log's `Hands: menu <name>` and
`Hands: no menu` lines placed between the marks around them. Behind an open menu OBVR pauses the world and shades
it, so a picture or a measured phase with a menu in it is not the game's. Usage: run-menus.py <run folder>...
  --from <mark> --to <mark>   exit 1 when a menu was open between those two marks (for a picture gate)"""
import os, re, sys


def opt(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def spans(run):
    """([(mark, seconds)], [(menu, from seconds, to seconds or None)]) of a run; a menu's times are placed by its
    log line's position between the marks before and after it."""
    marks, events = [], []
    for i, text in enumerate(open(os.path.join(run, 'OBVR.log'), errors='replace')):
        m = re.search(r'HandScript: mark (\S+) \(t=([\d.]+)', text)
        if m:
            marks.append((i, m.group(1), float(m.group(2))))
        m = re.search(r'Hands: menu (\w+) \(0x[0-9A-F]+\)', text)
        if m and m.group(1) != 'none':
            events.append((i, m.group(1)))
        elif 'Hands: no menu' in text or 'Hands: menu none' in text:
            events.append((i, None))

    def when(line):
        before = [k for k in marks if k[0] < line]
        after = [k for k in marks if k[0] > line]
        if not before:
            return None
        if not after:
            return before[-1][2]
        b, a = before[-1], after[0]
        return b[2] + (line - b[0]) / (a[0] - b[0]) * (a[2] - b[2])

    found, open_menu = [], None
    for line, menu in events:
        t = when(line)
        if t is None:
            continue
        if menu is not None and open_menu is None:
            open_menu = (menu, t)
        elif menu is None and open_menu is not None:
            found.append((open_menu[0], open_menu[1], t))
            open_menu = None
    if open_menu is not None:
        found.append((open_menu[0], open_menu[1], None))
    return [(name, t) for _, name, t in marks], found


def main():
    first, last = opt('--from'), opt('--to')
    runs = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith('--') and sys.argv[i - 1] not in ('--from', '--to')]
    bad = False
    for run in runs:
        marks, found = spans(run)
        times = dict(marks)
        print(f"{run}: {len(marks)} marks, {len(found)} menus after the first mark")
        for menu, a, b in found:
            print(f"   {menu:10s} from {a:6.1f} s to {'the end' if b is None else f'{b:6.1f} s'}")
        if first in times and last in times:
            inside = [f for f in found if f[1] < times[last] and (f[2] is None or f[2] > times[first])]
            print(f"   between {first} ({times[first]:.1f} s) and {last} ({times[last]:.1f} s): " + ('no menu' if not inside else 'MENU OPEN - ' + ', '.join(f[0] for f in inside)))
            bad = bad or bool(inside)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
