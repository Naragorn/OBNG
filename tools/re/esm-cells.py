#!/usr/bin/env python3
"""Cells of a worldspace in a TES4 plugin: editor id, grid coordinates, and
the references in them whose editor id or base matches a filter (with their
positions). For writing hand scripts (cow <world> x y, player.setpos).
Usage: esm-cells.py <plugin> <worldspace editor id> [reference filter regex]"""
import re, struct, sys, zlib
data = open(sys.argv[1], 'rb').read()
want = sys.argv[2].lower()
flt = re.compile(sys.argv[3], re.I) if len(sys.argv) > 3 else None
def subs(body):
    p = 0
    while p + 6 <= len(body):
        t = body[p:p + 4]; l = struct.unpack_from('<H', body, p + 4)[0]
        yield t, body[p + 6:p + 6 + l]
        p += 6 + l
def record(p):
    t = data[p:p + 4]; size, flags, form = struct.unpack_from('<III', data, p + 4)
    body = data[p + 20:p + 20 + size]
    if flags & 0x00040000:
        try:
            body = zlib.decompress(body[4:])
        except zlib.error:
            body = b''
    return t, form, body, p + 20 + size
def walk(p, end, world, cell):
    while p < end:
        if data[p:p + 4] == b'GRUP':
            size, label, gtype = struct.unpack_from('<I4sI', data, p + 4)
            walk(p + 20, p + size, world, cell)
            p += size
            continue
        t, form, body, nxt = record(p)
        if t == b'WRLD':
            world[0] = next((v[:-1].decode('latin1') for k, v in subs(body) if k == b'EDID'), '')
        elif t == b'CELL' and world[0].lower() == want:
            edid = ''; grid = None
            for k, v in subs(body):
                if k == b'EDID': edid = v[:-1].decode('latin1')
                if k == b'XCLC': grid = struct.unpack_from('<ii', v)
            cell[0] = (edid, grid)
            if edid:
                print(f"CELL {edid} grid {grid} form {form:08X}")
        elif t in (b'REFR', b'ACHR') and world[0].lower() == want and flt:
            edid = ''; pos = None
            for k, v in subs(body):
                if k == b'EDID': edid = v[:-1].decode('latin1')
                if k == b'DATA' and len(v) >= 24: pos = struct.unpack_from('<6f', v)
            if edid and flt.search(edid) and pos:
                print(f"  REF {edid} form {form:08X} in {cell[0]} at {pos[0]:.0f} {pos[1]:.0f} {pos[2]:.0f} rot z {pos[5] * 57.2958:.0f}")
        p = nxt
walk(0, len(data), [''], [None])
