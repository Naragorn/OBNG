#!/usr/bin/env python3
"""Generates tools/difftest/cases/0x0047DA70-rounding.json: 0x0047DA70 (proposed
NiBound::WhichSide) under the four x87 rounding controls at precision control 24.

    python3 tools/difftest/generators/0x0047DA70-rounding.py > tools/difftest/cases/0x0047DA70-rounding.json

Two groups:
  - the 23 cases of tools/difftest/cases/0x0047DA70.json taken at fpcw 0x007F
    (E18, the PC-64 case, left out), repeated at 0x047F (down), 0x087F (up)
    and 0x0C7F (truncate - the shadow walk's control word, measured);
  - R cases at all four control words whose inputs make the modes differ:
    ties and sticky bits in the dot product, overflow at either store,
    denormal results at the store, negative sums, the FSUB's rounding.
Values are float32 bit patterns; the emulator decides every result.
"""
import json
import struct
from pathlib import Path

SPHERE = 0x00B00000
PLANE = 0x00B00010
CONTROL_WORDS = (0x007F, 0x047F, 0x087F, 0x0C7F)
MODE_NAMES = {0x007F: 'nearest', 0x047F: 'down', 0x087F: 'up', 0x0C7F: 'truncate'}
FLT_MAX = 0x7F7FFFFF
INF = 0x7F800000


def f(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def neg(bits):
    return bits ^ 0x80000000


def hexbytes(words):
    return '  '.join(' '.join('%02x' % b for b in struct.pack('<I', w)) for w in words)


def case(name, sphere, plane, fpcw):
    # sphere = (cx, cy, cz, r), plane = (nx, ny, nz, constant), as bit patterns
    return {
        'name': '%s [fpcw %04X %s]' % (name, fpcw, MODE_NAMES[fpcw]),
        'regs': {'ecx': '0x%08X' % SPHERE, 'edx': '0x5A5A5A5A'},
        'stack': ['0x%08X' % PLANE],
        'fpcw': '0x%04X' % fpcw,
        'memory': {'0x%08X' % SPHERE: hexbytes(sphere), '0x%08X' % PLANE: hexbytes(plane)},
        'readback': [{'address': '0x%08X' % SPHERE, 'size': 32}],
    }


def words(text):
    raw = bytes.fromhex(text.replace(' ', ''))
    return tuple(struct.unpack('<4I', raw))


one, zero = f(1.0), f(0.0)
X = (one, zero, zero)  # unit normal along x: d = cx - constant
p24, p30, p40 = f(2.0 ** -24), f(2.0 ** -30), f(2.0 ** -40)
R_CASES = [
    ('R1 tie at the first FADDP: 1 + 2^-24, then + 2^-60; constant 1, r 2^-24',
     (one, one, p30, p24), (p24, one, p30, one)),
    ('R2 R1 mirrored: -(1 + 2^-24) - 2^-60; constant -1, r 2^-24',
     (one, one, p30, p24), (neg(p24), neg(one), neg(p30), neg(one))),
    ('R3 sticky below double: 1 + 2^-80 (y 1*1, x 2^-40*2^-40); constant 1, r 2^-24',
     (p40, one, zero, p24), (p40, one, zero, one)),
    ('R4 R3 mirrored: -1 - 2^-80; constant -1, r 2^-24',
     (p40, one, zero, p24), (neg(p40), neg(one), zero, neg(one))),
    ('R5 overflow at the first store: y 2^200; constant FLT_MAX, r 1',
     (zero, f(2.0 ** 100), zero, one), (zero, f(2.0 ** 100), zero, FLT_MAX)),
    ('R6 R5 mirrored: y -2^200; constant -FLT_MAX, r 1',
     (zero, f(2.0 ** 100), zero, one), (zero, neg(f(2.0 ** 100)), zero, neg(FLT_MAX))),
    ('R7 overflow at the second store: dot FLT_MAX minus -FLT_MAX; r +inf',
     (FLT_MAX, zero, zero, INF), X + (neg(FLT_MAX),)),
    ('R8 R7 mirrored: dot -FLT_MAX minus FLT_MAX; r +inf',
     (neg(FLT_MAX), zero, zero, INF), X + (FLT_MAX,)),
    ('R9 denormal at the store: x (1+2^-23)^2 * 2^-128; constant 2^-128, r 2^-149',
     (f((1 + 2.0 ** -23) * 2.0 ** -64), zero, zero, 0x00000001),
     (f((1 + 2.0 ** -23) * 2.0 ** -64), zero, zero, f(2.0 ** -128))),
    ('R10 a denormal below the smallest: x 2^-75 * 2^-75 = 2^-150; constant 0, r 2^-149',
     (f(2.0 ** -75), zero, zero, 0x00000001), (f(2.0 ** -75), zero, zero, zero)),
    ('R11 R10 negative: x -2^-150; constant 0, r 2^-149',
     (f(2.0 ** -75), zero, zero, 0x00000001), (neg(f(2.0 ** -75)), zero, zero, zero)),
    ('R12 the FSUB rounds: centre x 1, constant 2^-25 + 2^-48, r 1',
     (one, zero, zero, one), X + (f(2.0 ** -25 + 2.0 ** -48),)),
    ('R13 a negative tie: -(1 + 2^-23) - 3 * 2^-24 = -(1 + 2.5 * 2^-23); constant 0, r 1 + 3 * 2^-23',
     (f(3.0), f(1 + 2.0 ** -23), zero, f(1 + 3 * 2.0 ** -23)), (neg(p24), neg(one), zero, zero)),
    ('R14 R13 positive: (1 + 2^-23) + 3 * 2^-24; constant 0, r 1 + 3 * 2^-23',
     (f(3.0), f(1 + 2.0 ** -23), zero, f(1 + 3 * 2.0 ** -23)), (p24, one, zero, zero)),
    ('R15 exact cancellation: y 1, x -1; constant 0, r 0 (+0 or -0 by mode; both compare equal)',
     (one, one, zero, zero), (neg(one), one, zero, zero)),
]


def main():
    base = json.loads((Path(__file__).resolve().parents[1] / 'cases' / '0x0047DA70.json').read_text())['cases']
    e_cases = [c for c in base if not c['name'].startswith('E18 ')]
    cases = []
    for fpcw in CONTROL_WORDS[1:]:
        for c in e_cases:
            sphere = words(c['memory']['0x%08X' % SPHERE])
            plane = words(c['memory']['0x%08X' % PLANE])
            cases.append(case(c['name'], sphere, plane, fpcw))
    for name, sphere, plane in R_CASES:
        for fpcw in CONTROL_WORDS:
            cases.append(case(name, sphere, plane, fpcw))
    print(json.dumps({
        'function': '0x0047DA70',
        'note': ('Sphere against plane (proposed NiBound::WhichSide) under the four x87 rounding controls at PC 24. '
                 'this = ECX = the sphere {centre x, y, z, radius} at 0x00B00000, the stack argument = the plane '
                 '{normal x, y, z, constant} at 0x00B00010. No callees, no data references. The E cases are those of '
                 '0x0047DA70.json (E18 left out) at fpcw 0x047F, 0x087F and 0x0C7F; the R cases run at all four. '
                 'Generated by tools/difftest/generators/0x0047DA70-rounding.py; the emulator decides.'),
        'cases': cases,
    }, indent=1))


if __name__ == '__main__':
    main()
