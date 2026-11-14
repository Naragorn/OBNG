#!/usr/bin/env python3
"""Checks for tools/with-plugin-list.py: the list written for the run, the
refusals, and Plugins.txt put back byte for byte whatever the command did.
Run with: python3 tools/with-plugin-list-test.py"""
import importlib.util
import os
import sys
import tempfile
from pathlib import Path

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("with_plugin_list", here / "with-plugin-list.py")
wpl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wpl)

failures = 0


def check(condition, what):
    global failures
    print("  %-5s %s" % ("ok" if condition else "FAIL", what))
    if not condition:
        failures += 1


ORIGINAL = b"# This file is used to tell Oblivion which data files to load.\r\n# Please do not modify this file.\r\n\r\nOblivion.esm\r\nKnights.esp\r\nUnique Landscapes.esp\r\n"

print("the list's text")
check(wpl.plugin_names(ORIGINAL) == ["Oblivion.esm", "Knights.esp", "Unique Landscapes.esp"], "the names of a list, comments and blanks skipped")
check(wpl.list_text(ORIGINAL, ["Oblivion.esm"]) == b"# This file is used to tell Oblivion which data files to load.\r\n# Please do not modify this file.\r\n\r\nOblivion.esm\r\n",
      "the head kept, the original's line ends, one name")
check(wpl.list_text(b"A.esm\nB.esp\n", ["A.esm", "C.esp"]) == b"A.esm\nC.esp\n", "no head, unix line ends")
check(wpl.list_text(b"", ["A.esm"]) == b"A.esm\n", "an empty original")
check(wpl.list_text(b"# head", ["A.esm"]) == b"# head\nA.esm\n", "a head without a line end gets one")

print("what is refused")
entries = ["Oblivion.esm", "knights.ESP", "Meshes"]
check(wpl.refusal([], entries) == "no plugin named", "no name")
check(wpl.refusal(["Oblivion.esm", "notes.txt"], entries) == "not a plugin file (.esm or .esp): notes.txt", "a name that is no plugin")
check(wpl.refusal(["Oblivion.esm", "Gone.esp"], entries) == "not in the Data folder: Gone.esp", "a plugin the Data folder lacks")
check(wpl.refusal(["oblivion.ESM", "Knights.esp"], entries) is None, "names matched without case")


def game(tmp, text=ORIGINAL):
    data = Path(tmp) / "Data"
    data.mkdir()
    for name in ("Oblivion.esm", "Knights.esp", "Unique Landscapes.esp"):
        (data / name).write_bytes(b"x")
    plugins = Path(tmp) / "Plugins.txt"
    plugins.write_bytes(text)
    return str(plugins), str(data)


def idle():
    return False


print("a run")
with tempfile.TemporaryDirectory() as tmp:
    plugins, data = game(tmp)
    seen = Path(tmp) / "seen"
    command = [sys.executable, "-c", "import shutil,sys; shutil.copyfile(sys.argv[1], sys.argv[2])", plugins, str(seen)]
    code = wpl.run(plugins, data, ["Oblivion.esm"], command, idle)
    check(code == 0, "the command's exit code 0")
    check(seen.read_bytes() == wpl.list_text(ORIGINAL, ["Oblivion.esm"]), "the command saw the run's list")
    check(Path(plugins).read_bytes() == ORIGINAL, "Plugins.txt put back byte for byte")
    check(not os.path.exists(wpl.manifest_of(plugins)), "no manifest left")

with tempfile.TemporaryDirectory() as tmp:
    plugins, data = game(tmp)
    code = wpl.run(plugins, data, ["Oblivion.esm"], [sys.executable, "-c", "import sys; sys.exit(7)"], idle)
    check(code == 7, "a failing command's exit code handed on")
    check(Path(plugins).read_bytes() == ORIGINAL, "Plugins.txt put back after a failing command")

with tempfile.TemporaryDirectory() as tmp:
    plugins, data = game(tmp)
    code = wpl.run(plugins, data, ["Oblivion.esm"], [str(Path(tmp) / "no-such-command")], idle)
    check(code == 2 and Path(plugins).read_bytes() == ORIGINAL, "a command that cannot be started: exit 2, Plugins.txt put back")

print("a run refused")
with tempfile.TemporaryDirectory() as tmp:
    plugins, data = game(tmp)
    marker = Path(tmp) / "ran"
    touch = [sys.executable, "-c", "import sys; open(sys.argv[1], 'w').close()", str(marker)]
    check(wpl.run(plugins, data, ["Gone.esp"], touch, idle) == 2 and not marker.exists(), "an unknown plugin: exit 2, nothing run")
    check(wpl.run(plugins, data, ["Oblivion.esm"], touch, lambda: True) == 3 and not marker.exists(), "the game running: exit 3, nothing run")
    check(Path(plugins).read_bytes() == ORIGINAL, "Plugins.txt untouched by the refusals")
    Path(wpl.manifest_of(plugins)).write_text("{}")
    check(wpl.run(plugins, data, ["Oblivion.esm"], touch, idle) == 2 and not marker.exists(), "an earlier run's manifest: exit 2, nothing run")
    os.unlink(wpl.manifest_of(plugins))
    Path(plugins + wpl.WINDOWS_MODS_MANIFEST).write_text("{}")
    check(wpl.run(plugins, data, ["Oblivion.esm"], touch, idle) == 2 and not marker.exists(), "the Windows mod list linked in: exit 2, nothing run")
    check(Path(plugins).read_bytes() == ORIGINAL, "Plugins.txt untouched")

print("a killed run taken back")
with tempfile.TemporaryDirectory() as tmp:
    plugins, data = game(tmp)
    wpl.begin(plugins, ["Oblivion.esm"])
    check(Path(plugins).read_bytes() != ORIGINAL and os.path.exists(wpl.manifest_of(plugins)), "begun: the list written, the manifest kept")
    check(wpl.undo(plugins) is True and Path(plugins).read_bytes() == ORIGINAL, "undo puts Plugins.txt back")
    check(wpl.undo(plugins) is False, "undo with nothing to take back")

print("FAILED: %d" % failures if failures else "all ok")
sys.exit(1 if failures else 0)
