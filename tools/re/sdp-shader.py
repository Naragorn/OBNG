#!/usr/bin/env python3
"""The compiled programs of one of the game's shader packages (Data/Shaders/shaderpackageNNN.sdp).

  sdp-shader.py <package> list [prefix]             the entries, name and size
  sdp-shader.py <package> table <name> [...]        an entry's constant table: name, register set, first register, count
  sdp-shader.py <package> asm <name>                the entry disassembled (vkd3d-compiler)
  sdp-shader.py <package> save <name> <file>        the entry's bytecode into a file

A package is a count and then, per entry, a name of 0x100 bytes, a length and the bytecode. The constant table is
the CTAB comment of the Direct3D 9 bytecode."""
import struct, subprocess, sys, tempfile

REGISTER_SETS = {0: 'b', 1: 'i', 2: 'c', 3: 's'}


def entries(path):
    data = open(path, 'rb').read()
    count = struct.unpack_from('<I', data, 4)[0]
    off, out = 12, {}
    for _ in range(count):
        name = data[off:off + 0x100].split(b'\0')[0].decode('latin1')
        length = struct.unpack_from('<I', data, off + 0x100)[0]
        out[name.upper()] = (name, data[off + 0x104:off + 0x104 + length])
        off += 0x104 + length
    return out


def constant_table(code):
    """[(name, register set letter, first register, register count)] of a program, by register."""
    words = struct.unpack_from('<%dI' % (len(code) // 4), code)
    i = 1
    while i < len(words):
        if words[i] & 0xFFFF != 0xFFFE:                       # the comments come first
            break
        size = words[i] >> 16
        if words[i + 1] == 0x42415443:                        # 'CTAB'
            base = (i + 2) * 4
            _, _, _, count, info = struct.unpack_from('<IIIII', code, base)
            rows = []
            for k in range(count):
                name_at, register_set, first, n = struct.unpack_from('<IHHH', code, base + info + 20 * k)
                name = code[base + name_at:code.index(b'\0', base + name_at)].decode('latin1')
                rows.append((name, REGISTER_SETS.get(register_set, '?'), first, n))
            return sorted(rows, key=lambda r: (r[1], r[2]))
        i += 1 + size
    return []


def disassemble(code):
    with tempfile.NamedTemporaryFile(suffix='.bin') as tmp:
        tmp.write(code)
        tmp.flush()
        done = subprocess.run(['vkd3d-compiler', '-x', 'd3dbc', '-b', 'd3d-asm', tmp.name], capture_output=True, text=True)
    return done.stdout or done.stderr


def main():
    package, what = sys.argv[1], sys.argv[2]
    table = entries(package)
    if what == 'list':
        prefix = sys.argv[3].upper() if len(sys.argv) > 3 else ''
        for key in sorted(table):
            if key.startswith(prefix):
                print(f"{table[key][0]:28s} {len(table[key][1]):6d}")
        return
    for wanted in sys.argv[3:4] if what in ('asm', 'save') else sys.argv[3:]:
        if wanted.upper() not in table:
            raise SystemExit(f"{wanted}: not in the package")
        name, code = table[wanted.upper()]
        if what == 'table':
            kind = 'vertex' if code[2] == 0xFE else 'pixel'          # the version token: minor, major, 0xFFFE / 0xFFFF
            print(f"{name}: {len(code)} bytes, {kind} program, version {code[1]}.{code[0]}")
            for row in constant_table(code):
                print(f"   {row[1]}{row[2]:<3d} x{row[3]:<3d} {row[0]}")
        elif what == 'asm':
            print(disassemble(code))
        elif what == 'save':
            open(sys.argv[4], 'wb').write(code)


if __name__ == '__main__':
    main()
