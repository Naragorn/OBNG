#!/usr/bin/env python3
"""Self-test of emulate.py's stub features on a hand-assembled function:
a scalar stub with ret N, a stub with one value per call, a raw-code stub
that writes memory, the call trace, and the stack-write exclusion; and of
the "image" ranges: bytes taken from a PE file's section by address, run as
code, the refusals (no image, a range outside the sections, a wrong md5).

    emulate_test.py        (exit 0 when every check holds)
"""
import json
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from emulate import Emulation, Image  # noqa: E402

ENTRY = 0x00410000
STUB_A, STUB_B, STUB_C = 0x7D000000, 0x7D000100, 0x7D000200
RESULTS = 0x00500000


def call_absolute(target):
    return b'\xb8' + struct.pack('<I', target) + b'\xff\xd0'  # mov eax, target; call eax


def function_bytes():
    code = b'\xb9' + struct.pack('<I', 0x11223344)        # mov ecx, 11223344h
    code += b'\x6a\x55'                                    # push 55h
    code += call_absolute(STUB_A)                          # A(55h) -> eax, ret 4
    code += b'\xa3' + struct.pack('<I', RESULTS)           # mov [RESULTS], eax
    code += call_absolute(STUB_B) + call_absolute(STUB_B)  # B() twice: 1 then 2
    code += b'\xa3' + struct.pack('<I', RESULTS + 4)       # mov [RESULTS+4], eax
    code += b'\x68' + struct.pack('<I', RESULTS + 8)       # push RESULTS+8
    code += call_absolute(STUB_C)                          # C(&counter): raw code increments it
    code += b'\xc3'                                        # ret
    return code


def main():
    with tempfile.TemporaryDirectory() as tmp:
        export = Path(tmp)
        code = function_bytes()
        (export / 'bytes.bin').write_bytes(code)
        (export / 'meta.json').write_text(json.dumps({'entry': '%08x' % ENTRY, 'bytes_bin': {'from': '%08x' % ENTRY, 'length': len(code)}}))
        (export / 'data.json').write_text(json.dumps({'data': [], 'jump_tables': []}))
        case = {
            'name': 'stubs',
            'memory': {'0x%08X' % RESULTS: '00 ' * 16},
            'stubs': {
                '0x%08X' % STUB_A: {'ret': 4, 'eax': '0x7'},
                '0x%08X' % STUB_B: {'ret': 0, 'eax': ['0x1', '0x2']},
                '0x%08X' % STUB_C: {'code': '8b 44 24 04 ff 00 c2 04 00'},  # mov eax,[esp+4]; inc dword [eax]; ret 4
            },
            'readback': [{'address': '0x%08X' % RESULTS, 'size': 12}],
        }
        r = Emulation(export).run(case)

    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    check(r['errors'] == [], 'no errors: %s' % r['errors'])
    check(r['returned'] and r['stack_delta'] == 0, 'returned to the sentinel with a balanced stack')
    stubs = [c['stub'] for c in r['calls']]
    check(stubs == ['%08x' % STUB_A, '%08x' % STUB_B, '%08x' % STUB_B, '%08x' % STUB_C], 'the four calls in order: %s' % stubs)
    check(r['calls'][0]['ecx'] == '11223344' and r['calls'][0]['args'][0] == '00000055', 'A sees ECX and its stack argument')
    check(r['calls'][0]['from'] == '%08x' % (ENTRY + 5 + 2 + 7), 'the return address is the instruction after the CALL')
    check(r['readback']['%08x' % RESULTS] == '07 00 00 00 02 00 00 00 01 00 00 00',
          'A returned 7, B returned 1 then 2, C incremented the counter: %s' % r['readback'])
    check(r['calls'][3]['args'][0] == '%08x' % (RESULTS + 8), 'C sees the pointer argument')
    non_stack = [w['address'] for w in r['writes']]
    check(non_stack == ['%08x' % RESULTS, '%08x' % (RESULTS + 4), '%08x' % (RESULTS + 8)],
          'only the three non-stack writes are recorded (the pushes are not): %s' % non_stack)
    checks = 8 + image_checks(check)
    for f in failures:
        print('FAIL ' + f)
    print('%d checks, %d failed' % (checks, len(failures)))
    return 1 if failures else 0


