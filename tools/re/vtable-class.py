#!/usr/bin/env python3
"""The class a vtable of Oblivion.exe belongs to (its RTTI name, read through the pointer before the vtable) and
chosen slots of it.
Usage: vtable-class.py <Oblivion.exe> <vtable hex> [slot hex ...]"""
import struct, sys
data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
optsize = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
secs = [struct.unpack_from('<8sIIII', data, pe + 24 + optsize + 40 * i) for i in range(nsec)]


def off(va):
    for _, vsize, vaddr, rsize, raw in secs:
        if base + vaddr <= va < base + vaddr + max(vsize, rsize):
            return va - base - vaddr + raw
    raise SystemExit(f'{va:08X} is in no section')


def dword(va):
    return struct.unpack_from('<I', data, off(va))[0]


vt = int(sys.argv[2], 16)
locator = dword(vt - 4)
descriptor = dword(locator + 12)
name = data[off(descriptor + 8):].split(b'\0', 1)[0].decode()
print(f'{vt:08X} {name}')
for s in sys.argv[3:]:
    print(f'  +0x{int(s, 16):02X} {dword(vt + int(s, 16)):08X}')
