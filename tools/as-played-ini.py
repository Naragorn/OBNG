#!/usr/bin/env python3
"""Writes a test INI that is Nadi's Windows OBNG.ini - the routes as played - with the given keys set over it.
The Linux game's OBNG.ini is the harness's base and has fewer routes on (no occlusion culler, no instancing, no
far actors, no volatile pack on 2026-11-23), so a measuring INI copied from it does not show the game as played.
Usage: as-played-ini.py <out file> [Section.Key=Value ...]
  e.g. as-played-ini.py /tmp/x.ini Probe.GpuPasses=1 Log.SummaryTimer=1 Log.SummaryEverySeconds=10 Route.OcclusionCull=0"""
import re, sys
WINDOWS = '/run/media/nadi/Programs/Steam/steamapps/common/Oblivion/Data/OBSE/Plugins/OBNG.ini'
out, sets = sys.argv[1], sys.argv[2:]
text = open(WINDOWS, errors='replace').read().replace('\r\n', '\n')
for item in sets:
    where, value = item.split('=', 1)
    section, key = where.split('.', 1)
    m = re.search(r'^\[' + re.escape(section) + r'\]\n', text, re.M)
    if not m:
        text += f'\n[{section}]\n'
        m = re.search(r'^\[' + re.escape(section) + r'\]\n', text, re.M)
    start = m.end()
    nxt = re.search(r'^\[', text[start:], re.M)
    end = start + (nxt.start() if nxt else len(text) - start)
    body = text[start:end]
    if re.search(r'^' + re.escape(key) + r'=', body, re.M):
        body = re.sub(r'^' + re.escape(key) + r'=.*$', f'{key}={value}', body, flags=re.M)
    else:
        body = f'{key}={value}\n' + body
    text = text[:start] + body + text[end:]
open(out, 'w').write('; Nadi\'s Windows OBNG.ini (tools/as-played-ini.py)' + (' with ' + ', '.join(sets) if sets else '') + '\n' + text)
