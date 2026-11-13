#!/usr/bin/env python3
"""A record of a TES4 plugin by the low three bytes of its form id: its type, flags, editor id, name, and for a
reference its base and position. For naming a form a probe reported (its top byte is the load order's index).
Usage: esm-form.py <plugin> <form id hex>..."""
import struct, sys, zlib
data = open(sys.argv[1], 'rb').read()
want = {int(x, 16) & 0xFFFFFF for x in sys.argv[2:]}
def subs(body):
    p = 0
    while p + 6 <= len(body):
        tag, size = body[p:p + 4], struct.unpack_from('<H', body, p + 4)[0]
        yield tag, body[p + 6:p + 6 + size]
        p += 6 + size
def walk(p, end):
    while p + 20 <= end:
        tag = data[p:p + 4]
        size, = struct.unpack_from('<I', data, p + 4)
        if tag == b'GRUP':
            walk(p + 20, p + size)
            p += size
            continue
        flags, form = struct.unpack_from('<II', data, p + 8)
        if form & 0xFFFFFF in want:
            body = data[p + 20:p + 20 + size]
            if flags & 0x00040000:
                body = zlib.decompress(body[4:])
            out = [f"{form:08X} {tag.decode('latin1')} flags {flags:08X}"]
            for t, b in subs(body):
                if t in (b'EDID', b'FULL', b'MODL'):
                    out.append(f"{t.decode()} {b.rstrip(bytes(1)).decode('latin1')}")
                elif t == b'NAME' and len(b) == 4:
                    out.append(f"base {struct.unpack('<I', b)[0]:08X}")
                elif t == b'DATA' and tag in (b'REFR', b'ACHR', b'ACRE') and len(b) == 24:
                    out.append('at %.0f %.0f %.0f' % struct.unpack('<3f', b[:12]))
                elif t == b'SCRI' and len(b) == 4:
                    out.append(f"script {struct.unpack('<I', b)[0]:08X}")
            print(', '.join(out))
        p += 20 + size
walk(0, len(data))
