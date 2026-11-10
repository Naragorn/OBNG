#!/usr/bin/env python3
"""Runs a command with keys of the Linux game's Oblivion.ini changed and puts
the file back byte for byte afterwards, whatever the command did. A key must
exist already (it is replaced where it stands; nothing is added).
Usage: run-with-game-ini.py iMultiSample=2 [Key=Value ...] -- command [args...]"""
import os, re, subprocess, sys
INI = os.path.expanduser('~/.local/share/Steam/steamapps/compatdata/22330/pfx/drive_c/users/steamuser/Documents/My Games/Oblivion/Oblivion.ini')
args = sys.argv[1:]
if '--' not in args:
    sys.exit(__doc__)
split = args.index('--')
changes, command = args[:split], args[split + 1:]
original = open(INI, 'rb').read()
text = original.decode('latin1')
for change in changes:
    key, value = change.split('=', 1)
    pattern = re.compile(r'(?mi)^(' + re.escape(key) + r')=[^\r\n]*')
    if not pattern.search(text):
        sys.exit(f"{key} is not in {INI}")
    text = pattern.sub(lambda m: m.group(1) + '=' + value, text)
    print(f"Oblivion.ini for this run: {key}={value}")
code = 1
try:
    open(INI, 'wb').write(text.encode('latin1'))
    code = subprocess.call(command)
finally:
    open(INI, 'wb').write(original)
    print("Oblivion.ini put back" if open(INI, 'rb').read() == original else "Oblivion.ini NOT restored")
sys.exit(code)
