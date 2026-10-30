#!/usr/bin/env python3
"""Self-test of emulate.py's stub features on a hand-assembled function:
a scalar stub with ret N, a stub with one value per call, a raw-code stub
that writes memory, the call trace, and the stack-write exclusion.

    emulate_test.py        (exit 0 when every check holds)
"""
import json
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from emulate import Emulation  # noqa: E402

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
    for f in failures:
        print('FAIL ' + f)
    print('%d checks, %d failed' % (8, len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
