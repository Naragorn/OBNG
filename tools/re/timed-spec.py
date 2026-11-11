#!/usr/bin/env python3
"""[Timed] lines for OBNG.ini (plugin/src/TimedCalls.h): for each address the
entry's whole instructions that a five-byte jump displaces, read from
Oblivion.exe with objdump. An entry whose first instructions hold a relative
operand (call, jmp, jcc, loop) or end before five bytes is refused, with the
reason on stderr.
Usage: timed-spec.py <Oblivion.exe> <address>[=name] ...   (or - to read such words from stdin)"""
import re, subprocess, sys
exe = sys.argv[1]
words = sys.argv[2:]
if words == ['-']:
    words = sys.stdin.read().split('\n')
n = 0
for word in words:
    word = word.strip()
    if not word or word.startswith('#'):
        continue
    address, _, name = word.partition('=')
    a = int(address, 16)
    out = subprocess.run(['objdump', '-d', f'--start-address={a:#x}', f'--stop-address={a + 24:#x}', exe], capture_output=True, text=True).stdout
    taken = b''
    why = None
    for line in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t((?:[0-9a-f]{2} )+)\s*\t?(.*)', line)
        if not m:
            continue
        if int(m.group(1), 16) != a + len(taken):
            continue  # a continuation line of a long instruction
        data = bytes.fromhex(m.group(2).replace(' ', ''))
        text = m.group(3)
        # objdump wraps instructions longer than 7 bytes onto a second line
        nxt = [l for l in out.splitlines() if re.match(rf'\s*{int(m.group(1), 16) + 7:x}:\t((?:[0-9a-f]{{2}} )+)\s*$', l)]
        if len(data) == 7 and nxt:
            data += bytes.fromhex(re.match(r'\s*[0-9a-f]+:\t((?:[0-9a-f]{2} )+)', nxt[0]).group(1).replace(' ', ''))
        mnemonic = text.split()[0] if text.split() else '?'
        if mnemonic.startswith('j') or mnemonic.startswith('call') or mnemonic.startswith('loop') or mnemonic in ('ret', 'int3', '(bad)', '?'):
            why = f'{mnemonic} within the first five bytes'
            break
        taken += data
        if len(taken) >= 5:
            break
    if why is None and len(taken) < 5:
        why = 'fewer than five bytes read'
    if why is None and len(taken) > 15:
        why = 'more than fifteen bytes'
    if why:
        print(f'refused {a:08X} {name}: {why}', file=sys.stderr)
        continue
    n += 1
    print(f'F{n}={a:08X} {taken.hex().upper()} {name or f"0x{a:08X}"}')
