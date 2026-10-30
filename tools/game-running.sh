#!/usr/bin/env bash
# Exit 0 when Oblivion is running (under Proton the game's wine process has
# argv0 "Oblivion.exe"; its comm is not, so `pgrep -x Oblivion.exe` never sees
# it, and `pgrep -f Oblivion.exe` matches the caller's own shell).
#
#   tools/game-running.sh && echo running
for cmdline in /proc/[0-9]*/cmdline; do
	argv0="$(tr '\0' '\n' < "$cmdline" 2>/dev/null | head -n 1)" || continue
	argv0="${argv0//\\//}"
	if [[ "${argv0##*/}" =~ ^[Oo][Bb][Ll][Ii][Vv][Ii][Oo][Nn]\.[Ee][Xx][Ee]$ ]]; then
		exit 0
	fi
done
exit 1
