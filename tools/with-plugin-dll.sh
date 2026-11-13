#!/usr/bin/env bash
# Runs a command with one OBSE plugin DLL of the Linux game replaced by another build of it, and puts the game's own
# back afterwards whatever the command did (checked by md5). Refuses while the game runs or when an earlier run left
# its copy behind. For a run against another session's older build; that session's files are not touched otherwise.
#   tools/with-plugin-dll.sh OBVR.dll /path/to/other/OBVR.dll -- command [args...]
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plugins="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}/Data/OBSE/Plugins"
[[ $# -ge 4 && "$3" == "--" ]] || { echo "usage: $0 <plugin dll> <replacement file> -- command [args...]" >&2; exit 2; }
dll="$plugins/$1"; held="$plugins/$1.obng-held"; other="$2"
shift 3
[[ -f "$dll" ]] || { echo "no $dll" >&2; exit 2; }
[[ -f "$other" ]] || { echo "no $other" >&2; exit 2; }
[[ -e "$held" ]] && { echo "$held exists - an earlier run did not finish; put it back by hand first" >&2; exit 2; }
"$root/tools/game-running.sh" 2>/dev/null && { echo "Oblivion is running - $dll not touched" >&2; exit 3; }
before="$(md5sum < "$dll")"
mv "$dll" "$held" || exit 2
trap 'mv -f "$held" "$dll" && [[ "$(md5sum < "$dll")" == "$before" ]] && echo "$(basename "$dll") put back (${before%% *})" || echo "RESTORE FAILED: $held" >&2' EXIT
cp "$other" "$dll" || exit 2
echo "$(basename "$dll") for the run: $(md5sum < "$dll" | cut -d' ' -f1)"
"$@"
