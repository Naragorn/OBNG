#!/usr/bin/env python3
"""Runs a command with the Linux game's Plugins.txt holding only the plugins
named, and puts the file back byte for byte afterwards whatever the command
did - for the cut "the game's own data alone" (Oblivion.esm and nothing else).

Only the list changes: no file of the Data folder is moved, so loose meshes
and textures of the installed mods stay where they are, as do the OBSE plugins
(tools/without-plugin-dll.sh takes one of those out). The load order of
several names stays the one their files' times give. Refuses while the game
runs, while an earlier run of this tool or of run-with-windows-mods.py is not
taken back, and for a name that is no plugin of the Data folder. The original
is kept in a manifest beside Plugins.txt until the restore, so that a run that
was killed can be undone with --undo.

Usage: with-plugin-list.py [--dry-run] <plugin>... -- command [args...]
       with-plugin-list.py --undo
(tests: python3 tools/with-plugin-list-test.py)"""
import json, os, subprocess, sys

HOME = os.path.expanduser('~')
DATA = os.path.join(HOME, '.local/share/Steam/steamapps/common/Oblivion/Data')
PLUGINS = os.path.join(HOME, '.local/share/Steam/steamapps/compatdata/22330/pfx/drive_c/users/steamuser/AppData/Local/Oblivion/Plugins.txt')
WINDOWS_MODS_MANIFEST = '.obng-windows-mods.json'  # run-with-windows-mods.py's, beside Plugins.txt


def manifest_of(plugins):
    return plugins + '.obng-plugin-list.json'


def lines_of(original):
    lines = original.decode('latin1').replace('\r', '').split('\n')
    return lines[:-1] if lines[-1] == '' else lines  # the piece after the last line end is no line


def plugin_names(original):
    return [l.strip() for l in lines_of(original) if l.strip() and not l.startswith('#')]


def list_text(original, names):
    """The original's head (its comment and blank lines before the first plugin) and line ends, then the names."""
    eol = '\r\n' if b'\r\n' in original else '\n'
    head = []
    for line in lines_of(original):
        if line.strip() and not line.startswith('#'):
            break
        head.append(line)
    return ''.join(l + eol for l in head + list(names)).encode('latin1')


def refusal(names, entries):
    """Why these names cannot be a run's list; None when they can."""
    if not names:
        return 'no plugin named'
    no_plugin = [n for n in names if not n.lower().endswith(('.esm', '.esp'))]
    if no_plugin:
        return 'not a plugin file (.esm or .esp): ' + ', '.join(no_plugin)
    have = {e.lower() for e in entries}
    missing = [n for n in names if n.lower() not in have]
    if missing:
        return 'not in the Data folder: ' + ', '.join(missing)
    return None


def begin(plugins, names):
    original = open(plugins, 'rb').read()
    json.dump({'plugins': original.hex()}, open(manifest_of(plugins), 'w'))
    with open(plugins, 'wb') as f:
        f.write(list_text(original, names))


def undo(plugins):
    """Plugins.txt put back from the manifest; False when there is none."""
    manifest = manifest_of(plugins)
    if not os.path.exists(manifest):
        return False
    original = bytes.fromhex(json.load(open(manifest))['plugins'])
    with open(plugins, 'wb') as f:
        f.write(original)
    if open(plugins, 'rb').read() != original:
        raise OSError(f'RESTORE FAILED: {plugins} differs from the manifest {manifest}')
    os.unlink(manifest)
    return True


def run(plugins, data, names, command, game_running):
    """The command's exit code; 2 for a refusal or a command that cannot be started, 3 while the game runs."""
    why = refusal(names, os.listdir(data))
    if why is None and os.path.exists(manifest_of(plugins)):
        why = 'an earlier run was not taken back - run with --undo first'
    if why is None and os.path.exists(plugins + WINDOWS_MODS_MANIFEST):
        why = 'the Windows mod list is linked in (run-with-windows-mods.py) - take that back first'
    if why is not None:
        print(why, file=sys.stderr)
        return 2
    if game_running():
        print('Oblivion is running - Plugins.txt not touched', file=sys.stderr)
        return 3
    begin(plugins, names)
    try:
        return subprocess.call(command)
    except OSError as e:
        print(f'cannot start {command[0]}: {e}', file=sys.stderr)
        return 2
    finally:
        undo(plugins)
        print(f"Plugins.txt put back ({len(plugin_names(open(plugins, 'rb').read()))} plugins)")


def game_runs():
    return subprocess.call([os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game-running.sh')]) == 0


def main(argv):
    if '--undo' in argv:
        print('taken back: Plugins.txt' if undo(PLUGINS) else 'nothing to undo')
        return 0
    cut = argv.index('--') if '--' in argv else len(argv)
    names = [a for a in argv[1:cut] if a != '--dry-run']
    command = argv[cut + 1:]
    if '--dry-run' in argv[1:cut] or not command:
        why = refusal(names, os.listdir(DATA))
        now = plugin_names(open(PLUGINS, 'rb').read())
        print(why if why else f"the run's list: {', '.join(names)} (instead of {len(now)} plugins)")
        return 2 if why else 0
    return run(PLUGINS, DATA, names, command, game_runs)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
