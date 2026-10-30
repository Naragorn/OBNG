#!/usr/bin/env python3
"""Generates tools/difftest/cases/0x007D2EA0-rounding.json: 0x007D2EA0 (proposed
ShadowSceneLight::IsInRange) under the four x87 rounding controls at precision
control 24, with the game's own square root and, on the plane path, the game's
own WhichSide.

    python3 tools/difftest/generators/0x007D2EA0-rounding.py > tools/difftest/cases/0x007D2EA0-rounding.json
    OBNG_ORACLE_EXE=<reference Oblivion.exe> python3 tools/difftest/emulate.py \\
        re/export/draw-setup/0x007D2EA0 tools/difftest/cases/0x007D2EA0-rounding.json

Every case runs at fpcw 0x007F (nearest), 0x047F (down), 0x087F (up) and 0x0C7F
(truncate - the shadow walk's control word, measured 2026-10-30):
  G1      the garbage-sized scene-root bound of run 20261030-131826 (the
          sum of squares overflows: +inf at nearest, FLT_MAX at truncate);
  D1..D15 the distance cases of 0x007D2EA0.json (D6 left out: its NaN sum
          takes the CRT's error path 0x00991AB7, which calls further into the
          CRT than these cases map);
  M1..M4  distance inputs whose differences round differently by mode;
  P1      the plane path with WhichSide's own bytes, planes whose answers
          depend on the mode.

The square root 0x00982C30 is the game's code, not a model: the stub at
0x00982C30 jumps to a wrapper at 0x10006000 (FST dword [0x10005000] logs s;
SUB ESP,0xC / FST qword [ESP] / CALL 0x00991B88 / CALL 0x00982C4D / ADD ESP,0xC
- the original's own body, with absolute calls; FST dword [0x10005004] logs q;
RET), and the CRT's bodies 0x00982C4D..0x00982CE9 and 0x00991B15..0x00991BD4
come from the reference executable through the case's "image" ranges, with
the CRT flag dword [0x00BA9DDC] at its load value 0 (.bss). WhichSide on the
plane path is an "image" stub (its 92 bytes, traced).
"""
import json
import struct
from pathlib import Path

CONTROL_WORDS = (0x007F, 0x047F, 0x087F, 0x0C7F)
MODE_NAMES = {0x007F: 'nearest', 0x047F: 'down', 0x087F: 'up', 0x0C7F: 'truncate'}
THIS, OBJECT, SOURCE, VTABLE, RTTI_STUB, LOG, WRAPPER = (
    0x10000000, 0x10001000, 0x10002000, 0x10003000, 0x10004000, 0x10005000, 0x10006000)
POINT_LIGHT_RTTI = 0x00B3FD80

# The square-root wrapper (see the docstring), assembled by hand.
WRAPPER_CODE = ' '.join([
    'd9 15 00 50 00 10',  # fst dword [0x10005000]      log s
    '83 ec 0c',           # sub esp, 0xc                 as 0x00982C30
    'dd 14 24',           # fst qword [esp]
    'b8 88 1b 99 00',     # mov eax, 0x00991B88
    'ff d0',              # call eax
    'b8 4d 2c 98 00',     # mov eax, 0x00982C4D          (flags of 0x00991B88 kept: MOV and CALL leave them)
    'ff d0',              # call eax
    '83 c4 0c',           # add esp, 0xc
    'd9 15 04 50 00 10',  # fst dword [0x10005004]      log q
    'c3',                 # ret
])
SQRT_STUB = {'code': 'b8 00 60 00 10 ff e0'}  # mov eax, 0x10006000; jmp eax (traced as the call of 0x00982C30)
CRT_IMAGE = [
    {'address': '0x00982C4D', 'size': 0x00982CEA - 0x00982C4D},
    {'address': '0x00991B15', 'size': 0x00991BD5 - 0x00991B15},
]
CRT_FLAG = {'0x00BA9DDC': '00 00 00 00'}


