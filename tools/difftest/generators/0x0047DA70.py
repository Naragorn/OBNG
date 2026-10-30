#!/usr/bin/env python3
"""Generates tools/difftest/cases/0x0047DA70.json (layer-1 differential of 0x0047DA70,
proposed NiBound::WhichSide). Sphere (this, ECX) at 0x00B00000, plane (the stack
argument) at 0x00B00010; each value is given as a float32 bit pattern.

    python3 tools/difftest/generators/0x0047DA70.py > tools/difftest/cases/0x0047DA70.json
"""
import json
import struct

SPHERE = 0x00B00000
PLANE = 0x00B00010
NAN = 0x7FC00000
INF = 0x7F800000
NINF = 0xFF800000


def f(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def hexbytes(words):
    return '  '.join(' '.join('%02x' % b for b in struct.pack('<I', w)) for w in words)


def case(name, sphere, plane, fpcw=0x007F):
    # sphere = (cx, cy, cz, r), plane = (nx, ny, nz, constant), as bit patterns
    return {
        'name': name,
        'regs': {'ecx': '0x%08X' % SPHERE, 'edx': '0x5A5A5A5A'},
        'stack': ['0x%08X' % PLANE],
        'fpcw': '0x%04X' % fpcw,
        'memory': {'0x%08X' % SPHERE: hexbytes(sphere), '0x%08X' % PLANE: hexbytes(plane)},
        'readback': [{'address': '0x%08X' % SPHERE, 'size': 32}],
    }


one, zero = f(1.0), f(0.0)
X = (one, zero, zero)  # unit normal along x: d = cx - constant
cases = [
    case('E1 B1 not taken: d = -3 < -r = -1 -> 2', (f(-3.0), zero, zero, one), X + (zero,)),
    case('E2 B1 not taken at equality: d = -1 == -r -> 2', (f(-1.0), zero, zero, one), X + (zero,)),
    case('E3 B1 not taken: r = +0, d = +0 (-0 == +0) -> 2', (zero, zero, zero, zero), X + (zero,)),
    case('E4 B1 taken, B2 taken (r > d): straddle d = 0.5, r = 1 -> 0', (f(0.5), zero, zero, one), X + (zero,)),
    case('E5 B1 taken, B2 not taken at equality: d == r = 1 -> 1', (one, zero, zero, one), X + (zero,)),
    case('E6 B1 taken, B2 not taken: d = 3 > r = 1 -> 1', (f(3.0), zero, zero, one), X + (zero,)),
    case('E7 d NaN (centre x NaN): B1 taken, B2 taken (unordered) -> 0', (NAN, zero, zero, one), X + (zero,)),
    case('E8 r NaN: B1 taken, B2 taken (unordered) -> 0', (f(0.5), zero, zero, NAN), X + (zero,)),
    case('E9 r = -1, d = 0.5 <= -r: B1 not taken -> 2', (f(0.5), zero, zero, f(-1.0)), X + (zero,)),
    case('E10 r = -1, d = 2 > -r: B1 taken, B2 not taken (d > r) -> 1', (f(2.0), zero, zero, f(-1.0)), X + (zero,)),
    case('E11 r = -1, d NaN -> 0', (NAN, zero, zero, f(-1.0)), X + (zero,)),
    case('E12 r = +inf, d = 5 -> 0', (f(5.0), zero, zero, INF), X + (zero,)),
    case('E13 d = +inf (centre x +inf), r = 1 -> 1', (INF, zero, zero, one), X + (zero,)),
    case('E14 d = -inf (centre x -inf), r = 1 -> 2', (NINF, zero, zero, one), X + (zero,)),
    case('E15 the plane constant: centre x 5, constant 4.5, r 0.25 -> d 0.5 -> 1', (f(5.0), zero, zero, f(0.25)), X + (f(4.5),)),
    case('E16 summation order (C1/C2): y 1e8, x -1e8, z 1, constant 0.5, r 0.25: '
         '((y + x) + z) = 1 -> d 0.5 -> 1; pseudo.c (z + x) + y = 0 would give 2',
         (f(1e4), f(1e4), one, f(0.25)), (f(-1e4), f(1e4), one, f(0.5))),
    case('E17 PC 24 tie: y 1, x 2^-24, z 2^-60, constant 1, r 2^-24: 1 + 2^-24 rounds to 1 at the first FADDP -> d 0 -> 0',
         (one, one, f(2.0 ** -30), f(2.0 ** -24)), (f(2.0 ** -24), one, f(2.0 ** -30), one)),
    case('E18 the same at PC 64 (fpcw 0x037F): 1 + 2^-24 + 2^-60 stays exact, the store rounds up -> d 2^-23 -> 1',
         (one, one, f(2.0 ** -30), f(2.0 ** -24)), (f(2.0 ** -24), one, f(2.0 ** -30), one), fpcw=0x037F),
    case('E19 extended exponent in the register: y 2^200, x -2^200 cancel to 0 before the store, z 1, r 0.5 -> d 1 -> 1 '
         '(float arithmetic would give inf - inf = NaN -> 0)',
         (f(2.0 ** 100), f(2.0 ** 100), one, f(0.5)), (f(-(2.0 ** 100)), f(2.0 ** 100), one, zero)),
    case('E20 overflow at the store 0x0047DA88: y 2^200 -> +inf -> d +inf -> 1',
         (zero, f(2.0 ** 100), zero, one), (zero, f(2.0 ** 100), zero, zero)),
    case('E21 overflow at the store: y -2^200 -> -inf -> d -inf -> 2',
         (zero, f(2.0 ** 100), zero, one), (zero, f(-(2.0 ** 100)), zero, zero)),
    case('E22 stored +inf minus constant +inf: NaN -> 0',
         (zero, f(2.0 ** 100), zero, one), (zero, f(2.0 ** 100), zero, INF)),
    case('E23 double rounding at the store: x (1+2^-23)^2 * 2^-128 rounds to (1+2^-22)*2^-128 at PC 24, '
         'then to the denormal 2^-128 (tie, even) at the store; constant 2^-128, r 2^-149 -> d 0 -> 0 '
         '(a single rounding would give 2^-128 + 2^-149 -> d 2^-149 -> 1)',
         (f((1 + 2.0 ** -23) * 2.0 ** -64), zero, zero, 0x00000001),
         (f((1 + 2.0 ** -23) * 2.0 ** -64), zero, zero, f(2.0 ** -128))),
    case('E24 the subtraction 0x0047DA90 rounds to 24 bits: centre x 1, constant 2^-25 + 2^-48: 1 - 2^-25 - 2^-48 '
         'lies just below the tie 1 - 2^-25 and rounds to 1 - 2^-24 = r -> 1',
         (one, zero, zero, f(1 - 2.0 ** -24)), X + (f(2.0 ** -25 + 2.0 ** -48),)),
]
print(json.dumps({
    'function': '0x0047DA70',
    'note': ('Sphere against plane (proposed NiBound::WhichSide). this = ECX = the sphere {centre x, y, z, radius} '
             'at 0x00B00000, the stack argument = the plane {normal x, y, z, constant} at 0x00B00010. No callees, '
             'no data references. fpcw 0x007F (24-bit, nearest, all exceptions masked) unless the case says otherwise. '
             'Generated by a script (values as float32 bit patterns); the emulator decides.'),
    'cases': cases,
}, indent=1))