IMAGE_BASE = 0x00400000
TEXT_RVA = 0x1000
CALLEE = IMAGE_BASE + TEXT_RVA + 0x10  # inside the fake image's .text


def fake_pe(text):
    """A minimal PE32: DOS stub pointer, PE signature, file header, a 96-byte
    optional header with the image base, one .text section at RVA 0x1000."""
    pe = 0x40
    optional_size = 96
    table = pe + 24 + optional_size
    raw_offset = 0x200
    data = bytearray(raw_offset + len(text))
    data[0:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, pe)
    data[pe:pe + 4] = b'PE\0\0'
    struct.pack_into('<HH12xHH', data, pe + 4, 0x014C, 1, optional_size, 0x0102)
    struct.pack_into('<H', data, pe + 24, 0x010B)
    struct.pack_into('<I', data, pe + 24 + 28, IMAGE_BASE)
    struct.pack_into('<8sIIII', data, table, b'.text', len(text), TEXT_RVA, len(text), raw_offset)
    data[raw_offset:] = text
    return bytes(data)


def image_checks(check):
    callee = b'\xb8\x2a\x00\x00\x00\xc3'  # mov eax, 42; ret
    text = b'\xcc' * 0x10 + callee
    code = call_absolute(CALLEE) + b'\xa3' + struct.pack('<I', RESULTS) + b'\xc3'  # [RESULTS] = callee()
    with tempfile.TemporaryDirectory() as tmp:
        export = Path(tmp) / 'export'
        export.mkdir()
        (export / 'bytes.bin').write_bytes(code)
        (export / 'meta.json').write_text(json.dumps({'entry': '%08x' % ENTRY, 'bytes_bin': {'from': '%08x' % ENTRY, 'length': len(code)}}))
        (export / 'data.json').write_text(json.dumps({'data': [], 'jump_tables': []}))
        exe = Path(tmp) / 'fake.exe'
        exe.write_bytes(fake_pe(text))
        image = Image(exe, expected_md5=None)
        case = {'name': 'image', 'memory': {'0x%08X' % RESULTS: '00 00 00 00'},
                'image': [{'address': '0x%08X' % CALLEE, 'size': len(callee)}],
                'readback': [{'address': '0x%08X' % RESULTS, 'size': 4}]}
        r = Emulation(export, image).run(case)
        check(r['errors'] == [] and r['readback']['%08x' % RESULTS] == '2a 00 00 00',
              'the image range ran as code: %s %s' % (r['errors'], r['readback']))
        check(image.read(CALLEE, 2) == callee[:2], 'Image.read by virtual address')
        stubbed = dict(case, image=[], stubs={'0x%08X' % CALLEE: {'image': len(callee)}})
        r = Emulation(export, image).run(stubbed)
        check(r['errors'] == [] and r['readback']['%08x' % RESULTS] == '2a 00 00 00' and
              [c['stub'] for c in r['calls']] == ['%08x' % CALLEE],
              'an image stub runs the image bytes and is traced: %s %s %s' % (r['errors'], r['readback'], r['calls']))
        refused = []
        for what, run in (
                ('no image', lambda: Emulation(export).run(case)),
                ('no image for a stub', lambda: Emulation(export).run(stubbed)),
                ('outside the sections', lambda: image.read(IMAGE_BASE + TEXT_RVA + len(text) - 2, 4)),
                ('wrong md5', lambda: Image(exe))):
            try:
                run()
            except ValueError:
                refused.append(what)
        check(refused == ['no image', 'no image for a stub', 'outside the sections', 'wrong md5'],
              'the four refusals raise: %s' % refused)
    return 4


if __name__ == '__main__':
    sys.exit(main())
