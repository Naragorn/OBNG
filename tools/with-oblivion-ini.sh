#!/usr/bin/env bash
# Runs a command with one key of the Linux game's Oblivion.ini changed, and puts the file back whatever happens
# (the copy is compared byte for byte afterwards).
#   tools/with-oblivion-ini.sh iMultiSample=2 -- <command...>
# For a run that needs another anti-aliasing level than the player's (the harness has no switch for it: the device
# is made from Oblivion.ini before any script line runs). Refuses while the game runs.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ini="${OBNG_OBLIVION_INI:-$HOME/.local/share/Steam/steamapps/compatdata/22330/pfx/drive_c/users/steamuser/Documents/My Games/Oblivion/Oblivion.ini}"
pair="${1:-}"; [[ "$pair" == *=* && "${2:-}" == "--" ]] || { echo "usage: $0 Key=Value -- command..." >&2; exit 2; }
shift 2
key="${pair%%=*}"; value="${pair#*=}"
[[ -f "$ini" ]] || { echo "no $ini" >&2; exit 2; }
"$root/tools/game-running.sh" 2>/dev/null && { echo "Oblivion is running - Oblivion.ini not touched" >&2; exit 3; }
grep -q "^${key}=" "$ini" || { echo "no ${key}= line in $ini" >&2; exit 2; }
keep="$(mktemp)"; cp -p "$ini" "$keep"
restore() { cp -p "$keep" "$ini" && cmp -s "$keep" "$ini" && rm -f "$keep" && echo "Oblivion.ini put back (${key})"; }
trap restore EXIT
sed -i "s/^${key}=.*\r\?$/${key}=${value}\r/" "$ini"
echo "Oblivion.ini: $(grep "^${key}=" "$ini" | tr -d '\r') for this run"
"$@"
