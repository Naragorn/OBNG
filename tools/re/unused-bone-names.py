#!/usr/bin/env python3
"""Writes OBNG-unused-bones.txt for an Oblivion installation ([Route]
ActorUnusedBones, plugin/src/FastSceneUpdate.cpp): the names in the loose
skeleton files under Data/Meshes/Characters that no skeleton in any of the
installation's BSA archives has. Those are the bones a replaced skeleton
adds; nothing in the engine can ask for them by name, so the route may leave
one out while nothing hangs on it.

A name here is any length-prefixed printable string of a NIF (node names,
extra data strings and all): more strings than nodes on both sides, which
only makes the result smaller.

Usage: unused-bone-names.py <Data folder> [output file]
       (default output: <Data>/OBSE/Plugins/OBNG-unused-bones.txt, found without regard to case)"""
import os, re, struct, sys, zlib

def strings_of(d):
    out = set()
    p, n = 0, len(d)
    while p + 5 <= n:
        l = struct.unpack_from('<I', d, p)[0]
        if 1 <= l <= 64 and p + 4 + l <= n:
            s = d[p + 4:p + 4 + l]
            if all(32 <= c < 127 for c in s):
                out.add(s.decode())
                p += 4 + l
                continue
        p += 1
    return out

def bsa_files(path, pred):
    with open(path, 'rb') as f:
        magic, ver, off, aflags, nfold, nfiles, lfold, lfile, fflags = struct.unpack('<4sIIIIIIII', f.read(36))
        if magic != b'BSA\0':
            return
        folders = [struct.unpack('<QII', f.read(16)) for _ in range(nfold)]
        recs = []
        for _, cnt, _o in folders:
            n = f.read(1)[0]
            name = f.read(n)[:-1].decode('latin1')
            for _i in range(cnt):
                _h, size, offset = struct.unpack('<QII', f.read(16))
                recs.append((name, size, offset))
        names = f.read(lfile).split(b'\0')
        for (folder, size, offset), nm in zip(recs, names):
            full = (folder + '\\' + nm.decode('latin1')).lower()
            if pred(full):
                comp = bool(aflags & 4) ^ bool(size & 0x40000000)
                size &= 0x3fffffff
                f.seek(offset)
                b = f.read(size)
                yield full, (zlib.decompress(b[4:]) if comp else b)

def find(folder, *parts):
    """A path under folder without regard to case; None when a part is missing."""
    for part in parts:
        hit = [e for e in os.listdir(folder) if e.lower() == part.lower()] if os.path.isdir(folder) else []
        if not hit:
            return None
        folder = os.path.join(folder, hit[0])
    return folder

data = sys.argv[1]
game, archives = set(), 0
for entry in sorted(os.listdir(data)):
    if entry.lower().endswith('.bsa'):
        try:
            for _full, d in bsa_files(os.path.join(data, entry), lambda n: re.search(r'skeleton[^\\]*\.nif$', n)):
                game |= strings_of(d)
                archives += 1
        except (OSError, struct.error, zlib.error) as e:
            print(f'skipped {entry}: {e}', file=sys.stderr)
loose, files = set(), []
characters = find(data, 'meshes', 'characters')
if characters:
    for root, _dirs, names in os.walk(characters):
        for name in names:
            if re.fullmatch(r'skeleton[^/]*\.nif', name.lower()):
                files.append(os.path.join(root, name))
                loose |= strings_of(open(os.path.join(root, name), 'rb').read())
extra = sorted(loose - game)
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(find(data, 'obse', 'plugins') or data, 'OBNG-unused-bones.txt')
with open(out, 'w', newline='\r\n') as f:
    for name in extra:
        f.write(name + '\n')
print(f'{archives} skeletons in the archives with {len(game)} strings; {len(files)} loose skeleton files with {len(loose)} strings; {len(extra)} names written to {out}')
for path in files:
    print('  loose:', os.path.relpath(path, data))
