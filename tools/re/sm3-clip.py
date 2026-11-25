#!/usr/bin/env python3
"""The clip of the one-pass lighting's light sum, tried on a shader package outside the game: for every pixel
program of Lighting30Shader (SM3*.pso, SM3LL*.pso) the instruction that completes "ambient colour plus lights" is
looked for by the rule of src/engine/render/Lighting30ClipLogic.h - the first instruction that takes the constant
named AmbientColor into a temporary's xyz, and after it the first `add` that writes that temporary's xyz from
itself and another temporary - and given the saturate modifier, which is what [Probe] Lighting30Clip does to the
package in the game's memory. ShadowLightShader's passes clip that sum at 1 before the texture (SLS2003.pso:
mad_sat), the one-pass programs do not; knowledge/lighting-passes.md, "The picture gate outdoors".

With --fade it does what [Probe] Lighting30SpecularFade does as well (src/engine/render/Lighting30SpecularLogic.h): in
every program with specular - the one `mul rS.xyz, rN.w, rS`, rN the texld from the sampler named NormalMap - it puts
`mul_pp rS.xyz, rS, cT.w` behind that multiply, cT the register of ToggleADTS by the program's table.

Usage: sm3-clip.py <shaderpackageNNN.sdp> [--fade] [--diff <name>.pso] [--write <out.sdp>]
  no option   a line a program: clipped at which instruction, or why it is left
  --fade      also the specular fade's multiply (the program grows by four tokens)
  --diff      that program's disassembly before and after (vkd3d-compiler), the changed lines only
  --write     the package with the clip applied, for looking at it with tools/re/sdp-shader.py - never for the game"""
import difflib, struct, subprocess, sys, tempfile, os

TEMP, CONST = 0, 2
ADD, DCL, DEF = 2, 31, 81
SATURATE = 1 << 20


def entries(data):
    """[(name, offset of the bytecode, its length)] of a .sdp: a count at +4, then entries of a 0x100-byte name, a
    length and the bytecode."""
    count = struct.unpack_from('<I', data, 4)[0]
    at, found = 12, []
    for _ in range(count):
        name = data[at:at + 0x100].split(b'\0')[0].decode('latin-1')
        length = struct.unpack_from('<I', data, at + 0x100)[0]
        found.append((name, at + 0x104, length))
        at += 0x104 + length
    return found


def register(token):
    return ((token >> 28) & 7) | ((token >> 8) & 0x18), token & 0x7FF


def table_register(tokens, wanted, register_set):
    """The register of the constant named `wanted` in the register set (2 floats, 3 samplers), or None."""
    i = 1
    while i < len(tokens) and tokens[i] != 0x0000FFFF:
        t = tokens[i]
        if (t & 0xFFFF) == 0xFFFE:
            words = (t >> 16) & 0x7FFF
            if words >= 8 and i + 1 + words <= len(tokens) and tokens[i + 1] == 0x42415443:
                table = struct.pack(f'<{words - 1}I', *tokens[i + 2:i + 1 + words])
                count, info = struct.unpack_from('<II', table, 12)
                for k in range(count):
                    at = info + k * 20
                    if at + 20 > len(table):
                        return None
                    name_at, rset, index = struct.unpack_from('<IHH', table, at)
                    if rset == register_set and table[name_at:name_at + len(wanted) + 1] == wanted + b'\0':
                        return index
                return None
            i += 1 + words
        else:
            i += 1 + ((t >> 24) & 0xF)
    return None


def gloss_multiply(tokens):
    """(index behind the gloss multiply, the specular's temporary, ToggleADTS's register) or (None, why not)."""
    normal, toggle = table_register(tokens, b'NormalMap', 3), table_register(tokens, b'ToggleADTS', 2)
    if normal is None or toggle is None:
        return None, 'no NormalMap or ToggleADTS in the table'
    i, gloss, found, power = 1, None, None, False
    while i < len(tokens) and tokens[i] != 0x0000FFFF:
        t = tokens[i]
        if (t & 0xFFFF) == 0xFFFE:
            i += 1 + ((t >> 16) & 0x7FFF)
            continue
        op, length = t & 0xFFFF, (t >> 24) & 0xF
        p = tokens[i + 1:i + 1 + length]
        power = power or op == 32
        if op == 66 and length == 3 and register(p[2]) == (10, normal) and register(p[0])[0] == TEMP:
            gloss = register(p[0])[1]
        elif op == 5 and length == 3 and gloss is not None and register(p[0])[0] == TEMP and ((p[0] >> 16) & 0xF) == 7 and not p[0] & SATURATE:
            s = register(p[0])[1]
            w = lambda q: register(q) == (TEMP, gloss) and ((q >> 16) & 0xFF) == 0xFF
            other = lambda q: register(q) == (TEMP, s)
            if s != gloss and ((w(p[1]) and other(p[2])) or (w(p[2]) and other(p[1]))):
                if found is not None:
                    return None, 'two gloss multiplies'
                found = (i + 1 + length, s, toggle)
        i += 1 + length
    return (found, 'faded') if found and power else (None, 'no specular')


