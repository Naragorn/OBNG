#!/usr/bin/env python3
"""Generates tools/difftest/cases/0x004E1580.json: the synthetic memory layout and the
cases of the layer-1 differential for 0x004E1580 (TESObjectREFR::LoadBaseFormModels).

    python3 tools/difftest/generators/0x004E1580.py > tools/difftest/cases/0x004E1580.json

Written 2026-10-30; the test tests/TESObjectREFR_LoadBaseFormModelsEmulatorTest.cpp
transcribes the emulator result of those cases (tools/difftest/expected/0x004E1580.json)."""
import json
import struct
import sys

THIS = 0x10000000
REF_VT = 0x10001000
FORM = 0x10002000
OTHERFORM = 0x10002800
FORM_VT = 0x10003000
MODEL_VT = 0x10003800
BASEPATH = 0x10004000
NAMES = [0x10004100, 0x10004140, 0x10004180]
FLOAT = 0x10004F00
LIST = 0x10005000
NINODE = 0x10006000
NODE2 = 0x10006800
LOOKUP = 0x10007000
CBOL = 0x10008000
MODEL = 0x10009000
MODEL2 = 0x10009800
CREATED = 0x1000A000
CLONED = 0x1000B000
TARGET = 0x1000C000
TGT_VT = 0x1000C800
AT1C = 0x1000D000
RECV_VT = 0x1000D800
NAMED = 0x1000E000
SLOT0 = 0x1000F000
HELPER = 0x1000F800
SLOT4 = 0x10010000
DEL_VT = 0x10010800
LOG = 0x10011000
RESULTS = 0x10011800
SCALE_CREATE = 0x10011F00
SCALE_CLONE = 0x10011F10
LOGPTR = 0x10011FF0
RESPTR = 0x10011FF4
TES = 0x10013000
LOADER = 0x10014000

VFN168, VFN164, VFN190, VFNEC, VFNFC, VFN14, VFN58, VFN84, VFN0, IDEC = (
    0x20000100, 0x20000200, 0x20000300, 0x20000400, 0x20000500, 0x20000600, 0x20000700, 0x20000800, 0x20000900, 0x20000A00)

BASE_PATH = b'meshes\\creatures\\wolf\\skeleton.nif\0'
NAME_TEXT = [b'wolfhead.nif\0', b'wolfbody.nif\0', b'wolftail.nif\0']
SCALE = 0.75


def d(v):
    return struct.pack('<I', v & 0xFFFFFFFF)


def hexs(b):
    return ' '.join('%02x' % x for x in b)


def code_strrchr():
    # mov ecx,[esp+4]; mov dl,[esp+8]; xor eax,eax; L: mov dh,[ecx]; cmp dh,dl; jne N; mov eax,ecx;
    # N: test dh,dh; je D; inc ecx; jmp L; D: ret   (assembled with llvm-mc)
    return '8b 4c 24 04 8a 54 24 08 31 c0 8a 31 38 d6 75 02 89 c8 84 f6 74 03 41 eb f1 c3'


def code_load():
    # copy the path argument to [LOGPTR] (advancing it), return the next dword of [RESPTR], ret 0x10
    return hexs(b'\xa1' + d(LOGPTR) + bytes.fromhex('8b542404 8a0a 8808 40 42 84c9 75f6') + b'\xa3' + d(LOGPTR) +
                b'\xa1' + d(RESPTR) + bytes.fromhex('8b08 83c004') + b'\xa3' + d(RESPTR) + bytes.fromhex('89c8 c21000'))


def code_ctor(s0, s4):
    # mov dword [ecx], s0; mov dword [ecx+4], s4; ret
    return hexs(b'\xc7\x01' + d(s0) + b'\xc7\x41\x04' + d(s4) + b'\xc3')


