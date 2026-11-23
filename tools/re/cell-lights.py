#!/usr/bin/env python3
"""The light references placed in one interior cell, by plugin: every REFR under the cell's groups whose base is a
LIGH record of the load order, with the light's editor id and radius - which mod puts how many lights into a place.
Usage: cell-lights.py <Data folder> <Plugins.txt> <cell form id hex of Oblivion.esm>"""
import os, struct, sys, zlib
data_dir, plugins_txt, cell = sys.argv[1], sys.argv[2], int(sys.argv[3], 16) & 0xFFFFFF
def subs(body):
    r = 0
    while r + 6 <= len(body):
        t, n = body[r:r + 4], struct.unpack_from('<H', body, r + 4)[0]
        yield t, body[r + 6:r + 6 + n]
        r += 6 + n
def read(path):
    d = open(path, 'rb').read()
    size = struct.unpack_from('<I', d, 4)[0]
    masters = [v.rstrip(b'\0').decode('latin-1') for t, v in subs(d[20:20 + size]) if t == b'MAST']
    lights, refs = {}, []
    def walk(p, end, in_cell):
        while p + 20 <= end:
            tag = d[p:p + 4]
            if tag == b'GRUP':
                gsize, label, gtype = struct.unpack_from('<I4sI', d, p + 4)
                inside = in_cell
                if gtype in (6, 8, 9, 10):   # a cell's children: the label is the cell's form id
                    inside = (struct.unpack('<I', label)[0] & 0xFFFFFF) == cell and (struct.unpack('<I', label)[0] >> 24) == 0
                if gtype == 0 and label not in (b'CELL', b'LIGH'):
                    p += gsize
                    continue
                walk(p + 20, p + gsize, inside)
                p += gsize
                continue
            size, flags, form = struct.unpack_from('<III', d, p + 4)
            if tag == b'LIGH' or (tag == b'REFR' and in_cell):
                body = d[p + 20:p + 20 + size]
                if flags & 0x00040000:
                    body = zlib.decompress(body[4:])
                rec = dict(subs(body))
                if tag == b'LIGH' and b'DATA' in rec and len(rec[b'DATA']) >= 8:
                    lights[form] = (rec.get(b'EDID', b'').rstrip(b'\0').decode('latin-1'), struct.unpack_from('<I', rec[b'DATA'], 4)[0])
                elif tag == b'REFR' and b'NAME' in rec:
                    refs.append((form, struct.unpack('<I', rec[b'NAME'])[0], bool(flags & 0x20)))
            p += 20 + size
    walk(20 + size, len(d), False)
    return masters, lights, refs
order = [l.strip() for l in open(plugins_txt, errors='replace') if l.strip() and not l.startswith('#')]
order = [o for o in order if o.lower().endswith('.esm')] + [o for o in order if not o.lower().endswith('.esm')]
files = {f.lower(): f for f in os.listdir(data_dir)}
known, placed = {}, {}   # (owner, local) -> (edid, radius); (owner, local ref) -> (plugin, base key, deleted)
for name in order:
    if name.lower() not in files:
        continue
    masters, lights, refs = read(os.path.join(data_dir, files[name.lower()]))
    key = lambda form: (masters[form >> 24].lower() if (form >> 24) < len(masters) else name.lower(), form & 0xFFFFFF)
    for form, value in lights.items():
        known[key(form)] = value
    for form, base, deleted in refs:
        placed[key(form)] = (name, key(base), deleted)
by = {}
for ref, (name, base, deleted) in placed.items():
    if base in known and not deleted:
        by.setdefault(name, []).append(known[base])
total = sum(len(v) for v in by.values())
print(f'cell {cell:06X}: {total} light references in the load order (the last plugin to touch a reference owns it here)')
for name, rows in by.items():
    kinds = {}
    for edid, radius in rows:
        kinds[(edid, radius)] = kinds.get((edid, radius), 0) + 1
    print(f'  {name}: {len(rows)} lights, radius sum {sum(r for _, r in rows)}')
    for (edid, radius), n in sorted(kinds.items(), key=lambda kv: -kv[1])[:8]:
        print(f'     {n:3d} x {edid} (radius {radius})')
