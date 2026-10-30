#!/usr/bin/env python3
"""Run one exported Oblivion.exe function in an x86-32 emulator and report
what it did: the registers it left, every memory write (address, size,
value) and the bytes of chosen regions afterwards.

This is the original's side of the differential test (docs/re-workflow.md,
"the gate outside the workflow", layer 1): the rebuild runs the same cases
natively and must produce the same writes and results.

    emulate.py <export dir> <cases.json> [--json] [--image <Oblivion.exe>]

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
            "0x00401F40": {"code": "8b 44 24 04 ff 00 c2 04 00"},  # raw code, run as is
            "0x0047DA70": {"image": 92}            # the image's own 92 bytes there, run as is (see "image")
          },
          "image": [{"address": "0x00982C4D", "size": 157}],  # optional: the game's own bytes, see below
          "readback": [{"address": "0x00B33E90", "size": 24}]
        }
      ]
    }

Numbers are hex strings or integers. Every case runs until the function
returns to the sentinel return address; a jump or call to unmapped memory
that no stub covers stops the case with an error, never silently.

"image" copies address ranges of the reference Oblivion.exe (md5
cdd2f0c5eff198d4f26b7b5b54ce4930, the Steam GOTY 1.2.0.416 build) into
memory before the case's own "memory" - so a case can run the game's own
code (a CRT helper behind a stub, say) without its bytes in the repository.
The executable is given with --image or the environment variable
OBNG_ORACLE_EXE; a case with "image" and no executable, a different md5, or
a range outside the file's sections is an error, never silently skipped.

Every stub entry is traced: the result's "calls" lists them in order with
the return address ("from", the instruction after the CALL), ECX, EDX, EAX
on entry and the first six stack dwords above the return address ("args").
A vtable slot that points at a stub makes a virtual call traceable too.
Writes to the emulator's own stack are not recorded; writes everywhere else
are. The FS segment has base 0 (no TEB): code that links an SEH frame
through FS:[0] needs 4 bytes mapped at address 0 by the case ("memory").
"""
import hashlib
import json
import os
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


REFERENCE_MD5 = 'cdd2f0c5eff198d4f26b7b5b54ce4930'


class Image:
    """The sections of a PE file, read by virtual address."""

    def __init__(self, path, expected_md5=REFERENCE_MD5):
        self.raw = Path(path).read_bytes()
        digest = hashlib.md5(self.raw).hexdigest()
        if expected_md5 is not None and digest != expected_md5:
            raise ValueError('%s has md5 %s, not the reference %s' % (path, digest, expected_md5))
        pe = struct.unpack_from('<I', self.raw, 0x3C)[0]
        if self.raw[pe:pe + 4] != b'PE\0\0':
            raise ValueError('%s is not a PE file' % path)
        sections, optional_size = struct.unpack_from('<H12xH', self.raw, pe + 6)
        image_base = struct.unpack_from('<I', self.raw, pe + 24 + 28)[0]
        table = pe + 24 + optional_size
        self.sections = []
        for i in range(sections):
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<IIII', self.raw, table + 40 * i + 8)
            self.sections.append((image_base + virtual_address, min(virtual_size, raw_size), raw_offset))

    def read(self, address, size):
        for start, length, offset in self.sections:
            if start <= address and address + size <= start + length:
                return self.raw[offset + address - start:offset + address - start + size]
        raise ValueError('image range %08x+%d is not inside one section\'s file bytes' % (address, size))


class Emulation:
    def __init__(self, export_dir, image=None):
        self.image = image  # an Image, or None
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
        for region in case.get('image') or []:
            if self.image is None:
                raise ValueError('case %r asks for image bytes and no executable was given (--image or OBNG_ORACLE_EXE)'
                                 % case.get('name', ''))
            a, size = num(region['address']), int(region['size'])
            self._map(uc, a, size)
            uc.mem_write(a, self.image.read(a, size))
        for address, text in (case.get('memory') or {}).items():
            raw = bytes.fromhex(text.replace(' ', ''))
            self._map(uc, num(address), len(raw))
            uc.mem_write(num(address), raw)
        calls = []
        stubs = {num(address): stub for address, stub in (case.get('stubs') or {}).items()}
        for a, stub in stubs.items():
            if 'image' in stub:
                if self.image is None:
                    raise ValueError('case %r stubs %08x with image bytes and no executable was given (--image or '
                                     'OBNG_ORACLE_EXE)' % (case.get('name', ''), a))
                code = self.image.read(a, int(stub['image']))
            elif 'code' in stub:
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
            if 'code' not in stub and 'image' not in stub:
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
    image_path = os.environ.get('OBNG_ORACLE_EXE')
    if '--image' in argv:
        image_path = argv[argv.index('--image') + 1]
    emu = Emulation(argv[1], Image(image_path) if image_path else None)
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