def code_copy_scale(arg_offset, log, result, ret):
    # mov eax,[esp+arg]; copy the context's +0x10/+0x14/+0x18 to log; mov eax,result; ret N
    b = b'\x8b\x44\x24' + bytes([arg_offset])
    for i in range(3):
        b += b'\x8b\x50' + bytes([0x10 + 4 * i]) + b'\x89\x15' + d(log + 4 * i)
    b += b'\xb8' + d(result) + b'\xc2' + struct.pack('<H', ret)
    return hexs(b)


def code_store(pairs):
    # mov dword [address], value ... ; ret
    b = b''
    for address, value in pairs:
        b += b'\xc7\x05' + d(address) + d(value)
    return hexs(b + b'\xc3')


NIPOINTER_ASSIGN = '8b 44 24 04 85 c0 74 03 ff 40 04 89 01 c2 04 00'  # value -> [ecx], inc [value+4] when non-null, ret 4
INTERLOCKED_DECREMENT = '8b 44 24 04 ff 08 8b 00 c2 04 00'


def build(case):
    p = dict(
        type=0x24, vfn168=0, vfn164=0, ninode=NINODE, vfn190=0, lookup=None, cbol=0, probe=0, names=None,
        loads=[MODEL], r480820=[0], r471b80=[0], find=[0], vfn58=[0], refcount=0, slots=(0, 0), vfnfc=0,
        after4728c0=None, otherform_type=0x24)
    p.update(case['p'])
    mem = {}

    def put(address, data):
        for i, byte in enumerate(data):
            assert address + i not in mem, hex(address + i)
            mem[address + i] = byte

    put(0, d(0xDEADBEEF))  # FS:[0], the previous SEH frame
    put(0x00A2807C, d(IDEC))  # the IAT slot of InterlockedDecrement
    put(0x00B33B00, d(CBOL))
    put(0x00B333A0, d(TES))
    put(0x00B33A1C, d(LOADER))
    this = bytearray(0x40)
    this[0:4] = d(REF_VT)
    this[0x1C:0x20] = d(FORM)
    this[0x3C:0x40] = d(p['ninode'])
    put(THIS, this)
    vt = bytearray(0x194)
    vt[0xEC:0xF0] = d(VFNEC)
    vt[0x164:0x168] = d(VFN164)
    vt[0x168:0x16C] = d(VFN168)
    vt[0x190:0x194] = d(VFN190)
    put(REF_VT, vt)
    for form, type_id in ((FORM, p['type']), (OTHERFORM, p['otherform_type'])):
        f = bytearray(0xF0)
        f[0:4] = d(FORM_VT)
        f[4] = type_id
        f[0xAC:0xB0] = d(MODEL_VT)
        put(form, f)
    fvt = bytearray(0x100)
    fvt[0xFC:0x100] = d(VFNFC)
    put(FORM_VT, fvt)
    mvt = bytearray(0x18)
    mvt[0x14:0x18] = d(VFN14)
    put(MODEL_VT, mvt)
    put(BASEPATH, BASE_PATH)
    for address, text in zip(NAMES, NAME_TEXT):
        put(address, text)
    put(FLOAT, struct.pack('<f', SCALE))
    head = 0
    if p['names'] is not None:
        head = LIST
        for i, name in enumerate(p['names']):
            data = NAMES[name] if name is not None else 0
            nxt = LIST + 0x10 * (i + 1) if i + 1 < len(p['names']) else 0
            put(LLIST(i), d(data) + d(nxt))
    lk = bytearray(0x20)
    lk[0x1C:0x20] = d(p['lookup'] or 0)
    put(LOOKUP, lk)
    cb = bytearray(0x1C)
    cb[0x18:0x1C] = d(p['cbol'])
    put(CBOL, cb)
    put(CREATED, d(DEL_VT) + d(p['refcount']))
    put(SLOT0, d(DEL_VT))
    put(SLOT4, d(DEL_VT))
    put(DEL_VT, d(VFN0))
    t = bytearray(0x20)
    t[0:4] = d(TGT_VT)
    t[0x1C:0x20] = d(AT1C)
    put(TARGET, t)
    tvt = bytearray(0x5C)
    tvt[0x58:0x5C] = d(VFN58)
    put(TGT_VT, tvt)
    put(AT1C, d(RECV_VT))
    put(NAMED, d(RECV_VT))
    rvt = bytearray(0x88)
    rvt[0x84:0x88] = d(VFN84)
    put(RECV_VT, rvt)
    put(LOG, bytes(0x200))
    put(RESULTS, b''.join(d(v) for v in p['loads']))
    put(SCALE_CREATE, bytes(0x20))
    put(LOGPTR, d(LOG) + d(RESULTS))

    # contiguous runs
    memory = {}
    addresses = sorted(mem)
    start = prev = None
    run = bytearray()
    for a in addresses:
        if start is None or a != prev + 1:
            if start is not None:
                memory['0x%08X' % start] = hexs(run)
            start, run = a, bytearray()
        run.append(mem[a])
        prev = a
    memory['0x%08X' % start] = hexs(run)

    stubs = {
        '0x%08X' % VFN168: {'ret': 0, 'eax': '0x%X' % p['vfn168']},
        '0x%08X' % VFN164: {'ret': 0, 'eax': '0x%X' % p['vfn164']},
        '0x%08X' % VFN190: {'ret': 0, 'eax': '0x%X' % p['vfn190']},
        '0x%08X' % VFNEC: {'code': hexs(b'\xd9\x05' + d(FLOAT) + b'\xc3')},
        '0x%08X' % VFNFC: {'ret': 0, 'eax': '0x%X' % p['vfnfc']},
        '0x%08X' % VFN14: {'ret': 0, 'eax': '0x%08X' % BASEPATH},
        '0x%08X' % VFN58: {'ret': 4, 'eax': ['0x%08X' % v for v in p['vfn58']]},
        '0x%08X' % VFN84: {'ret': 8, 'eax': '0x0'},
        '0x%08X' % VFN0: {'ret': 4, 'eax': '0x0'},
        '0x%08X' % IDEC: {'code': INTERLOCKED_DECREMENT},
        '0x0047D120': {'ret': 0, 'eax': '0x0'},
        '0x0047D0F0': {'ret': 0, 'eax': '0x0'},
        '0x004728C0': ({'code': code_store(p['after4728c0'])} if p['after4728c0'] else {'ret': 0, 'eax': '0x0'}),
        '0x00526DB0': {'ret': 4, 'eax': '0x0'},
        '0x005E0380': {'ret': 0, 'eax': '0x%08X' % (LOOKUP if p['lookup'] is not None else 0)},
        '0x004533F0': {'ret': 8, 'eax': '0x%08X' % p['probe']},
        '0x0051D460': {'ret': 4, 'eax': '0x0'},
        '0x0051E240': {'ret': 16, 'eax': '0x0'},
        '0x006899C0': {'ret': 0, 'eax': '0x%08X' % head},
        '0x00983FD0': {'code': code_strrchr()},
        '0x00439EB0': {'code': code_load()},
        '0x00478B90': {'code': code_ctor(*p['slots'])},
        '0x00480820': {'ret': 0, 'eax': ['0x%X' % v for v in p['r480820']]},
        '0x004430C0': {'code': code_copy_scale(8, SCALE_CREATE, CREATED, 8)},
        '0x0075FA90': {'code': NIPOINTER_ASSIGN},
        '0x00700610': {'code': code_copy_scale(4, SCALE_CLONE, CLONED, 4)},
        '0x00471B80': {'ret': 0, 'eax': ['0x%X' % v for v in p['r471b80']]},
        '0x004D96F0': {'ret': 8, 'eax': ['0x%08X' % v for v in p['find']]},
        '0x00478EC0': {'ret': 0, 'eax': '0x0'},
        '0x00479140': {'ret': 0, 'eax': '0x0'},
        '0x009811E2': {'code': 'c3'},  # ret, EAX untouched: AL is the result set at 0x004E18CB before the call
    }
    return {
        'name': case['name'],
        'regs': {'ecx': '0x%08X' % THIS},
        'memory': memory,
        'stubs': stubs,
        'readback': [
            {'address': '0x%08X' % LOG, 'size': 0x80},
            {'address': '0x%08X' % SCALE_CREATE, 'size': 0x20},
            {'address': '0x%08X' % (CREATED + 4), 'size': 4},
            {'address': '0x0', 'size': 4},
        ],
    }


