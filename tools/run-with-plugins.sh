#!/usr/bin/env bash
# Runs a command with extra plugins active in the Linux game's Plugins.txt
# and puts the file back afterwards, whatever the command did.
# Usage: run-with-plugins.sh "Plugin A.esm" "Plugin B.esp" -- command [args...]
set -u
plugins_txt="$HOME/.local/share/Steam/steamapps/compatdata/22330/pfx/drive_c/users/steamuser/AppData/Local/Oblivion/Plugins.txt"
extra=()
while [[ $# -gt 0 && "$1" != "--" ]]; do extra+=("$1"); shift; done
[[ $# -gt 0 ]] || { echo "usage: $0 plugin... -- command" >&2; exit 2; }
shift
[[ -f "$plugins_txt" ]] || { echo "no Plugins.txt at $plugins_txt" >&2; exit 2; }
backup="$(mktemp "${plugins_txt}.obng-backup.XXXXXX")"
cp -p "$plugins_txt" "$backup"
restore() { cp -p "$backup" "$plugins_txt" && rm -f "$backup"; }
trap restore EXIT
eol=$'\n'
grep -q $'\r' "$plugins_txt" && eol=$'\r\n'
[[ -z "$(tail -c1 "$plugins_txt")" ]] || printf '%s' "$eol" >> "$plugins_txt"
for p in "${extra[@]}"; do
	grep -qiF -x "$p" <(tr -d '\r' < "$plugins_txt") || printf '%s%s' "$p" "$eol" >> "$plugins_txt"
done
"$@"
