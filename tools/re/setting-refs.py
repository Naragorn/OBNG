#!/usr/bin/env python3
"""For INI setting names: the string's address in Oblivion.exe, the setting
object that points at it, and the functions whose code refers to the
object's first dwords (the value). Usage: setting-refs.py <exe> <functions dir> name [...]"""
import bisect, glob, os, struct, sys
exe, export = sys.argv[1], sys.argv[2]
data = open(exe, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
optsize = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
secs = []
off = pe + 24 + optsize
for _ in range(nsec):
    name = data[off:off + 8].rstrip(b'\0').decode()
    vsize, va, rsize, raw = struct.unpack_from('<IIII', data, off + 8)
    secs.append((name, va, vsize, raw, rsize))
    off += 40
def va_of(fileoff):
    for name, va, vsize, raw, rsize in secs:
        if raw <= fileoff < raw + rsize:
            return base + va + (fileoff - raw)
    return None
text = [s for s in secs if s[0] == '.text'][0]
tdata = data[text[3]:text[3] + text[4]]
starts = sorted(int(os.path.basename(f)[2:-2], 16) for f in glob.glob(os.path.join(export, '0x*.c')))
def func(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i]
def refs(value):
    pat = struct.pack('<I', value)
    out = []
    i = 0
    while True:
        i = data.find(pat, i)
        if i < 0:
            break
        v = va_of(i)
        if v is not None:
            out.append((v, i))
        i += 1
    return out
for name in sys.argv[3:]:
    key = name.encode() + b'\0'
    i = data.find(b'\0' + key)
    if i < 0:
        print(f"{name}: string not found")
        continue
    sva = va_of(i + 1)
    holders = [v for v, _ in refs(sva)]
    print(f"{name}: string {sva:08X}; referenced at {[f'{h:08X}' for h in holders]}")
    for h in holders:
        in_text = base + text[1] <= h < base + text[1] + text[4]
        if in_text:
            print(f"   code at {h:08X} in function {func(h):08X} (registers the name)")
            continue
        # a setting object in data: assume the value sits in the dwords before the name pointer
        for back in (4, 8):
            obj = h - back
            users = sorted({func(v) for v, _ in refs(obj) if base + text[1] <= v < base + text[1] + text[4]})
            if users:
                print(f"   object {obj:08X} (name pointer at +{back}): code in {[f'{u:08X}' for u in users][:14]}")
