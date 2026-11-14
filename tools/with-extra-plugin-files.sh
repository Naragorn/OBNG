#!/usr/bin/env bash
# Runs a command with extra files in the Linux game's Data/OBSE/Plugins - an
# OBSE plugin the Linux game does not have, with what belongs to it - and
# takes them out again afterwards, whatever the command did. A file that is
# there already is left alone (and stays).
#   tools/with-extra-plugin-files.sh <file>... -- command [args...]
# OBNG_GAME_DIR points somewhere other than Steam's default Oblivion folder.
set -u
game="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}"
target="$game/Data/OBSE/Plugins"
files=()
while [[ $# -gt 0 && "$1" != "--" ]]; do files+=("$1"); shift; done
[[ $# -gt 0 ]] || { echo "usage: $0 file... -- command" >&2; exit 2; }
shift
[[ -d "$target" ]] || { echo "no plugin folder at $target" >&2; exit 2; }
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/tools/game-running.sh" && { echo "the game is running: nothing copied" >&2; exit 2; }
added=()
remove() { for f in "${added[@]}"; do rm -f "$f"; done; }
trap remove EXIT
for f in "${files[@]}"; do
	[[ -f "$f" ]] || { echo "no file $f" >&2; exit 2; }
	to="$target/$(basename "$f")"
	if [[ -e "$to" ]]; then
		echo "kept as it is: $to"
	else
		cp -p "$f" "$to" && added+=("$to")
	fi
done
"$@"