def LLIST(i):
    return LIST + 0x10 * i


CASES = [
    {'name': 'a1 other type 0x10, vfn[0xFC] returns 1', 'p': {'type': 0x10, 'vfnfc': 1}},
    {'name': 'a2 other type 0x10, vfn[0xFC] returns 0', 'p': {'type': 0x10, 'vfnfc': 0}},
    {'name': 'b1 type 0x23, vfn[0x168] returns 1: 0x00526DB0', 'p': {'type': 0x23, 'vfn168': 1}},
    {'name': 'b2 type 0x23, vfn[0x168] returns 0: no 0x00526DB0', 'p': {'type': 0x23, 'vfn168': 0}},
    {'name': 'b3 type 0x23, vfn[0x168] returns 0x100 (low byte 0): the whole dword is tested', 'p': {'type': 0x23, 'vfn168': 0x100}},
    {'name': 'c type 0x24, niNode null: nothing built', 'p': {'ninode': 0}},
    {'name': 'd type 0x24, vfn[0x190] 0, probe bit clear: 0x0051E240(this, 1, 1, 0), empty list', 'p': {}},
    {'name': 'e1 lookup flags bit 20: 0x0051E240(this, 0, 1, 0)', 'p': {'vfn190': 1, 'lookup': 1 << 20}},
    {'name': 'e2 lookup flags bit 21: 0x0051E240(this, 1, 0, 0)', 'p': {'vfn190': 1, 'lookup': 1 << 21}},
    {'name': 'e3 lookup flags bits 20 and 21: 0x0051E240(this, 0, 0, 0)', 'p': {'vfn190': 1, 'lookup': 3 << 20}},
    {'name': 'e4 lookup returns null: 0x0051E240(this, 1, 1, 0)', 'p': {'vfn190': 1, 'lookup': None}},
    {'name': 'f1 probe bit set, 0x004533F0 returns 0x08000000: 0x0051D460', 'p': {'cbol': 2, 'probe': 0x08000000}},
    {'name': 'f2 probe bit set, 0x004533F0 returns 0xF7FFFFFF, lookup bit 20: 0x0051E240(this, 0, 1, 0)',
     'p': {'cbol': 2, 'probe': 0xF7FFFFFF, 'vfn190': 1, 'lookup': 1 << 20}},
    {'name': 'g two entries: create + SkinAttachment found by name + delete at 0; clone + plain attach',
     'p': {'names': [0, 1], 'loads': [MODEL, MODEL2], 'r480820': [1, 0], 'r471b80': [1, 0], 'find': [TARGET, TARGET],
           'vfn58': [NAMED], 'refcount': 0}},
    {'name': 'h vfn[0x58] returns null: vfn[0x84] on the object at target+0x1C',
     'p': {'names': [0], 'r480820': [0], 'r471b80': [1], 'find': [TARGET], 'vfn58': [0]}},
    {'name': 'i first load returns null: the second entry is loaded',
     'p': {'names': [0, 1], 'loads': [0, MODEL2], 'r480820': [0], 'r471b80': [0]}},
    {'name': 'j vfn[0x164] returns an object: 0x004728C0 on it', 'p': {'type': 0x10, 'vfn164': HELPER}},
    {'name': 'k context slots non-null: both get vfn[0](1)',
     'p': {'names': [0], 'r480820': [0], 'r471b80': [0], 'slots': (SLOT0, SLOT4)}},
    {'name': 'n NiPointer decrement leaves 1: no delete',
     'p': {'names': [0], 'r480820': [1], 'r471b80': [0], 'refcount': 1}},
    {'name': 'o second node has data 0 and next 0: the walk ends after the first',
     'p': {'names': [0, None], 'r480820': [0], 'r471b80': [0]}},
    {'name': 'p head node has data 0 and next 0: no entry visited', 'p': {'names': [None]}},
    {'name': 'q 0x00471B80 true, 0x004D96F0 null: plain attach',
     'p': {'names': [0], 'r480820': [0], 'r471b80': [1], 'find': [0]}},
    {'name': 'l 0x004728C0 changes this+0x1C (type 0x24) and this+0x3C: type and node re-read, builder/list/path on the cached form',
     'p': {'type': 0x10, 'ninode': 0, 'vfn164': HELPER, 'otherform_type': 0x24,
           'after4728c0': [(THIS + 0x1C, OTHERFORM), (THIS + 0x3C, NODE2)],
           'names': [0], 'r480820': [0], 'r471b80': [0]}},
    {'name': 'r 0x004728C0 changes this+0x1C (type 0x23): 0x00526DB0 on the form read before vfn[0x168]',
     'p': {'type': 0x10, 'vfn168': 1, 'vfn164': HELPER, 'otherform_type': 0x23,
           'after4728c0': [(THIS + 0x1C, OTHERFORM)]}},
    {'name': 'm 0x004728C0 changes this+0x1C (type 0x10): vfn[0xFC] on the re-read form',
     'p': {'type': 0x24, 'vfn164': HELPER, 'otherform_type': 0x10, 'vfnfc': 1,
           'after4728c0': [(THIS + 0x1C, OTHERFORM)]}},
]

