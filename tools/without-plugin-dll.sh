#!/usr/bin/env bash
# Runs a command with one OBSE plugin DLL of the Linux game moved out of Data/OBSE/Plugins, and puts it back
# afterwards whatever the command did. Refuses while the game runs or when an earlier run left its copy behind.
#   tools/without-plugin-dll.sh OblivionWalkers.dll -- command [args...]
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plugins="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}/Data/OBSE/Plugins"
[[ $# -ge 3 && "$2" == "--" ]] || { echo "usage: $0 <plugin dll> -- command [args...]" >&2; exit 2; }
dll="$plugins/$1"; held="$plugins/$1.obng-held"
shift 2
[[ -f "$dll" ]] || { echo "no $dll" >&2; exit 2; }
[[ -e "$held" ]] && { echo "$held exists - an earlier run did not finish; put it back by hand first" >&2; exit 2; }
"$root/tools/game-running.sh" 2>/dev/null && { echo "Oblivion is running - $dll not touched" >&2; exit 3; }
before="$(md5sum < "$dll")"
mv "$dll" "$held" || exit 2
trap 'mv "$held" "$dll" && [[ "$(md5sum < "$dll")" == "$before" ]] && echo "$(basename "$dll") put back" || echo "RESTORE FAILED: $held" >&2' EXIT
"$@"
