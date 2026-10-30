#!/usr/bin/env python3
"""Run one exported Oblivion.exe function in an x86-32 emulator and report
what it did: the registers it left, every memory write (address, size,
value) and the bytes of chosen regions afterwards.

This is the original's side of the differential test (docs/re-workflow.md,
"the gate outside the workflow", layer 1): the rebuild runs the same cases
natively and must produce the same writes and results.

    emulate.py <export dir> <cases.json> [--json]

The export dir is one function's directory under re/export/<slice>/ (it
needs bytes.bin, meta.json and data.json from tools/ghidra/ExportFunctions).
Memory is set up from data.json (every referenced data address gets its
dumped bytes, so constants are the binary's), then from the case.

A cases file:

    {
      "cases": [
        {
          "name": "variable mode, 13 ms frame",
          "regs": {"ecx": "0x00B33E90"},
          "stack": ["0x00001000"],              # pushed last first: the arguments as the caller pushes them
          "memory": {"0x00B33E90": "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 f3 0f 00 00 00 00 00 00"},
          "fpcw": "0x027F",                      # optional x87 control word (default: the emulator's)
          "stubs": {                             # optional: callees replaced by stubs
            "0x00401F00": {"ret": 4, "eax": "0x1"},          # returns eax, ret N (ret 0 = plain ret)
            "0x00401F20": {"ret": 0, "eax": ["0x1", "0x0"]}, # one value per call, the last repeats
            "0x00401F40": {"code": "8b 44 24 04 ff 00 c2 04 00"}  # raw code, run as is
          },
          "readback": [{"address": "0x00B33E90", "size": 24}]
        }
      ]
    }

Numbers are hex strings or integers. Every case runs until the function
returns to the sentinel return address; a jump or call to unmapped memory
that no stub covers stops the case with an error, never silently.

Every stub entry is traced: the result's "calls" lists them in order with
the return address ("from", the instruction after the CALL), ECX, EDX, EAX
on entry and the first six stack dwords above the return address ("args").
A vtable slot that points at a stub makes a virtual call traceable too.
Writes to the emulator's own stack are not recorded; writes everywhere else
are. The FS segment has base 0 (no TEB): code that links an SEH frame
through FS:[0] needs 4 bytes mapped at address 0 by the case ("memory").
"""
import json
import struct
import sys
from pathlib import Path

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_FETCH_UNMAPPED,
                     UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED, UcError)
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
                               UC_X86_REG_EIP, UC_X86_REG_FPCW)

PAGE = 0x1000
STACK_BASE = 0x7F000000
STACK_SIZE = 0x10000
SENTINEL = 0x7E000000  # the return address pushed for the function; execution stops when it is fetched

REGS = {'eax': UC_X86_REG_EAX, 'ebx': UC_X86_REG_EBX, 'ecx': UC_X86_REG_ECX, 'edx': UC_X86_REG_EDX,
        'esi': UC_X86_REG_ESI, 'edi': UC_X86_REG_EDI, 'ebp': UC_X86_REG_EBP, 'esp': UC_X86_REG_ESP}


def num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def page_down(a):
    return a & ~(PAGE - 1)