NOTE = (
    "Layer-1 differential cases for 0x004E1580 (proposed TESObjectREFR::LoadBaseFormModels). Synthetic memory: "
    "this 0x10000000 (vtable 0x10001000: +0xEC 0x20000400, +0x164 0x20000200, +0x168 0x20000100, +0x190 0x20000300; "
    "+0x1C base form 0x10002000; +0x3C niNode 0x10006000), base form (vtable 0x10003000: +0xFC 0x20000500; +0x04 type; "
    "+0xAC component with vtable 0x10003800: +0x14 0x20000600 returning the path 0x10004000), list nodes from 0x10005000 "
    "({data, next}, names at 0x10004100/0x10004140), 0x005E0380's object 0x10007000 (+0x1C flags), [0x00B33B00] = 0x10008000 "
    "(+0x18 flags), [0x00B333A0] g_TES = 0x10013000, [0x00B33A1C] g_modelLoader = 0x10014000, models 0x10009000/0x10009800, "
    "created node 0x1000A000 (vtable 0x10010800: slot 0 0x20000900; +0x04 refcount), cloned 0x1000B000, 0x004D96F0's target "
    "0x1000C000 (vtable +0x58 0x20000700; +0x1C 0x1000D000), named object 0x1000E000 (vtable +0x84 0x20000800), context slot "
    "objects 0x1000F000/0x10010000, vfn[0x164]'s object 0x1000F800. FS:[0] at address 0. The IAT slot 0x00A2807C points at "
    "0x20000A00 (InterlockedDecrement as raw code). Raw-code stubs: 0x00983FD0 a real strrchr; 0x00439EB0 copies its path "
    "argument to the log 0x10011000 (pointer at 0x10011FF0) and returns the next dword of the table 0x10011800 (pointer at "
    "0x10011FF4), ret 0x10; 0x00478B90 stores the case's two context slots ([ecx], [ecx+4]); 0x004430C0 (ret 8) and "
    "0x00700610 (ret 4) copy the context's +0x10/+0x14/+0x18 to 0x10011F00 / 0x10011F10 and return 0x1000A000 / 0x1000B000; "
    "0x0075FA90 stores the value into [ecx] and increments the value's +0x04 when non-null (the increment is an assumption, "
    "unverified: 0x0075FA90's body is not exported); vfn 0xEC loads 0.75f from 0x10004F00 into ST0. Every other callee is a "
    "plain traced stub with the ret N the body's stack use requires (re/functions/0x004E1580.md).")


def main():
    out = {'function': '0x004E1580', 'note': NOTE, 'cases': [build(c) for c in CASES]}
    json.dump(out, sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    main()