def f(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def neg(bits):
    return bits ^ 0x80000000


def le(words):
    return ' '.join('%02x' % b for w in words for b in struct.pack('<I', w))


def this_memory(plane_source, planes, mask):
    return 'ee ' * 0x14C + le([plane_source]) + ' ' + le([w for p in planes for w in p]) + ' ' + le([mask])


def object_memory(bound):
    return 'ee ' * 0x20 + le(bound)


def source_memory(position, light_range):
    return (le([VTABLE]) + ' ' + 'ee ' * (0x88 - 4) + le(position) + ' ' + 'ee ' * (0xF8 - 0x94) + le([light_range]))


def build(name, plane_source, planes, mask, bound, position, light_range, fpcw, plane_stub):
    return {
        'name': '%s [fpcw %04X %s]' % (name, fpcw, MODE_NAMES[fpcw]),
        'regs': {'ecx': '0x%08X' % THIS, 'eax': '0xA5A5A5A5', 'edx': '0x5A5A5A5A'},
        'stack': ['0x%08X' % OBJECT, '0x%08X' % SOURCE],
        'fpcw': '0x%04X' % fpcw,
        'image': CRT_IMAGE,
        'memory': dict({
            '0x%08X' % THIS: this_memory(plane_source, planes, mask),
            '0x%08X' % OBJECT: object_memory(bound),
            '0x%08X' % SOURCE: source_memory(position, light_range),
            '0x%08X' % VTABLE: le([0x10003F00, RTTI_STUB]),
            '0x%08X' % LOG: 'cd cd cd cd cd cd cd cd',
            '0x%08X' % WRAPPER: WRAPPER_CODE,
        }, **CRT_FLAG),
        'stubs': {
            '0x%08X' % RTTI_STUB: {'ret': 0, 'eax': '0x%08X' % POINT_LIGHT_RTTI},
            '0x00982C30': SQRT_STUB,
            '0x0047DA70': plane_stub,
        },
        'readback': [{'address': '0x%08X' % LOG, 'size': 8}, {'address': '0x%08X' % (THIS + 0x1B0), 'size': 4}],
    }


def words_at(memory, address, offset, count):
    raw = bytes.fromhex(memory['0x%08X' % address].replace(' ', ''))
    return list(struct.unpack_from('<%dI' % count, raw, offset))


DEFAULT_PLANES = [[f(float(i + 1)), 0, 0, f(float(i))] for i in range(6)]  # never read on the distance path
NO_PLANE = {'ret': 4, 'eax': ['0x00000000']}  # the distance path never calls WhichSide
WHICH_SIDE = {'image': 92}


def distance_case(name, bound, position, light_range):
    return (name, 0, DEFAULT_PLANES, 0x3F, bound, position, light_range, NO_PLANE)


def from_main(case):
    memory = case['memory']
    return (case['name'].split(' -> ')[0], 0, DEFAULT_PLANES, words_at(memory, THIS, 0x1B0, 1)[0],
            words_at(memory, OBJECT, 0x20, 4), words_at(memory, SOURCE, 0x88, 3),
            words_at(memory, SOURCE, 0xF8, 1)[0], NO_PLANE)


def main():
    here = Path(__file__).resolve().parents[1] / 'cases'
    main_cases = json.loads((here / '0x007D2EA0.json').read_text())['cases']
    p24, p30, one, zero = f(2.0 ** -24), f(2.0 ** -30), f(1.0), 0
    inputs = [
        # The root's bound, the light's position and range as the mismatch lines of
        # artifacts/shadow/obng-range-walk-routed/20261030-131826/OBNG.log print them.
        distance_case('G1 garbage scene-root bound (run 20261030-131826): the sum of squares overflows',
                      [0xFD935C0F, 0xFD935C13, 0xFEBFCBA9, 0x5FDFFFF8], [0xC3F5B6A8, 0x4486928F, 0x4401EB54], 0x43800000),
    ]
    inputs += [from_main(c) for c in main_cases if c['name'][:3] in
               ('D1 ', 'D2 ', 'D3 ', 'D4 ', 'D5 ', 'D7 ', 'D8 ', 'D9 ') or c['name'][:4] in ('D10 ', 'D11 ', 'D12 ', 'D13 ', 'D14 ', 'D15 ')]
    inputs += [
        distance_case('M1 the difference ties: light x 2^24, centre x -1 (2^24 + 1); radius 0, range 2^24 + 2',
                      [f(-1.0), zero, zero, zero], [f(2.0 ** 24), zero, zero], f(2.0 ** 24 + 2)),
        distance_case('M2 M1 negative: light x -2^24, centre x 1 (-(2^24 + 1)); radius 0, range 2^24 + 2',
                      [one, zero, zero, zero], [f(-(2.0 ** 24)), zero, zero], f(2.0 ** 24 + 2)),
        distance_case('M3 an exact zero difference: light x = centre x = 5 (+0, or -0 under down); dy 3, dz 4, radius 0, range 6',
                      [f(5.0), zero, zero, zero], [f(5.0), f(3.0), f(4.0)], f(6.0)),
        distance_case('M4 the sum of squares rounds: dy 1 + 2^-23 (dy^2 = 1 + 2^-22 + 2^-46), dx 2^-40; radius 0, range 1 + 2^-23',
                      [zero, zero, zero, zero], [f(2.0 ** -40), f(1 + 2.0 ** -23), zero], f(1 + 2.0 ** -23)),
        ('P1 plane path, WhichSide\'s own bytes: plane 0 = R1 (1 under up), plane 1 = R2 (2 under down), planes 2..5 wholly '
         'positive', 1, [[p24, one, p30, one], [neg(p24), neg(one), neg(p30), neg(one)],
                         [one, zero, zero, zero], [one, zero, zero, zero], [one, zero, zero, zero], [one, zero, zero, zero]],
         0x3F, [one, one, p30, p24], [zero, zero, zero], f(100.0), WHICH_SIDE),
    ]
    cases = []
    for item in inputs:
        for fpcw in CONTROL_WORDS:
            name, plane_source, planes, mask, bound, position, light_range, plane_stub = item
            cases.append(build(name, plane_source, planes, mask, bound, position, light_range, fpcw, plane_stub))
    print(json.dumps({
        'function': '0x007D2EA0',
        'note': ('IsInRange (proposed) under the four x87 rounding controls at PC 24, with the game\'s own square root '
                 '(0x00982C30 through a logging wrapper, the CRT bodies from the reference executable) and WhichSide\'s '
                 'own bytes on the plane path. Layout as in 0x007D2EA0.json; readback: the s and q the wrapper logged '
                 'at 0x10005000, and the mask +0x1B0. Generated by tools/difftest/generators/0x007D2EA0-rounding.py; '
                 'needs --image or OBNG_ORACLE_EXE (the reference Oblivion.exe).'),
        'cases': cases,
    }, indent=1))


if __name__ == '__main__':
    main()