def ambient_register(tokens):
    """The float register of the constant named AmbientColor from the program's constant table, or None."""
    i = 1
    while i < len(tokens) and tokens[i] != 0x0000FFFF:
        t = tokens[i]
        if (t & 0xFFFF) == 0xFFFE:
            words = (t >> 16) & 0x7FFF
            if words >= 8 and i + 1 + words <= len(tokens) and tokens[i + 1] == 0x42415443:
                table = struct.pack(f'<{words - 1}I', *tokens[i + 2:i + 1 + words])
                count, info = struct.unpack_from('<II', table, 12)
                for k in range(count):
                    at = info + k * 20
                    if at + 20 > len(table):
                        return None
                    name_at, register_set, index = struct.unpack_from('<IHH', table, at)
                    if register_set == 2 and table[name_at:name_at + 13] == b'AmbientColor\0':
                        return index
                return None
            i += 1 + words
        else:
            i += 1 + ((t >> 24) & 0xF)
    return None


def light_sum(tokens):
    """(index of the destination token of the add that completes the light sum, text) or (None, why not)."""
    if len(tokens) < 2 or (tokens[0] >> 16) != 0xFFFF or ((tokens[0] >> 8) & 0xFF) < 2:
        return None, 'no pixel program of version 2 or 3'
    ambient = ambient_register(tokens)
    if ambient is None:
        return None, 'no constant AmbientColor'
    i, lit, number = 1, None, 0
    while i < len(tokens) and tokens[i] != 0x0000FFFF:
        t = tokens[i]
        if (t & 0xFFFF) == 0xFFFE:
            i += 1 + ((t >> 16) & 0x7FFF)
            continue
        op, length = t & 0xFFFF, (t >> 24) & 0xF
        if i + 1 + length > len(tokens):
            return None, 'an instruction runs past the end'
        args = tokens[i + 1:i + 1 + length]
        if op not in (DCL, DEF) and length >= 2:
            number += 1
            dest, sources = args[0], [register(a) for a in args[1:]]
            to_temp_xyz = register(dest)[0] == TEMP and ((dest >> 16) & 7) == 7
            if lit is None:
                if to_temp_xyz and (CONST, ambient) in sources:
                    lit = register(dest)
            elif to_temp_xyz and register(dest) == lit:
                others = [s for s in sources if s != lit]
                if op == ADD and length == 3 and lit in sources and len(others) == 1 and others[0][0] == TEMP:
                    state = ' (saturated already)' if dest & SATURATE else ''
                    return i + 1, f'add r{lit[1]}.xyz, r{others[0][1]}, r{lit[1]} - instruction {number}{state}'
                if lit not in sources:
                    return None, f'r{lit[1]} is written anew before a sum is made of it'
        i += 1 + length
    return None, 'the ambient colour is never added to a light sum' if lit is not None else 'the ambient colour is never read'


def disassembly(code):
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(code)
    try:
        return subprocess.run(['vkd3d-compiler', '-x', 'd3dbc', '-b', 'd3d-asm', f.name], capture_output=True, text=True).stdout.splitlines()
    finally:
        os.unlink(f.name)


def faded(tokens):
    """The tokens with the fade's multiply behind the gloss multiply, and the text; the tokens as they were when none."""
    found, text = gloss_multiply(tokens)
    if found is None:
        return tokens, text
    after, s, toggle = found
    added = [5 | (3 << 24), 0x80000000 | s | (7 << 16) | (1 << 21), 0x80000000 | s | (0xE4 << 16), 0x80000000 | (2 << 28) | toggle | (0xFF << 16)]
    return tokens[:after] + added + tokens[after:], f'{text}: r{s}.xyz times c{toggle}.w after token {after}'


def main():
    path = sys.argv[1]
    data = bytearray(open(path, 'rb').read())
    wanted = sys.argv[sys.argv.index('--diff') + 1] if '--diff' in sys.argv else None
    fade = '--fade' in sys.argv
    clipped = left = with_fade = 0
    for name, at, length in entries(data):
        if not (name.upper().startswith('SM3') and name.lower().endswith('.pso')):
            continue
        tokens = list(struct.unpack_from(f'<{length // 4}I', data, at))
        index, text = light_sum(tokens)
        before = bytes(data[at:at + length])
        if index is not None:
            struct.pack_into('<I', data, at + index * 4, tokens[index] | SATURATE)
            clipped += 1
        else:
            left += 1
        after = bytes(data[at:at + length])
        fade_text = ''
        if fade:
            longer, fade_text = faded(list(struct.unpack_from(f'<{length // 4}I', after)))
            if len(longer) > length // 4:
                with_fade += 1
            after = struct.pack(f'<{len(longer)}I', *longer)
            fade_text = f'; fade: {fade_text}'
        if wanted is None:
            print(f"{name:14s} {'clip ' if index is not None else 'leave'}  {text}{fade_text}")
        elif name == wanted:
            old, new = disassembly(before), disassembly(after)
            print(f'{name}: {text}{fade_text}; {len(old)} lines before, {len(new)} after')
            for line in difflib.unified_diff([a.strip() for a in old], [b.strip() for b in new], lineterm='', n=1):
                if not line.startswith(('---', '+++')):
                    print(f'  {line}')
    if wanted is None:
        print(f'{clipped} programs clipped, {left} left' + (f'; {with_fade} with the specular fade' if fade else ''))
    if '--write' in sys.argv:
        if fade:
            raise SystemExit('--write with --fade: the package keeps its lengths; not written')
        open(sys.argv[sys.argv.index('--write') + 1], 'wb').write(data)


if __name__ == '__main__':
    main()
