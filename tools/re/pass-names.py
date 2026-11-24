#!/usr/bin/env python3
"""The engine's own name for every shader pass id: the switch of 0x007B4920 (Oblivion.exe 1.2.0.416), whose
every case returns a string. Reads the jump table at 0x007B69D8 (0x1A3 entries) and each case's
`mov eax, <string>`.

Usage: pass-names.py <Oblivion.exe> [id ...]      ids in hex; all when none is given"""
import struct, sys

FUNCTION, TABLE, COUNT = 0x007B4920, 0x007B69D8, 0x1A3
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


def name_of(pass_id):
    target = struct.unpack_from('<I', data, file_offset(TABLE) + 4 * pass_id)[0]
    at = file_offset(target)
    if data[at] != 0xB8:                       # not `mov eax, imm32`: the case builds its answer another way
        return f"(case at {target:#010x} does not load a string)"
    text = file_offset(struct.unpack_from('<I', data, at + 1)[0])
    return data[text:data.index(b'\0', text)].decode('latin1')


if __name__ == '__main__':
    wanted = [int(a, 16) for a in sys.argv[2:]] or range(COUNT)
    for pass_id in wanted:
        print(f"{pass_id:#05x} {pass_id:4d}  {name_of(pass_id)}")
