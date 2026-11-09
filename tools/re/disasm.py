#!/usr/bin/env python3
"""Disassemble ranges of Oblivion.exe by virtual address (objdump, Intel syntax).
Usage: disasm.py <Oblivion.exe> <start-end hex> [...], e.g. 70a0d0-70a184"""
import re, struct, subprocess, sys, tempfile
data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
optsize = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
secs = []
off = pe + 24 + optsize
for _ in range(nsec):
    vsize, va, rsize, raw = struct.unpack_from('<IIII', data, off + 8)
    secs.append((va, vsize, raw, rsize))
    off += 40
def file_offset(v):
    rva = v - base
    for va, vsize, raw, rsize in secs:
        if va <= rva < va + max(vsize, rsize):
            return raw + (rva - va)
    raise SystemExit(f"no section for {v:#x}")
for spec in sys.argv[2:]:
    a, b = (int(x, 16) for x in spec.split('-'))
    with tempfile.NamedTemporaryFile(suffix='.bin') as tmp:
        tmp.write(data[file_offset(a):file_offset(b) + 1])
        tmp.flush()
        out = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel', '--adjust-vma', hex(a), tmp.name],
                             capture_output=True, text=True).stdout
    print(f"==== {a:08X}-{b:08X}")
    for line in out.splitlines():
        if re.match(r'^\s*[0-9a-f]+:', line):
            print(line.split('\t', 1)[0].strip() + '\t' + line.split('\t')[-1].strip())
