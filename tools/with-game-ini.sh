#!/usr/bin/env bash
# Runs a command with keys of the Linux game's Oblivion.ini changed, and puts
# the file back afterwards whatever happened (the game rewrites its ini on
# exit, so the saved copy is restored, not the keys reverted).
#
#   tools/with-game-ini.sh Section:Key=Value [Section:Key=Value ...] -- command [args...]
#
# For a run that needs a graphics setting the Linux install does not have -
# the Windows install has the blur shader on ([BlurShader] bUseBlurShader=1),
# the Linux one off. Refuses while the game runs.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ini="${OBNG_GAME_INI:-$HOME/.local/share/Steam/steamapps/compatdata/22330/pfx/drive_c/users/steamuser/Documents/My Games/Oblivion/Oblivion.ini}"
[[ -f "$ini" ]] || { echo "no Oblivion.ini at $ini" >&2; exit 2; }
keys=()
while [[ $# -gt 0 && "$1" != "--" ]]; do keys+=("$1"); shift; done
[[ "${1:-}" == "--" && $# -ge 2 && ${#keys[@]} -ge 1 ]] || { echo "usage: $0 Section:Key=Value [...] -- command [args...]" >&2; exit 2; }
shift
"$root/tools/game-running.sh" 2>/dev/null && { echo "Oblivion is running - Oblivion.ini not touched" >&2; exit 3; }
backup="$ini.obng-backup"
[[ -e "$backup" ]] && { echo "$backup exists - an earlier run did not finish; restore it by hand first" >&2; exit 2; }
cp -p "$ini" "$backup" || exit 2
trap 'cp -p "$backup" "$ini" && rm -f "$backup"; echo "Oblivion.ini restored"' EXIT
python3 - "$ini" "${keys[@]}" <<'PY' || exit 2
import re, sys
path, pairs = sys.argv[1], sys.argv[2:]
text = open(path, "rb").read().decode("latin-1")
newline = "\r\n" if "\r\n" in text else "\n"
lines = text.split(newline)
for pair in pairs:
    m = re.fullmatch(r"([^:=]+):([^:=]+)=(.*)", pair)
    if not m:
        sys.exit(f"not Section:Key=Value: {pair}")
    section, key, value = m.groups()
    inside, done = False, False
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("["):
            inside = stripped.lower() == f"[{section.lower()}]"
            continue
        if inside and stripped.lower().startswith(key.lower() + "="):
            print(f"[{section}] {stripped} -> {key}={value}")
            lines[i] = f"{key}={value}"
            done = True
            break
    if not done:
        sys.exit(f"[{section}] {key} not found in {path}")
open(path, "wb").write(newline.join(lines).encode("latin-1"))
PY
"$@"
