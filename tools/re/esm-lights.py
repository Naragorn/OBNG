#!/usr/bin/env python3
"""The light records (LIGH) of a load order: for every light a plugin overrides or adds, its radius, colour and flags
beside the master's - which mod makes lights reach further. Plugins in load order from a Plugins.txt, read from a
Data folder; a light's radius is DATA's second dword (time, radius, colour, flags, falloff, fov; UESP's TES4 LIGH).
Usage: esm-lights.py <Data folder> <Plugins.txt> [--all]   (--all: also lights whose radius did not change)"""
import os, struct, sys, zlib
data_dir, plugins_txt = sys.argv[1], sys.argv[2]
show_all = '--all' in sys.argv
def records(path, want=b'LIGH'):
    d = open(path, 'rb').read()
    masters, p = [], 0
    # the header record
    size = struct.unpack_from('<I', d, 4)[0]
    body = d[20:20 + size]
    q = 0
    while q + 6 <= len(body):
        tag, n = body[q:q + 4], struct.unpack_from('<H', body, q + 4)[0]
        if tag == b'MAST':
            masters.append(body[q + 6:q + 6 + n].rstrip(b'\0').decode('latin-1'))
        q += 6 + n
    p = 20 + size
    out = {}
    while p + 20 <= len(d):
        if d[p:p + 4] != b'GRUP':
            break
        gsize, label, gtype = struct.unpack_from('<I4sI', d, p + 4)
        if gtype == 0 and label == want:
            q, end = p + 20, p + gsize
            while q + 20 <= end:
                tag, size, flags, form = struct.unpack_from('<4sIII', d, q)
                body = d[q + 20:q + 20 + size]
                if flags & 0x00040000:
                    body = zlib.decompress(body[4:])
                rec, r = {}, 0
                while r + 6 <= len(body):
                    t, n = body[r:r + 4], struct.unpack_from('<H', body, r + 4)[0]
                    rec[t] = body[r + 6:r + 6 + n]
                    r += 6 + n
                out[form] = rec
                q += 20 + size
        p += gsize
    return masters, out
order = [l.strip() for l in open(plugins_txt, errors='replace') if l.strip() and not l.startswith('#')]
files = {f.lower(): f for f in os.listdir(data_dir)}
# masters first (the game loads .esm before .esp), each kind in the list's order
order = [o for o in order if o.lower().endswith('.esm')] + [o for o in order if not o.lower().endswith('.esm')]
seen = {}   # (owner plugin lower, local id) -> (radius, plugin)
changes = []
for name in order:
    if name.lower() not in files:
        continue
    try:
        masters, lights = records(os.path.join(data_dir, files[name.lower()]))
    except Exception as e:
        print(f'  ({name}: not read, {e})')
        continue
    for form, rec in lights.items():
        if b'DATA' not in rec or len(rec[b'DATA']) < 16:
            continue
        index = form >> 24
        owner = masters[index].lower() if index < len(masters) else name.lower()
        key = (owner, form & 0xFFFFFF)
        time, radius, colour, flags = struct.unpack_from('<iIII', rec[b'DATA'], 0)
        edid = rec.get(b'EDID', b'').rstrip(b'\0').decode('latin-1')
        before = seen.get(key)
        if before is not None and (show_all or before[0] != radius):
            changes.append((name, edid, f'{key[1]:06X}', before[0], radius, before[1]))
        elif before is None and owner == name.lower() and show_all:
            changes.append((name, edid, f'{key[1]:06X}', None, radius, ''))
        seen[key] = (radius, name)
by = {}
for c in changes:
    by.setdefault(c[0], []).append(c)
for name, rows in by.items():
    bigger = sum(1 for r in rows if r[3] is not None and r[4] > r[3])
    print(f'{name}: {len(rows)} lights changed, {bigger} with a larger radius')
    for r in sorted(rows, key=lambda r: -(r[4] - (r[3] or 0)))[:12]:
        print(f'   {r[1]:32s} {r[2]}  radius {r[3]} -> {r[4]}  (before: {r[5]})')
if not changes:
    print('no light of a master has its radius changed by a later plugin')
