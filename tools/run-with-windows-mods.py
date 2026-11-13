#!/usr/bin/env python3
"""Runs a command with the Windows installation's mod list on the Linux game
and takes everything back afterwards.

For the run: every file under the Windows Data folder that the Linux Data
folder does not have (compared without case, component by component) is
linked into it - nothing existing is replaced - and Plugins.txt becomes the
Linux list plus every plugin of the Windows list it lacks. The game loads
plugins in the order of their file times, so the added plugins are copied
(not linked) and every active plugin's time is set for the run: masters
first, then the Windows list in its order, then the plugins only Linux has.
Afterwards the
links and the folders made for them are removed and Plugins.txt is put back,
whatever the command did. Left out: Data/OBSE (the Linux game keeps its own
OBSE plugins) and Data/menus (the harness reads the Linux interface).
A manifest of what was linked is kept beside Plugins.txt until the restore,
so that a run that was killed can be undone with --undo.

Usage: [OBNG_WITHOUT_PLUGINS="A.esp;B.esp"] run-with-windows-mods.py [--dry-run] -- command [args...]
       run-with-windows-mods.py --undo"""
import json, os, subprocess, sys
HOME = os.path.expanduser('~')
LINUX = os.path.join(HOME, '.local/share/Steam/steamapps/common/Oblivion/Data')
WINDOWS = '/run/media/nadi/Programs/Steam/steamapps/common/Oblivion/Data'
PLUGINS = os.path.join(HOME, '.local/share/Steam/steamapps/compatdata/22330/pfx/drive_c/users/steamuser/AppData/Local/Oblivion/Plugins.txt')
WINDOWS_PLUGINS = '/run/media/nadi/Programs/Users/Nadi/AppData/Local/Oblivion/Plugins.txt'
MANIFEST = PLUGINS + '.obng-windows-mods.json'
SKIP_TOP = {'obse', 'menus'}

def undo():
    if not os.path.exists(MANIFEST):
        print('nothing to undo')
        return
    m = json.load(open(MANIFEST))
    for link in m['links']:
        if os.path.islink(link):
            os.unlink(link)
    for copy in m.get('copies', []):
        if os.path.isfile(copy) and not os.path.islink(copy):
            os.unlink(copy)
    for path, (atime, mtime) in m.get('times', {}).items():
        if os.path.exists(path):
            os.utime(path, ns=(atime, mtime))
    for d in sorted(m['dirs'], key=len, reverse=True):
        try:
            os.rmdir(d)
        except OSError:
            pass
    with open(PLUGINS, 'wb') as f:
        f.write(bytes.fromhex(m['plugins']))
    os.unlink(MANIFEST)
    print(f"taken back: {len(m['links'])} links, {len(m['dirs'])} folders, Plugins.txt")

def plan():
    links, dirs = [], []
    listing = {}
    def entries(path):
        if path not in listing:
            try:
                listing[path] = {e.lower(): e for e in os.listdir(path)}
            except OSError:
                listing[path] = {}
        return listing[path]
    def visit(wdir, ldir, exists, top):
        for name in sorted(os.listdir(wdir)):
            if top and name.lower() in SKIP_TOP:
                continue
            wpath = os.path.join(wdir, name)
            have = entries(ldir).get(name.lower()) if exists else None
            if os.path.isdir(wpath):
                if have is not None:
                    visit(wpath, os.path.join(ldir, have), True, False)
                else:
                    made = os.path.join(ldir, name)
                    dirs.append(made)
                    visit(wpath, made, False, False)
            elif have is None:
                links.append((os.path.join(ldir, name), wpath))
    visit(WINDOWS, LINUX, True, True)
    return links, dirs

def plugin_names(path):
    return [l.strip() for l in open(path, 'rb').read().decode('latin1').replace('\r', '').split('\n') if l.strip() and not l.startswith('#')]

if '--undo' in sys.argv:
    undo()
    sys.exit(0)
dry = '--dry-run' in sys.argv
command = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
if os.path.exists(MANIFEST):
    print('an earlier run was not taken back - run with --undo first', file=sys.stderr)
    sys.exit(2)
links, dirs = plan()
have = {p.lower() for p in plugin_names(PLUGINS)}
# OBNG_WITHOUT_PLUGINS="A.esp;B.esp": plugins of the Windows list left out of the run's list (their files are linked
# all the same; a plugin not in Plugins.txt is not loaded) - for finding which of them a fault needs.
without = {p.strip().lower() for p in os.environ.get('OBNG_WITHOUT_PLUGINS', '').split(';') if p.strip()}
unknown = without - {p.lower() for p in plugin_names(WINDOWS_PLUGINS)}
if unknown:
    print(f"OBNG_WITHOUT_PLUGINS names plugins the Windows list does not have: {', '.join(sorted(unknown))}", file=sys.stderr)
    sys.exit(2)
extra = [p for p in plugin_names(WINDOWS_PLUGINS) if p.lower() not in have and p.lower() not in without]
if without:
    print(f"left out: {', '.join(sorted(without))}")
print(f"{len(links)} files to link, {len(dirs)} folders to make, {len(extra)} plugins to add: {', '.join(extra)}")
if dry or not command:
    by_top = {}
    for l, _ in links:
        top = os.path.relpath(l, LINUX).split(os.sep)[0]
        by_top[top] = by_top.get(top, 0) + 1
    print(', '.join(f"{k} {v}" for k, v in sorted(by_top.items(), key=lambda kv: -kv[1])[:25]))
    sys.exit(0)
original = open(PLUGINS, 'rb').read()
# The run's load order: the Windows list, then what only Linux has; masters first.
windows_order = [p for p in plugin_names(WINDOWS_PLUGINS) if p.lower() not in without]
linux_only = [p for p in plugin_names(PLUGINS) if p.lower() not in {w.lower() for w in windows_order}]
order = windows_order + linux_only
order = [p for p in order if p.lower().endswith('.esm')] + [p for p in order if not p.lower().endswith('.esm')]
top = {e.lower(): e for e in os.listdir(LINUX)}
extra_lower = {p.lower() for p in extra}
copies = [l for l, w in links if os.path.dirname(l) == LINUX and os.path.basename(l).lower() in extra_lower]
copy_sources = {l: w for l, w in links if l in copies}
links = [(l, w) for l, w in links if l not in copy_sources]
times = {}
for p in order:
    real = top.get(p.lower())
    if real is not None:
        st = os.stat(os.path.join(LINUX, real))
        times[os.path.join(LINUX, real)] = (st.st_atime_ns, st.st_mtime_ns)
json.dump({'links': [l for l, _ in links], 'dirs': dirs, 'plugins': original.hex(), 'copies': copies, 'times': times}, open(MANIFEST, 'w'))
code = 1
try:
    for d in dirs:
        os.makedirs(d, exist_ok=True)
    for l, w in links:
        os.symlink(w, l)
    import shutil
    for l, w in copy_sources.items():
        shutil.copyfile(w, l)
    base = 1262304000  # 2010-01-01, a minute apart in the run's order
    for i, p in enumerate(order):
        path = os.path.join(LINUX, top.get(p.lower(), p))
        if os.path.exists(path):
            os.utime(path, (base + 60 * i, base + 60 * i))
    eol = b'\r\n' if b'\r\n' in original else b'\n'
    text = original if original.endswith(b'\n') else original + eol
    with open(PLUGINS, 'wb') as f:
        f.write(text + b''.join(p.encode('latin1') + eol for p in extra))
    code = subprocess.call(command)
finally:
    undo()
sys.exit(code)