class Emulation:
    def __init__(self, export_dir):
        self.export = Path(export_dir)
        self.meta = json.loads((self.export / 'meta.json').read_text())
        self.data = json.loads((self.export / 'data.json').read_text())
        self.code = (self.export / 'bytes.bin').read_bytes()
        self.code_base = int(self.meta['bytes_bin']['from'], 16)
        self.entry = int(self.meta['entry'], 16)

    def _map(self, uc, address, size):
        start = page_down(address)
        end = page_down(address + size - 1) + PAGE
        for page in range(start, end, PAGE):
            if page not in self.mapped:
                uc.mem_map(page, PAGE)
                self.mapped.add(page)

    def run(self, case):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.mapped = set()
        writes = []
        errors = []

        self._map(uc, self.code_base, len(self.code))
        uc.mem_write(self.code_base, self.code)
        for item in self.data['data']:
            address = int(item['address'], 16)
            raw = bytes.fromhex(item['bytes'].replace(' ', ''))
            if raw:
                self._map(uc, address, len(raw))
                uc.mem_write(address, raw)
        for address, text in (case.get('memory') or {}).items():
            raw = bytes.fromhex(text.replace(' ', ''))
            self._map(uc, num(address), len(raw))
            uc.mem_write(num(address), raw)
        calls = []
        stubs = {num(address): stub for address, stub in (case.get('stubs') or {}).items()}
        for a, stub in stubs.items():
            if 'code' in stub:
                code = bytes.fromhex(stub['code'].replace(' ', ''))
            else:
                ret = num(stub.get('ret', 0))
                code = (b'\xc2' + struct.pack('<H', ret)) if ret else b'\xc3'  # ret N / ret; eax set on entry
            self._map(uc, a, len(code))
            uc.mem_write(a, code)
            stub['_hits'] = 0

        def on_stub(u, address, size, user):
            stub = stubs[address]
            eax_in = u.reg_read(UC_X86_REG_EAX)
            if 'code' not in stub:
                values = stub.get('eax', 0)
                if isinstance(values, list):
                    values = values[min(stub['_hits'], len(values) - 1)] if values else 0
                u.reg_write(UC_X86_REG_EAX, num(values) & 0xFFFFFFFF)
            stub['_hits'] += 1
            sp = u.reg_read(UC_X86_REG_ESP)
            frame = u.mem_read(sp, 4 * 7)
            words = struct.unpack('<7I', bytes(frame))
            calls.append({'stub': '%08x' % address, 'from': '%08x' % words[0],
                          'ecx': '%08x' % u.reg_read(UC_X86_REG_ECX), 'edx': '%08x' % u.reg_read(UC_X86_REG_EDX),
                          'eax': '%08x' % eax_in, 'esp': '%08x' % sp,
                          'args': ['%08x' % w for w in words[1:]]})

        for a in stubs:
            uc.hook_add(UC_HOOK_CODE, on_stub, begin=a, end=a)
        self._map(uc, SENTINEL, PAGE)
        uc.mem_map(STACK_BASE, STACK_SIZE)
        self.mapped.add(STACK_BASE)

        esp = STACK_BASE + STACK_SIZE - 0x100
        for arg in reversed(case.get('stack') or []):
            esp -= 4
            uc.mem_write(esp, struct.pack('<I', num(arg) & 0xFFFFFFFF))
        esp -= 4
        uc.mem_write(esp, struct.pack('<I', SENTINEL))
        uc.reg_write(UC_X86_REG_ESP, esp)
        for name, value in (case.get('regs') or {}).items():
            uc.reg_write(REGS[name.lower()], num(value) & 0xFFFFFFFF)
        if 'fpcw' in case:
            uc.reg_write(UC_X86_REG_FPCW, num(case['fpcw']))

        def on_write(u, access, address, size, value, user):
            if STACK_BASE <= address < STACK_BASE + STACK_SIZE:
                return
            writes.append({'at': '%08x' % u.reg_read(UC_X86_REG_EIP), 'address': '%08x' % address,
                           'size': size, 'value': value & ((1 << (8 * size)) - 1)})

        def on_unmapped(u, access, address, size, value, user):
            errors.append('unmapped access at eip %08x: address %08x size %d' % (u.reg_read(UC_X86_REG_EIP), address, size))
            return False

        uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
        uc.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, on_unmapped)

        try:
            uc.emu_start(self.entry, SENTINEL, timeout=2_000_000, count=1_000_000)
        except UcError as e:
            errors.append('emulation stopped: %s (eip %08x)' % (e, uc.reg_read(UC_X86_REG_EIP)))

        result = {
            'name': case.get('name', ''),
            'regs': {name: '%08x' % uc.reg_read(reg) for name, reg in REGS.items()},
            'returned': uc.reg_read(UC_X86_REG_EIP) == SENTINEL,
            'stack_delta': uc.reg_read(UC_X86_REG_ESP) - (esp + 4),
            'writes': writes,
            'calls': calls,
            'readback': {},
            'errors': errors,
        }
        for region in case.get('readback') or []:
            a, size = num(region['address']), int(region['size'])
            result['readback']['%08x' % a] = uc.mem_read(a, size).hex(' ')
        return result


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    emu = Emulation(argv[1])
    cases = json.loads(Path(argv[2]).read_text())['cases']
    results = [emu.run(c) for c in cases]
    if '--json' in argv:
        print(json.dumps(results, indent=1))
        return 0
    for r in results:
        print('== %s' % r['name'])
        print('  returned=%s stack_delta=%+d eax=%s' % (r['returned'], r['stack_delta'], r['regs']['eax']))
        for w in r['writes']:
            print('  write @%s [%s] size %d = 0x%0*x' % (w['at'], w['address'], w['size'], w['size'] * 2, w['value']))
        for c in r['calls']:
            print('  call  %s from %s ecx=%s edx=%s eax=%s args %s' % (c['stub'], c['from'], c['ecx'], c['edx'], c['eax'], ' '.join(c['args'])))
        for a, b in r['readback'].items():
            print('  mem %s: %s' % (a, b))
        for e in r['errors']:
            print('  ERROR %s' % e)
    return 1 if any(r['errors'] for r in results) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
