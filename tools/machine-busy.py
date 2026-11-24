#!/usr/bin/env python3
"""What keeps the machine busy for a measuring run, a line each: a running Oblivion, a running Blade & Sorcery (the
other sessions' game) or a harness run. Nothing printed and exit 1: the machine is free.

By the processes' own argument lists, not by pgrep: under Proton the game's process name is not "Oblivion.exe"
(`pgrep -x` never sees it), and `pgrep -f hand-script-run.py` also finds every shell whose command text only names
the script - a waiting loop of another session had waited on itself for hours that way (2026-11-24).

Usage: machine-busy.py [--others]     --others leaves out Oblivion and the harness (what runs beside an own run)"""
import glob
import sys

others = '--others' in sys.argv
found = False
for path in glob.glob('/proc/[0-9]*/cmdline'):
    try:
        argv = open(path, 'rb').read().split(b'\0')
    except OSError:
        continue
    name = argv[0].decode(errors='replace').replace('\\', '/').rsplit('/', 1)[-1].lower()
    pid = path.split('/')[2]
    if name == 'oblivion.exe':
        kind = 'oblivion'
    elif name == 'bladeandsorcery.exe':
        kind = 'bladeandsorcery'
    elif name.startswith('python') and any(a.decode(errors='replace').endswith('hand-script-run.py') for a in argv[1:3]):
        kind = 'harness'
    else:
        continue
    if others and kind in ('oblivion', 'harness'):
        continue
    found = True
    print(kind, pid)
sys.exit(0 if found else 1)
